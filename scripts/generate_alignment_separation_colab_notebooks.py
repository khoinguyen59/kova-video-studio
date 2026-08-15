"""Generate exact-model Colab workers for alignment and voice isolation.

Each notebook loads one catalog family on CUDA, advertises only that family,
and returns HTTP 409 if LA Studio requests a different model.  Direct Colab
workers are deliberately independent from API Gateway.
"""

from __future__ import annotations

import json
from pathlib import Path
from textwrap import dedent

from colab_worker_launch import build_worker_launch

ROOT = Path(__file__).resolve().parents[1]
NOTEBOOKS = ROOT / "notebooks"


ALIGNMENT_COMMON = r'''
import os
import re
import subprocess
import tempfile
import threading
from pathlib import Path

import torch
from fastapi import FastAPI, File, Form, Header, HTTPException, UploadFile

if not torch.cuda.is_available():
    raise RuntimeError("CUDA is unavailable. In Colab choose Runtime > Change runtime type > GPU, then Run all.")

# __ADAPTER__

TOKEN = os.environ["LA_STUDIO_COLAB_ALIGNMENT_TOKEN"]
MAX_UPLOAD_BYTES = 512 * 1024 * 1024
MAX_AUDIO_SECONDS = 300
ALLOWED_CONTENT_TYPES = {
    "audio/wav", "audio/x-wav", "audio/mpeg", "audio/mp4", "audio/webm",
    "audio/ogg", "audio/flac", "application/octet-stream",
}
ALLOWED_EXTENSIONS = {".wav", ".mp3", ".m4a", ".mp4", ".webm", ".ogg", ".flac"}
REQUEST_SLOTS = threading.BoundedSemaphore(1)
MODEL_LOCK = threading.Lock()

def authorize(authorization: str | None) -> None:
    if authorization != "Bearer " + TOKEN:
        raise HTTPException(status_code=401, detail="invalid worker token")

def require_exact_model(requested: str) -> None:
    if requested.strip().lower() != MODEL_ID:
        raise HTTPException(
            status_code=409,
            detail=f"This worker loaded '{MODEL_ID}', but LA Studio requested '{requested}'. Open the notebook for the selected model.",
        )

def media_duration_seconds(path: str) -> float:
    probe = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries", "format=duration",
         "-of", "default=nokey=1:noprint_wrappers=1", path],
        text=True, capture_output=True,
    )
    try:
        duration = float(probe.stdout.strip())
    except ValueError:
        duration = 0.0
    if probe.returncode != 0 or duration <= 0.0:
        raise HTTPException(status_code=415, detail="audio is unsupported or could not be decoded")
    return duration

def validate_segments(raw_segments) -> list[dict]:
    segments = []
    previous_end = 0.0
    for raw in raw_segments:
        text = str(raw.get("text", "")).strip()
        if not text:
            continue
        start = float(raw.get("start", raw.get("start_time", 0.0)))
        end = float(raw.get("end", raw.get("end_time", start)))
        score = max(0.0, min(1.0, float(raw.get("score", raw.get("confidence", 1.0)))))
        if start < 0.0 or end < start or start + 0.002 < previous_end:
            raise RuntimeError("aligner returned non-monotonic timestamps")
        segments.append({"text": text, "start": start, "end": end, "score": score, "kind": raw.get("kind", "word")})
        previous_end = end
    if not segments:
        raise RuntimeError("aligner returned no timestamped tokens")
    return segments

app = FastAPI(title=f"LA Studio Alignment - {MODEL_NAME}", docs_url=None, redoc_url=None, openapi_url=None)

@app.get("/health")
@app.get("/v1/health")
def health(authorization: str | None = Header(default=None)):
    authorize(authorization)
    return {
        "status": "ready",
        "ready": True,
        "device": "cuda",
        "gpu": torch.cuda.get_device_name(0),
        "model": MODEL_ID,
        "variant": "fixed",
        "upstream_model": UPSTREAM_MODEL,
        "cpu_fallback": False,
    }

@app.get("/v1/capabilities")
def capabilities(authorization: str | None = Header(default=None)):
    authorize(authorization)
    return {
        "contract_version": 1,
        "device": "cuda",
        "capabilities": [{
            "id": "forced-alignment",
            "models": [{
                "id": MODEL_ID,
                "name": MODEL_NAME,
                "variant": "fixed",
                "upstream_model": UPSTREAM_MODEL,
                "languages": SUPPORTED_LANGUAGES,
                "max_audio_seconds": MAX_AUDIO_SECONDS,
                "device": "cuda",
                "loaded": True,
            }],
        }],
    }

@app.post("/v1/audio/alignments")
async def align(
    audio: UploadFile = File(...),
    transcript: str = Form(...),
    language: str = Form("en"),
    model: str = Form(...),
    authorization: str | None = Header(default=None),
):
    authorize(authorization)
    require_exact_model(model)
    text = transcript.strip()
    if not text:
        raise HTTPException(status_code=422, detail="transcript is required")
    suffix = Path(audio.filename or "audio.wav").suffix.lower() or ".wav"
    if suffix not in ALLOWED_EXTENSIONS:
        raise HTTPException(status_code=415, detail="unsupported audio filename extension")
    if audio.content_type and audio.content_type not in ALLOWED_CONTENT_TYPES:
        raise HTTPException(status_code=415, detail="unsupported audio MIME type")
    if not REQUEST_SLOTS.acquire(blocking=False):
        raise HTTPException(status_code=429, detail="the Colab alignment worker is busy; retry shortly")
    source_path = None
    try:
        descriptor, source_path = tempfile.mkstemp(suffix=suffix)
        with os.fdopen(descriptor, "wb") as output:
            while chunk := await audio.read(1024 * 1024):
                output.write(chunk)
                if output.tell() > MAX_UPLOAD_BYTES:
                    raise HTTPException(status_code=413, detail="audio exceeds 512 MB upload limit")
        duration = media_duration_seconds(source_path)
        if duration > MAX_AUDIO_SECONDS:
            raise HTTPException(status_code=413, detail="audio exceeds the five minute duration limit")
        with MODEL_LOCK:
            raw_segments = align_exact(source_path, text, language.strip().lower() or "en")
        segments = validate_segments(raw_segments)
        return {"duration": duration, "segments": segments, "unaligned_tokens": []}
    except HTTPException:
        raise
    except Exception as error:
        raise HTTPException(
            status_code=503,
            detail=f"{MODEL_NAME} alignment failed: {type(error).__name__}: {str(error)[:300]}",
        ) from error
    finally:
        if source_path:
            Path(source_path).unlink(missing_ok=True)
        await audio.close()
        REQUEST_SLOTS.release()
'''


