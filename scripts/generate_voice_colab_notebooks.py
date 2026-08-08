"""Generate exact-model CUDA Colab workers for voice cloning and voice design.

The desktop chooses a catalog family before opening Colab.  Every generated
notebook loads exactly that family, advertises only that model, and rejects a
request carrying a different model ID.  API Gateway is intentionally absent:
these are temporary direct Colab workers.
"""

from __future__ import annotations

import json
from pathlib import Path
from textwrap import dedent

from colab_worker_launch import build_worker_launch

ROOT = Path(__file__).resolve().parents[1]
NOTEBOOKS = ROOT / "notebooks"


CLONE_COMMON = r'''
import io
import os
import shutil
import tempfile
import threading
import uuid
from pathlib import Path

import numpy as np
import soundfile as sf
import torch
from fastapi import FastAPI, File, Form, Header, HTTPException, UploadFile
from fastapi.responses import Response
from pydantic import BaseModel, Field

if not torch.cuda.is_available():
    raise RuntimeError("CUDA is unavailable. In Colab choose Runtime > Change runtime type > GPU, then Run all.")

TOKEN = os.environ["LA_STUDIO_COLAB_VOICE_CLONE_TOKEN"]
DATA_DIR = Path("/content/la-studio-voice-clone-data") / MODEL_ID
DATA_DIR.mkdir(parents=True, exist_ok=True)
MAX_REFERENCE_BYTES = 256 * 1024 * 1024
MAX_INPUT_CHARS = 4000
MAX_OUTPUT_SECONDS = 300
MODEL_LOCK = threading.Lock()
STATE_LOCK = threading.Lock()
PROFILES = {}
JOBS = {}

class GenerationRequest(BaseModel):
    model: str = Field(min_length=1, max_length=120)
    profile_id: str = Field(min_length=1, max_length=160)
    text: str = Field(min_length=1, max_length=MAX_INPUT_CHARS)
    language: str = Field(default="vi", max_length=40)
    speed: float = Field(default=1.0, ge=0.1, le=2.0)
    num_step: int = Field(default=32, ge=1, le=64)

def authorize(authorization: str | None) -> None:
    if authorization != "Bearer " + TOKEN:
        raise HTTPException(status_code=401, detail="invalid worker token")

def require_exact_model(model: str) -> None:
    if model.strip().lower() != MODEL_ID:
        raise HTTPException(
            status_code=409,
            detail=f"This worker loaded '{MODEL_ID}', but LA Studio requested '{model}'. Open the notebook for the selected model.",
        )

def audio_array(value):
    if isinstance(value, (list, tuple)):
        if not value:
            raise RuntimeError("the selected model returned no audio")
        value = value[0]
    if torch.is_tensor(value):
        value = value.detach().float().cpu().numpy()
    audio = np.asarray(value, dtype=np.float32).reshape(-1)
    if audio.size == 0 or not np.isfinite(audio).all():
        raise RuntimeError("the selected model returned invalid audio")
    return audio

def write_wav(path: Path, value, sample_rate: int) -> None:
    audio = audio_array(value)
    if audio.size > int(sample_rate) * MAX_OUTPUT_SECONDS:
        raise RuntimeError("generated audio exceeds the five minute output limit")
    peak = float(np.max(np.abs(audio)))
    if peak > 1.2:
        audio = audio / peak
    sf.write(path, audio, int(sample_rate), format="WAV", subtype="PCM_16")

def public_job(job_id: str):
    with STATE_LOCK:
        job = JOBS.get(job_id)
        if not job:
            raise HTTPException(status_code=404, detail="voice job not found")
        return {key: value for key, value in job.items() if key not in {"audio_path", "cancelled"}}

def fail_job(job_id: str, error: Exception) -> None:
    with STATE_LOCK:
        job = JOBS.get(job_id)
        if job:
            job.update({
                "status": "failed",
                "stage": "failed",
                "error": {"message": f"{type(error).__name__}: {str(error)[:300]}"},
            })

def build_profile(job_id: str, profile_id: str) -> None:
    try:
        with STATE_LOCK:
            profile = PROFILES[profile_id]
            JOBS[job_id].update({"status": "running", "stage": "prepare_profile", "percent": 10})
        with MODEL_LOCK:
            state = prepare_exact_profile(profile)
        with STATE_LOCK:
            profile["state"] = state
            JOBS[job_id].update({
                "status": "succeeded",
                "stage": "complete",
                "percent": 100,
                "result": {"id": profile_id, "model": MODEL_ID},
            })
    except Exception as error:
        fail_job(job_id, error)

def generate_audio(job_id: str, request: GenerationRequest) -> None:
    try:
        with STATE_LOCK:
            profile = PROFILES.get(request.profile_id)
            if not profile:
                raise RuntimeError("voice profile no longer exists")
            JOBS[job_id].update({"status": "running", "stage": "generate", "percent": 10})
        with MODEL_LOCK:
            audio, sample_rate = clone_with_exact_model(profile, request)
        output_path = DATA_DIR / f"{job_id}.wav"
        write_wav(output_path, audio, sample_rate)
        with STATE_LOCK:
            if JOBS[job_id].get("cancelled"):
                JOBS[job_id].update({"status": "cancelled", "stage": "cancelled", "percent": 0})
                output_path.unlink(missing_ok=True)
            else:
                JOBS[job_id].update({
                    "status": "succeeded",
                    "stage": "complete",
                    "percent": 100,
                    "audio_path": str(output_path),
                    "result": {"model": MODEL_ID, "sample_rate": int(sample_rate)},
                })
    except Exception as error:
        fail_job(job_id, error)

app = FastAPI(title=f"LA Studio Voice Cloning - {MODEL_NAME}", docs_url=None, redoc_url=None, openapi_url=None)

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
            "id": "voice-cloning",
            "models": [{
                "id": MODEL_ID,
                "name": MODEL_NAME,
                "variant": "fixed",
                "upstream_model": UPSTREAM_MODEL,
                "formats": ["wav"],
                "reference_formats": ["wav", "mp3", "flac"],
                "reference_duration_seconds": {"min": 3, "max": 30},
                "requires_consent": True,
                "device": "cuda",
                "loaded": True,
            }],
        }],
    }

@app.post("/v2/jobs/profile", status_code=202)
async def create_profile(
    model: str = Form(...),
    name: str = Form(...),
    consent_confirmed: bool = Form(...),
    ref_text: str = Form(default=""),
    language: str = Form(default="vi"),
    separate_music: bool = Form(default=False),
    ref_audio: UploadFile = File(...),
    authorization: str | None = Header(default=None),
):
    authorize(authorization)
    require_exact_model(model)
    if not consent_confirmed:
        raise HTTPException(status_code=403, detail="explicit voice-cloning consent is required")
    if not name.strip():
        raise HTTPException(status_code=422, detail="profile name is required")
    suffix = Path(ref_audio.filename or "").suffix.lower()
    if suffix not in {".wav", ".mp3", ".flac"}:
        raise HTTPException(status_code=415, detail="reference audio must be WAV, MP3, or FLAC")
    profile_id = uuid.uuid4().hex
    reference_path = DATA_DIR / f"{profile_id}{suffix}"
    size = 0
    with reference_path.open("wb") as output:
        while chunk := await ref_audio.read(1024 * 1024):
            size += len(chunk)
            if size > MAX_REFERENCE_BYTES:
                reference_path.unlink(missing_ok=True)
                raise HTTPException(status_code=413, detail="reference audio exceeds 256 MB")
            output.write(chunk)
    try:
        info = sf.info(reference_path)
        duration = float(info.frames) / float(info.samplerate)
    except Exception as error:
        reference_path.unlink(missing_ok=True)
        raise HTTPException(status_code=422, detail=f"reference audio cannot be decoded: {error}") from error
    if duration < 3.0 or duration > 30.0:
        reference_path.unlink(missing_ok=True)
        raise HTTPException(status_code=422, detail="reference audio must be between 3 and 30 seconds")
    job_id = uuid.uuid4().hex
    profile = {
        "id": profile_id,
        "model": MODEL_ID,
        "name": name.strip(),
        "ref_audio": str(reference_path),
        "ref_text": ref_text.strip(),
        "language": language.strip() or "vi",
        "separate_music": bool(separate_music),
    }
    with STATE_LOCK:
        PROFILES[profile_id] = profile
        JOBS[job_id] = {"id": job_id, "status": "queued", "stage": "queued", "percent": 0}
    threading.Thread(target=build_profile, args=(job_id, profile_id), daemon=True).start()
    return public_job(job_id)

@app.post("/v2/jobs/generation", status_code=202)
def create_generation(request: GenerationRequest, authorization: str | None = Header(default=None)):
    authorize(authorization)
    require_exact_model(request.model)
    with STATE_LOCK:
        profile = PROFILES.get(request.profile_id)
        if not profile:
            raise HTTPException(status_code=404, detail="voice profile not found")
        if profile["model"] != MODEL_ID:
            raise HTTPException(status_code=409, detail="voice profile belongs to a different model worker")
        job_id = uuid.uuid4().hex
        JOBS[job_id] = {"id": job_id, "status": "queued", "stage": "queued", "percent": 0}
    threading.Thread(target=generate_audio, args=(job_id, request), daemon=True).start()
    return public_job(job_id)

@app.get("/v2/jobs/{job_id}/audio")
def job_audio(job_id: str, authorization: str | None = Header(default=None)):
    authorize(authorization)
    with STATE_LOCK:
        job = JOBS.get(job_id)
        path = Path(job.get("audio_path", "")) if job else None
    if not job:
        raise HTTPException(status_code=404, detail="voice job not found")
    if job.get("status") != "succeeded" or not path or not path.is_file():
        raise HTTPException(status_code=409, detail="voice job audio is not ready")
    return Response(path.read_bytes(), media_type="audio/wav", headers={"Cache-Control": "no-store"})

@app.get("/v2/jobs/{job_id}")
def job_status(job_id: str, authorization: str | None = Header(default=None)):
    authorize(authorization)
    return public_job(job_id)

@app.delete("/v2/jobs/{job_id}")
def cancel_job(job_id: str, authorization: str | None = Header(default=None)):
    authorize(authorization)
    with STATE_LOCK:
        job = JOBS.get(job_id)
        if not job:
            raise HTTPException(status_code=404, detail="voice job not found")
        job["cancelled"] = True
        if job["status"] == "queued":
            job.update({"status": "cancelled", "stage": "cancelled", "percent": 0})
    return {"cancelled": True}

@app.delete("/v1/profiles/{profile_id}")
def delete_profile(profile_id: str, authorization: str | None = Header(default=None)):
    authorize(authorization)
    with STATE_LOCK:
        profile = PROFILES.pop(profile_id, None)
    if profile:
        Path(profile["ref_audio"]).unlink(missing_ok=True)
    return {"deleted": bool(profile)}
'''


