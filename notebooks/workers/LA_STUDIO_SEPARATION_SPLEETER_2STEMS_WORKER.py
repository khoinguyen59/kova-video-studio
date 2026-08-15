"""Temporary Direct Colab worker for the exact Spleeter 2-stem FP16 artifact.

The worker deliberately uses ONNX Runtime's CUDA provider directly.  The
sherpa-onnx source-separation wrapper fixes its CUDA convolution search to
HEURISTIC, which fails on some current Colab cuDNN 9 images.  This worker uses
the same upstream FP16 ONNX files, but chooses ORT's documented DEFAULT
convolution algorithm instead and bounds every inference input.
"""

import math
import os
import secrets
import shutil
import subprocess
import threading
import traceback
from pathlib import Path

import kaldi_native_fbank as knf
import numpy as np
import torch
import onnxruntime as ort
import soundfile as sf
from fastapi import FastAPI, File, Form, Header, HTTPException, UploadFile
from fastapi.responses import FileResponse


WORKER_CONTRACT = "spleeter-cuda-safe-20260816.1"
MODEL_ID = "sherpa-onnx-spleeter-2stems-fp16"
MODEL_NAME = "Spleeter 2-stem FP16"
UPSTREAM_MODEL = "k2-fsa/sherpa-onnx-spleeter-2stems-fp16"
ARTIFACT_URL = (
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/"
    "source-separation-models/sherpa-onnx-spleeter-2stems-fp16.tar.bz2"
)
MODEL_ROOT = Path("/content/sherpa-onnx-spleeter-2stems-fp16")
TOKEN = os.environ["LA_STUDIO_COLAB_SEPARATION_TOKEN"]
ROOT = Path("/content/la-studio-separation-jobs") / MODEL_ID
ROOT.mkdir(parents=True, exist_ok=True)

MAX_UPLOAD_BYTES = 512 * 1024 * 1024
MAX_AUDIO_SECONDS = 30 * 60
ARTIFACT_TTL_SECONDS = 1800
# A 20-second core has at most two 512-frame Spleeter splits.  The previous
# worker sent a complete long video through one CUDA call, producing a large
# dynamic Conv shape that caused CUDNN_FE_HEURISTIC_QUERY_FAILED on Colab.
CORE_SECONDS = 20.0
CONTEXT_SECONDS = 1.5
PROBE_SECONDS = CORE_SECONDS

ALLOWED_CONTENT_TYPES = {
    "audio/wav", "audio/x-wav", "audio/mpeg", "audio/mp4", "audio/webm",
    "audio/ogg", "audio/flac", "video/mp4", "video/webm", "video/quicktime",
    "video/x-matroska", "application/octet-stream",
}
ALLOWED_EXTENSIONS = {
    ".wav", ".mp3", ".m4a", ".mp4", ".webm", ".ogg", ".flac", ".mkv", ".mov", ".avi",
}
CUDA_OPTIONS = {
    # DEFAULT avoids the cuDNN heuristic-plan query that failed in the old
    # sherpa-onnx wrapper.  The exact model remains FP16 and executes on CUDA.
    "cudnn_conv_algo_search": "DEFAULT",
    "cudnn_conv_use_max_workspace": "1",
    "do_copy_in_default_stream": "1",
    "arena_extend_strategy": "kSameAsRequested",
}

if not torch.cuda.is_available():
    raise RuntimeError("Colab GPU is not available; select a GPU runtime before starting this worker")


def _cuda_session(path: Path) -> ort.InferenceSession:
    if "CUDAExecutionProvider" not in ort.get_available_providers():
        raise RuntimeError("ONNX Runtime CUDAExecutionProvider is unavailable in this Colab runtime")
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    session = ort.InferenceSession(
        str(path),
        sess_options=options,
        providers=[("CUDAExecutionProvider", CUDA_OPTIONS), "CPUExecutionProvider"],
    )
    if not session.get_providers() or session.get_providers()[0] != "CUDAExecutionProvider":
        raise RuntimeError("The exact Spleeter ONNX session did not bind CUDAExecutionProvider")
    return session