CRISP_ALIGNMENT_ADAPTER = r'''
import json

MODEL_ID = "{family_id}"
MODEL_NAME = "{name}"
UPSTREAM_MODEL = "{upstream}"
SUPPORTED_LANGUAGES = {languages}
CRISPASR = "/content/CrispASR/build/bin/crispasr"
ALIGNER_MODEL = "{model_path}"

if not Path(CRISPASR).is_file() or not Path(ALIGNER_MODEL).is_file():
    raise RuntimeError("CrispASR CUDA runtime or the exact aligner model is missing")

def _collect_crisp_segments(payload):
    if isinstance(payload, list):
        entries = payload
    elif isinstance(payload, dict):
        entries = payload.get("segments", payload.get("alignment", payload.get("results", [])))
    else:
        entries = []
    output = []
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        words = entry.get("words")
        if isinstance(words, list) and words:
            output.extend(words)
        else:
            output.append(entry)
    return output

def align_exact(source_path: str, transcript: str, language: str):
    if MODEL_ID == "wav2vec2-aligner-zh" and language not in {{"zh", "zho", "chi", "cmn", "zh-cn"}}:
        raise HTTPException(status_code=422, detail="the selected Wav2Vec2 aligner supports Mandarin Chinese only")
    with tempfile.TemporaryDirectory(prefix="la-studio-crisp-align-") as directory:
        wav_path = str(Path(directory) / "source.wav")
        output_path = str(Path(directory) / "alignment.json")
        subprocess.run(
            ["ffmpeg", "-y", "-v", "error", "-i", source_path, "-vn", "-ac", "1", "-ar", "16000", wav_path],
            check=True,
        )
        command = [
            CRISPASR, "--align-only", "-am", ALIGNER_MODEL, "-f", wav_path,
            "--ref-text", transcript, "--align-format", "json",
            "--align-granularity", "word", "--align-output", output_path,
            "--gpu-backend", "cuda", "--strict-pipeline", "-l", language,
        ]
        result = subprocess.run(command, text=True, capture_output=True)
        if result.returncode != 0 or not Path(output_path).is_file():
            raise RuntimeError("CrispASR CUDA aligner failed: " + (result.stderr or result.stdout)[-1600:])
        payload = json.loads(Path(output_path).read_text(encoding="utf-8"))
        return _collect_crisp_segments(payload)
'''


