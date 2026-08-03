"""Generate exact-model, CUDA-only Colab workers for LA Studio TTS.

Each notebook loads one catalog family and rejects requests for every other
model ID.  Keep the adapter code close to the upstream project's public Python
API so notebook audits can verify what actually runs on the Colab GPU.
"""

from __future__ import annotations

import json
from pathlib import Path
from textwrap import dedent

from colab_worker_launch import build_worker_launch

ROOT = Path(__file__).resolve().parents[1]
NOTEBOOKS = ROOT / "notebooks"

COMMON_IMPORTS = r'''
import io
import os
import threading

import numpy as np
import soundfile as sf
import torch
from fastapi import FastAPI, Header, HTTPException
from fastapi.responses import Response
from pydantic import BaseModel, Field

if not torch.cuda.is_available():
    raise RuntimeError("CUDA is unavailable. In Colab choose Runtime > Change runtime type > GPU, then Run all.")

TOKEN = os.environ["LA_STUDIO_COLAB_TTS_TOKEN"]
MAX_INPUT_CHARS = 4000
MAX_OUTPUT_SECONDS = 300
REQUEST_SLOTS = threading.BoundedSemaphore(1)

class SpeechRequest(BaseModel):
    model: str = Field(min_length=1, max_length=120)
    input: str = Field(min_length=1, max_length=MAX_INPUT_CHARS)
    voice: str = Field(default="auto", max_length=160)
    language: str = Field(default="auto", max_length=40)
    speed: float = Field(default=1.0, ge=0.25, le=4.0)
    response_format: str = "wav"
    settings: dict = Field(default_factory=dict)

def authorize(authorization: str | None) -> None:
    if authorization != "Bearer " + TOKEN:
        raise HTTPException(status_code=401, detail="invalid worker token")

def wav_response(samples, sample_rate: int):
    audio = np.asarray(samples, dtype=np.float32).reshape(-1)
    if audio.size == 0:
        raise RuntimeError("the selected model returned no audio")
    if audio.size > int(sample_rate) * MAX_OUTPUT_SECONDS:
        raise HTTPException(status_code=413, detail="generated audio exceeds the five minute output limit")
    if not np.isfinite(audio).all():
        raise RuntimeError("the selected model returned non-finite audio")
    peak = float(np.max(np.abs(audio)))
    if peak > 1.2:
        audio = audio / peak
    output = io.BytesIO()
    sf.write(output, audio, int(sample_rate), format="WAV", subtype="PCM_16")
    return Response(output.getvalue(), media_type="audio/wav", headers={"Cache-Control": "no-store"})
'''

COMMON_SERVER = r'''
app = FastAPI(title=f"LA Studio TTS — {MODEL_NAME}", docs_url=None, redoc_url=None, openapi_url=None)

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
            "id": "tts",
            "models": [{
                "id": MODEL_ID,
                "name": MODEL_NAME,
                "variant": "fixed",
                "upstream_model": UPSTREAM_MODEL,
                "languages": SUPPORTED_LANGUAGES,
                "voices": SUPPORTED_VOICES,
                "formats": ["wav"],
                "device": "cuda",
                "loaded": True,
            }],
        }],
    }

@app.post("/v1/audio/speech")
def speech(request: SpeechRequest, authorization: str | None = Header(default=None)):
    authorize(authorization)
    if request.model.strip().lower() != MODEL_ID:
        raise HTTPException(
            status_code=409,
            detail=f"This worker loaded '{MODEL_ID}', but LA Studio requested '{request.model}'. Open the notebook for the selected model.",
        )
    if request.response_format.strip().lower() != "wav":
        raise HTTPException(status_code=422, detail="this worker returns WAV audio only")
    if not REQUEST_SLOTS.acquire(blocking=False):
        raise HTTPException(status_code=429, detail="the Colab TTS worker is busy; retry shortly")
    try:
        samples, sample_rate = synthesize_exact_model(request)
        return wav_response(samples, sample_rate)
    except HTTPException:
        raise
    except Exception as error:
        raise HTTPException(
            status_code=503,
            detail=f"{MODEL_NAME} synthesis failed: {type(error).__name__}: {str(error)[:240]}",
        ) from error
    finally:
        REQUEST_SLOTS.release()
'''