DESIGN_COMMON = r'''
import io
import os
import re
import threading

import numpy as np
import soundfile as sf
import torch
from fastapi import FastAPI, Header, HTTPException
from fastapi.responses import Response
from pydantic import BaseModel, Field

if not torch.cuda.is_available():
    raise RuntimeError("CUDA is unavailable. In Colab choose Runtime > Change runtime type > GPU, then Run all.")

TOKEN = os.environ["LA_STUDIO_COLAB_VOICE_DESIGN_TOKEN"]
MAX_INPUT_CHARS = 4000
MAX_OUTPUT_SECONDS = 300
REQUEST_SLOTS = threading.BoundedSemaphore(1)

class VoiceDesignRequest(BaseModel):
    model: str = Field(min_length=1, max_length=120)
    input: str = Field(min_length=1, max_length=MAX_INPUT_CHARS)
    voice_description: str = Field(min_length=1, max_length=2000)
    style: str = Field(default="", max_length=1000)
    language: str = Field(default="en", max_length=40)
    temperature: float = Field(default=0.9, ge=0.1, le=2.0)
    seed: int = Field(default=-1, ge=-1)

def authorize(authorization: str | None) -> None:
    if authorization != "Bearer " + TOKEN:
        raise HTTPException(status_code=401, detail="invalid worker token")

def audio_array(value):
    if isinstance(value, (list, tuple)):
        if not value:
            raise RuntimeError("the selected model returned no audio")
        value = value[0]
    if torch.is_tensor(value):
        value = value.detach().float().cpu().numpy()
    audio = np.asarray(value, dtype=np.float32).reshape(-1)
    if audio.size == 0 or not np.isfinite(audio).all():
        raise RuntimeError("the selected model returned invalid audio")
    return audio

def wav_response(value, sample_rate: int):
    audio = audio_array(value)
    if audio.size > int(sample_rate) * MAX_OUTPUT_SECONDS:
        raise HTTPException(status_code=413, detail="generated audio exceeds the five minute output limit")
    peak = float(np.max(np.abs(audio)))
    if peak > 1.2:
        audio = audio / peak
    output = io.BytesIO()
    sf.write(output, audio, int(sample_rate), format="WAV", subtype="PCM_16")
    return Response(output.getvalue(), media_type="audio/wav", headers={"Cache-Control": "no-store"})

app = FastAPI(title=f"LA Studio Voice Design - {MODEL_NAME}", docs_url=None, redoc_url=None, openapi_url=None)

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
            "id": "voice-design",
            "models": [{
                "id": MODEL_ID,
                "name": MODEL_NAME,
                "variant": "fixed",
                "upstream_model": UPSTREAM_MODEL,
                "formats": ["wav"],
                "device": "cuda",
                "loaded": True,
            }],
        }],
    }

@app.post("/v1/audio/voice_designs")
def voice_design(request: VoiceDesignRequest, authorization: str | None = Header(default=None)):
    authorize(authorization)
    if request.model.strip().lower() != MODEL_ID:
        raise HTTPException(
            status_code=409,
            detail=f"This worker loaded '{MODEL_ID}', but LA Studio requested '{request.model}'. Open the notebook for the selected model.",
        )
    if not REQUEST_SLOTS.acquire(blocking=False):
        raise HTTPException(status_code=429, detail="the Colab voice-design worker is busy; retry shortly")
    try:
        audio, sample_rate = design_with_exact_model(request)
        return wav_response(audio, sample_rate)
    except HTTPException:
        raise
    except Exception as error:
        raise HTTPException(
            status_code=503,
            detail=f"{MODEL_NAME} voice design failed: {type(error).__name__}: {str(error)[:240]}",
        ) from error
    finally:
        REQUEST_SLOTS.release()
'''


