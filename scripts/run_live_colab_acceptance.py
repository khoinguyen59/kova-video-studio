#!/usr/bin/env python3
"""Run real, model-specific acceptance checks against temporary Colab workers.

This is deliberately separate from the unit, contract, and notebook-generation
tests.  It talks to the actual HTTPS tunnel created by a notebook, sends a
small inference request to the exact model configured in the JSON file, and
writes a Markdown report.  Credentials are read only from process environment
variables and are never written to the report or console.

The tool does not start Colab, invent a worker URL, or downgrade a failure to a
warning.  A worker that has only passed /health and /v1/capabilities is reported
as preflight-only; a capability passes only after its model-specific inference
result is validated.
"""

from __future__ import annotations

import argparse
import json
import mimetypes
import os
import secrets
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Iterable
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen


CAPABILITIES = {
    "stt",
    "tts",
    "voice-cloning",
    "voice-design",
    "forced-alignment",
    "voice-isolation",
    "subtitle-ocr",
    "translation",
    "llm-chat",
}


class AcceptanceError(RuntimeError):
    """A worker, protocol, or inference result did not meet acceptance."""


@dataclass
class Check:
    name: str
    passed: bool
    detail: str
    elapsed_seconds: float = 0.0


@dataclass
class WorkerReport:
    capability: str
    model: str
    checks: list[Check] = field(default_factory=list)

    @property
    def passed(self) -> bool:
        return bool(self.checks) and all(check.passed for check in self.checks)


def redact_detail(value: Any, limit: int = 500) -> str:
    """Keep reports actionable without copying a URL or bearer token."""
    text = str(value).replace("\r", " ").replace("\n", " ").strip()
    for marker in ("https://", "http://"):
        start = text.find(marker)
        if start >= 0:
            end = text.find(" ", start)
            if end < 0:
                end = len(text)
            text = text[:start] + "[worker-url-redacted]" + text[end:]
    return text[:limit]


def redact_with_secrets(value: Any, sensitive_values: Iterable[str]) -> str:
    """Remove known session values even when a remote error echoes a request."""
    text = redact_detail(value)
    for secret_value in sensitive_values:
        if secret_value:
            text = text.replace(secret_value, "[credential-redacted]")
    return text


def require_string(config: dict[str, Any], key: str, context: str) -> str:
    value = str(config.get(key, "")).strip()
    if not value:
        raise AcceptanceError(f"{context} requires '{key}' in the acceptance config")
    return value


def read_config(path: Path) -> list[dict[str, Any]]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise AcceptanceError(f"Cannot read acceptance config: {error}") from error
    workers = document.get("workers") if isinstance(document, dict) else None
    if not isinstance(workers, list) or not workers:
        raise AcceptanceError("Acceptance config must contain a non-empty 'workers' array")
    if not all(isinstance(worker, dict) for worker in workers):
        raise AcceptanceError("Every 'workers' entry must be an object")
    return workers


def content_type_for(path: Path) -> str:
    explicit = {
        ".wav": "audio/wav", ".mp3": "audio/mpeg", ".flac": "audio/flac",
        ".m4a": "audio/mp4", ".mp4": "video/mp4", ".webm": "video/webm",
        ".ogg": "audio/ogg", ".mkv": "video/x-matroska", ".mov": "video/quicktime",
        ".avi": "video/x-msvideo",
    }
    return explicit.get(path.suffix.lower(), mimetypes.guess_type(path.name)[0] or "application/octet-stream")