MMS_ADAPTER = r'''
import math
import numpy as np
import onnxruntime as ort
from huggingface_hub import snapshot_download
from transformers import AutoConfig, AutoTokenizer
from ctc_forced_aligner import get_alignments, get_spans, postprocess_results, preprocess_text

MODEL_ID = "mms-forced-aligner-onnx"
MODEL_NAME = "MMS Forced Aligner ONNX"
UPSTREAM_MODEL = "onnx-community/mms-300m-1130-forced-aligner-ONNX"
SUPPORTED_LANGUAGES = ["158 languages (ISO 639-1 or ISO 639-3)"]
MODEL_DIR = snapshot_download(
    UPSTREAM_MODEL,
    allow_patterns=[
        "config.json", "preprocessor_config.json", "special_tokens_map.json",
        "tokenizer.json", "tokenizer_config.json", "vocab.json", "onnx/model_fp16.onnx",
    ],
)
SESSION = ort.InferenceSession(
    str(Path(MODEL_DIR) / "onnx/model_fp16.onnx"),
    providers=["CUDAExecutionProvider"],
)
if "CUDAExecutionProvider" not in SESSION.get_providers():
    raise RuntimeError("MMS ONNX did not activate CUDAExecutionProvider")
TOKENIZER = AutoTokenizer.from_pretrained(MODEL_DIR, word_delimiter_token=None)
CONFIG = AutoConfig.from_pretrained(MODEL_DIR)
RATIO = int(CONFIG.inputs_to_logits_ratio)
SAMPLE_RATE = 16000

ISO3 = {
    "ar": "ara", "be": "bel", "bg": "bul", "de": "deu", "el": "ell", "en": "eng",
    "fa": "fas", "he": "heb", "kk": "kaz", "ky": "kir", "lv": "lav", "lt": "lit",
    "mk": "mkd", "mn": "mon", "ru": "rus", "sr": "srp", "th": "tha", "tr": "tur",
    "ug": "uig", "uk": "ukr", "yi": "yid", "vi": "vie", "zh": "chi", "ja": "jpn",
    "fr": "fra", "es": "spa", "it": "ita", "pt": "por", "ko": "kor",
}

def _load_mono(path: str):
    raw = subprocess.run(
        ["ffmpeg", "-nostdin", "-threads", "0", "-i", path, "-f", "f32le",
         "-ac", "1", "-ar", str(SAMPLE_RATE), "-"],
        check=True, capture_output=True,
    ).stdout
    return np.frombuffer(raw, dtype=np.float32).copy()

def _onnx_emissions(audio: np.ndarray):
    window = 30 * SAMPLE_RATE
    context = 2 * SAMPLE_RATE
    context_frames = context // RATIO
    window_frames = window // RATIO
    if audio.size < window:
        chunks = [audio]
        extension = 0
        use_context = False
    else:
        count = math.ceil(audio.size / window)
        extension = count * window - audio.size
        padded = np.pad(audio, (context, context + extension))
        chunks = [padded[index * window:index * window + window + 2 * context] for index in range(count)]
        use_context = True
    outputs = []
    input_name = SESSION.get_inputs()[0].name
    for chunk in chunks:
        logits = SESSION.run(None, {input_name: chunk[None, :].astype(np.float32)})[0][0]
        if use_context:
            logits = logits[context_frames:context_frames + window_frames]
        outputs.append(logits)
    emissions = np.concatenate(outputs, axis=0)
    if extension:
        emissions = emissions[:-(extension // RATIO)]
    values = torch.from_numpy(emissions).float().log_softmax(-1)
    values = torch.cat([values, torch.zeros(values.size(0), 1)], dim=1)
    return values, RATIO * 1000.0 / SAMPLE_RATE

def align_exact(source_path: str, transcript: str, language: str):
    iso = ISO3.get(language, language)
    emissions, stride = _onnx_emissions(_load_mono(source_path))
    tokens, text_tokens = preprocess_text(transcript, romanize=True, language=iso)
    segments, scores, blank = get_alignments(emissions, tokens, TOKENIZER)
    spans = get_spans(tokens, segments, blank)
    return postprocess_results(text_tokens, spans, stride, scores)
'''


QWEN_ADAPTER = r'''
from qwen_asr import Qwen3ForcedAligner

MODEL_ID = "qwen3-forced-aligner-0.6b"
MODEL_NAME = "Qwen3 ForcedAligner 0.6B"
UPSTREAM_MODEL = "Qwen/Qwen3-ForcedAligner-0.6B"
LANGUAGE_NAMES = {
    "zh": "Chinese", "zho": "Chinese", "chi": "Chinese", "en": "English", "eng": "English",
    "yue": "Cantonese", "ja": "Japanese", "jpn": "Japanese",
    "ko": "Korean", "kor": "Korean", "de": "German", "deu": "German",
    "fr": "French", "fra": "French", "ru": "Russian", "rus": "Russian",
    "pt": "Portuguese", "por": "Portuguese", "es": "Spanish", "spa": "Spanish",
    "it": "Italian", "ita": "Italian",
}
SUPPORTED_LANGUAGES = sorted(set(LANGUAGE_NAMES.values()))
ALIGNER = Qwen3ForcedAligner.from_pretrained(
    UPSTREAM_MODEL,
    dtype=torch.float16,
    device_map="cuda:0",
)

def align_exact(source_path: str, transcript: str, language: str):
    language_name = LANGUAGE_NAMES.get(language)
    if not language_name:
        raise HTTPException(status_code=422, detail="unsupported Qwen3 ForcedAligner language")
    results = ALIGNER.align(audio=source_path, text=transcript, language=language_name)
    if not results:
        return []
    return [
        {"text": item.text, "start": item.start_time, "end": item.end_time, "score": 1.0}
        for item in results[0]
    ]
'''


