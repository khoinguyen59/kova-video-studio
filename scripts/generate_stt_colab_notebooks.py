"""Generate the four model-specific LA Studio STT Colab workers.

The notebooks intentionally load exactly one GPU model. The worker rejects a
request whose multipart `model` field does not match that loaded family ID.
"""

from __future__ import annotations

import json
from pathlib import Path
from textwrap import dedent


ROOT = Path(__file__).resolve().parents[1]
NOTEBOOKS = ROOT / "notebooks"

COMMON_SERVER = r'''
import asyncio
import os
import secrets
import tempfile
import threading
from pathlib import Path

import soundfile as sf
import torch
from fastapi import FastAPI, File, Form, Header, HTTPException, UploadFile

if not torch.cuda.is_available():
    raise RuntimeError("A Colab GPU runtime is required. Choose Runtime > Change runtime type > GPU.")

TOKEN = secrets.token_urlsafe(32)
MAX_UPLOAD_BYTES = 512 * 1024 * 1024
MAX_AUDIO_SECONDS = 30 * 60
ALLOWED_CONTENT_TYPES = {
    "audio/wav", "audio/x-wav", "audio/mpeg", "audio/mp3", "audio/mp4",
    "audio/flac", "audio/ogg", "audio/webm", "application/octet-stream",
}
REQUEST_SLOTS = threading.BoundedSemaphore(1)
JOB_TTL_SECONDS = 30 * 60
JOB_LOCK = threading.Lock()
JOBS: dict[str, dict] = {}

__MODEL_LOADER__

app = FastAPI(title=f"LA Studio STT — {{MODEL_NAME}}")


def require_token(authorization: str | None) -> None:
    if authorization != f"Bearer {{TOKEN}}":
        raise HTTPException(status_code=401, detail="Invalid or missing Colab session token")


@app.get("/health")
def health():
    return {{
        "ok": True,
        "ready": True,
        "device": "cuda",
        "gpu": torch.cuda.get_device_name(0),
        "model": MODEL_ID,
        "upstream_model": UPSTREAM_MODEL,
        "cpu_fallback": False,
    }}


@app.get("/v1/capabilities")
def capabilities(authorization: str | None = Header(default=None)):
    require_token(authorization)
    return {{
        "contract_version": 1,
        "device": "cuda",
        "cuda": True,
        "cpu_fallback": False,
        "endpoints": {
            "transcription_jobs": "/v2/jobs/transcriptions",
        },
        "capabilities": [{{
            "id": "stt",
            "models": [{{
                "id": MODEL_ID,
                "name": MODEL_NAME,
                "upstream_model": UPSTREAM_MODEL,
                "loaded": True,
                "device": "cuda",
            }}],
        }}],
    }}


async def save_upload(file: UploadFile) -> tuple[Path, int]:
    suffix = Path(file.filename or "audio.wav").suffix or ".wav"
    handle = tempfile.NamedTemporaryFile(prefix="la-studio-stt-", suffix=suffix, delete=False)
    path = Path(handle.name)
    total = 0
    try:
        with handle:
            while True:
                chunk = await file.read(1024 * 1024)
                if not chunk:
                    break
                total += len(chunk)
                if total > MAX_UPLOAD_BYTES:
                    raise HTTPException(status_code=413, detail="Audio upload exceeds 512 MiB")
                handle.write(chunk)
        return path, total
    except Exception:
        path.unlink(missing_ok=True)
        raise


def snapshot_job(job: dict) -> dict:
    response = {
        "job_id": job["job_id"],
        "status": job["status"],
        "progress": job["progress"],
        "model": MODEL_ID,
    }
    if job.get("detail"):
        response["detail"] = job["detail"]
    if job.get("result") is not None:
        response["result"] = job["result"]
    return response


def prune_finished_jobs() -> None:
    now = asyncio.get_running_loop().time()
    expired_paths = []
    with JOB_LOCK:
        expired = [
            job_id for job_id, job in JOBS.items()
            if job.get("finished_at") and now - job["finished_at"] > JOB_TTL_SECONDS
        ]
        for job_id in expired:
            job = JOBS.pop(job_id)
            if job.get("path"):
                expired_paths.append(Path(job["path"]))
    for path in expired_paths:
        path.unlink(missing_ok=True)


async def run_job(job_id: str) -> None:
    try:
        with JOB_LOCK:
            job = JOBS.get(job_id)
            if job is None:
                return
            if job.get("cancel_requested"):
                job["status"] = "cancelled"
                job["progress"] = 100
                job["detail"] = "Transcription cancelled"
                job["finished_at"] = asyncio.get_running_loop().time()
                return
            job["status"] = "running"
            job["progress"] = 15
            path = job["path"]
            language = job["language"]
            response_format = job["response_format"]
        result = await asyncio.to_thread(run_transcription, path, language)
        text = str(result.get("text", "")).strip()
        if not text:
            raise RuntimeError("The loaded model returned an empty transcript")
        payload = {
            "text": text,
            "segments": result.get("segments", []),
            "language": result.get("language", language),
            "model": MODEL_ID,
            "upstream_model": UPSTREAM_MODEL,
            "response_format": response_format,
        }
        with JOB_LOCK:
            job = JOBS.get(job_id)
            if job is not None:
                if job.get("cancel_requested"):
                    job["status"] = "cancelled"
                    job["detail"] = "Transcription cancelled"
                else:
                    job["status"] = "succeeded"
                    job["result"] = payload
                    job["progress"] = 100
                job["finished_at"] = asyncio.get_running_loop().time()
    except Exception as error:
        with JOB_LOCK:
            job = JOBS.get(job_id)
            if job is not None:
                if job.get("cancel_requested"):
                    job["status"] = "cancelled"
                    job["detail"] = "Transcription cancelled"
                else:
                    job["status"] = "failed"
                    job["detail"] = f"{MODEL_NAME} transcription failed: {type(error).__name__}: {str(error)[:300]}"
                job["progress"] = 100
                job["finished_at"] = asyncio.get_running_loop().time()
    finally:
        with JOB_LOCK:
            job = JOBS.get(job_id)
            source_path = Path(job["path"]) if job and job.get("path") else None
            if job is not None:
                job["path"] = None
        if source_path is not None:
            source_path.unlink(missing_ok=True)
        REQUEST_SLOTS.release()


@app.post("/v2/jobs/transcriptions", status_code=202)
async def create_transcription_job(
    file: UploadFile = File(...),
    model: str = Form(...),
    language: str = Form(default="auto"),
    response_format: str = Form(default="verbose_json"),
    authorization: str | None = Header(default=None),
):
    require_token(authorization)
    if model.strip().lower() != MODEL_ID:
        raise HTTPException(
            status_code=409,
            detail=f"This worker loaded '{{MODEL_ID}}', but LA Studio requested '{{model}}'. Open the notebook for the selected model.",
        )
    content_type = (file.content_type or "application/octet-stream").lower()
    if content_type not in ALLOWED_CONTENT_TYPES and not content_type.startswith("audio/"):
        raise HTTPException(status_code=415, detail=f"Unsupported audio content type: {{content_type}}")
    if not REQUEST_SLOTS.acquire(blocking=False):
        raise HTTPException(status_code=429, detail="The Colab GPU is already processing another transcription")

    path = None
    queued = False
    try:
        path, size = await save_upload(file)
        if size == 0:
            raise HTTPException(status_code=422, detail="The uploaded audio file is empty")
        try:
            info = sf.info(str(path))
            if info.duration > MAX_AUDIO_SECONDS:
                raise HTTPException(status_code=413, detail="Audio is longer than the 30 minute session limit")
        except HTTPException:
            raise
        except Exception:
            # Compressed formats may not be readable by libsndfile; the
            # model-specific decoder below remains the source of truth.
            pass
        await prune_finished_jobs()
        job_id = secrets.token_urlsafe(18)
        with JOB_LOCK:
            JOBS[job_id] = {
                "job_id": job_id,
                "status": "queued",
                "progress": 5,
                "path": str(path),
                "language": language,
                "response_format": response_format,
                "cancel_requested": False,
                "detail": "",
                "result": None,
            }
            response = snapshot_job(JOBS[job_id])
        asyncio.create_task(run_job(job_id))
        queued = True
        return response
    finally:
        await file.close()
        if not queued:
            if path is not None:
                path.unlink(missing_ok=True)
            REQUEST_SLOTS.release()


@app.get("/v2/jobs/transcriptions/{{job_id}}")
async def transcription_job_status(job_id: str,
                                   authorization: str | None = Header(default=None)):
    require_token(authorization)
    await prune_finished_jobs()
    with JOB_LOCK:
        job = JOBS.get(job_id)
        if job is None:
            raise HTTPException(status_code=404, detail="Transcription job was not found or has expired")
        return snapshot_job(job)


@app.delete("/v2/jobs/transcriptions/{{job_id}}")
async def cancel_transcription_job(job_id: str,
                                   authorization: str | None = Header(default=None)):
    require_token(authorization)
    with JOB_LOCK:
        job = JOBS.get(job_id)
        if job is None:
            raise HTTPException(status_code=404, detail="Transcription job was not found or has expired")
        if job["status"] in {{"queued", "running"}}:
            job["cancel_requested"] = True
            job["status"] = "cancelled"
            job["detail"] = "Transcription cancellation requested"
            job["progress"] = 100
        return snapshot_job(job)


@app.post("/v1/audio/transcriptions")
async def transcriptions(
    file: UploadFile = File(...),
    model: str = Form(...),
    language: str = Form(default="auto"),
    response_format: str = Form(default="verbose_json"),
    authorization: str | None = Header(default=None),
):
    require_token(authorization)
    if model.strip().lower() != MODEL_ID:
        raise HTTPException(
            status_code=409,
            detail=f"This worker loaded '{{MODEL_ID}}', but LA Studio requested '{{model}}'. Open the notebook for the selected model.",
        )
    content_type = (file.content_type or "application/octet-stream").lower()
    if content_type not in ALLOWED_CONTENT_TYPES and not content_type.startswith("audio/"):
        raise HTTPException(status_code=415, detail=f"Unsupported audio content type: {{content_type}}")
    if not REQUEST_SLOTS.acquire(blocking=False):
        raise HTTPException(status_code=429, detail="The Colab GPU is already processing another transcription")

    path = None
    try:
        path, size = await save_upload(file)
        if size == 0:
            raise HTTPException(status_code=422, detail="The uploaded audio file is empty")
        try:
            info = sf.info(str(path))
            if info.duration > MAX_AUDIO_SECONDS:
                raise HTTPException(status_code=413, detail="Audio is longer than the 30 minute session limit")
        except HTTPException:
            raise
        except Exception:
            # Compressed formats may not be readable by libsndfile; the
            # model-specific decoder below remains the source of truth.
            pass

        # Compatibility endpoint for older desktop builds. New LA Studio builds
        # use /v2/jobs/transcriptions so a long GPU run cannot hit the
        # Cloudflare 120-second proxy response limit.
        result = await asyncio.to_thread(run_transcription, str(path), language)
        text = str(result.get("text", "")).strip()
        if not text:
            raise HTTPException(status_code=502, detail="The loaded model returned an empty transcript")
        return {{
            "text": text,
            "segments": result.get("segments", []),
            "language": result.get("language", language),
            "model": MODEL_ID,
            "upstream_model": UPSTREAM_MODEL,
            "response_format": response_format,
        }}
    finally:
        if path is not None:
            path.unlink(missing_ok=True)
        REQUEST_SLOTS.release()
'''