def assert_audio_file(path: Path, context: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file() or resolved.stat().st_size <= 0:
        raise AcceptanceError(f"{context} requires a readable non-empty audio_path")
    if resolved.stat().st_size > 512 * 1024 * 1024:
        raise AcceptanceError(f"{context} audio_path exceeds the 512 MiB worker limit")
    if content_type_for(resolved) == "application/octet-stream":
        raise AcceptanceError(f"{context} audio_path has an unsupported file extension")
    return resolved


def encode_multipart(fields: dict[str, str], file_field: str, file_path: Path) -> tuple[bytes, str]:
    boundary = "----LAStudioAcceptance" + secrets.token_hex(16)
    chunks: list[bytes] = []
    for key, value in fields.items():
        chunks.extend((
            f"--{boundary}\r\n".encode(),
            f'Content-Disposition: form-data; name="{key}"\r\n\r\n'.encode(),
            str(value).encode("utf-8"), b"\r\n",
        ))
    chunks.extend((
        f"--{boundary}\r\n".encode(),
        (f'Content-Disposition: form-data; name="{file_field}"; filename="{file_path.name}"\r\n').encode(),
        f"Content-Type: {content_type_for(file_path)}\r\n\r\n".encode(),
        file_path.read_bytes(), b"\r\n",
        f"--{boundary}--\r\n".encode(),
    ))
    return b"".join(chunks), f"multipart/form-data; boundary={boundary}"


class WorkerClient:
    def __init__(self, config: dict[str, Any], allow_http_localhost: bool) -> None:
        self.capability = require_string(config, "capability", "Worker")
        self.model = require_string(config, "model", f"Worker '{self.capability}'")
        if self.capability not in CAPABILITIES:
            raise AcceptanceError(f"Unsupported capability '{self.capability}'")
        self.config = config
        url_env = require_string(config, "url_env", f"Worker '{self.capability}/{self.model}'")
        token_env = require_string(config, "token_env", f"Worker '{self.capability}/{self.model}'")
        self.base_url = os.environ.get(url_env, "").strip().rstrip("/")
        self.token = os.environ.get(token_env, "").strip()
        if not self.base_url or not self.token:
            raise AcceptanceError(
                f"Worker '{self.capability}/{self.model}' needs non-empty environment variables "
                f"'{url_env}' and '{token_env}'")
        parsed = urlparse(self.base_url)
        is_local_http = parsed.scheme == "http" and parsed.hostname in {"127.0.0.1", "localhost", "::1"}
        if parsed.scheme != "https" and not (allow_http_localhost and is_local_http):
            raise AcceptanceError("Worker URL must use HTTPS (or explicit localhost test mode)")
        if not parsed.netloc:
            raise AcceptanceError("Worker URL is invalid")
        self.timeout_seconds = int(config.get("request_timeout_seconds", 180))
        if self.timeout_seconds < 5 or self.timeout_seconds > 1800:
            raise AcceptanceError("request_timeout_seconds must be between 5 and 1800")

    def url(self, path: str) -> str:
        return self.base_url + "/" + path.lstrip("/")

    def request(self, method: str, path: str, body: bytes | None = None,
                content_type: str | None = None, accept: str | None = None) -> tuple[int, bytes, dict[str, str]]:
        headers = {"Authorization": "Bearer " + self.token}
        if content_type:
            headers["Content-Type"] = content_type
        if accept:
            headers["Accept"] = accept
        request = Request(self.url(path), data=body, headers=headers, method=method)
        try:
            with urlopen(request, timeout=self.timeout_seconds) as response:
                return response.status, response.read(), dict(response.headers.items())
        except HTTPError as error:
            return error.code, error.read(), dict(error.headers.items())
        except (URLError, TimeoutError, OSError) as error:
            raise AcceptanceError(f"Network request failed: {redact_detail(error)}") from error

    def request_json(self, method: str, path: str, payload: dict[str, Any] | None = None) -> tuple[int, dict[str, Any]]:
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8") if payload is not None else None
        status, data, _ = self.request(method, path, body, "application/json" if body else None, "application/json")
        try:
            decoded = json.loads(data.decode("utf-8")) if data else {}
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise AcceptanceError(f"{path} returned non-JSON response (HTTP {status})") from error
        if not isinstance(decoded, dict):
            raise AcceptanceError(f"{path} returned a JSON value other than an object")
        return status, decoded

    def require_success(self, status: int, payload: dict[str, Any], action: str) -> dict[str, Any]:
        if 200 <= status < 300:
            return payload
        detail = payload.get("detail", payload.get("error", payload))
        raise AcceptanceError(f"{action} failed with HTTP {status}: {redact_detail(detail)}")


def exact_model_preflight(client: WorkerClient) -> list[Check]:
    checks: list[Check] = []
    expected_device = "cuda"
    worker_label = "CUDA worker"
    started = time.monotonic()
    status, health = client.request_json("GET", "/health")
    elapsed = time.monotonic() - started
    try:
        client.require_success(status, health, "health")
        if health.get("ready") is not True or str(health.get("device", "")).lower() != expected_device:
            raise AcceptanceError(f"health did not prove ready {worker_label} execution")
        if health.get("cpu_fallback") is not False or health.get("model") != client.model:
            raise AcceptanceError("health did not prove the exact selected model without a route fallback")
        checks.append(Check("live worker health", True, f"ready {worker_label} exact model", elapsed))
    except AcceptanceError as error:
        checks.append(Check("live worker health", False, redact_detail(error), elapsed))
        return checks

    started = time.monotonic()
    status, capabilities = client.request_json("GET", "/v1/capabilities")
    elapsed = time.monotonic() - started
    try:
        client.require_success(status, capabilities, "capabilities")
        if capabilities.get("contract_version") != 1:
            raise AcceptanceError("unsupported Colab capability contract version")
        advertised = capabilities.get("capabilities")
        if not isinstance(advertised, list):
            raise AcceptanceError("capabilities did not contain a capability list")
        selected = next((item for item in advertised if item.get("id") == client.capability), None)
        if not isinstance(selected, dict):
            raise AcceptanceError("worker did not advertise the requested capability")
        models = selected.get("models")
        selected_model = next((item for item in models if item.get("id") == client.model), None) if isinstance(models, list) else None
        if not isinstance(selected_model, dict) or selected_model.get("loaded") is not True \
                or str(selected_model.get("device", "")).lower() != expected_device:
            raise AcceptanceError(f"worker did not advertise the selected loaded {expected_device} model")
        checks.append(Check("live exact-model capability", True, "capability and model match", elapsed))
    except AcceptanceError as error:
        checks.append(Check("live exact-model capability", False, redact_detail(error), elapsed))
    return checks


def exact_model_rejection_probe(client: WorkerClient) -> Check:
    """Prove the live endpoint refuses a request for any other model ID."""
    started = time.monotonic()
    wrong_model = "lastudio-live-acceptance-wrong-model"
    try:
        if client.capability == "stt":
            status, response = client.request_json("POST", "/v2/uploads/stt", {
                "model": wrong_model, "size_bytes": 1, "language": "auto", "response_format": "verbose_json",
            })
        elif client.capability == "tts":
            status, response = client.request_json("POST", "/v1/audio/speech", {
                "model": wrong_model, "input": "model guard", "voice": str(client.config.get("voice", "auto")),
                "language": str(client.config.get("language", "auto")), "response_format": "wav",
            })
        elif client.capability == "translation":
            status, response = client.request_json("POST", "/v1/translations", {
                "model": wrong_model, "source_language": str(client.config.get("source_language", "en")),
                "target_language": str(client.config.get("target_language", "vi")),
                "segments": [{"id": "wrong-model", "sourceText": "model guard"}],
            })
        elif client.capability == "llm-chat":
            status, response = client.request_json("POST", "/v1/chat/completions", {
                "model": wrong_model, "messages": [{"role": "user", "content": "model guard"}],
                "stream": True, "max_tokens": 1, "context_tokens": 512, "temperature": 0.2,
                "top_p": 0.8, "top_k": 20, "repeat_penalty": 1.05,
            })
        elif client.capability == "voice-design":
            status, response = client.request_json("POST", "/v1/audio/voice_designs", {
                "model": wrong_model, "input": "model guard",
                "voice_description": require_string(client.config, "voice_description", "voice-design"),
                "language": str(client.config.get("language", "en")), "temperature": 0.7,
                "seed": 42, "response_format": "wav",
            })
        elif client.capability == "subtitle-ocr":
            image = require_subtitle_image_config(client)
            fields, file_field, path = {
                "model": wrong_model,
                "language": str(client.config.get("language", "en")),
            }, "file", "/v1/ocr/subtitles"
            body, content_type = encode_multipart(fields, file_field, image)
            status, raw, _ = client.request("POST", path, body, content_type, "application/json")
            try:
                response = json.loads(raw.decode("utf-8")) if raw else {}
            except (UnicodeDecodeError, json.JSONDecodeError):
                response = {}
        else:
            audio = require_audio_config(client)
            if client.capability == "forced-alignment":
                fields, file_field, path = {
                    "model": wrong_model,
                    "transcript": require_string(client.config, "transcript", "forced-alignment"),
                    "language": str(client.config.get("language", "en")),
                }, "audio", "/v1/audio/alignments"
            elif client.capability == "voice-isolation":
                fields, file_field, path = {"model": wrong_model, "stems": "vocals,background"}, "file", "/v1/audio/separations"
            elif client.capability == "voice-cloning":
                fields, file_field, path = {
                    "model": wrong_model, "name": "live-acceptance-model-guard", "consent_confirmed": "true",
                    "ref_text": require_string(client.config, "reference_text", "voice-cloning"),
                    "language": str(client.config.get("language", "vi")), "separate_music": "false",
                }, "ref_audio", "/v2/jobs/profile"
            else:  # Kept defensive even though the constructor rejects unknown capabilities.
                raise AcceptanceError(f"No exact-model probe for {client.capability}")
            body, content_type = encode_multipart(fields, file_field, audio)
            status, raw, _ = client.request("POST", path, body, content_type, "application/json")
            try:
                response = json.loads(raw.decode("utf-8")) if raw else {}
            except (UnicodeDecodeError, json.JSONDecodeError):
                response = {}
        if status != 409:
            detail = response.get("detail", response) if isinstance(response, dict) else response
            raise AcceptanceError(f"wrong-model request returned HTTP {status}, expected 409: {redact_detail(detail)}")
        return Check("live wrong-model rejection", True, "worker returned HTTP 409 for a different model ID", time.monotonic() - started)
    except AcceptanceError as error:
        return Check("live wrong-model rejection", False, redact_detail(error), time.monotonic() - started)


def expect_wav(client: WorkerClient, path: str, payload: dict[str, Any], action: str) -> Check:
    started = time.monotonic()
    status, body, _ = client.request("POST", path, json.dumps(payload, ensure_ascii=False).encode("utf-8"),
                                     "application/json", "audio/wav, application/octet-stream")
    elapsed = time.monotonic() - started
    if not 200 <= status < 300:
        return Check(action, False, f"HTTP {status}: {redact_detail(body.decode('utf-8', 'replace'))}", elapsed)
    if len(body) < 44 or body[:4] != b"RIFF" or body[8:12] != b"WAVE":
        return Check(action, False, "worker did not return a valid WAV container", elapsed)
    return Check(action, True, f"received WAV ({len(body)} bytes)", elapsed)


def require_audio_config(client: WorkerClient) -> Path:
    return assert_audio_file(Path(require_string(client.config, "audio_path", client.capability)), client.capability)


def require_subtitle_image_config(client: WorkerClient) -> Path:
    path = Path(require_string(client.config, "image_path", "subtitle-ocr")).expanduser().resolve()
    if not path.is_file() or path.stat().st_size <= 0:
        raise AcceptanceError("subtitle-ocr requires a readable non-empty image_path")
    if path.stat().st_size > 16 * 1024 * 1024:
        raise AcceptanceError("subtitle-ocr image_path exceeds the 16 MiB worker limit")
    if path.suffix.lower() != ".png" or not path.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
        raise AcceptanceError("subtitle-ocr image_path must be a PNG crop frame")
    return path


def poll_job(client: WorkerClient, path: str, job_id: str, result_statuses: set[str],
             progress_key: str = "progress") -> tuple[dict[str, Any], list[int]]:
    maximum = int(client.config.get("job_timeout_seconds", 900))
    if maximum < 10 or maximum > 3600:
        raise AcceptanceError("job_timeout_seconds must be between 10 and 3600")
    deadline = time.monotonic() + maximum
    progress: list[int] = []
    while time.monotonic() < deadline:
        status, payload = client.request_json("GET", path.format(job_id=job_id))
        client.require_success(status, payload, "job status")
        if isinstance(payload.get(progress_key), (int, float)):
            value = int(payload[progress_key])
            if progress and value < progress[-1]:
                raise AcceptanceError(f"worker progress regressed from {progress[-1]} to {value}")
            progress.append(value)
        state = str(payload.get("status", "")).lower()
        if state in result_statuses:
            return payload, progress
        if state in {"failed", "cancelled", "canceled"}:
            raise AcceptanceError(f"job ended as {state}: {redact_detail(payload.get('detail', payload.get('error', '')))}")
        time.sleep(2.0)
    raise AcceptanceError("job did not finish before job_timeout_seconds")


def run_subtitle_ocr(client: WorkerClient) -> Check:
    """Run a real visible subtitle crop through the exact OCR worker."""
    started = time.monotonic()
    try:
        image = require_subtitle_image_config(client)
        body, content_type = encode_multipart({
            "model": client.model,
            "language": str(client.config.get("language", "en")),
        }, "file", image)
        status, raw, _ = client.request("POST", "/v1/ocr/subtitles", body, content_type, "application/json")
        try:
            response = json.loads(raw.decode("utf-8")) if raw else {}
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise AcceptanceError("Subtitle OCR returned a non-JSON response") from error
        response = client.require_success(status, response, "Subtitle OCR")
        if not isinstance(response.get("text"), str) or not response["text"].strip():
            raise AcceptanceError("Subtitle OCR did not return non-empty text; use a crop containing a readable subtitle")
        confidence = response.get("confidence")
        if not isinstance(confidence, (int, float)) or not 0.0 <= float(confidence) <= 1.0:
            raise AcceptanceError("Subtitle OCR returned an invalid confidence")
        return Check("real Subtitle OCR inference", True,
                     "received non-empty exact-model text and confidence", time.monotonic() - started)
    except (AcceptanceError, json.JSONDecodeError) as error:
        return Check("real Subtitle OCR inference", False, redact_detail(error), time.monotonic() - started)


def run_stt(client: WorkerClient) -> Check:
    started = time.monotonic()
    try:
        audio = require_audio_config(client)
        content = audio.read_bytes()
        status, upload = client.request_json("POST", "/v2/uploads/stt", {
            "model": client.model, "size_bytes": len(content),
            "language": str(client.config.get("language", "auto")), "response_format": "verbose_json",
        })
        upload = client.require_success(status, upload, "STT upload creation")
        upload_id = require_string(upload, "upload_id", "STT upload response")
        chunk_size = int(upload.get("chunk_bytes", 2 * 1024 * 1024))
        if chunk_size <= 0 or chunk_size > 8 * 1024 * 1024:
            raise AcceptanceError("STT worker reported an invalid chunk size")
        for index, offset in enumerate(range(0, len(content), chunk_size)):
            status, body, _ = client.request("PUT", f"/v2/uploads/stt/{upload_id}/chunks/{index}",
                                             content[offset: offset + chunk_size], "application/octet-stream", "application/json")
            if not 200 <= status < 300:
                raise AcceptanceError(f"STT chunk {index} failed with HTTP {status}: {redact_detail(body.decode('utf-8', 'replace'))}")
        # Match the desktop client exactly: this endpoint has no JSON body.
        status, raw_job, _ = client.request("POST", f"/v2/uploads/stt/{upload_id}/commit", None, None, "application/json")
        try:
            job = json.loads(raw_job.decode("utf-8")) if raw_job else {}
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise AcceptanceError("STT upload commit returned non-JSON response") from error
        if not isinstance(job, dict):
            raise AcceptanceError("STT upload commit returned a JSON value other than an object")
        job = client.require_success(status, job, "STT upload commit")
        job_id = require_string(job, "job_id", "STT job response")
        complete, progress = poll_job(client, "/v2/jobs/transcriptions/{job_id}", job_id, {"succeeded"})
        result = complete.get("result")
        if not isinstance(result, dict) or result.get("model") != client.model or not str(result.get("text", "")).strip():
            raise AcceptanceError("STT job did not return non-empty text from the exact selected model")
        if not progress or progress[-1] != 100:
            raise AcceptanceError("STT job did not report completion progress of 100")
        return Check("real STT inference", True, f"completed with monotonic progress ({len(progress)} samples)", time.monotonic() - started)
    except AcceptanceError as error:
        return Check("real STT inference", False, redact_detail(error), time.monotonic() - started)


def run_tts(client: WorkerClient) -> Check:
    return expect_wav(client, "/v1/audio/speech", {
        "model": client.model,
        "input": str(client.config.get("text", "LA Studio live model validation.")),
        "voice": str(client.config.get("voice", "auto")),
        "language": str(client.config.get("language", "auto")),
        "speed": float(client.config.get("speed", 1.0)),
        "response_format": "wav",
    }, "real TTS inference")


def run_translation(client: WorkerClient) -> Check:
    started = time.monotonic()
    try:
        source = str(client.config.get("source_language", "en")).strip()
        target = str(client.config.get("target_language", "vi")).strip()
        text = str(client.config.get("text", "LA Studio validation.")).strip()
        status, response = client.request_json("POST", "/v1/translations", {
            "model": client.model, "source_language": source, "target_language": target,
            "segments": [{"id": "live-acceptance-1", "sourceText": text}],
        })
        response = client.require_success(status, response, "translation")
        patches = response.get("patches")
        if not isinstance(patches, list) or len(patches) != 1 or patches[0].get("id") != "live-acceptance-1" or not str(patches[0].get("targetText", "")).strip():
            raise AcceptanceError("translation worker returned an invalid or empty patch")
        return Check("real translation inference", True, "received one non-empty exact-model patch", time.monotonic() - started)
    except AcceptanceError as error:
        return Check("real translation inference", False, redact_detail(error), time.monotonic() - started)


def run_chat(client: WorkerClient) -> Check:
    started = time.monotonic()
    try:
        status, body, _ = client.request("POST", "/v1/chat/completions", json.dumps({
            "model": client.model,
            "messages": [{"role": "user", "content": str(client.config.get("prompt", "Reply with the single word ready."))}],
            "stream": True, "max_tokens": int(client.config.get("max_tokens", 32)),
            "context_tokens": int(client.config.get("context_tokens", 2048)),
            "temperature": float(client.config.get("temperature", 0.2)), "top_p": 0.8,
            "top_k": 20, "repeat_penalty": 1.05,
        }, ensure_ascii=False).encode("utf-8"), "application/json", "text/event-stream")
        if not 200 <= status < 300:
            raise AcceptanceError(f"chat failed with HTTP {status}: {redact_detail(body.decode('utf-8', 'replace'))}")
        text = ""
        for line in body.decode("utf-8", "replace").splitlines():
            if not line.startswith("data:"):
                continue
            raw = line[5:].strip()
            if not raw or raw == "[DONE]":
                continue
            message = json.loads(raw)
            for choice in message.get("choices", []):
                text += str(choice.get("delta", {}).get("content", ""))
        if not text.strip():
            raise AcceptanceError("chat stream did not contain generated text")
        return Check("real chat inference", True, "received non-empty streaming output", time.monotonic() - started)
    except (AcceptanceError, json.JSONDecodeError) as error:
        return Check("real chat inference", False, redact_detail(error), time.monotonic() - started)


def run_voice_design(client: WorkerClient) -> Check:
    return expect_wav(client, "/v1/audio/voice_designs", {
        "model": client.model,
        "input": str(client.config.get("text", "LA Studio live model validation.")),
        "voice_description": require_string(client.config, "voice_description", "voice-design"),
        "style": str(client.config.get("style", "neutral")),
        "language": str(client.config.get("language", "en")),
        "temperature": float(client.config.get("temperature", 0.7)),
        "seed": int(client.config.get("seed", 42)), "response_format": "wav",
    }, "real VoiceDesign inference")


def run_alignment(client: WorkerClient) -> Check:
    started = time.monotonic()
    try:
        audio = require_audio_config(client)
        body, content_type = encode_multipart({
            "model": client.model, "transcript": require_string(client.config, "transcript", "forced-alignment"),
            "language": str(client.config.get("language", "en")),
        }, "audio", audio)
        status, raw, _ = client.request("POST", "/v1/audio/alignments", body, content_type, "application/json")
        try:
            response = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise AcceptanceError("alignment returned non-JSON response") from error
        if not 200 <= status < 300:
            raise AcceptanceError(f"alignment failed with HTTP {status}: {redact_detail(response.get('detail', response))}")
        segments = response.get("segments")
        if not isinstance(segments, list) or not segments:
            raise AcceptanceError("alignment returned no timestamped segments")
        previous_end = 0.0
        for segment in segments:
            start, end = float(segment.get("start", -1)), float(segment.get("end", -1))
            if start < previous_end or end <= start:
                raise AcceptanceError("alignment returned non-monotonic timestamps")
            previous_end = end
        return Check("real forced-alignment inference", True, f"received {len(segments)} monotonic segments", time.monotonic() - started)
    except AcceptanceError as error:
        return Check("real forced-alignment inference", False, redact_detail(error), time.monotonic() - started)


def run_separation(client: WorkerClient) -> Check:
    started = time.monotonic()
    try:
        audio = require_audio_config(client)
        body, content_type = encode_multipart({"model": client.model, "stems": "vocals,background"}, "file", audio)
        status, raw, _ = client.request("POST", "/v1/audio/separations", body, content_type, "application/json")
        response = json.loads(raw.decode("utf-8")) if raw else {}
        if not 200 <= status < 300:
            raise AcceptanceError(f"separation failed with HTTP {status}: {redact_detail(response.get('detail', response))}")
        job_id = require_string(response, "job_id", "separation job response")
        complete, progress = poll_job(client, "/v1/audio/separations/{job_id}", job_id, {"ready"})
        if not progress or progress[-1] != 100:
            raise AcceptanceError("separation did not report completion progress of 100")
        for stem in ("vocals", "background"):
            status, wav, _ = client.request("GET", f"/v1/audio/separations/{job_id}/artifacts/{stem}", None, None, "audio/wav")
            if not 200 <= status < 300 or len(wav) < 44 or wav[:4] != b"RIFF" or wav[8:12] != b"WAVE":
                raise AcceptanceError(f"separation {stem} artifact is not valid WAV audio")
        return Check("real voice-isolation inference", True, f"two WAV stems with monotonic progress ({len(progress)} samples)", time.monotonic() - started)
    except (AcceptanceError, json.JSONDecodeError) as error:
        return Check("real voice-isolation inference", False, redact_detail(error), time.monotonic() - started)


def run_voice_clone(client: WorkerClient) -> Check:
    started = time.monotonic()
    profile_id = ""
    try:
        audio = require_audio_config(client)
        body, content_type = encode_multipart({
            "model": client.model, "name": "live-acceptance", "consent_confirmed": "true",
            "ref_text": require_string(client.config, "reference_text", "voice-cloning"),
            "language": str(client.config.get("language", "vi")), "separate_music": "false",
        }, "ref_audio", audio)
        status, raw, _ = client.request("POST", "/v2/jobs/profile", body, content_type, "application/json")
        response = json.loads(raw.decode("utf-8")) if raw else {}
        if not 200 <= status < 300:
            raise AcceptanceError(f"voice profile failed with HTTP {status}: {redact_detail(response.get('detail', response))}")
        profile_job = require_string(response, "id", "voice profile job response")
        profile_complete, profile_progress = poll_job(client, "/v2/jobs/{job_id}", profile_job, {"succeeded"}, "percent")
        result = profile_complete.get("result")
        if not isinstance(result, dict) or result.get("model") != client.model:
            raise AcceptanceError("voice profile did not identify the exact selected model")
        profile_id = require_string(result, "id", "voice profile result")
        status, generation = client.request_json("POST", "/v2/jobs/generation", {
            "model": client.model, "profile_id": profile_id,
            "text": str(client.config.get("text", "LA Studio live voice cloning validation.")),
            "language": str(client.config.get("language", "vi")), "speed": 1.0, "num_step": 8,
        })
        generation = client.require_success(status, generation, "voice generation")
        generation_job = require_string(generation, "id", "voice generation job response")
        finished, generation_progress = poll_job(client, "/v2/jobs/{job_id}", generation_job, {"succeeded"}, "percent")
        generated = finished.get("result")
        if not isinstance(generated, dict) or generated.get("model") != client.model:
            raise AcceptanceError("voice generation did not identify the exact selected model")
        status, wav, _ = client.request("GET", f"/v2/jobs/{generation_job}/audio", None, None, "audio/wav")
        if not 200 <= status < 300 or len(wav) < 44 or wav[:4] != b"RIFF" or wav[8:12] != b"WAVE":
            raise AcceptanceError("voice generation did not return valid WAV audio")
        if not profile_progress or not generation_progress or profile_progress[-1] != 100 or generation_progress[-1] != 100:
            raise AcceptanceError("voice-cloning jobs did not report completion progress of 100")
        return Check("real voice-cloning inference", True, "profile and generated WAV completed on exact model", time.monotonic() - started)
    except (AcceptanceError, json.JSONDecodeError) as error:
        return Check("real voice-cloning inference", False, redact_detail(error), time.monotonic() - started)
    finally:
        if profile_id:
            try:
                client.request("DELETE", f"/v1/profiles/{profile_id}", None, None, "application/json")
            except AcceptanceError:
                pass


INFERENCE_RUNNERS: dict[str, Callable[[WorkerClient], Check]] = {
    "subtitle-ocr": run_subtitle_ocr,
    "stt": run_stt,
    "tts": run_tts,
    "translation": run_translation,
    "llm-chat": run_chat,
    "voice-design": run_voice_design,
    "forced-alignment": run_alignment,
    "voice-isolation": run_separation,
    "voice-cloning": run_voice_clone,
}


def render_report(reports: Iterable[WorkerReport], sensitive_values: Iterable[str] = ()) -> str:
    rendered = list(reports)
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    lines = [
        "# Live Colab acceptance report", "",
        f"Generated: {now}", "",
        "This report is real-worker evidence. It does not expose worker URLs or tokens. "
        "A worker passes only when the required worker health, exact-model capability, and one model-specific operation all pass.", "",
        "| Capability | Model | Result |",
        "| --- | --- | --- |",
    ]
    for report in rendered:
        lines.append(f"| {report.capability} | `{report.model}` | {'PASS' if report.passed else 'FAIL'} |")
    for report in rendered:
        lines.extend(("", f"## {report.capability} — `{report.model}`", "", "| Check | Result | Duration | Detail |", "| --- | --- | ---: | --- |"))
        for check in report.checks:
            detail = redact_with_secrets(check.detail, sensitive_values).replace("|", "\\|")
            lines.append(f"| {check.name} | {'PASS' if check.passed else 'FAIL'} | {check.elapsed_seconds:.1f}s | {detail} |")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True, help="JSON config with worker URL/token environment variable names")
    parser.add_argument("--report", type=Path, required=True, help="Markdown report to create")
    parser.add_argument("--only", action="append", default=[], help="Run only an exact capability or capability:model entry")
    parser.add_argument("--allow-http-localhost", action="store_true", help="Test-only: allow http://localhost workers")
    args = parser.parse_args()

    try:
        configured = read_config(args.config)
    except AcceptanceError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    requested = {entry.strip().lower() for entry in args.only if entry.strip()}
    reports: list[WorkerReport] = []
    sensitive_values: list[str] = []
    for config in configured:
        capability = str(config.get("capability", "")).strip().lower()
        model = str(config.get("model", "")).strip().lower()
        if requested and capability not in requested and f"{capability}:{model}" not in requested:
            continue
        report = WorkerReport(capability or "invalid", model or "invalid")
        try:
            client = WorkerClient(config, args.allow_http_localhost)
            sensitive_values.extend((client.token, client.base_url))
            report.checks.extend(exact_model_preflight(client))
            if report.passed:
                report.checks.append(exact_model_rejection_probe(client))
            if report.passed:
                report.checks.append(INFERENCE_RUNNERS[client.capability](client))
        except AcceptanceError as error:
            report.checks.append(Check("configuration", False, redact_detail(error)))
        reports.append(report)

    if not reports:
        print("error: --only did not match any configured worker", file=sys.stderr)
        return 2
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(render_report(reports, sensitive_values), encoding="utf-8")
    passed = sum(report.passed for report in reports)
    print(f"Live Colab acceptance: {passed}/{len(reports)} workers passed. Report: {args.report}")
    return 0 if passed == len(reports) else 1


if __name__ == "__main__":
    raise SystemExit(main())