ALIGNMENT_START = r'''
import os, re, secrets, subprocess, sys, time, urllib.request

TOKEN = secrets.token_urlsafe(32)
env = os.environ.copy()
env["LA_STUDIO_COLAB_ALIGNMENT_TOKEN"] = TOKEN
env["PYTHONUNBUFFERED"] = "1"
worker = subprocess.Popen(
    [sys.executable, "-m", "uvicorn", "la_studio_alignment_worker:app", "--host", "127.0.0.1", "--port", "3923"],
    cwd="/content", env=env,
)
for _ in range(240):
    try:
        check = urllib.request.Request(
            "http://127.0.0.1:3923/health",
            headers={"Authorization": "Bearer " + TOKEN},
        )
        with urllib.request.urlopen(check, timeout=5) as response:
            if response.status == 200:
                break
    except Exception:
        time.sleep(2)
else:
    worker.terminate()
    raise RuntimeError("The exact-model alignment worker did not become ready. Inspect the cell output above.")

subprocess.run(
    ["bash", "-lc", "wget -q -O /content/cloudflared.deb https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64.deb && dpkg -i /content/cloudflared.deb"],
    check=True,
)
tunnel = subprocess.Popen(
    ["cloudflared", "tunnel", "--url", "http://127.0.0.1:3923", "--no-autoupdate"],
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
)
public_url = None
for _ in range(120):
    line = tunnel.stdout.readline()
    print(line, end="")
    match = re.search(r"https://[^\s]+trycloudflare\.com", line)
    if match:
        public_url = match.group(0)
        break
if not public_url:
    worker.terminate()
    tunnel.terminate()
    raise RuntimeError("Cloudflare tunnel URL was not found")

print("\nLA_STUDIO_COLAB_ALIGNMENT_URL=" + public_url)
print("LA_STUDIO_COLAB_ALIGNMENT_TOKEN=" + TOKEN)
print("LA_STUDIO_COLAB_ALIGNMENT_MODEL=" + MODEL_ID)
print("DEVICE=cuda; CPU_FALLBACK=false")
'''