def clone_specs() -> list[dict]:
    qwen_adapter = r'''
from qwen_tts import Qwen3TTSModel

MODEL_ID = "{family_id}"
MODEL_NAME = "{name}"
UPSTREAM_MODEL = "{upstream}"
MODEL = Qwen3TTSModel.from_pretrained(
    UPSTREAM_MODEL,
    device_map="cuda:0",
    dtype=torch.bfloat16,
    attn_implementation="sdpa",
)

def qwen_language(value: str):
    mapping = {{"auto": "Auto", "zh": "Chinese", "en": "English", "ja": "Japanese", "ko": "Korean", "de": "German", "fr": "French", "ru": "Russian", "pt": "Portuguese", "es": "Spanish", "it": "Italian", "vi": "Auto"}}
    return mapping.get(value.strip().lower(), value.strip().title() or "Auto")

def prepare_exact_profile(profile):
    if not profile["ref_text"]:
        # Qwen supports speaker-only cloning. It avoids making the transcript
        # a form requirement, with a clear quality trade-off for this mode.
        return MODEL.create_voice_clone_prompt(
            ref_audio=profile["ref_audio"],
            x_vector_only_mode=True,
        )
    return MODEL.create_voice_clone_prompt(
        ref_audio=profile["ref_audio"],
        ref_text=profile["ref_text"],
        x_vector_only_mode=False,
    )

def clone_with_exact_model(profile, request):
    wavs, sample_rate = MODEL.generate_voice_clone(
        text=request.text,
        language=qwen_language(request.language),
        voice_clone_prompt=profile["state"],
    )
    return wavs[0], sample_rate
'''
    return [
        {
            "family_id": "omnivoice",
            "name": "OmniVoice",
            "upstream": "k2-fsa/OmniVoice",
            "file": "LA_STUDIO_VOICE_CLONE_OMNIVOICE_GPU.ipynb",
            "install": r'''
!nvidia-smi
%pip install -q "git+https://github.com/k2-fsa/OmniVoice.git@468e927ba371" "soundfile==0.13.1" "python-multipart==0.0.20" "fastapi==0.115.12" "uvicorn==0.34.3"
''',
            "adapter": r'''
from omnivoice import OmniVoice

MODEL_ID = "omnivoice"
MODEL_NAME = "OmniVoice"
UPSTREAM_MODEL = "k2-fsa/OmniVoice"
MODEL = OmniVoice.from_pretrained(UPSTREAM_MODEL, device_map="cuda:0", dtype=torch.float16)

def prepare_exact_profile(profile):
    # OmniVoice auto-transcribes the reference with Whisper when ref_text is
    # omitted. Pass the keyword only when the user supplied an exact transcript.
    kwargs = {"ref_audio": profile["ref_audio"]}
    if profile["ref_text"]:
        kwargs["ref_text"] = profile["ref_text"]
    return MODEL.create_voice_clone_prompt(**kwargs)

def clone_with_exact_model(profile, request):
    audio = MODEL.generate(
        text=request.text,
        voice_clone_prompt=profile["state"],
        speed=request.speed,
        num_step=request.num_step,
    )
    return audio, 24000
''',
        },
        {
            "family_id": "qwen3-tts-0.6b-base",
            "name": "Qwen3-TTS Base 0.6B",
            "upstream": "Qwen/Qwen3-TTS-12Hz-0.6B-Base",
            "file": "LA_STUDIO_VOICE_CLONE_QWEN3_BASE_0_6B_GPU.ipynb",
            "install": r'''
!nvidia-smi
%pip install -q "qwen-tts==0.1.1" "soundfile==0.13.1" "python-multipart==0.0.20" "fastapi==0.115.12" "uvicorn==0.34.3"
''',
            "adapter": qwen_adapter.format(
                family_id="qwen3-tts-0.6b-base",
                name="Qwen3-TTS Base 0.6B",
                upstream="Qwen/Qwen3-TTS-12Hz-0.6B-Base",
            ),
        },
        {
            "family_id": "qwen3-tts-1.7b-base",
            "name": "Qwen3-TTS Base 1.7B",
            "upstream": "Qwen/Qwen3-TTS-12Hz-1.7B-Base",
            "file": "LA_STUDIO_VOICE_CLONE_QWEN3_BASE_1_7B_GPU.ipynb",
            "install": r'''
!nvidia-smi
%pip install -q "qwen-tts==0.1.1" "soundfile==0.13.1" "python-multipart==0.0.20" "fastapi==0.115.12" "uvicorn==0.34.3"
''',
            "adapter": qwen_adapter.format(
                family_id="qwen3-tts-1.7b-base",
                name="Qwen3-TTS Base 1.7B",
                upstream="Qwen/Qwen3-TTS-12Hz-1.7B-Base",
            ),
        },
        {
            "family_id": "vieneu-tts-v2-turbo",
            "name": "VieNeu-TTS v2 Turbo",
            "upstream": "pnnbao-ump/VieNeu-TTS-v2-Turbo",
            "file": "LA_STUDIO_VOICE_CLONE_VIENEU_V2_TURBO_GPU.ipynb",
            "install": r'''
!nvidia-smi
%pip install -q "torch==2.8.0" "torchaudio==2.8.0" --index-url https://download.pytorch.org/whl/cu128
%pip install -q --upgrade --force-reinstall --no-deps "torchvision==0.23.0" --index-url https://download.pytorch.org/whl/cu128
%pip install -q "transformers==4.57.6" "git+https://github.com/pnnbao97/VieNeu-TTS.git@f56ce97ffb37" "soundfile==0.13.1" "python-multipart==0.0.20" "fastapi==0.115.12" "uvicorn==0.34.3"

# Colab can retain an older torchvision after torch is upgraded. Transformers
# then masks the binary mismatch as a missing PreTrainedModel/Qwen3 class.
import importlib.metadata as package_metadata
import traceback

import torch
import torchvision

print("PyTorch stack:", torch.__version__, torchvision.__version__)
assert torch.cuda.is_available(), "CUDA is unavailable. In Colab choose Runtime > Change runtime type > GPU."
assert package_metadata.version("torchvision").split("+")[0] == "0.23.0", "VieNeu requires torchvision 0.23.0 with torch 2.8.0."
try:
    from transformers import PreTrainedModel
    from transformers.models.qwen3.modeling_qwen3 import Qwen3ForCausalLM
except Exception as error:
    traceback.print_exc()
    raise RuntimeError(
        "The Colab PyTorch/Transformers stack is not importable for VieNeu. "
        "Restart the runtime, rerun this install cell, then run all cells again."
    ) from error
print("Transformers imports verified for VieNeu:", PreTrainedModel.__name__, Qwen3ForCausalLM.__name__)
''',
            "adapter": r'''
from vieneu import Vieneu

MODEL_ID = "vieneu-tts-v2-turbo"
MODEL_NAME = "VieNeu-TTS v2 Turbo"
UPSTREAM_MODEL = "pnnbao-ump/VieNeu-TTS-v2-Turbo"
MODEL = Vieneu(mode="turbo_gpu", device="cuda", backend="standard", backbone_repo=UPSTREAM_MODEL)

def prepare_exact_profile(profile):
    return MODEL.encode_reference(profile["ref_audio"])

def clone_with_exact_model(profile, request):
    audio = MODEL.infer(text=request.text, voice=profile["state"])
    return audio, 24000
''',
        },
        {
            "family_id": "vieneu-tts-v3-turbo",
            "name": "VieNeu-TTS v3 Turbo",
            "upstream": "pnnbao-ump/VieNeu-TTS-v3-Turbo",
            "file": "LA_STUDIO_VOICE_CLONE_VIENEU_V3_TURBO_GPU.ipynb",
            "install": r'''
!nvidia-smi
%pip install -q "torch==2.8.0" "torchaudio==2.8.0" --index-url https://download.pytorch.org/whl/cu128
%pip install -q --upgrade --force-reinstall --no-deps "torchvision==0.23.0" --index-url https://download.pytorch.org/whl/cu128
%pip install -q "transformers==4.57.6" "git+https://github.com/pnnbao97/VieNeu-TTS.git@f56ce97ffb37" "soundfile==0.13.1" "python-multipart==0.0.20" "fastapi==0.115.12" "uvicorn==0.34.3"

# Colab can retain an older torchvision after torch is upgraded. Transformers
# then masks the binary mismatch as a missing PreTrainedModel/Qwen3 class.
import importlib.metadata as package_metadata
import traceback

import torch
import torchvision

print("PyTorch stack:", torch.__version__, torchvision.__version__)
assert torch.cuda.is_available(), "CUDA is unavailable. In Colab choose Runtime > Change runtime type > GPU."
assert package_metadata.version("torchvision").split("+")[0] == "0.23.0", "VieNeu requires torchvision 0.23.0 with torch 2.8.0."
try:
    from transformers import PreTrainedModel
    from transformers.models.qwen3.modeling_qwen3 import Qwen3ForCausalLM
except Exception as error:
    traceback.print_exc()
    raise RuntimeError(
        "The Colab PyTorch/Transformers stack is not importable for VieNeu. "
        "Restart the runtime, rerun this install cell, then run all cells again."
    ) from error
print("Transformers imports verified for VieNeu:", PreTrainedModel.__name__, Qwen3ForCausalLM.__name__)
''',
            "adapter": r'''
from vieneu import Vieneu

MODEL_ID = "vieneu-tts-v3-turbo"
MODEL_NAME = "VieNeu-TTS v3 Turbo"
UPSTREAM_MODEL = "pnnbao-ump/VieNeu-TTS-v3-Turbo"
MODEL = Vieneu(mode="v3turbo", device="cuda", backend="pytorch", backbone_repo=UPSTREAM_MODEL)

def prepare_exact_profile(profile):
    return MODEL.encode_reference(profile["ref_audio"], denoise=True)

def clone_with_exact_model(profile, request):
    speaker_emb, ref_codes = profile["state"]
    voice = {"speaker_emb": speaker_emb, "codes": ref_codes}
    audio = MODEL.infer(text=request.text, voice=voice, denoise=False)
    return audio, 48000
''',
        },
        {
            "family_id": "voxcpm2",
            "name": "VoxCPM2",
            "upstream": "openbmb/VoxCPM2",
            "file": "LA_STUDIO_VOICE_CLONE_VOXCPM2_GPU.ipynb",
            "install": r'''
!nvidia-smi
!git clone --quiet https://github.com/OpenBMB/VoxCPM.git /content/VoxCPM
!git -C /content/VoxCPM checkout --quiet 616d3d3e630a
%pip install -q -e /content/VoxCPM "soundfile==0.13.1" "python-multipart==0.0.20" "fastapi==0.115.12" "uvicorn==0.34.3"
''',
            "adapter": r'''
from voxcpm import VoxCPM

MODEL_ID = "voxcpm2"
MODEL_NAME = "VoxCPM2"
UPSTREAM_MODEL = "openbmb/VoxCPM2"
MODEL = VoxCPM.from_pretrained(UPSTREAM_MODEL, load_denoiser=False, optimize=True, device="cuda")

def prepare_exact_profile(profile):
    return {"ref_audio": profile["ref_audio"], "ref_text": profile["ref_text"]}

def clone_with_exact_model(profile, request):
    kwargs = {
        "text": request.text,
        "reference_wav_path": profile["state"]["ref_audio"],
        "cfg_value": 2.0,
        "inference_timesteps": max(1, min(request.num_step, 50)),
    }
    # VoxCPM can clone from reference audio alone. An optional transcript adds
    # the stronger prompt-guided mode when the user provides one.
    if profile["state"]["ref_text"]:
        kwargs.update({
            "prompt_wav_path": profile["state"]["ref_audio"],
            "prompt_text": profile["state"]["ref_text"],
        })
    audio = MODEL.generate(**kwargs)
    return audio, int(MODEL.tts_model.sample_rate)
''',
        },
    ]