LAUNCH = r'''
import queue
import re
import subprocess
import threading
import time
import urllib.request

def wait_for_health():
    deadline = time.time() + 60
    while time.time() < deadline:
        try:
            with urllib.request.urlopen("http://127.0.0.1:8000/health", timeout=2) as response:
                if response.status == 200:
                    return
        except Exception:
            time.sleep(0.5)
    raise RuntimeError("The local STT worker did not become healthy")

def run_server():
    import uvicorn
    uvicorn.run(app, host="127.0.0.1", port=8000, log_level="warning")

threading.Thread(target=run_server, daemon=True).start()
wait_for_health()

cloudflared_path = "/content/cloudflared"
subprocess.run(
    ["wget", "-q", "-O", cloudflared_path,
     "https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64"],
    check=True,
)
subprocess.run(["chmod", "+x", cloudflared_path], check=True)

process = subprocess.Popen(
    [cloudflared_path, "tunnel", "--url", "http://127.0.0.1:8000", "--no-autoupdate"],
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    bufsize=1,
)
lines = queue.Queue()

def collect_output():
    assert process.stdout is not None
    for line in process.stdout:
        lines.put(line)

threading.Thread(target=collect_output, daemon=True).start()
worker_url = ""
deadline = time.time() + 90
while time.time() < deadline and not worker_url:
    try:
        line = lines.get(timeout=1)
    except queue.Empty:
        if process.poll() is not None:
            raise RuntimeError("cloudflared exited before creating a public tunnel")
        continue
    match = re.search(r"https://[a-z0-9-]+\.trycloudflare\.com", line)
    if match:
        worker_url = match.group(0)

if not worker_url:
    process.terminate()
    raise RuntimeError("Timed out while creating the Cloudflare tunnel")

os.environ["LA_STUDIO_COLAB_STT_URL"] = worker_url
os.environ["LA_STUDIO_COLAB_STT_TOKEN"] = TOKEN
os.environ["LA_STUDIO_COLAB_STT_MODEL"] = MODEL_ID

print("\nLA Studio Colab STT worker is ready")
print("MODEL:", MODEL_ID)
print("URL:", worker_url)
print("TOKEN:", TOKEN)
print("\nPaste the URL and TOKEN into LA Studio. Keep this cell running.")
'''