class ExactSpleeterCuda:
    def __init__(self) -> None:
        vocals = MODEL_ROOT / "vocals.fp16.onnx"
        accompaniment = MODEL_ROOT / "accompaniment.fp16.onnx"
        if not vocals.is_file() or not accompaniment.is_file():
            raise RuntimeError("The exact Spleeter FP16 ONNX artifacts are missing")
        self.vocals = _cuda_session(vocals)
        self.accompaniment = _cuda_session(accompaniment)
        self.stft_config = knf.StftConfig(
            n_fft=4096,
            hop_length=1024,
            win_length=4096,
            center=False,
            window_type="hann",
        )

    @staticmethod
    def _stft(samples: np.ndarray, channel: int) -> tuple[np.ndarray, np.ndarray]:
        result = knf.Stft(knf.StftConfig(
            n_fft=4096, hop_length=1024, win_length=4096,
            center=False, window_type="hann",
        ))(samples[:, channel].tolist())
        real = np.asarray(result.real, dtype=np.float32).reshape(result.num_frames, -1)
        imag = np.asarray(result.imag, dtype=np.float32).reshape(result.num_frames, -1)
        return real, imag

    def process(self, sample_rate: int, samples: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        if sample_rate != 44100:
            raise RuntimeError(f"expected 44100 Hz worker input, received {sample_rate}")
        if samples.ndim != 2 or samples.shape[1] != 2 or samples.shape[0] == 0:
            raise RuntimeError("expected non-empty stereo audio")
        real0, imag0 = self._stft(samples, 0)
        real1, imag1 = self._stft(samples, 1)
        if real0.shape[0] == 0 or real1.shape[0] == 0:
            raise RuntimeError("audio is too short for Spleeter analysis")
        frame_count = real0.shape[0]
        if real1.shape[0] != frame_count:
            raise RuntimeError("stereo channel frame counts differ")

        magnitude0 = np.sqrt(real0[:, :1024] ** 2 + imag0[:, :1024] ** 2).astype(np.float32)
        magnitude1 = np.sqrt(real1[:, :1024] ** 2 + imag1[:, :1024] ** 2).astype(np.float32)
        padded_frames = int(math.ceil(frame_count / 512.0) * 512)
        if padded_frames != frame_count:
            padding = ((0, padded_frames - frame_count), (0, 0))
            magnitude0 = np.pad(magnitude0, padding)
            magnitude1 = np.pad(magnitude1, padding)
        model_input = np.ascontiguousarray(
            np.stack((magnitude0, magnitude1), axis=0).reshape(2, -1, 512, 1024),
            dtype=np.float32,
        )
        vocals_spec = self.vocals.run(None, {self.vocals.get_inputs()[0].name: model_input})[0]
        accompaniment_spec = self.accompaniment.run(
            None, {self.accompaniment.get_inputs()[0].name: model_input}
        )[0]
        denominator = vocals_spec ** 2 + accompaniment_spec ** 2 + 1e-10
        masks = (
            (vocals_spec ** 2 + 5e-11) / denominator,
            (accompaniment_spec ** 2 + 5e-11) / denominator,
        )

        stems: list[np.ndarray] = []
        for mask in masks:
            channels: list[np.ndarray] = []
            for channel, (real, imag) in enumerate(((real0, imag0), (real1, imag1))):
                channel_mask = mask[channel].reshape(-1, 1024)[:frame_count]
                channel_mask = np.pad(channel_mask, ((0, 0), (0, real.shape[1] - 1024)))
                masked = knf.StftResult(
                    real=(channel_mask * real).reshape(-1).tolist(),
                    imag=(channel_mask * imag).reshape(-1).tolist(),
                    num_frames=frame_count,
                )
                waveform = knf.IStft(self.stft_config)(masked)
                channels.append(np.asarray(waveform, dtype=np.float32))
            stem = np.column_stack(channels)
            stems.append(stem)
        return stems[0], stems[1]


# Constructing and running the same bounded shape before /health is exposed
# proves CUDA works for this exact model.  An unsupported Colab image fails in
# the notebook cell, rather than accepting a URL and later failing at a random
# workflow step.
SEPARATOR = ExactSpleeterCuda()
_probe = np.zeros((int(44100 * PROBE_SECONDS), 2), dtype=np.float32)
_probe_vocals, _probe_background = SEPARATOR.process(44100, _probe)
if _probe_vocals.shape[0] == 0 or _probe_background.shape[0] == 0:
    raise RuntimeError("exact Spleeter CUDA startup probe produced empty audio")
del _probe, _probe_vocals, _probe_background

JOB_SLOTS = threading.BoundedSemaphore(1)
JOB_LOCK = threading.Lock()
JOBS: dict[str, dict] = {}


def authorize(authorization: str | None) -> None:
    if authorization != "Bearer " + TOKEN:
        raise HTTPException(status_code=401, detail="invalid worker token")


def require_exact_model(requested: str) -> None:
    if requested.strip().lower() != MODEL_ID:
        raise HTTPException(
            status_code=409,
            detail=(f"This worker loaded '{MODEL_ID}', but LA Studio requested '{requested}'. "
                    "Open the notebook for the selected model."),
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


def is_cancelled(job_id: str) -> bool:
    with JOB_LOCK:
        return bool(JOBS.get(job_id, {}).get("cancel_requested", False))


def cleanup(job_id: str) -> None:
    with JOB_LOCK:
        job = JOBS.pop(job_id, None)
    if job:
        shutil.rmtree(job.get("directory", ""), ignore_errors=True)


def _fit_piece(stem: np.ndarray, start: int, length: int) -> np.ndarray:
    piece = stem[start:start + length]
    if piece.shape[0] >= length:
        return piece[:length]
    return np.pad(piece, ((0, length - piece.shape[0]), (0, 0)))


def separate_bounded(job_id: str, samples: np.ndarray, sample_rate: int) -> tuple[np.ndarray, np.ndarray]:
    core = int(CORE_SECONDS * sample_rate)
    context = int(CONTEXT_SECONDS * sample_rate)
    total = samples.shape[0]
    pieces = max(1, math.ceil(total / core))
    vocals_parts: list[np.ndarray] = []
    background_parts: list[np.ndarray] = []
    for index, core_start in enumerate(range(0, total, core), start=1):
        if is_cancelled(job_id):
            raise RuntimeError("Separation cancelled")
        core_end = min(total, core_start + core)
        window_start = max(0, core_start - context)
        window_end = min(total, core_end + context)
        update(
            job_id,
            status="running",
            progress=20 + int(65 * (index - 1) / pieces),
            detail=f"{MODEL_NAME} CUDA segment {index}/{pieces}",
        )
        vocals, background = SEPARATOR.process(sample_rate, samples[window_start:window_end])
        trim = core_start - window_start
        core_length = core_end - core_start
        vocals_parts.append(_fit_piece(vocals, trim, core_length))
        background_parts.append(_fit_piece(background, trim, core_length))
        update(
            job_id,
            status="running",
            progress=20 + int(65 * index / pieces),
            detail=f"{MODEL_NAME} CUDA segment {index}/{pieces} complete",
        )
    return np.concatenate(vocals_parts, axis=0), np.concatenate(background_parts, axis=0)


def concise_failure(error: Exception) -> str:
    text = str(error).replace("\n", " ").strip()
    if "CUDNN" in text.upper() or "CUDA" in text.upper():
        return ("The verified Colab CUDA worker failed during Spleeter inference. "
                "No local model was started. Stop this job, reopen the current Spleeter notebook, "
                "and use its startup probe before reconnecting. Full worker detail is in the Colab output.")
    return f"{type(error).__name__}: {text[:600]}"


def run_job(job_id: str, directory: Path, source: Path, output_format: str) -> None:
    try:
        update(job_id, status="running", progress=12, detail="Decoding media for bounded CUDA separation")
        wav_path = directory / "source-44100-stereo.wav"
        subprocess.run(
            ["ffmpeg", "-y", "-v", "error", "-i", str(source), "-vn", "-acodec", "pcm_s16le",
             "-ar", "44100", "-ac", "2", str(wav_path)],
            check=True,
        )
        samples, sample_rate = sf.read(wav_path, dtype="float32", always_2d=True)
        samples = np.ascontiguousarray(samples, dtype=np.float32)
        vocals_data, background_data = separate_bounded(job_id, samples, sample_rate)
        if is_cancelled(job_id):
            update(job_id, status="cancelled", progress=0, detail="Separation cancelled")
            return
        update(job_id, status="running", progress=90, detail="Writing separated CUDA stems")
        suffix = ".wav" if output_format == "wav" else ".flac"
        vocals = directory / ("vocals" + suffix)
        background = directory / ("background" + suffix)
        # FLAC is lossless and typically reduces the 44.1 kHz stereo transfer
        # by far more than 50%; PCM WAV remains the explicit compatibility
        # choice for an operator who needs it.
        sf.write(vocals, vocals_data, sample_rate,
                 format="WAV" if output_format == "wav" else "FLAC",
                 subtype="PCM_16")
        sf.write(background, background_data, sample_rate,
                 format="WAV" if output_format == "wav" else "FLAC",
                 subtype="PCM_16")
        update(
            job_id, status="ready", progress=100, detail="Separated CUDA stems are ready",
            vocals=str(vocals), background=str(background),
            artifact_format=output_format, artifacts_ready=True,
        )
    except Exception as error:
        traceback.print_exc()
        update(job_id, status="failed", progress=0, detail=concise_failure(error))
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
        "onnxruntime": ort.__version__,
        "cuda_provider_options": CUDA_OPTIONS,
        "bounded_core_seconds": CORE_SECONDS,
        "startup_probe": "passed",
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
        job_id, status="queued", progress=10,
        detail=f"Media uploaded; {MODEL_NAME} CUDA job is queued",
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
    media_type = "audio/flac" if output_format == "flac" else "audio/wav"
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