def design_specs() -> list[dict]:
    return [
        {
            "family_id": "omnivoice",
            "name": "OmniVoice",
            "upstream": "k2-fsa/OmniVoice",
            "file": "LA_STUDIO_VOICE_DESIGN_OMNIVOICE_GPU.ipynb",
            "install": r'''
!nvidia-smi
%pip install -q "git+https://github.com/k2-fsa/OmniVoice.git@468e927ba371" "soundfile==0.13.1" "fastapi==0.115.12" "uvicorn==0.34.3"
''',
            "adapter": r'''
from omnivoice import OmniVoice

MODEL_ID = "omnivoice"
MODEL_NAME = "OmniVoice"
UPSTREAM_MODEL = "k2-fsa/OmniVoice"
MODEL = OmniVoice.from_pretrained(UPSTREAM_MODEL, device_map="cuda:0", dtype=torch.float16)

def design_with_exact_model(request):
    instruction = ", ".join(part for part in (request.voice_description.strip(), request.style.strip()) if part)
    kwargs = {"text": request.input, "instruct": instruction}
    if request.language.strip().lower() not in ("", "auto"):
        kwargs["language_id"] = request.language.strip().lower()
    audio = MODEL.generate(**kwargs)
    return audio, 24000
''',
        },
        {
            "family_id": "qwen3-tts-1.7b-voicedesign",
            "name": "Qwen3-TTS VoiceDesign 1.7B",
            "upstream": "Qwen/Qwen3-TTS-12Hz-1.7B-VoiceDesign",
            "file": "LA_STUDIO_VOICE_DESIGN_QWEN3_1_7B_GPU.ipynb",
            "install": r'''
!nvidia-smi
%pip install -q "qwen-tts==0.1.1" "soundfile==0.13.1" "fastapi==0.115.12" "uvicorn==0.34.3"
''',
            "adapter": r'''
from qwen_tts import Qwen3TTSModel

MODEL_ID = "qwen3-tts-1.7b-voicedesign"
MODEL_NAME = "Qwen3-TTS VoiceDesign 1.7B"
UPSTREAM_MODEL = "Qwen/Qwen3-TTS-12Hz-1.7B-VoiceDesign"
MODEL = Qwen3TTSModel.from_pretrained(
    UPSTREAM_MODEL,
    device_map="cuda:0",
    dtype=torch.bfloat16,
    attn_implementation="sdpa",
)

def qwen_language(value: str):
    mapping = {"auto": "Auto", "zh": "Chinese", "en": "English", "ja": "Japanese", "ko": "Korean", "de": "German", "fr": "French", "ru": "Russian", "pt": "Portuguese", "es": "Spanish", "it": "Italian"}
    return mapping.get(value.strip().lower(), value.strip().title() or "Auto")

def design_with_exact_model(request):
    instruction = ", ".join(part for part in (request.voice_description.strip(), request.style.strip()) if part)
    if request.seed >= 0:
        torch.manual_seed(request.seed)
        torch.cuda.manual_seed_all(request.seed)
    wavs, sample_rate = MODEL.generate_voice_design(
        text=request.input,
        language=qwen_language(request.language),
        instruct=instruction,
        temperature=request.temperature,
    )
    return wavs[0], sample_rate
''',
        },
        {
            "family_id": "voxcpm2",
            "name": "VoxCPM2",
            "upstream": "openbmb/VoxCPM2",
            "file": "LA_STUDIO_VOICE_DESIGN_VOXCPM2_GPU.ipynb",
            "install": r'''
!nvidia-smi
!git clone --quiet https://github.com/OpenBMB/VoxCPM.git /content/VoxCPM
!git -C /content/VoxCPM checkout --quiet 616d3d3e630a
%pip install -q -e /content/VoxCPM "soundfile==0.13.1" "fastapi==0.115.12" "uvicorn==0.34.3"
''',
            "adapter": r'''
from voxcpm import VoxCPM

MODEL_ID = "voxcpm2"
MODEL_NAME = "VoxCPM2"
UPSTREAM_MODEL = "openbmb/VoxCPM2"
MODEL = VoxCPM.from_pretrained(UPSTREAM_MODEL, load_denoiser=False, optimize=True, device="cuda")

def design_with_exact_model(request):
    instruction = ", ".join(part for part in (request.voice_description.strip(), request.style.strip()) if part)
    instruction = re.sub(r"[()（）]", "", instruction).strip()
    text = f"({instruction}){request.input}"
    audio = MODEL.generate(
        text=text,
        cfg_value=2.0,
        inference_timesteps=10,
        seed=None if request.seed < 0 else request.seed,
    )
    return audio, int(MODEL.tts_model.sample_rate)
''',
        },
    ]


