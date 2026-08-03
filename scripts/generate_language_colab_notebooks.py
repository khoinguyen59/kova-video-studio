"""Generate exact-model Colab workers for Translation and LLM Chat.

Every generated notebook loads one catalog family on CUDA, advertises only
that family, rejects every other model ID, and contains no API Gateway route.
"""

from __future__ import annotations

import json
from pathlib import Path
from textwrap import dedent

from colab_worker_launch import build_worker_launch

ROOT = Path(__file__).resolve().parents[1]
NOTEBOOKS = ROOT / "notebooks"


TRANSLATION_COMMON = r'''
import os
import re
import secrets
import threading

import torch
from fastapi import Depends, FastAPI, Header, HTTPException
from pydantic import BaseModel, Field

if not torch.cuda.is_available():
    raise RuntimeError("CUDA is unavailable. Choose a Colab GPU runtime; CPU fallback is disabled.")

# __ADAPTER__

TOKEN = os.environ["LA_STUDIO_COLAB_TRANSLATION_TOKEN"]
WORKER_REVISION = "translation-2026-07-30.3"
RESPONSE_CONTRACT = "translation-patches-v3"
MAX_TRANSLATION_SEGMENTS = 128
MAX_TRANSLATION_CHARS = 50000
INFERENCE_SLOTS = threading.BoundedSemaphore(1)

# A translation result must never make the rest of a dubbing job disappear.
# The worker retries blank model output once, then preserves the source in a
# visible needs-review patch. This is deliberately not reported as a completed
# translation: the editor can show it for review while later segments continue.
NONLEXICAL_UTTERANCES = {
    "ah", "aha", "eh", "er", "ha", "haha", "heh", "hmm", "hm", "ho", "oh",
    "uh", "um", "wow", "嗯", "嗯哼", "啊", "啊哈", "哎", "哎呀", "诶", "欸",
    "哈", "哈哈", "呵", "呵呵", "嘿", "哼", "唉", "呀", "呃", "哦", "哦哦",
    "喔", "噢", "哇", "唔",
}

def is_nonlexical_utterance(text: str) -> bool:
    normalized = re.sub(r"[^\w]", "", text, flags=re.UNICODE).casefold()
    return normalized in NONLEXICAL_UTTERANCES

def retry_empty_translations(texts: list[str], translated: list[str], source: str, target: str) -> list[str]:
    if not isinstance(translated, list) or len(translated) != len(texts):
        return translated
    empty_indices = [
        index for index, value in enumerate(translated)
        if not isinstance(value, str) or not value.strip()
    ]
    if not empty_indices:
        return translated
    # Retry only the affected source strings with the same selected, pinned
    # model. If the retry itself fails, the patch builder below keeps the source
    # and flags it for review instead of terminating unrelated segments.
    try:
        retry_values = translate_exact([texts[index] for index in empty_indices], source, target)
    except Exception:
        return translated
    if not isinstance(retry_values, list) or len(retry_values) != len(empty_indices):
        return translated
    for index, retry_value in zip(empty_indices, retry_values):
        if isinstance(retry_value, str) and retry_value.strip():
            translated[index] = retry_value
    return translated

def make_translation_patches(segments: list["TranslationSegment"], translated: list[str]) -> list[dict]:
    if len(translated) != len(segments):
        raise RuntimeError("model returned a different number of translations")
    patches = []
    for index, (item, value) in enumerate(zip(segments, translated), start=1):
        target = value.strip() if isinstance(value, str) else ""
        if target:
            patches.append({"id": item.id, "targetText": target, "state": "translated"})
            continue
        source = item.sourceText.strip()
        if is_nonlexical_utterance(source):
            patches.append({
                "id": item.id,
                "targetText": source,
                "state": "needs-review",
                "translationDiagnostic": (
                    "The exact translation model returned no lexical text for this short vocal reaction; "
                    "the source was preserved for review."
                ),
            })
            continue
        patches.append({
            "id": item.id,
            "targetText": source,
            "state": "needs-review",
            "translationDiagnostic": (
                "The exact translation model returned no text after retry; the source was preserved "
                "and later segments continued. Review this segment before export."
            ),
        })
    return patches

def authorize(authorization: str = Header(default="")):
    if not secrets.compare_digest(authorization, "Bearer " + TOKEN):
        raise HTTPException(status_code=401, detail="invalid or missing bearer token")

def require_exact_model(requested: str) -> None:
    if requested.strip().lower() != MODEL_ID:
        raise HTTPException(
            status_code=409,
            detail=f"This worker loaded '{MODEL_ID}', but LA Studio requested '{requested}'. Open the notebook for the selected model.",
        )

class TranslationSegment(BaseModel):
    id: str = Field(min_length=1, max_length=128)
    sourceText: str = Field(min_length=1, max_length=5000)

class TranslationRequest(BaseModel):
    model: str
    source_language: str = Field(min_length=2, max_length=12)
    target_language: str = Field(min_length=2, max_length=12)
    segments: list[TranslationSegment]

app = FastAPI(title=f"LA Studio Translation - {MODEL_NAME}", docs_url=None, redoc_url=None, openapi_url=None)

@app.get("/health")
@app.get("/v1/health")
def health(_: None = Depends(authorize)):
    return {
        "status": "ready",
        "ready": True,
        "device": "cuda",
        "gpu": torch.cuda.get_device_name(0),
        "model": MODEL_ID,
        "variant": "fixed",
        "upstream_model": UPSTREAM_MODEL,
        "worker_revision": WORKER_REVISION,
        "response_contract": RESPONSE_CONTRACT,
        "cpu_fallback": False,
    }

@app.get("/v1/capabilities")
def capabilities(_: None = Depends(authorize)):
    return {
        "contract_version": 1,
        "response_contract": RESPONSE_CONTRACT,
        "worker_revision": WORKER_REVISION,
        "device": "cuda",
        "capabilities": [{
            "id": "translation",
            "models": [{
                "id": MODEL_ID,
                "name": MODEL_NAME,
                "variant": "fixed",
                "upstream_model": UPSTREAM_MODEL,
                "languages": SUPPORTED_LANGUAGES,
                "device": "cuda",
                "loaded": True,
                "response_contract": RESPONSE_CONTRACT,
            }],
        }],
    }

@app.post("/v1/translations")
def translate(request: TranslationRequest, _: None = Depends(authorize)):
    require_exact_model(request.model)
    if not request.segments:
        raise HTTPException(status_code=400, detail="segments must not be empty")
    texts = [item.sourceText.strip() for item in request.segments]
    if any(not text for text in texts):
        raise HTTPException(status_code=400, detail="each segment needs sourceText")
    if len(texts) > MAX_TRANSLATION_SEGMENTS or sum(map(len, texts)) > MAX_TRANSLATION_CHARS:
        raise HTTPException(status_code=413, detail="translation request is too large")
    if not INFERENCE_SLOTS.acquire(blocking=False):
        raise HTTPException(status_code=429, detail="worker is busy; retry shortly")
    try:
        translated = translate_exact(
            texts,
            request.source_language.strip(),
            request.target_language.strip(),
        )
        translated = retry_empty_translations(
            texts,
            translated,
            request.source_language.strip(),
            request.target_language.strip(),
        )
        return {"patches": make_translation_patches(request.segments, translated)}
    except HTTPException:
        raise
    except Exception as error:
        raise HTTPException(
            status_code=503,
            detail=f"{MODEL_NAME} translation failed: {type(error).__name__}: {str(error)[:300]}",
        ) from error
    finally:
        INFERENCE_SLOTS.release()
'''