MODELS = [
    {
        "file": "LA_STUDIO_STT_WHISPER_GPU.ipynb",
        "family_id": "whisper.cpp",
        "name": "Whisper large-v3 (faster-whisper CUDA)",
        "upstream": "large-v3",
        "install": "faster-whisper==1.2.1",
        "loader": r'''
from faster_whisper import WhisperModel

MODEL_ID = "whisper.cpp"
MODEL_NAME = "Whisper large-v3 (faster-whisper CUDA)"
UPSTREAM_MODEL = "large-v3"
stt_model = WhisperModel(UPSTREAM_MODEL, device="cuda", compute_type="float16")

def run_transcription(path: str, language: str):
    requested_language = language.strip().lower()
    if not requested_language or requested_language == "auto":
        requested_language = None
    segments, info = stt_model.transcribe(
        path,
        language=requested_language,
        beam_size=5,
        vad_filter=True,
    )
    rows = []
    text_parts = []
    for index, segment in enumerate(segments):
        clean = segment.text.strip()
        if clean:
            text_parts.append(clean)
        rows.append({"id": index, "start": float(segment.start), "end": float(segment.end), "text": clean})
    return {"text": " ".join(text_parts), "segments": rows, "language": info.language}
''',
    },
    {
        "file": "LA_STUDIO_STT_QWEN3_ASR_0_6B_GPU.ipynb",
        "family_id": "qwen3-asr-0.6b",
        "name": "Qwen3-ASR 0.6B",
        "upstream": "Qwen/Qwen3-ASR-0.6B",
        "install": "qwen-asr==0.0.6",
        "loader": r'''
from qwen_asr import Qwen3ASRModel

MODEL_ID = "qwen3-asr-0.6b"
MODEL_NAME = "Qwen3-ASR 0.6B"
UPSTREAM_MODEL = "Qwen/Qwen3-ASR-0.6B"
stt_model = Qwen3ASRModel.from_pretrained(
    UPSTREAM_MODEL,
    dtype=torch.bfloat16,
    device_map="cuda:0",
    max_inference_batch_size=1,
    max_new_tokens=2048,
)

QWEN_LANGUAGE_NAMES = {
    "ar": "Arabic", "cs": "Czech", "da": "Danish", "de": "German",
    "en": "English", "es": "Spanish", "fa": "Persian", "fi": "Finnish",
    "fil": "Filipino", "fr": "French", "el": "Greek", "hi": "Hindi",
    "hu": "Hungarian", "id": "Indonesian", "it": "Italian", "ja": "Japanese",
    "ko": "Korean", "ms": "Malay", "nl": "Dutch", "pl": "Polish",
    "pt": "Portuguese", "ro": "Romanian", "ru": "Russian", "sv": "Swedish",
    "th": "Thai", "tr": "Turkish", "vi": "Vietnamese", "yue": "Cantonese",
    "zh": "Chinese",
}

def run_transcription(path: str, language: str):
    requested = language.strip().lower()
    language_name = None if not requested or requested == "auto" else QWEN_LANGUAGE_NAMES.get(requested)
    results = stt_model.transcribe(audio=path, language=language_name)
    result = results[0]
    return {"text": result.text, "segments": [], "language": result.language}
''',
    },
    {
        "file": "LA_STUDIO_STT_QWEN3_ASR_1_7B_GPU.ipynb",
        "family_id": "qwen3-asr-1.7b",
        "name": "Qwen3-ASR 1.7B",
        "upstream": "Qwen/Qwen3-ASR-1.7B",
        "install": "qwen-asr==0.0.6",
        "loader": r'''
from qwen_asr import Qwen3ASRModel

MODEL_ID = "qwen3-asr-1.7b"
MODEL_NAME = "Qwen3-ASR 1.7B"
UPSTREAM_MODEL = "Qwen/Qwen3-ASR-1.7B"
stt_model = Qwen3ASRModel.from_pretrained(
    UPSTREAM_MODEL,
    dtype=torch.bfloat16,
    device_map="cuda:0",
    max_inference_batch_size=1,
    max_new_tokens=2048,
)

QWEN_LANGUAGE_NAMES = {
    "ar": "Arabic", "cs": "Czech", "da": "Danish", "de": "German",
    "en": "English", "es": "Spanish", "fa": "Persian", "fi": "Finnish",
    "fil": "Filipino", "fr": "French", "el": "Greek", "hi": "Hindi",
    "hu": "Hungarian", "id": "Indonesian", "it": "Italian", "ja": "Japanese",
    "ko": "Korean", "ms": "Malay", "nl": "Dutch", "pl": "Polish",
    "pt": "Portuguese", "ro": "Romanian", "ru": "Russian", "sv": "Swedish",
    "th": "Thai", "tr": "Turkish", "vi": "Vietnamese", "yue": "Cantonese",
    "zh": "Chinese",
}

def run_transcription(path: str, language: str):
    requested = language.strip().lower()
    language_name = None if not requested or requested == "auto" else QWEN_LANGUAGE_NAMES.get(requested)
    results = stt_model.transcribe(audio=path, language=language_name)
    result = results[0]
    return {"text": result.text, "segments": [], "language": result.language}
''',
    },
    {
        "file": "LA_STUDIO_STT_NEMOTRON_3_5_0_6B_GPU.ipynb",
        "family_id": "nemotron-3.5-asr-streaming-0.6b",
        "name": "Nemotron-3.5 ASR Streaming 0.6B",
        "upstream": "nvidia/nemotron-3.5-asr-streaming-0.6b",
        "install": "transformers==5.14.1 accelerate==1.10.1 librosa==0.11.0",
        "loader": r'''
import librosa
from transformers import AutoModelForRNNT, AutoProcessor

MODEL_ID = "nemotron-3.5-asr-streaming-0.6b"
MODEL_NAME = "Nemotron-3.5 ASR Streaming 0.6B"
UPSTREAM_MODEL = "nvidia/nemotron-3.5-asr-streaming-0.6b"
processor = AutoProcessor.from_pretrained(UPSTREAM_MODEL)
stt_model = AutoModelForRNNT.from_pretrained(
    UPSTREAM_MODEL,
    device_map="auto",
    dtype=torch.float16,
)
if not next(stt_model.parameters()).is_cuda:
    raise RuntimeError("Nemotron was not loaded on CUDA")

def run_transcription(path: str, language: str):
    sampling_rate = processor.feature_extractor.sampling_rate
    audio, _ = librosa.load(path, sr=sampling_rate, mono=True)
    requested = language.strip() or "auto"
    inputs = processor(
        audio,
        sampling_rate=sampling_rate,
        language=requested,
        return_tensors="pt",
    )
    inputs = inputs.to(stt_model.device, dtype=stt_model.dtype)
    with torch.inference_mode():
        output = stt_model.generate(**inputs, return_dict_in_generate=True)
    text = processor.decode(output.sequences, skip_special_tokens=True).strip()
    return {"text": text, "segments": [], "language": requested}
''',
    },
]