SEPARATION_COMMON = r'''
import os
import secrets
import shutil
import subprocess
import threading
import time
from pathlib import Path

import numpy as np
import sherpa_onnx
import soundfile as sf
from fastapi import FastAPI, File, Form, Header, HTTPException, UploadFile
from fastapi.responses import FileResponse

if "+cuda" not in sherpa_onnx.__version__:
    raise RuntimeError("The installed sherpa-onnx wheel is not CUDA-enabled")

# __ADAPTER__

TOKEN = os.environ["LA_STUDIO_COLAB_SEPARATION_TOKEN"]
ROOT = Path("/content/la-studio-separation-jobs") / MODEL_ID
ROOT.mkdir(parents=True, exist_ok=True)
MAX_UPLOAD_BYTES = 512 * 1024 * 1024
MAX_AUDIO_SECONDS = 30 * 60
ARTIFACT_TTL_SECONDS = 1800
ALLOWED_CONTENT_TYPES = {
    "audio/wav", "audio/x-wav", "audio/mpeg", "audio/mp4", "audio/webm",
    "audio/ogg", "audio/flac", "video/mp4", "video/webm", "video/quicktime",
    "video/x-matroska", "application/octet-stream",
}
ALLOWED_EXTENSIONS = {".wav", ".mp3", ".m4a", ".mp4", ".webm", ".ogg", ".flac", ".mkv", ".mov", ".avi"}
JOB_SLOTS = threading.BoundedSemaphore(1)
JOB_LOCK = threading.Lock()
JOBS = {}

def authorize(authorization: str | None) -> None:
    if authorization != "Bearer " + TOKEN:
        raise HTTPException(status_code=401, detail="invalid worker token")

def require_exact_model(requested: str) -> None:
    if requested.strip().lower() != MODEL_ID:
        raise HTTPException(
            status_code=409,
            detail=f"This worker loaded '{MODEL_ID}', but LA Studio requested '{requested}'. Open the notebook for the selected model.",
        )

def media_duration_seconds(path: Path) -> float:
    probe = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries", "format=duration",
         "-of", "default=nokey=1:noprint_wrappers=1", str(path)],
        text=True, capture_output=True,
    )
    try:
        duration = float(probe.stdout.strip())
    except ValueError:
        duration = 0.0
    if probe.returncode != 0 or duration <= 0.0:
        raise HTTPException(status_code=415, detail="media is unsupported or could not be decoded")
    return duration

def update(job_id: str, **values) -> dict:
    with JOB_LOCK:
        job = dict(JOBS.get(job_id, {}))
        job.update(values)
        JOBS[job_id] = job
        return job

def cleanup(job_id: str) -> None:
    with JOB_LOCK:
        job = JOBS.pop(job_id, None)
    if job:
        shutil.rmtree(job.get("directory", ""), ignore_errors=True)

def run_job(job_id: str, directory: Path, source: Path, output_format: str) -> None:
    try:
        update(job_id, status="running", progress=20, detail=f"{MODEL_NAME} is separating vocals on CUDA")
        wav_path = directory / "source-44100-stereo.wav"
        subprocess.run(
            ["ffmpeg", "-y", "-v", "error", "-i", str(source), "-vn", "-acodec", "pcm_s16le",
             "-ar", "44100", "-ac", "2", str(wav_path)],
            check=True,
        )
        samples, sample_rate = sf.read(wav_path, dtype="float32", always_2d=True)
        samples = np.ascontiguousarray(samples.T)
        output = SEPARATOR.process(sample_rate=sample_rate, samples=samples)
        if len(output.stems) != 2:
            raise RuntimeError(f"expected two stems, received {len(output.stems)}")
        suffix = ".wav" if output_format == "wav" else ".flac"
        file_format = "WAV" if output_format == "wav" else "FLAC"
        vocals = directory / ("vocals" + suffix)
        background = directory / ("background" + suffix)
        sf.write(vocals, np.asarray(output.stems[0].data).T, output.sample_rate,
                 format=file_format, subtype="PCM_16")
        sf.write(background, np.asarray(output.stems[1].data).T, output.sample_rate,
                 format=file_format, subtype="PCM_16")
        with JOB_LOCK:
            cancelled = JOBS.get(job_id, {}).get("cancel_requested", False)
        if cancelled:
            vocals.unlink(missing_ok=True)
            background.unlink(missing_ok=True)
            update(job_id, status="cancelled", progress=0, detail="Separation cancelled")
        else:
            update(
                job_id, status="ready", progress=100, detail="Separated stems are ready",
                vocals=str(vocals), background=str(background),
                artifact_format=output_format, artifacts_ready=True,
            )
    except Exception as error:
        update(job_id, status="failed", progress=0, detail=f"{type(error).__name__}: {str(error)[:1800]}")
    finally:
        threading.Timer(ARTIFACT_TTL_SECONDS, cleanup, args=[job_id]).start()
        JOB_SLOTS.release()

app = FastAPI(title=f"LA Studio Voice Isolation - {MODEL_NAME}", docs_url=None, redoc_url=None, openapi_url=None)

@app.get("/health")
@app.get("/v1/health")
def health(authorization: str | None = Header(default=None)):
    authorize(authorization)
    return {
        "status": "ready",
        "ready": True,
        "device": "cuda",
        "model": MODEL_ID,
        "variant": "fixed",
        "upstream_model": UPSTREAM_MODEL,
        "sherpa_onnx": sherpa_onnx.__version__,
        "cpu_fallback": False,
    }

@app.get("/v1/capabilities")
def capabilities(authorization: str | None = Header(default=None)):
    authorize(authorization)
    return {
        "contract_version": 1,
        "device": "cuda",
        "capabilities": [{
            "id": "voice-isolation",
            "models": [{
                "id": MODEL_ID,
                "name": MODEL_NAME,
                "variant": "fixed",
                "upstream_model": UPSTREAM_MODEL,
                "artifact_url": ARTIFACT_URL,
                "stems": ["vocals", "background"],
                "formats": ["flac", "wav"],
                "device": "cuda",
                "loaded": True,
            }],
        }],
    }

@app.post("/v1/audio/separations")
async def create_separation(
    file: UploadFile = File(...),
    stems: str = Form("vocals,background"),
    model: str = Form(...),
    output_format: str = Form("flac"),
    authorization: str | None = Header(default=None),
):
    authorize(authorization)
    require_exact_model(model)
    if stems != "vocals,background":
        raise HTTPException(status_code=422, detail="this worker returns vocals and background stems")
    output_format = output_format.strip().lower()
    if output_format not in {"flac", "wav"}:
        raise HTTPException(status_code=422, detail="output_format must be flac or wav")
    suffix = Path(file.filename or "source.wav").suffix.lower() or ".wav"
    if suffix not in ALLOWED_EXTENSIONS:
        raise HTTPException(status_code=415, detail="unsupported media filename extension")
    if file.content_type and file.content_type not in ALLOWED_CONTENT_TYPES:
        raise HTTPException(status_code=415, detail="unsupported media MIME type")
    if not JOB_SLOTS.acquire(blocking=False):
        raise HTTPException(status_code=429, detail="the Colab separation worker is busy; retry shortly")
    job_id = secrets.token_urlsafe(18)
    directory = ROOT / job_id
    directory.mkdir(parents=True, exist_ok=True)
    source = directory / ("source" + suffix)
    try:
        with source.open("wb") as output:
            while chunk := await file.read(1024 * 1024):
                output.write(chunk)
                if output.tell() > MAX_UPLOAD_BYTES:
                    raise HTTPException(status_code=413, detail="media exceeds 512 MB upload limit")
        if source.stat().st_size <= 0:
            raise HTTPException(status_code=413, detail="media must not be empty")
        if media_duration_seconds(source) > MAX_AUDIO_SECONDS:
            raise HTTPException(status_code=413, detail="media exceeds the 30 minute duration limit")
    except Exception:
        shutil.rmtree(directory, ignore_errors=True)
        JOB_SLOTS.release()
        raise
    finally:
        await file.close()
    update(
        job_id, status="queued", progress=10, detail=f"Media uploaded; {MODEL_NAME} CUDA job is queued",
        directory=str(directory), cancel_requested=False,
    )
    threading.Thread(target=run_job, args=(job_id, directory, source, output_format), daemon=True).start()
    return {"job_id": job_id, "status": "queued", "progress": 10,
            "artifact_format": output_format}

@app.get("/v1/audio/separations/{job_id}")
def separation_status(job_id: str, authorization: str | None = Header(default=None)):
    authorize(authorization)
    with JOB_LOCK:
        job = dict(JOBS.get(job_id, {}))
    if not job:
        raise HTTPException(status_code=404, detail="separation job not found")
    return {key: job.get(key) for key in ("status", "progress", "detail", "artifact_format", "artifacts_ready") if key in job} | {"job_id": job_id}

@app.get("/v1/audio/separations/{job_id}/artifacts/{stem}")
def artifact(job_id: str, stem: str, authorization: str | None = Header(default=None)):
    authorize(authorization)
    if stem not in {"vocals", "background"}:
        raise HTTPException(status_code=404, detail="unknown stem")
    with JOB_LOCK:
        job = dict(JOBS.get(job_id, {}))
    path = Path(job.get(stem, ""))
    if job.get("status") != "ready" or not path.is_file():
        raise HTTPException(status_code=409, detail="stem is not ready")
    output_format = job.get("artifact_format", "wav")
    media_type = "audio/wav" if output_format == "wav" else "audio/flac"
    return FileResponse(path, media_type=media_type, filename=stem + "." + output_format)

@app.delete("/v1/audio/separations/{job_id}")
def cancel_separation(job_id: str, authorization: str | None = Header(default=None)):
    authorize(authorization)
    with JOB_LOCK:
        if job_id not in JOBS:
            raise HTTPException(status_code=404, detail="separation job not found")
        JOBS[job_id]["cancel_requested"] = True
        JOBS[job_id]["status"] = "cancelling"
    return {"job_id": job_id, "status": "cancelling"}
'''