M2M_ADAPTER = r'''
from transformers import M2M100ForConditionalGeneration, M2M100Tokenizer

MODEL_ID = "m2m100-418m"
MODEL_NAME = "M2M-100 418M"
UPSTREAM_MODEL = "facebook/m2m100_418M"
UPSTREAM_REVISION = "55c2e61bbf05dfb8d7abccdc3fae6fc8512fd636"
SUPPORTED_LANGUAGES = "101 M2M100 language codes"

TOKENIZER = M2M100Tokenizer.from_pretrained(UPSTREAM_MODEL, revision=UPSTREAM_REVISION)
MODEL = M2M100ForConditionalGeneration.from_pretrained(
    UPSTREAM_MODEL,
    revision=UPSTREAM_REVISION,
    torch_dtype=torch.float16,
    low_cpu_mem_usage=True,
).to("cuda").eval()

def translate_exact(texts: list[str], source: str, target: str) -> list[str]:
    source = source.lower()
    target = target.lower()
    try:
        TOKENIZER.src_lang = source
        target_id = TOKENIZER.get_lang_id(target)
    except KeyError as error:
        raise HTTPException(status_code=422, detail=f"unsupported M2M100 language pair: {source} -> {target}") from error
    def generate(batch: list[str], **generation_options: object) -> list[str]:
        inputs = TOKENIZER(batch, return_tensors="pt", padding=True, truncation=True, max_length=512).to("cuda")
        with torch.inference_mode():
            output = MODEL.generate(
                **inputs,
                forced_bos_token_id=target_id,
                max_new_tokens=512,
                **generation_options,
            )
        return TOKENIZER.batch_decode(output, skip_special_tokens=True)

    translated = generate(texts)
    # Greedy decoding can terminate at EOS immediately for a noisy but valid
    # ASR segment. Retry only those blanks with the same pinned M2M checkpoint
    # and a deterministic beam search; this is not a source-text fallback.
    for index, value in enumerate(translated):
        if isinstance(value, str) and value.strip():
            continue
        retry_text = " ".join(texts[index].split())
        retry = generate(
            [retry_text],
            num_beams=4,
            min_new_tokens=1,
            early_stopping=True,
            repetition_penalty=1.05,
        )
        translated[index] = retry[0] if retry else ""
    return translated
'''