START_TEMPLATE = r'''
import json, os, re, secrets, subprocess, sys, time, urllib.error, urllib.request
from pathlib import Path

MODEL_ID = {model_id!r}
TOKEN = secrets.token_urlsafe(32)
STARTUP_TIMEOUT_SECONDS = 20 * 60
WORKER_LOG = Path("/content/la_studio_{log_name}_worker.log")
env = os.environ.copy()
env["{token_env}"] = TOKEN
env["PYTHONUNBUFFERED"] = "1"

def worker_log_tail() -> str:
    try:
        return WORKER_LOG.read_text(encoding="utf-8", errors="replace")[-12000:]
    except FileNotFoundError:
        return "(worker log was not created)"

def fail_startup(message: str) -> None:
    if worker.poll() is None:
        worker.terminate()
        try:
            worker.wait(timeout=10)
        except subprocess.TimeoutExpired:
            worker.kill()
    raise RuntimeError(
        message + "\\n\\n---- LA Studio worker log (last 12,000 characters) ----\\n" + worker_log_tail()
    )

with WORKER_LOG.open("w", encoding="utf-8", buffering=1) as worker_output:
    worker = subprocess.Popen(
        [sys.executable, "-m", "uvicorn", "{module}:app", "--host", "127.0.0.1", "--port", "{port}"],
        cwd="/content",
        env=env,
        stdout=worker_output,
        stderr=subprocess.STDOUT,
    )
    print("Starting exact CUDA worker; initial model download can take several minutes.")
    deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
    last_error = "worker has not answered /health yet"
    while time.monotonic() < deadline:
        exit_code = worker.poll()
        if exit_code is not None:
            fail_startup(f"The exact-model worker exited before becoming ready (exit code {{exit_code}}).")
        try:
            check = urllib.request.Request(
                "http://127.0.0.1:{port}/health",
                headers={{"Authorization": "Bearer " + TOKEN}},
            )
            with urllib.request.urlopen(check, timeout=10) as response:
                health = json.loads(response.read().decode("utf-8"))
            if (response.status == 200
                    and health.get("ready") is True
                    and health.get("device") == "cuda"
                    and health.get("model") == MODEL_ID
                    and health.get("cpu_fallback") is False):
                print("Exact CUDA worker is ready:", health)
                break
            last_error = "unexpected /health response: " + json.dumps(health, ensure_ascii=False)
        except urllib.error.HTTPError as error:
            last_error = f"/health returned HTTP {{error.code}}: " + error.read().decode("utf-8", errors="replace")[:1000]
        except Exception as error:
            last_error = f"/health is not ready: {{type(error).__name__}}: {{error}}"
        if int(time.monotonic()) % 30 == 0:
            print("Waiting for the exact CUDA model…", last_error)
        time.sleep(2)
    else:
        fail_startup(
            f"The exact-model worker did not become CUDA-ready within {{STARTUP_TIMEOUT_SECONDS // 60}} minutes. "
            f"Last health-check result: {{last_error}}"
        )

subprocess.run(
    ["bash", "-lc", "wget -q -O /content/cloudflared.deb https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64.deb && dpkg -i /content/cloudflared.deb"],
    check=True,
)
tunnel = subprocess.Popen(
    ["cloudflared", "tunnel", "--url", "http://127.0.0.1:{port}", "--no-autoupdate"],
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
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

print("\n{url_label}=" + public_url)
print("{token_label}=" + TOKEN)
print("{model_label}=" + MODEL_ID)
print("DEVICE=cuda; CPU_FALLBACK=false")
'''