def lines(value: str) -> list[str]:
    text = dedent(value).strip() + "\n"
    return text.splitlines(keepends=True)


def notebook(spec: dict[str, str]) -> dict:
    install = (
        "%pip install -q "
        f"{spec['install']} "
        "fastapi==0.115.12 uvicorn==0.34.3 python-multipart==0.0.20 "
        "soundfile==0.13.1\n"
    )
    server = (
        COMMON_SERVER.replace("__MODEL_LOADER__", dedent(spec["loader"]).strip())
        .replace("{{", "{")
        .replace("}}", "}")
    )
    return {
        "cells": [
            {
                "cell_type": "markdown",
                "metadata": {},
                "source": lines(
                    f"""
                    # LA Studio — {spec['name']} Colab GPU worker

                    This notebook loads exactly `{spec['family_id']}` on the Colab GPU.
                    It rejects transcription requests for every other model ID.

                    Long recordings use an asynchronous GPU job: the app uploads the
                    audio once, then polls short status requests until this exact model
                    completes. This avoids Cloudflare's 120-second proxy response limit.

                    Run every cell in order, then paste the printed URL and TOKEN into
                    LA Studio. The tunnel is public, but every worker endpoint requires
                    the random session token printed by the last cell.
                    """
                ),
            },
            {
                "cell_type": "code",
                "execution_count": None,
                "metadata": {},
                "outputs": [],
                "source": lines(install),
            },
            {
                "cell_type": "code",
                "execution_count": None,
                "metadata": {},
                "outputs": [],
                "source": lines(server),
            },
            {
                "cell_type": "code",
                "execution_count": None,
                "metadata": {},
                "outputs": [],
                "source": lines(LAUNCH),
            },
        ],
        "metadata": {
            "accelerator": "GPU",
            "colab": {"gpuType": "T4", "provenance": []},
            "kernelspec": {
                "display_name": "Python 3",
                "language": "python",
                "name": "python3",
            },
            "language_info": {"name": "python"},
            "la_studio": {
                "capability": "stt",
                "family_id": spec["family_id"],
                "upstream_model": spec["upstream"],
                "contract_version": 1,
                "device": "cuda",
                "cpu_fallback": False,
            },
        },
        "nbformat": 4,
        "nbformat_minor": 5,
    }


def main() -> None:
    NOTEBOOKS.mkdir(parents=True, exist_ok=True)
    for spec in MODELS:
        target = NOTEBOOKS / spec["file"]
        target.write_text(json.dumps(notebook(spec), indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
        print(target.relative_to(ROOT))


if __name__ == "__main__":
    main()