MADLAD_ADAPTER = r'''
from transformers import AutoModelForSeq2SeqLM, AutoTokenizer

MODEL_ID = "madlad400-3b-mt"
MODEL_NAME = "MADLAD-400 3B MT"
UPSTREAM_MODEL = "google/madlad400-3b-mt"
UPSTREAM_REVISION = "fa184c675da0b5c9e1c8694fccd4e12e2d422094"
SUPPORTED_LANGUAGES = "419 MADLAD language codes"

TOKENIZER = AutoTokenizer.from_pretrained(UPSTREAM_MODEL, revision=UPSTREAM_REVISION)
MODEL = AutoModelForSeq2SeqLM.from_pretrained(
    UPSTREAM_MODEL,
    revision=UPSTREAM_REVISION,
    torch_dtype=torch.float16,
    low_cpu_mem_usage=True,
).to("cuda").eval()

def translate_exact(texts: list[str], source: str, target: str) -> list[str]:
    del source
    target = target.lower()
    tag = f"<2{target}>"
    if TOKENIZER.convert_tokens_to_ids(tag) == TOKENIZER.unk_token_id:
        raise HTTPException(status_code=422, detail=f"unsupported MADLAD target language: {target}")
    prompts = [f"{tag} {text}" for text in texts]
    inputs = TOKENIZER(prompts, return_tensors="pt", padding=True, truncation=True, max_length=512).to("cuda")
    with torch.inference_mode():
        output = MODEL.generate(**inputs, max_new_tokens=512)
    return TOKENIZER.batch_decode(output, skip_special_tokens=True)
'''