SEPARATION_ADAPTER = r'''
MODEL_ID = "{family_id}"
MODEL_NAME = "{name}"
UPSTREAM_MODEL = "{upstream}"
ARTIFACT_URL = "{artifact_url}"
{config}
if not CONFIG.validate():
    raise RuntimeError("The exact sherpa-onnx CUDA separation configuration is invalid")
SEPARATOR = sherpa_onnx.OfflineSourceSeparation(CONFIG)
'''


SEPARATION_START = r'''
import os, re, secrets, subprocess, sys, time, urllib.request

TOKEN = secrets.token_urlsafe(32)
env = os.environ.copy()
env["LA_STUDIO_COLAB_SEPARATION_TOKEN"] = TOKEN
env["PYTHONUNBUFFERED"] = "1"
worker = subprocess.Popen(
    [sys.executable, "-m", "uvicorn", "la_studio_separation_worker:app", "--host", "127.0.0.1", "--port", "3924"],
    cwd="/content", env=env,
)
for _ in range(180):
    try:
        check = urllib.request.Request(
            "http://127.0.0.1:3924/health",
            headers={"Authorization": "Bearer " + TOKEN},
        )
        with urllib.request.urlopen(check, timeout=5) as response:
            if response.status == 200:
                break
    except Exception:
        time.sleep(2)
else:
    worker.terminate()
    raise RuntimeError("The exact-model separation worker did not become ready. Inspect the cell output above.")

subprocess.run(
    ["bash", "-lc", "wget -q -O /content/cloudflared.deb https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64.deb && dpkg -i /content/cloudflared.deb"],
    check=True,
)
tunnel = subprocess.Popen(
    ["cloudflared", "tunnel", "--url", "http://127.0.0.1:3924", "--no-autoupdate"],
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
)
public_url = None
for _ in range(120):
    line = tunnel.stdout.readline()
    print(line, end="")
    match = re.search(r"https://[^\s]+trycloudflare\.com", line)
    if match:
        public_url = match.group(0)
        break
if not public_url:
    worker.terminate()
    tunnel.terminate()
    raise RuntimeError("Cloudflare tunnel URL was not found")

print("\nLA_STUDIO_COLAB_SEPARATION_URL=" + public_url)
print("LA_STUDIO_COLAB_SEPARATION_TOKEN=" + TOKEN)
print("LA_STUDIO_COLAB_SEPARATION_MODEL=" + MODEL_ID)
print("DEVICE=cuda; CPU_FALLBACK=false")
'''