START_CELL = r'''
import json, os, re, secrets, subprocess, sys, time, urllib.error, urllib.request
from pathlib import Path

TOKEN = secrets.token_urlsafe(32)
STARTUP_TIMEOUT_SECONDS = 20 * 60
WORKER_LOG = Path("/content/la_studio_tts_worker.log")
env = os.environ.copy()
env["LA_STUDIO_COLAB_TTS_TOKEN"] = TOKEN
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
        message + "\\n\\n---- LA Studio TTS worker log (last 12,000 characters) ----\\n" + worker_log_tail()
    )

with WORKER_LOG.open("w", encoding="utf-8", buffering=1) as worker_output:
    worker = subprocess.Popen(
        [sys.executable, "-m", "uvicorn", "la_studio_tts_worker:app", "--host", "127.0.0.1", "--port", "3921"],
        cwd="/content",
        env=env,
        stdout=worker_output,
        stderr=subprocess.STDOUT,
    )
    print("Starting exact CUDA TTS worker; initial model download can take several minutes.")
    deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
    next_report = time.monotonic()
    last_error = "worker has not answered /health yet"
    while time.monotonic() < deadline:
        exit_code = worker.poll()
        if exit_code is not None:
            fail_startup(f"The exact-model TTS worker exited before becoming ready (exit code {exit_code}).")
        try:
            check = urllib.request.Request(
                "http://127.0.0.1:3921/health",
                headers={"Authorization": "Bearer " + TOKEN},
            )
            with urllib.request.urlopen(check, timeout=10) as response:
                health = json.loads(response.read().decode("utf-8"))
            if (response.status == 200
                    and health.get("ready") is True
                    and health.get("device") == "cuda"
                    and health.get("model") == MODEL_ID
                    and health.get("cpu_fallback") is False):
                print("Exact CUDA TTS worker is ready:", health)
                break
            last_error = "unexpected /health response: " + json.dumps(health, ensure_ascii=False)
        except urllib.error.HTTPError as error:
            last_error = f"/health returned HTTP {error.code}: " + error.read().decode("utf-8", errors="replace")[:1000]
        except Exception as error:
            last_error = f"/health is not ready: {type(error).__name__}: {error}"
        if time.monotonic() >= next_report:
            print("Waiting for the exact CUDA TTS model…", last_error)
            next_report = time.monotonic() + 30
        time.sleep(2)
    else:
        fail_startup(
            f"The exact-model TTS worker did not become CUDA-ready within {STARTUP_TIMEOUT_SECONDS // 60} minutes. "
            f"Last health-check result: {last_error}"
        )

subprocess.run(
    ["bash", "-lc", "wget -q -O /content/cloudflared.deb https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64.deb && dpkg -i /content/cloudflared.deb"],
    check=True,
)
tunnel = subprocess.Popen(
    ["cloudflared", "tunnel", "--url", "http://127.0.0.1:3921", "--no-autoupdate"],
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

print("\nLA_STUDIO_COLAB_TTS_URL=" + public_url)
print("LA_STUDIO_COLAB_TTS_TOKEN=" + TOKEN)
print("LA_STUDIO_COLAB_TTS_MODEL=" + MODEL_ID)
print("DEVICE=cuda; CPU_FALLBACK=false")
'''

VIENEU_CUDA_IMPORT_PREFLIGHT = r'''

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
'''

