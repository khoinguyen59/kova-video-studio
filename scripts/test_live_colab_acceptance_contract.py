#!/usr/bin/env python3
"""Exercise every live-acceptance HTTP path against a local deterministic worker.

This validates the acceptance *runner*, not a Colab model.  A real model is
accepted only by run_live_colab_acceptance.py against a user-owned HTTPS Colab
worker without --allow-http-localhost.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import threading
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "scripts" / "run_live_colab_acceptance.py"
TOKENS = {capability: f"local-contract-{capability}-token" for capability in (
    "stt", "tts", "translation", "llm-chat", "voice-design",
    "forced-alignment", "voice-isolation", "voice-cloning", "subtitle-ocr",
)}
WAV = b"RIFF" + (36).to_bytes(4, "little") + b"WAVEfmt " + (16).to_bytes(4, "little") \
    + (1).to_bytes(2, "little") + (1).to_bytes(2, "little") + (16000).to_bytes(4, "little") \
    + (32000).to_bytes(4, "little") + (2).to_bytes(2, "little") + (16).to_bytes(2, "little") \
    + b"data" + (0).to_bytes(4, "little")

MODELS = {
    "stt": "whisper.cpp",
    "tts": "kokoro",
    "translation": "m2m100-418m",
    "llm-chat": "qwen3.5-2b",
    "voice-design": "omnivoice",
    "forced-alignment": "canary-ctc-aligner",
    "voice-isolation": "sherpa-onnx-spleeter-2stems-fp16",
    "voice-cloning": "omnivoice",
    "subtitle-ocr": "pp-ocrv5-multilingual-3.1",
}
PNG = (b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01"
       b"\x08\x02\x00\x00\x00\x90wS\xde\x00\x00\x00\x0cIDAT\x08\xd7c``\x00\x00\x00\x04\x00\x01"
       b"\xf6\x178U\x00\x00\x00\x00IEND\xaeB`\x82")


def json_bytes(payload: dict[str, Any]) -> bytes:
    return json.dumps(payload, separators=(",", ":")).encode("utf-8")


class AcceptanceFixture(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, _format: str, *_args: object) -> None:
        pass

    def body(self) -> bytes:
        return self.rfile.read(int(self.headers.get("Content-Length", "0")))

    def send_json(self, status: int, payload: dict[str, Any]) -> None:
        body = json_bytes(payload)
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_wav(self) -> None:
        self.send_response(200)
        self.send_header("Content-Type", "audio/wav")
        self.send_header("Content-Length", str(len(WAV)))
        self.end_headers()
        self.wfile.write(WAV)

    def selected(self) -> tuple[str, str] | None:
        authorization = self.headers.get("Authorization", "")
        for capability, token in TOKENS.items():
            if authorization == f"Bearer {token}":
                return capability, MODELS[capability]
        return None

    @staticmethod
    def requested_model(body: bytes) -> str:
        try:
            value = json.loads(body.decode("utf-8"))
            if isinstance(value, dict):
                return str(value.get("model", "")).strip().lower()
        except (UnicodeDecodeError, json.JSONDecodeError):
            pass
        for model in (*MODELS.values(), "lastudio-live-acceptance-wrong-model"):
            if model.encode("utf-8") in body:
                return model
        return ""

    def require_token(self) -> bool:
        if self.selected() is not None:
            return True
        self.send_json(401, {"detail": "missing contract token"})
        return False

    def do_GET(self) -> None:
        if not self.require_token():
            return
        path = urlparse(self.path).path
        if path == "/health":
            capability, model = self.selected() or ("", "")
            device = "cuda"
            self.send_json(200, {"ready": True, "device": device, "model": model,
                                 "cpu_fallback": False})
            return
        if path == "/v1/capabilities":
            capability, model = self.selected() or ("", "")
            device = "cuda"
            model_entry = {"id": model, "loaded": True, "device": device}
            self.send_json(200, {"contract_version": 1, "device": device, "capabilities": [{
                "id": capability, "models": [model_entry],
            }]})
            return
        if path == "/v1/media/downloads/media-job":
            self.send_json(200, {"state": "ready", "received_bytes": len(WAV),
                                 "total_bytes": len(WAV), "file_name": "fixture.wav"})
            return
        if path == "/v1/media/downloads/media-job/file":
            self.send_wav()
            return
        if path == "/v2/jobs/transcriptions/stt-job":
            self.send_json(200, {"status": "succeeded", "progress": 100,
                                 "result": {"model": MODELS["stt"], "text": "ready"}})
            return
        if path == "/v1/audio/separations/separation-job":
            self.send_json(200, {"status": "ready", "progress": 100})
            return
        if path.startswith("/v1/audio/separations/separation-job/artifacts/"):
            self.send_wav()
            return
        if path == "/v2/jobs/profile-job":
            self.send_json(200, {"status": "succeeded", "percent": 100,
                                 "result": {"model": MODELS["voice-cloning"], "id": "profile-1"}})
            return
        if path == "/v2/jobs/generation-job":
            self.send_json(200, {"status": "succeeded", "percent": 100,
                                 "result": {"model": MODELS["voice-cloning"]}})
            return
        if path == "/v2/jobs/generation-job/audio":
            self.send_wav()
            return
        self.send_json(404, {"detail": path})

    def do_PUT(self) -> None:
        if not self.require_token():
            return
        if urlparse(self.path).path.startswith("/v2/uploads/stt/stt-upload/chunks/"):
            self.body()
            self.send_json(200, {"ok": True})
            return
        self.send_json(404, {"detail": self.path})

    def do_DELETE(self) -> None:
        if not self.require_token():
            return
        self.send_json(200, {"deleted": True})

    def do_POST(self) -> None:
        if not self.require_token():
            return
        path = urlparse(self.path).path
        body = self.body()
        if path == "/v2/uploads/stt/stt-upload/commit":
            self.send_json(200, {"job_id": "stt-job"})
            return
        identity = self.selected()
        if identity is None:
            self.send_json(404, {"detail": path})
            return
        capability, expected_model = identity
        requested_model = self.requested_model(body)
        if requested_model != expected_model:
            self.send_json(409, {"detail": "exact model required"})
            return
        if path == "/v2/uploads/stt":
            self.send_json(200, {"upload_id": "stt-upload", "chunk_bytes": 1024})
        elif path == "/v1/audio/speech" or path == "/v1/audio/voice_designs":
            self.send_wav()
        elif path == "/v1/translations":
            self.send_json(200, {"patches": [{"id": "live-acceptance-1", "targetText": "sẵn sàng"}]})
        elif path == "/v1/chat/completions":
            response = b'data: {"choices":[{"delta":{"content":"ready"}}]}\n\ndata: [DONE]\n\n'
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Content-Length", str(len(response)))
            self.end_headers()
            self.wfile.write(response)
        elif path == "/v1/audio/alignments":
            self.send_json(200, {"segments": [{"start": 0.0, "end": 0.5, "text": "ready"}]})
        elif path == "/v1/ocr/subtitles":
            self.send_json(200, {"text": "ready", "confidence": 0.99})
        elif path == "/v1/audio/separations":
            self.send_json(200, {"job_id": "separation-job"})
        elif path == "/v2/jobs/profile":
            self.send_json(200, {"id": "profile-job"})
        elif path == "/v2/jobs/generation":
            self.send_json(200, {"id": "generation-job"})
        else:
            self.send_json(404, {"detail": path})


def write_sample_wav(path: Path) -> None:
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(16000)
        output.writeframes(b"\0\0" * 160)


def worker_config(base_url: str, audio_path: Path, image_path: Path) -> list[dict[str, Any]]:
    workers: list[dict[str, Any]] = []
    for capability, model in MODELS.items():
        prefix = "LASTUDIO_CONTRACT_" + capability.upper().replace("-", "_")
        worker: dict[str, Any] = {
            "capability": capability,
            "model": model,
            "url_env": prefix + "_URL",
            "token_env": prefix + "_TOKEN",
        "request_timeout_seconds": 10,
        }
        if capability in {"stt", "forced-alignment", "voice-isolation", "voice-cloning"}:
            worker["audio_path"] = str(audio_path)
        if capability == "forced-alignment":
            worker["transcript"] = "ready"
        if capability == "voice-cloning":
            worker["reference_text"] = "ready"
        if capability == "voice-design":
            worker["voice_description"] = "calm narrator"
        if capability == "subtitle-ocr":
            worker["image_path"] = str(image_path)
            worker["language"] = "en"
        workers.append(worker)
        os.environ[prefix + "_URL"] = base_url
        os.environ[prefix + "_TOKEN"] = TOKENS[capability]
    return workers


def main() -> int:
    server = ThreadingHTTPServer(("127.0.0.1", 0), AcceptanceFixture)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix="lastudio-live-acceptance-contract-") as temporary:
            root = Path(temporary)
            audio_path = root / "sample.wav"
            image_path = root / "subtitle.png"
            config_path = root / "workers.json"
            report_path = root / "report.md"
            write_sample_wav(audio_path)
            image_path.write_bytes(PNG)
            base_url = f"http://127.0.0.1:{server.server_port}"
            config_path.write_text(json.dumps({"workers": worker_config(base_url, audio_path, image_path)}), encoding="utf-8")
            completed = subprocess.run(
                [sys.executable, str(RUNNER), "--config", str(config_path), "--report", str(report_path),
                 "--allow-http-localhost"],
                cwd=ROOT, capture_output=True, text=True, check=False,
            )
            if completed.returncode != 0:
                raise RuntimeError("live acceptance contract failed:\n" + completed.stdout + completed.stderr)
            report = report_path.read_text(encoding="utf-8")
            if "9/9 workers passed" not in completed.stdout:
                raise RuntimeError("runner did not report all nine capability paths as passed")
            if base_url in report or any(token in report for token in TOKENS.values()):
                raise RuntimeError("acceptance report exposed a local worker secret")
    finally:
        server.shutdown()
        server.server_close()
    print("Live Colab acceptance runner contract verified: 9 capability paths.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