def alignment_specs() -> list[dict[str, str]]:
    crisp_install = r'''
!nvidia-smi
!git clone --quiet --recursive https://github.com/CrispStrobe/CrispASR.git /content/CrispASR
!git -C /content/CrispASR checkout --quiet 754b67289cf1137e3ed722885705f94132fc614f
!git -C /content/CrispASR submodule update --init --recursive
!cmake -S /content/CrispASR -B /content/CrispASR/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGGML_CUDA=ON
!cmake --build /content/CrispASR/build --target crispasr -j2
%pip install -q "fastapi==0.115.12" "uvicorn==0.34.3" "python-multipart==0.0.20"
'''
    return [
        {
            "family_id": "wav2vec2-aligner-zh",
            "name": "Wav2Vec2 Chinese Aligner",
            "upstream": "cstr/wav2vec2-large-xlsr-53-chinese-zh-cn-GGUF",
            "file": "LA_STUDIO_ALIGNMENT_WAV2VEC2_ZH_GPU.ipynb",
            "install": crisp_install + r'''
!wget -q --show-progress -O /content/wav2vec2-aligner-zh-q4_k.gguf https://huggingface.co/cstr/wav2vec2-large-xlsr-53-chinese-zh-cn-GGUF/resolve/main/wav2vec2-large-xlsr-53-chinese-zh-cn-q4_k.gguf
''',
            "adapter": CRISP_ALIGNMENT_ADAPTER.format(
                family_id="wav2vec2-aligner-zh",
                name="Wav2Vec2 Chinese Aligner",
                upstream="cstr/wav2vec2-large-xlsr-53-chinese-zh-cn-GGUF",
                languages='["zh", "zho", "chi", "cmn", "zh-cn"]',
                model_path="/content/wav2vec2-aligner-zh-q4_k.gguf",
            ),
        },
        {
            "family_id": "canary-ctc-aligner",
            "name": "Canary CTC Aligner",
            "upstream": "cstr/canary-ctc-aligner-GGUF",
            "file": "LA_STUDIO_ALIGNMENT_CANARY_CTC_GPU.ipynb",
            "install": crisp_install + r'''
!wget -q --show-progress -O /content/canary-ctc-aligner-q4_k.gguf https://huggingface.co/cstr/canary-ctc-aligner-GGUF/resolve/main/canary-ctc-aligner-q4_k.gguf
''',
            "adapter": CRISP_ALIGNMENT_ADAPTER.format(
                family_id="canary-ctc-aligner",
                name="Canary CTC Aligner",
                upstream="cstr/canary-ctc-aligner-GGUF",
                languages='["bg", "cs", "da", "de", "el", "en", "es", "et", "fi", "fr", "hr", "hu", "it", "lt", "lv", "mt", "nl", "pl", "pt", "ro", "ru", "sk", "sl", "sv", "uk"]',
                model_path="/content/canary-ctc-aligner-q4_k.gguf",
            ),
        },
        {
            "family_id": "mms-forced-aligner-onnx",
            "name": "MMS Forced Aligner ONNX",
            "upstream": "onnx-community/mms-300m-1130-forced-aligner-ONNX",
            "file": "LA_STUDIO_ALIGNMENT_MMS_ONNX_GPU.ipynb",
            "install": r'''
!nvidia-smi
%pip install -q "onnxruntime-gpu==1.22.0" "transformers==4.57.6" "huggingface-hub==0.36.0" "git+https://github.com/MahmoudAshraf97/ctc-forced-aligner.git@11855d1de76af2b490dd2e8e2db2661805ae90a0" "fastapi==0.115.12" "uvicorn==0.34.3" "python-multipart==0.0.20"
''',
            "adapter": MMS_ADAPTER,
        },
        {
            "family_id": "qwen3-forced-aligner-0.6b",
            "name": "Qwen3 ForcedAligner 0.6B",
            "upstream": "Qwen/Qwen3-ForcedAligner-0.6B",
            "file": "LA_STUDIO_ALIGNMENT_QWEN3_0_6B_GPU.ipynb",
            "install": r'''
!nvidia-smi
%pip install -q "git+https://github.com/QwenLM/Qwen3-ASR.git@7c6daf77a2421100f5fb066495372c00129d39ff" "fastapi==0.115.12" "uvicorn==0.34.3" "python-multipart==0.0.20"
''',
            "adapter": QWEN_ADAPTER,
        },
    ]


def separation_specs() -> list[dict[str, str]]:
    install = r'''
!nvidia-smi
%pip install -q "sherpa-onnx==1.13.4+cuda12.cudnn9" --find-links https://k2-fsa.github.io/sherpa/onnx/cuda.html
%pip install -q "soundfile==0.13.1" "fastapi==0.115.12" "uvicorn==0.34.3" "python-multipart==0.0.20"
'''
    spleeter_config = r'''
CONFIG = sherpa_onnx.OfflineSourceSeparationConfig(
    model=sherpa_onnx.OfflineSourceSeparationModelConfig(
        spleeter=sherpa_onnx.OfflineSourceSeparationSpleeterModelConfig(
            vocals="/content/sherpa-onnx-spleeter-2stems-fp16/vocals.fp16.onnx",
            accompaniment="/content/sherpa-onnx-spleeter-2stems-fp16/accompaniment.fp16.onnx",
        ),
        num_threads=1,
        debug=False,
        provider="cuda",
    )
)
'''
    uvr_config = r'''
CONFIG = sherpa_onnx.OfflineSourceSeparationConfig(
    model=sherpa_onnx.OfflineSourceSeparationModelConfig(
        uvr=sherpa_onnx.OfflineSourceSeparationUvrModelConfig(
            model="/content/UVR-MDX-NET-Voc_FT.onnx",
        ),
        num_threads=1,
        debug=False,
        provider="cuda",
    )
)
'''
    return [
        {
            "family_id": "sherpa-onnx-spleeter-2stems-fp16",
            "name": "Spleeter 2-stem FP16",
            "upstream": "k2-fsa/sherpa-onnx-spleeter-2stems-fp16",
            "artifact_url": "https://github.com/k2-fsa/sherpa-onnx/releases/download/source-separation-models/sherpa-onnx-spleeter-2stems-fp16.tar.bz2",
            "file": "LA_STUDIO_SEPARATION_SPLEETER_2STEMS_GPU.ipynb",
            "install": install + r'''
!wget -q --show-progress -O /content/spleeter.tar.bz2 https://github.com/k2-fsa/sherpa-onnx/releases/download/source-separation-models/sherpa-onnx-spleeter-2stems-fp16.tar.bz2
!tar -xjf /content/spleeter.tar.bz2 -C /content
''',
            "adapter": SEPARATION_ADAPTER.format(
                family_id="sherpa-onnx-spleeter-2stems-fp16",
                name="Spleeter 2-stem FP16",
                upstream="k2-fsa/sherpa-onnx-spleeter-2stems-fp16",
                artifact_url="https://github.com/k2-fsa/sherpa-onnx/releases/download/source-separation-models/sherpa-onnx-spleeter-2stems-fp16.tar.bz2",
                config=dedent(spleeter_config).strip(),
            ),
        },
        {
            "family_id": "sherpa-onnx-uvr-vocals-ft",
            "name": "UVR MDX-Net Vocals FT",
            "upstream": "k2-fsa/sherpa-onnx-uvr-vocals-ft",
            "artifact_url": "https://github.com/k2-fsa/sherpa-onnx/releases/download/source-separation-models/UVR-MDX-NET-Voc_FT.onnx",
            "file": "LA_STUDIO_SEPARATION_UVR_VOCALS_GPU.ipynb",
            "install": install + r'''
!wget -q --show-progress -O /content/UVR-MDX-NET-Voc_FT.onnx https://github.com/k2-fsa/sherpa-onnx/releases/download/source-separation-models/UVR-MDX-NET-Voc_FT.onnx
''',
            "adapter": SEPARATION_ADAPTER.format(
                family_id="sherpa-onnx-uvr-vocals-ft",
                name="UVR MDX-Net Vocals FT",
                upstream="k2-fsa/sherpa-onnx-uvr-vocals-ft",
                artifact_url="https://github.com/k2-fsa/sherpa-onnx/releases/download/source-separation-models/UVR-MDX-NET-Voc_FT.onnx",
                config=dedent(uvr_config).strip(),
            ),
        },
    ]