HY_MT2_ADAPTER = r'''
from transformers import AutoModelForCausalLM, AutoTokenizer

MODEL_ID = "hy-mt2-1.8b"
MODEL_NAME = "Tencent Hy-MT2 1.8B"
UPSTREAM_MODEL = "tencent/Hy-MT2-1.8B"
UPSTREAM_REVISION = "9a341cd1b679d3efd23b46e847b01745a71ed792"
LANGUAGE_NAMES = {
    "zh": "Chinese", "en": "English", "fr": "French", "pt": "Portuguese",
    "es": "Spanish", "ja": "Japanese", "tr": "Turkish", "ru": "Russian",
    "ar": "Arabic", "ko": "Korean", "th": "Thai", "it": "Italian",
    "de": "German", "vi": "Vietnamese", "ms": "Malay", "id": "Indonesian",
    "tl": "Filipino", "hi": "Hindi", "zh-hant": "Traditional Chinese",
    "pl": "Polish", "cs": "Czech", "nl": "Dutch", "km": "Khmer",
    "my": "Burmese", "fa": "Persian", "gu": "Gujarati", "ur": "Urdu",
    "te": "Telugu", "mr": "Marathi", "he": "Hebrew", "bn": "Bengali",
    "ta": "Tamil", "uk": "Ukrainian", "bo": "Tibetan", "kk": "Kazakh",
    "mn": "Mongolian", "ug": "Uyghur", "yue": "Cantonese",
}
SUPPORTED_LANGUAGES = list(LANGUAGE_NAMES)

TOKENIZER = AutoTokenizer.from_pretrained(
    UPSTREAM_MODEL, revision=UPSTREAM_REVISION, trust_remote_code=True
)
MODEL = AutoModelForCausalLM.from_pretrained(
    UPSTREAM_MODEL,
    revision=UPSTREAM_REVISION,
    dtype=torch.bfloat16,
    device_map={"": 0},
    trust_remote_code=True,
    low_cpu_mem_usage=True,
).eval()

def translate_exact(texts: list[str], source: str, target: str) -> list[str]:
    source_key = source.lower()
    target_key = target.lower()
    if source_key not in LANGUAGE_NAMES or target_key not in LANGUAGE_NAMES:
        raise HTTPException(status_code=422, detail=f"unsupported Hy-MT2 language pair: {source} -> {target}")
    results = []
    for text in texts:
        prompt = (
            f"Translate the following text from {LANGUAGE_NAMES[source_key]} into {LANGUAGE_NAMES[target_key]}. "
            "Only output the translated result without any additional explanation:\n"
            f"{text}"
        )
        inputs = TOKENIZER.apply_chat_template(
            [{"role": "user", "content": prompt}],
            add_generation_prompt=True,
            return_tensors="pt",
            return_dict=True,
        ).to("cuda")
        with torch.inference_mode():
            output = MODEL.generate(
                **inputs,
                max_new_tokens=512,
                # Translation needs reproducible decoding. Sampling can end at
                # EOS immediately and was the only adapter that could emit an
                # empty response nondeterministically for the same segment.
                do_sample=False,
            )
        generated = output[0][inputs["input_ids"].shape[-1]:]
        results.append(TOKENIZER.decode(generated, skip_special_tokens=True).strip())
    return results
'''