def source_lines(source: str) -> list[str]:
    return (dedent(source).strip() + "\n").splitlines(keepends=True)


def make_notebook(spec: dict, capability: str) -> dict:
    common = CLONE_COMMON if capability == "voice-cloning" else DESIGN_COMMON
    # Model adapters run before the shared server is declared, so torch must be
    # available while the exact model is loaded at module import time.
    worker_source = "import torch\n\n" + dedent(spec["adapter"]).strip() + "\n\n" + dedent(common).strip() + "\n"
    module = "la_studio_voice_clone_worker" if capability == "voice-cloning" else "la_studio_voice_design_worker"
    worker_file = f"/content/{module}.py"
    write_worker = (
        "from pathlib import Path\n\n"
        f"WORKER = Path({worker_file!r})\n"
        f"WORKER.write_text({worker_source!r}, encoding='utf-8')\n"
        "print('Worker source:', WORKER)\n"
    )
    if capability == "voice-cloning":
        start = build_worker_launch(
            capability_label="Voice Cloning",
            module=f"{module}:app",
            port=3923,
            model_id=spec["family_id"],
            token_env="LA_STUDIO_COLAB_VOICE_CLONE_TOKEN",
            url_env="LA_STUDIO_COLAB_VOICE_CLONE_URL",
            model_env="LA_STUDIO_COLAB_VOICE_CLONE_MODEL",
            log_path="/content/la_studio_voice_clone_worker.log",
        )
        panel = "Voice Cloning"
    else:
        start = build_worker_launch(
            capability_label="Voice Design",
            module=f"{module}:app",
            port=3924,
            model_id=spec["family_id"],
            token_env="LA_STUDIO_COLAB_VOICE_DESIGN_TOKEN",
            url_env="LA_STUDIO_COLAB_VOICE_DESIGN_URL",
            model_env="LA_STUDIO_COLAB_VOICE_DESIGN_MODEL",
            log_path="/content/la_studio_voice_design_worker.log",
        )
        panel = "Voice Design"
    return {
        "cells": [
            {
                "cell_type": "markdown",
                "metadata": {},
                "source": source_lines(
                    f"""
                    # LA Studio {panel} - {spec['name']}

                    This notebook loads exactly `{spec['family_id']}` (`{spec['upstream']}`) on CUDA.
                    It is independent from API Gateway and refuses every other model ID.

                    1. Choose **Runtime -> Change runtime type -> GPU**.
                    2. Run all cells.
                    3. Copy the printed URL and token into LA Studio's {panel} panel.
                    """
                ),
            },
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": source_lines(spec["install"])},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": source_lines(write_worker)},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": source_lines(start)},
        ],
        "metadata": {
            "accelerator": "GPU",
            "colab": {"gpuType": "T4", "provenance": []},
            "kernelspec": {"display_name": "Python 3", "name": "python3"},
            "language_info": {"name": "python"},
            "la_studio": {
                "capability": capability,
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
    for capability, specs in (("voice-cloning", clone_specs()), ("voice-design", design_specs())):
        for spec in specs:
            target = NOTEBOOKS / spec["file"]
            target.write_text(
                json.dumps(make_notebook(spec, capability), indent=1, ensure_ascii=False) + "\n",
                encoding="utf-8",
            )
            print(target.relative_to(ROOT))


if __name__ == "__main__":
    main()