def source_lines(source: str) -> list[str]:
    return (dedent(source).strip() + "\n").splitlines(keepends=True)


def make_notebook(spec: dict[str, str], capability: str, common: str, worker_name: str) -> dict:
    marker = "# __ADAPTER__"
    if marker not in common:
        raise ValueError(f"Worker template for {capability} has no adapter marker")
    worker_source = dedent(common).replace(marker, dedent(spec["adapter"]).strip(), 1).strip() + "\n"
    writer = (
        "from pathlib import Path\n\n"
        f"WORKER = Path('/content/{worker_name}')\n"
        f"WORKER.write_text({worker_source!r}, encoding='utf-8')\n"
        "print('Worker source:', WORKER)\n"
    )
    la_studio_metadata = {
        "capability": capability,
        "family_id": spec["family_id"],
        "upstream_model": spec["upstream"],
        "contract_version": 1,
        "device": "cuda",
        "cpu_fallback": False,
    }
    if spec.get("artifact_url"):
        la_studio_metadata["artifact_url"] = spec["artifact_url"]
    if capability == "forced-alignment":
        start = build_worker_launch(
            capability_label="Forced Alignment",
            module="la_studio_alignment_worker:app",
            port=3923,
            model_id=spec["family_id"],
            token_env="LA_STUDIO_COLAB_ALIGNMENT_TOKEN",
            url_env="LA_STUDIO_COLAB_ALIGNMENT_URL",
            model_env="LA_STUDIO_COLAB_ALIGNMENT_MODEL",
            log_path="/content/la_studio_alignment_worker.log",
        )
    elif capability == "voice-isolation":
        start = build_worker_launch(
            capability_label="Voice Isolation",
            module="la_studio_separation_worker:app",
            port=3924,
            model_id=spec["family_id"],
            token_env="LA_STUDIO_COLAB_SEPARATION_TOKEN",
            url_env="LA_STUDIO_COLAB_SEPARATION_URL",
            model_env="LA_STUDIO_COLAB_SEPARATION_MODEL",
            log_path="/content/la_studio_separation_worker.log",
        )
    else:
        raise ValueError(f"Unsupported Colab capability: {capability}")
    return {
        "cells": [
            {
                "cell_type": "markdown",
                "metadata": {},
                "source": source_lines(
                    f"""
                    # LA Studio {capability} — {spec['name']}

                    This notebook loads exactly `{spec['family_id']}` (`{spec['upstream']}`) on CUDA.
                    It does not use API Gateway and refuses every other model ID.

                    1. Choose **Runtime → Change runtime type → GPU**.
                    2. Run all cells.
                    3. Copy the printed URL and token into LA Studio.
                    """
                ),
            },
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": source_lines(spec["install"])},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": source_lines(writer)},
            {
                "cell_type": "code",
                "execution_count": None,
                "metadata": {},
                "outputs": [],
                "source": source_lines(f"MODEL_ID = {spec['family_id']!r}\n" + start),
            },
        ],
        "metadata": {
            "accelerator": "GPU",
            "colab": {"gpuType": "T4", "provenance": []},
            "kernelspec": {"display_name": "Python 3", "name": "python3"},
            "language_info": {"name": "python"},
            "la_studio": la_studio_metadata,
        },
        "nbformat": 4,
        "nbformat_minor": 5,
    }


def main() -> None:
    NOTEBOOKS.mkdir(parents=True, exist_ok=True)
    for spec in alignment_specs():
        target = NOTEBOOKS / spec["file"]
        target.write_text(
            json.dumps(
                make_notebook(
                    spec, "forced-alignment", ALIGNMENT_COMMON,
                    "la_studio_alignment_worker.py",
                ),
                indent=1,
                ensure_ascii=False,
            )
            + "\n",
            encoding="utf-8",
        )
        print(target.relative_to(ROOT))
    for spec in separation_specs():
        target = NOTEBOOKS / spec["file"]
        target.write_text(
            json.dumps(
                make_notebook(
                    spec, "voice-isolation", SEPARATION_COMMON,
                    "la_studio_separation_worker.py",
                ),
                indent=1,
                ensure_ascii=False,
            )
            + "\n",
            encoding="utf-8",
        )
        print(target.relative_to(ROOT))


if __name__ == "__main__":
    main()