CHAT_WORKER = r'''
import json
import os
import secrets
import threading

import torch
from fastapi import Depends, FastAPI, Header, HTTPException, Request
from fastapi.responses import StreamingResponse
from pydantic import BaseModel, Field
from transformers import (
    AutoModelForMultimodalLM,
    AutoProcessor,
    StoppingCriteria,
    StoppingCriteriaList,
    TextIteratorStreamer,
)

if not torch.cuda.is_available():
    raise RuntimeError("CUDA is unavailable. Choose a Colab GPU runtime; CPU fallback is disabled.")

MODEL_ID = "qwen3.5-2b"
MODEL_NAME = "Qwen3.5 2B"
UPSTREAM_MODEL = "Qwen/Qwen3.5-2B"
UPSTREAM_REVISION = "15852e8c16360a2fea060d615a32b45270f8a8fc"
TOKEN = os.environ["LA_STUDIO_COLAB_CHAT_TOKEN"]
MAX_CHAT_MESSAGES = 64
MAX_CHAT_CHARS = 50000
MAX_CHAT_TOKENS = 32768
INFERENCE_SLOTS = threading.BoundedSemaphore(1)

PROCESSOR = AutoProcessor.from_pretrained(UPSTREAM_MODEL, revision=UPSTREAM_REVISION)
MODEL = AutoModelForMultimodalLM.from_pretrained(
    UPSTREAM_MODEL,
    revision=UPSTREAM_REVISION,
    torch_dtype=torch.float16,
    device_map={"": 0},
    low_cpu_mem_usage=True,
).eval()
TOKENIZER = PROCESSOR.tokenizer

def authorize(authorization: str = Header(default="")):
    if not secrets.compare_digest(authorization, "Bearer " + TOKEN):
        raise HTTPException(status_code=401, detail="invalid or missing bearer token")

def require_exact_model(requested: str) -> None:
    if requested.strip().lower() != MODEL_ID:
        raise HTTPException(
            status_code=409,
            detail=f"This worker loaded '{MODEL_ID}', but LA Studio requested '{requested}'. Open the notebook for the selected model.",
        )

class ChatMessage(BaseModel):
    role: str = Field(pattern="^(system|user|assistant)$")
    content: str = Field(min_length=1, max_length=8000)

class ChatRequest(BaseModel):
    model: str
    messages: list[ChatMessage]
    stream: bool = True
    max_tokens: int = Field(default=1024, ge=1, le=MAX_CHAT_TOKENS)
    context_tokens: int = Field(default=4096, ge=512, le=131072)
    temperature: float = Field(default=0.7, ge=0.01, le=2.0)
    top_p: float = Field(default=0.8, ge=0.01, le=1.0)
    top_k: int = Field(default=20, ge=1, le=200)
    repeat_penalty: float = Field(default=1.05, ge=0.8, le=2.0)

class DisconnectStop(StoppingCriteria):
    def __init__(self, cancelled: threading.Event):
        self.cancelled = cancelled
    def __call__(self, input_ids, scores, **kwargs):
        return self.cancelled.is_set()

app = FastAPI(title="LA Studio Chat - Qwen3.5 2B", docs_url=None, redoc_url=None, openapi_url=None)

@app.get("/health")
@app.get("/v1/health")
def health(_: None = Depends(authorize)):
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
def capabilities(_: None = Depends(authorize)):
    return {
        "contract_version": 1,
        "device": "cuda",
        "capabilities": [{
            "id": "llm-chat",
            "models": [{
                "id": MODEL_ID,
                "name": MODEL_NAME,
                "variant": "fixed",
                "upstream_model": UPSTREAM_MODEL,
                "device": "cuda",
                "loaded": True,
            }],
        }],
    }

@app.post("/v1/chat/completions")
async def chat(request: ChatRequest, http_request: Request, _: None = Depends(authorize)):
    require_exact_model(request.model)
    if not request.stream:
        raise HTTPException(status_code=400, detail="this direct worker requires stream=true")
    if not request.messages:
        raise HTTPException(status_code=400, detail="messages must not be empty")
    if len(request.messages) > MAX_CHAT_MESSAGES or sum(len(item.content) for item in request.messages) > MAX_CHAT_CHARS:
        raise HTTPException(status_code=413, detail="chat request is too large")
    if not INFERENCE_SLOTS.acquire(blocking=False):
        raise HTTPException(status_code=429, detail="worker is busy; retry shortly")
    try:
        messages = [{"role": item.role, "content": item.content} for item in request.messages]
        inputs = PROCESSOR.apply_chat_template(
            messages,
            add_generation_prompt=True,
            tokenize=True,
            return_dict=True,
            return_tensors="pt",
            truncation=True,
            max_length=request.context_tokens,
        ).to("cuda")
        streamer = TextIteratorStreamer(TOKENIZER, skip_prompt=True, skip_special_tokens=True)
        cancelled = threading.Event()
        generation_errors = []
        def generate():
            try:
                MODEL.generate(
                    **inputs,
                    streamer=streamer,
                    max_new_tokens=request.max_tokens,
                    do_sample=True,
                    temperature=request.temperature,
                    top_p=request.top_p,
                    top_k=request.top_k,
                    repetition_penalty=request.repeat_penalty,
                    pad_token_id=TOKENIZER.eos_token_id,
                    stopping_criteria=StoppingCriteriaList([DisconnectStop(cancelled)]),
                )
            except Exception as error:
                generation_errors.append(error)
                streamer.end()
        generation = threading.Thread(target=generate, daemon=True)
        generation.start()
    except Exception:
        INFERENCE_SLOTS.release()
        raise

    async def events():
        try:
            for token in streamer:
                if await http_request.is_disconnected():
                    cancelled.set()
                    break
                yield "data: " + json.dumps({"choices": [{"delta": {"content": token}}]}, ensure_ascii=False) + "\n\n"
            generation.join(timeout=10)
            if generation_errors:
                message = f"{type(generation_errors[0]).__name__}: {str(generation_errors[0])[:300]}"
                yield "data: " + json.dumps({"error": {"message": message}}) + "\n\n"
            yield "data: [DONE]\n\n"
        finally:
            cancelled.set()
            INFERENCE_SLOTS.release()
    return StreamingResponse(events(), media_type="text/event-stream", headers={"Cache-Control": "no-store"})
'''