SPECS = [
    {
        "family_id": "kokoro",
        "name": "Kokoro 82M",
        "upstream": "hexgrad/Kokoro-82M",
        "file": "LA_STUDIO_TTS_KOKORO_GPU.ipynb",
        "install": r'''
!nvidia-smi
!apt-get -qq update && apt-get -qq install -y espeak-ng
%pip install -q "git+https://github.com/hexgrad/kokoro.git@dfb907a02bba" "soundfile==0.13.1" "fastapi==0.115.12" "uvicorn==0.34.3"
''',
        "adapter": r'''
from functools import lru_cache
from kokoro import KPipeline

MODEL_ID = "kokoro"
MODEL_NAME = "Kokoro 82M"
UPSTREAM_MODEL = "hexgrad/Kokoro-82M"
LANGUAGE_CODES = {"en": "a", "en-us": "a", "en-gb": "b", "ja": "j", "zh": "z", "es": "e", "fr": "f", "hi": "h", "it": "i", "pt-br": "p"}
SUPPORTED_LANGUAGES = sorted(LANGUAGE_CODES)
SUPPORTED_VOICES = ["af_heart", "af_bella", "af_nicole", "am_adam", "am_michael", "bf_emma", "bm_george"]

@lru_cache(maxsize=10)
def pipeline_for(code: str):
    pipeline = KPipeline(lang_code=code)
    moved = False
    for attribute in ("model", "kokoro_model"):
        candidate = getattr(pipeline, attribute, None)
        if hasattr(candidate, "to"):
            candidate.to("cuda")
            moved = True
    if not moved:
        raise RuntimeError("Kokoro did not expose a CUDA-movable model")
    return pipeline

pipeline_for("a")

def synthesize_exact_model(request: SpeechRequest):
    language = request.language.strip().lower() or "en"
    code = LANGUAGE_CODES.get(language)
    if not code:
        raise HTTPException(status_code=422, detail="unsupported Kokoro language")
    voice = request.voice.strip().lower() or "af_heart"
    if voice not in SUPPORTED_VOICES:
        raise HTTPException(status_code=422, detail="unsupported Kokoro voice")
    chunks = [
        np.asarray(audio, dtype=np.float32).reshape(-1)
        for _, _, audio in pipeline_for(code)(request.input, voice=voice, speed=request.speed)
    ]
    chunks = [chunk for chunk in chunks if chunk.size]
    if not chunks:
        raise RuntimeError("Kokoro returned no chunks")
    return np.concatenate(chunks), 24000
''',
    },
    {
        "family_id": "kokoro-vietnamese",
        "name": "Kokoro Vietnamese",
        "upstream": "contextboxai/Kokoro-Vietnamese",
        "file": "LA_STUDIO_TTS_KOKORO_VIETNAMESE_GPU.ipynb",
        "install": r'''
!nvidia-smi
%pip install -q "git+https://github.com/iamdinhthuan/Kokoro-Vietnamese.git@a249afe5555a" "onnxruntime-gpu==1.22.0" "soundfile==0.13.1" "fastapi==0.115.12" "uvicorn==0.34.3"
''',
        "adapter": r'''
from functools import lru_cache
from kokoro_vietnamese.core import list_voices
from kokoro_vietnamese.onnx_cli import KokoroVietnameseONNX

MODEL_ID = "kokoro-vietnamese"
MODEL_NAME = "Kokoro Vietnamese"
UPSTREAM_MODEL = "contextboxai/Kokoro-Vietnamese"
SUPPORTED_LANGUAGES = ["vi"]
SUPPORTED_VOICES = list(list_voices()) or ["diem_trinh"]

@lru_cache(maxsize=24)
def model_for(voice: str):
    runtime = KokoroVietnameseONNX(voice=voice, device="cuda")
    if "CUDAExecutionProvider" not in runtime.session.get_providers():
        raise RuntimeError("Kokoro Vietnamese ONNX did not activate CUDAExecutionProvider")
    return runtime

model_for(SUPPORTED_VOICES[0])

def synthesize_exact_model(request: SpeechRequest):
    if request.language.strip().lower() not in ("", "auto", "vi", "vi-vn"):
        raise HTTPException(status_code=422, detail="Kokoro Vietnamese supports Vietnamese only")
    voice = request.voice.strip() or SUPPORTED_VOICES[0]
    if voice not in SUPPORTED_VOICES:
        raise HTTPException(status_code=422, detail="unsupported Kokoro Vietnamese voice")
    audio, _phonemes = model_for(voice).synthesize(request.input, speed=request.speed)
    return audio, 24000
''',
    },
    {
        "family_id": "omnivoice",
        "name": "OmniVoice",
        "upstream": "k2-fsa/OmniVoice",
        "file": "LA_STUDIO_TTS_OMNIVOICE_GPU.ipynb",
        "install": r'''
!nvidia-smi
%pip install -q "git+https://github.com/k2-fsa/OmniVoice.git@468e927ba371" "soundfile==0.13.1" "fastapi==0.115.12" "uvicorn==0.34.3"
''',
        "adapter": r'''
from omnivoice import OmniVoice

MODEL_ID = "omnivoice"
MODEL_NAME = "OmniVoice"
UPSTREAM_MODEL = "k2-fsa/OmniVoice"
SUPPORTED_LANGUAGES = ["auto", "vi", "en", "zh", "ja", "ko", "fr", "es", "de"]
SUPPORTED_VOICES = ["auto"]
MODEL = OmniVoice.from_pretrained(UPSTREAM_MODEL, device_map="cuda:0", dtype=torch.float16)
if not next(MODEL.parameters()).is_cuda:
    raise RuntimeError("OmniVoice did not load on CUDA")

def synthesize_exact_model(request: SpeechRequest):
    kwargs = {"text": request.input, "speed": request.speed}
    language = request.language.strip().lower()
    if language and language != "auto":
        kwargs["language_id"] = language
    audio = MODEL.generate(**kwargs)
    return audio[0] if isinstance(audio, (list, tuple)) else audio, 24000
''',
    },
    {
        "family_id": "qwen3-tts-1.7b-customvoice",
        "name": "Qwen3-TTS CustomVoice 1.7B",
        "upstream": "Qwen/Qwen3-TTS-12Hz-1.7B-CustomVoice",
        "file": "LA_STUDIO_TTS_QWEN3_CUSTOMVOICE_1_7B_GPU.ipynb",
        "install": r'''
!nvidia-smi
%pip install -q "qwen-tts==0.1.1" "soundfile==0.13.1" "fastapi==0.115.12" "uvicorn==0.34.3"
''',
        "adapter": r'''
from qwen_tts import Qwen3TTSModel

MODEL_ID = "qwen3-tts-1.7b-customvoice"
MODEL_NAME = "Qwen3-TTS CustomVoice 1.7B"
UPSTREAM_MODEL = "Qwen/Qwen3-TTS-12Hz-1.7B-CustomVoice"
SUPPORTED_LANGUAGES = ["Auto", "Chinese", "English", "Japanese", "Korean", "German", "French", "Russian", "Portuguese", "Spanish", "Italian"]
SUPPORTED_VOICES = ["Aiden", "Dylan", "Eric", "Ono_Anna", "Ryan", "Serena", "Sohee", "Uncle_Fu", "Vivian"]
MODEL = Qwen3TTSModel.from_pretrained(UPSTREAM_MODEL, device_map="cuda:0", dtype=torch.float16, attn_implementation="sdpa")

def qwen_language(value: str):
    normalized = value.strip().lower()
    mapping = {"auto": "Auto", "zh": "Chinese", "en": "English", "ja": "Japanese", "ko": "Korean", "de": "German", "fr": "French", "ru": "Russian", "pt": "Portuguese", "es": "Spanish", "it": "Italian"}
    return mapping.get(normalized, value.strip().title() or "Auto")

def synthesize_exact_model(request: SpeechRequest):
    speaker = request.voice.strip() or "Aiden"
    if speaker.lower() not in {voice.lower() for voice in SUPPORTED_VOICES}:
        raise HTTPException(status_code=422, detail="unsupported Qwen3 CustomVoice speaker")
    canonical = next(voice for voice in SUPPORTED_VOICES if voice.lower() == speaker.lower())
    instruct = str(request.settings.get("instruct", "")).strip()
    wavs, sample_rate = MODEL.generate_custom_voice(
        text=request.input,
        language=qwen_language(request.language),
        speaker=canonical,
        instruct=instruct,
    )
    return wavs[0], sample_rate
''',
    },
    {
        "family_id": "vibevoice",
        "name": "VibeVoice Realtime 0.5B",
        "upstream": "microsoft/VibeVoice-Realtime-0.5B",
        "file": "LA_STUDIO_TTS_VIBEVOICE_0_5B_GPU.ipynb",
        "install": r'''
!nvidia-smi
!git clone --quiet https://github.com/microsoft/VibeVoice.git /content/VibeVoice
!git -C /content/VibeVoice checkout --quiet 94da20d98b2f
%pip install -q -e "/content/VibeVoice[streamingtts]" "soundfile==0.13.1" "fastapi==0.115.12" "uvicorn==0.34.3"
!bash /content/VibeVoice/demo/download_experimental_voices.sh
''',
        "adapter": r'''
import copy
import glob
from pathlib import Path
from transformers.cache_utils import DynamicCache
from transformers.modeling_outputs import BaseModelOutputWithPast
from vibevoice.modular.modeling_vibevoice_streaming_inference import VibeVoiceStreamingForConditionalGenerationInference
from vibevoice.processor.vibevoice_streaming_processor import VibeVoiceStreamingProcessor

MODEL_ID = "vibevoice"
MODEL_NAME = "VibeVoice Realtime 0.5B"
UPSTREAM_MODEL = "microsoft/VibeVoice-Realtime-0.5B"
# The pinned VibeVoice Realtime 0.5B release is English-only. Advertising
# unsupported languages here would let the desktop UI select a route the
# upstream model explicitly describes as unpredictable.
SUPPORTED_LANGUAGES = ["en"]
VOICE_DIR = Path("/content/VibeVoice/demo/voices/streaming_model")
VOICE_FILES = {Path(path).stem.lower(): path for path in glob.glob(str(VOICE_DIR / "**" / "*.pt"), recursive=True)}
if not VOICE_FILES:
    raise RuntimeError("VibeVoice voice presets were not downloaded")
SUPPORTED_VOICES = sorted(VOICE_FILES)
PROCESSOR = VibeVoiceStreamingProcessor.from_pretrained(UPSTREAM_MODEL)
MODEL = VibeVoiceStreamingForConditionalGenerationInference.from_pretrained(
    UPSTREAM_MODEL,
    torch_dtype=torch.bfloat16,
    device_map="cuda",
    attn_implementation="sdpa",
)
MODEL.eval()
MODEL.set_ddpm_inference_steps(num_steps=5)

def synthesize_exact_model(request: SpeechRequest):
    voice = request.voice.strip().lower() or SUPPORTED_VOICES[0]
    if voice not in VOICE_FILES:
        raise HTTPException(status_code=422, detail="unsupported VibeVoice preset")
    with torch.serialization.safe_globals([BaseModelOutputWithPast, DynamicCache]):
        cached = torch.load(VOICE_FILES[voice], map_location="cuda", weights_only=True)
    inputs = PROCESSOR.process_input_with_cached_prompt(
        text=request.input.replace("’", "'").replace("“", '"').replace("”", '"'),
        cached_prompt=cached,
        padding=True,
        return_tensors="pt",
        return_attention_mask=True,
    )
    for key, value in inputs.items():
        if torch.is_tensor(value):
            inputs[key] = value.to("cuda")
    outputs = MODEL.generate(
        **inputs,
        max_new_tokens=None,
        cfg_scale=float(request.settings.get("cfg_scale", 1.5)),
        tokenizer=PROCESSOR.tokenizer,
        generation_config={"do_sample": False},
        verbose=False,
        all_prefilled_outputs=copy.deepcopy(cached),
    )
    if not outputs.speech_outputs or outputs.speech_outputs[0] is None:
        raise RuntimeError("VibeVoice returned no speech output")
    return outputs.speech_outputs[0].detach().float().cpu().numpy(), 24000
''',
    },
    {
        "family_id": "vieneu-tts-v2-turbo",
        "name": "VieNeu-TTS v2 Turbo",
        "upstream": "pnnbao-ump/VieNeu-TTS-v2-Turbo",
        "file": "LA_STUDIO_TTS_VIENEU_V2_TURBO_GPU.ipynb",
        "install": r'''
!nvidia-smi
%pip install -q "torch==2.8.0" "torchaudio==2.8.0" --index-url https://download.pytorch.org/whl/cu128
%pip install -q --upgrade --force-reinstall --no-deps "torchvision==0.23.0" --index-url https://download.pytorch.org/whl/cu128
%pip install -q "transformers==4.57.6" "git+https://github.com/pnnbao97/VieNeu-TTS.git@f56ce97ffb37" "onnxruntime-gpu==1.22.0" "soundfile==0.13.1" "fastapi==0.115.12" "uvicorn==0.34.3"
''' + VIENEU_CUDA_IMPORT_PREFLIGHT,
        "adapter": r'''
from vieneu import Vieneu

MODEL_ID = "vieneu-tts-v2-turbo"
MODEL_NAME = "VieNeu-TTS v2 Turbo"
UPSTREAM_MODEL = "pnnbao-ump/VieNeu-TTS-v2-Turbo"
SUPPORTED_LANGUAGES = ["vi", "en"]
MODEL = Vieneu(mode="turbo_gpu", device="cuda", backend="standard", backbone_repo=UPSTREAM_MODEL)
if getattr(MODEL, "device", "") != "cuda":
    raise RuntimeError("VieNeu v2 Turbo did not load on CUDA")
VOICE_ROWS = MODEL.list_preset_voices()
SUPPORTED_VOICES = [str(row[1]) for row in VOICE_ROWS] if VOICE_ROWS else ["auto"]

def synthesize_exact_model(request: SpeechRequest):
    voice = request.voice.strip()
    kwargs = {"text": request.input}
    if voice and voice.lower() != "auto":
        kwargs["voice"] = voice
    audio = MODEL.infer(**kwargs)
    return audio, 24000
''',
    },
    {
        "family_id": "vieneu-tts-v3-turbo",
        "name": "VieNeu-TTS v3 Turbo",
        "upstream": "pnnbao-ump/VieNeu-TTS-v3-Turbo",
        "file": "LA_STUDIO_TTS_VIENEU_V3_TURBO_GPU.ipynb",
        "install": r'''
!nvidia-smi
%pip install -q "torch==2.8.0" "torchaudio==2.8.0" --index-url https://download.pytorch.org/whl/cu128
%pip install -q --upgrade --force-reinstall --no-deps "torchvision==0.23.0" --index-url https://download.pytorch.org/whl/cu128
%pip install -q "transformers==4.57.6" "git+https://github.com/pnnbao97/VieNeu-TTS.git@f56ce97ffb37" "soundfile==0.13.1" "fastapi==0.115.12" "uvicorn==0.34.3"
''' + VIENEU_CUDA_IMPORT_PREFLIGHT,
        "adapter": r'''
from vieneu import Vieneu

MODEL_ID = "vieneu-tts-v3-turbo"
MODEL_NAME = "VieNeu-TTS v3 Turbo"
UPSTREAM_MODEL = "pnnbao-ump/VieNeu-TTS-v3-Turbo"
SUPPORTED_LANGUAGES = ["vi", "en"]
MODEL = Vieneu(mode="v3turbo", device="cuda", backend="pytorch", backbone_repo=UPSTREAM_MODEL)
if getattr(MODEL, "backend", "") != "pytorch":
    raise RuntimeError("VieNeu v3 Turbo did not activate the PyTorch CUDA backend")
VOICE_ROWS = MODEL.list_preset_voices()
SUPPORTED_VOICES = [str(row[1]) for row in VOICE_ROWS] if VOICE_ROWS else ["auto"]

def synthesize_exact_model(request: SpeechRequest):
    voice = request.voice.strip()
    kwargs = {"text": request.input}
    if voice and voice.lower() != "auto":
        kwargs["voice"] = voice
    style = str(request.settings.get("style", "")).strip()
    if style:
        kwargs["style"] = style
    audio = MODEL.infer(**kwargs)
    return audio, 48000
''',
    },
    {
        "family_id": "voxcpm2",
        "name": "VoxCPM2",
        "upstream": "openbmb/VoxCPM2",
        "file": "LA_STUDIO_TTS_VOXCPM2_GPU.ipynb",
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
SUPPORTED_LANGUAGES = ["auto", "vi", "en", "zh", "ja", "ko", "fr", "de", "es", "it", "pt", "th"]
SUPPORTED_VOICES = ["auto"]
MODEL = VoxCPM.from_pretrained(UPSTREAM_MODEL, load_denoiser=False, optimize=True, device="cuda")
if "cuda" not in str(MODEL.model.device).lower():
    raise RuntimeError("VoxCPM2 did not load on CUDA")

def synthesize_exact_model(request: SpeechRequest):
    audio = MODEL.generate(
        text=request.input,
        cfg_value=float(request.settings.get("cfg_value", 2.0)),
        inference_timesteps=int(request.settings.get("inference_timesteps", 10)),
        normalize=bool(request.settings.get("normalize", True)),
        seed=request.settings.get("seed"),
    )
    return audio, 48000
''',
    },
]


def source_lines(source: str) -> list[str]:
    text = dedent(source).strip() + "\n"
    return text.splitlines(keepends=True)


def notebook(spec: dict[str, str]) -> dict:
    worker_source = (
        dedent(COMMON_IMPORTS).strip()
        + "\n\n"
        + dedent(spec["adapter"]).strip()
        + "\n\n"
        + dedent(COMMON_SERVER).strip()
        + "\n"
    )
    write_worker = (
        "from pathlib import Path\n\n"
        "WORKER = Path('/content/la_studio_tts_worker.py')\n"
        f"WORKER.write_text({worker_source!r}, encoding='utf-8')\n"
        "print('Worker source:', WORKER)\n"
    )
    return {
        "cells": [
            {
                "cell_type": "markdown",
                "metadata": {},
                "source": source_lines(
                    f"""
                    # LA Studio TTS — {spec['name']}

                    This notebook loads exactly `{spec['family_id']}` (`{spec['upstream']}`) on CUDA.
                    It does not use API Gateway and refuses every other model ID.

                    1. Choose **Runtime → Change runtime type → GPU**.
                    2. Run all cells.
                    3. Copy the printed URL and token into LA Studio's TTS panel.
                    """
                ),
            },
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": source_lines(spec["install"])},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": source_lines(write_worker)},
            {
                "cell_type": "code",
                "execution_count": None,
                "metadata": {},
                "outputs": [],
                "source": source_lines(build_worker_launch(
                    capability_label="TTS",
                    module="la_studio_tts_worker:app",
                    port=3921,
                    model_id=spec["family_id"],
                    token_env="LA_STUDIO_COLAB_TTS_TOKEN",
                    url_env="LA_STUDIO_COLAB_TTS_URL",
                    model_env="LA_STUDIO_COLAB_TTS_MODEL",
                    log_path="/content/la_studio_tts_worker.log",
                )),
            },
        ],
        "metadata": {
            "accelerator": "GPU",
            "colab": {"gpuType": "T4", "provenance": []},
            "kernelspec": {"display_name": "Python 3", "name": "python3"},
            "language_info": {"name": "python"},
            "la_studio": {
                "capability": "tts",
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
    for spec in SPECS:
        target = NOTEBOOKS / spec["file"]
        target.write_text(json.dumps(notebook(spec), indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
        print(target.relative_to(ROOT))


if __name__ == "__main__":
    main()