START_TEMPLATE = r'''
import json
import os
import secrets
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

TOKEN = secrets.token_urlsafe(32)
env = os.environ.copy()
env[{token_env!r}] = TOKEN
log_path = {log_path!r}
worker = subprocess.Popen(
    [sys.executable, "-m", "uvicorn", {module!r}, "--host", "127.0.0.1", "--port", {port!r}],
    cwd="/content",
    env=env,
    stdout=open(log_path, "w"),
    stderr=subprocess.STDOUT,
)
for _ in range(180):
    try:
        request = urllib.request.Request(
            "http://127.0.0.1:{port}/health",
            headers={{"Authorization": "Bearer " + TOKEN}},
        )
        with urllib.request.urlopen(request, timeout=5) as response:
            health = json.load(response)
        if health.get("ready") and health.get("device") == "cuda" and health.get("model") == MODEL_ID:
            break
    except Exception:
        time.sleep(2)
else:
    raise RuntimeError("Exact-model worker did not become ready. Log tail:\n" + Path(log_path).read_text(errors="replace")[-5000:])

subprocess.run(
    ["wget", "-q", "-O", "/content/cloudflared.deb",
     "https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64.deb"],
    check=True,
)
subprocess.run(["dpkg", "-i", "/content/cloudflared.deb"], check=True)
tunnel = subprocess.Popen(
    ["cloudflared", "tunnel", "--url", "http://127.0.0.1:{port}", "--no-autoupdate"],
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
)
public_url = None
for _ in range(180):
    line = tunnel.stdout.readline()
    print(line, end="")
    if "https://" in line and "trycloudflare.com" in line:
        public_url = line[line.find("https://"):].split()[0]
        break
if not public_url:
    raise RuntimeError("Cloudflare tunnel URL was not found")

print("\n{url_name}=" + public_url)
print("{token_name}=" + TOKEN)
print("{model_name}=" + MODEL_ID)
print("Paste only these direct-worker values into the matching LA Studio feature. Do not add /v1.")
'''


def source_lines(source: str) -> list[str]:
    return (dedent(source).strip() + "\n").splitlines(keepends=True)


def notebook(
    *,
    title: str,
    family_id: str,
    upstream: str,
    capability: str,
    install: str,
    worker_source: str,
    worker_path: str,
    module: str,
    token_env: str,
    port: int,
    url_name: str,
    token_name: str,
    model_name: str,
) -> dict:
    writer = (
        "from pathlib import Path\n\n"
        f"WORKER = Path({worker_path!r})\n"
        f"WORKER.write_text({dedent(worker_source).strip()!r} + '\\n', encoding='utf-8')\n"
        "print('Worker source:', WORKER)\n"
    )
    start = build_worker_launch(
        capability_label=capability,
        module=f"{module}:app",
        port=port,
        model_id=family_id,
        token_env=token_env,
        url_env=url_name,
        model_env=model_name,
        log_path=f"/content/{module}.log",
    )
    return {
        "cells": [
            {
                "cell_type": "markdown",
                "metadata": {},
                "source": source_lines(
                    f"""
                    # LA Studio {capability} — {title}

                    This notebook loads exactly `{family_id}` (`{upstream}`) on CUDA.
                    It is independent from API Gateway and rejects every other model ID.

                    1. Choose **Runtime → Change runtime type → GPU**.
                    2. Run all cells.
                    3. Copy the printed URL and token into the matching LA Studio feature.
                    """
                ),
            },
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": source_lines(install)},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": source_lines(writer)},
            {
                "cell_type": "code",
                "execution_count": None,
                "metadata": {},
                "outputs": [],
                "source": source_lines(f"MODEL_ID = {family_id!r}\n" + start),
            },
        ],
        "metadata": {
            "accelerator": "GPU",
            "colab": {"gpuType": "T4", "provenance": []},
            "kernelspec": {"display_name": "Python 3", "name": "python3"},
            "language_info": {"name": "python"},
            "la_studio": {
                "capability": capability,
                "family_id": family_id,
                "upstream_model": upstream,
                "contract_version": 1,
                "device": "cuda",
                "cpu_fallback": False,
            },
        },
        "nbformat": 4,
        "nbformat_minor": 5,
    }


def translation_specs() -> list[dict]:
    common_install = r'''
!nvidia-smi
%pip install -q "fastapi==0.115.12" "uvicorn==0.34.3" "transformers==4.57.3" "accelerate==1.12.0" "sentencepiece==0.2.1" "safetensors==0.6.2"
'''
    hy_install = r'''
!nvidia-smi
%pip install -q "fastapi==0.115.12" "uvicorn==0.34.3" "transformers>=5.6.0,<6" "accelerate>=1.12,<2" "sentencepiece==0.2.1" "safetensors>=0.6,<1"
'''
    return [
        {
            "file": "LA_STUDIO_TRANSLATION_M2M100_418M_GPU.ipynb",
            "title": "M2M-100 418M",
            "family_id": "m2m100-418m",
            "upstream": "facebook/m2m100_418M",
            "install": common_install,
            "adapter": M2M_ADAPTER,
        },
        {
            "file": "LA_STUDIO_TRANSLATION_MADLAD400_3B_GPU.ipynb",
            "title": "MADLAD-400 3B MT",
            "family_id": "madlad400-3b-mt",
            "upstream": "google/madlad400-3b-mt",
            "install": common_install,
            "adapter": MADLAD_ADAPTER,
        },
        {
            "file": "LA_STUDIO_TRANSLATION_HY_MT2_1_8B_GPU.ipynb",
            "title": "Tencent Hy-MT2 1.8B",
            "family_id": "hy-mt2-1.8b",
            "upstream": "tencent/Hy-MT2-1.8B",
            "install": hy_install,
            "adapter": HY_MT2_ADAPTER,
        },
    ]


def main() -> None:
    NOTEBOOKS.mkdir(parents=True, exist_ok=True)
    for spec in translation_specs():
        worker = dedent(TRANSLATION_COMMON).replace(
            "# __ADAPTER__", dedent(spec["adapter"]).strip(), 1
        )
        payload = notebook(
            title=spec["title"],
            family_id=spec["family_id"],
            upstream=spec["upstream"],
            capability="translation",
            install=spec["install"],
            worker_source=worker,
            worker_path="/content/la_studio_translation_worker.py",
            module="la_studio_translation_worker",
            token_env="LA_STUDIO_COLAB_TRANSLATION_TOKEN",
            port=3943,
            url_name="LA_STUDIO_COLAB_TRANSLATION_URL",
            token_name="LA_STUDIO_COLAB_TRANSLATION_TOKEN",
            model_name="LA_STUDIO_COLAB_TRANSLATION_MODEL",
        )
        target = NOTEBOOKS / spec["file"]
        target.write_text(json.dumps(payload, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
        print(target.relative_to(ROOT))

    chat_install = r'''
!nvidia-smi
%pip install -q "fastapi==0.115.12" "uvicorn==0.34.3" "transformers>=5.6.0,<6" "accelerate>=1.12,<2" "safetensors>=0.6,<1"
'''
    chat_payload = notebook(
        title="Qwen3.5 2B",
        family_id="qwen3.5-2b",
        upstream="Qwen/Qwen3.5-2B",
        capability="llm-chat",
        install=chat_install,
        worker_source=CHAT_WORKER,
        worker_path="/content/la_studio_chat_worker.py",
        module="la_studio_chat_worker",
        token_env="LA_STUDIO_COLAB_CHAT_TOKEN",
        port=3944,
        url_name="LA_STUDIO_COLAB_CHAT_URL",
        token_name="LA_STUDIO_COLAB_CHAT_TOKEN",
        model_name="LA_STUDIO_COLAB_CHAT_MODEL",
    )
    chat_target = NOTEBOOKS / "LA_STUDIO_LLM_QWEN3_5_2B_GPU.ipynb"
    chat_target.write_text(json.dumps(chat_payload, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(chat_target.relative_to(ROOT))


if __name__ == "__main__":
    main()
