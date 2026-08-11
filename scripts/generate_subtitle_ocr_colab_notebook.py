#!/usr/bin/env python3
"""Generate the exact-model direct Colab worker for sampled Subtitle OCR.

The desktop only uploads one already-cropped PNG frame at a time.  This keeps
the source video local, makes request cancellation prompt, and permits real
per-frame progress rather than guessed workflow percentages.
"""

from __future__ import annotations

import json
from pathlib import Path
from textwrap import dedent

from colab_worker_launch import build_worker_launch


ROOT = Path(__file__).resolve().parents[1]
NOTEBOOKS = ROOT / "notebooks"
MODEL_ID = "pp-ocrv5-multilingual-3.1"
NOTEBOOK = "LA_STUDIO_SUBTITLE_OCR_PP_OCRV5_GPU.ipynb"


WORKER = r'''
import json
import os
import secrets
import tempfile
import threading
from pathlib import Path

import paddle
from fastapi import Depends, FastAPI, File, Form, Header, HTTPException, Request, UploadFile
from fastapi.responses import JSONResponse
from paddleocr import PaddleOCR

if not paddle.device.is_compiled_with_cuda():
    raise RuntimeError("CUDA is unavailable. Choose a Colab GPU runtime; CPU fallback is disabled.")
paddle.device.set_device("gpu:0")
GPU_NAME = paddle.device.cuda.get_device_name(0)

MODEL_ID = "pp-ocrv5-multilingual-3.1"
MODEL_NAME = "PP-OCRv5 Multilingual 3.1"
UPSTREAM_MODEL = "PaddlePaddle/PaddleOCR PP-OCRv5"
UPSTREAM_VERSION = "PaddleOCR 3.1.1"
LICENSE = "Apache-2.0"
WORKER_REVISION = "subtitle-ocr-2026-08-11.9"
RESPONSE_CONTRACT = "subtitle-ocr-crops-v1"
TOKEN = os.environ["LA_STUDIO_COLAB_SUBTITLE_OCR_TOKEN"]
MAX_UPLOAD_BYTES = 16 * 1024 * 1024
INFERENCE_SLOTS = threading.BoundedSemaphore(1)
ENGINE_LOCK = threading.Lock()
ENGINES: dict[str, PaddleOCR] = {}

# The desktop has stored legacy Tesseract codes since Subtitle OCR started.
# Map them explicitly to the PP-OCRv5 language profiles; unsupported codes are
# rejected instead of routed to an arbitrary model or a CPU fallback.
LANGUAGE_PROFILES = {
    "eng": "en", "en": "en",
    "vie": "vi", "vi": "vi",
    "chi_sim": "ch", "chi_tra": "chinese_cht", "ch": "ch", "zh": "ch",
    "jpn": "japan", "ja": "japan",
    "kor": "korean", "ko": "korean",
}


def authorize(authorization: str = Header(default="")):
    if not secrets.compare_digest(authorization, "Bearer " + TOKEN):
        raise HTTPException(status_code=401, detail="invalid or missing bearer token")


def require_exact_model(requested: str) -> None:
    if requested.strip().lower() != MODEL_ID:
        raise HTTPException(
            status_code=409,
            detail=f"This worker loaded '{MODEL_ID}', but LA Studio requested '{requested}'. Open the matching notebook.",
        )


def resolve_profile(language: str) -> str:
    # A composite Tesseract value (for example eng+chi_sim) is intentionally
    # rejected. The user must choose one visible OCR language per run, which
    # makes the actual PP-OCR profile and expected quality unambiguous.
    normalized = language.strip().lower()
    if normalized not in LANGUAGE_PROFILES:
        raise HTTPException(status_code=422, detail=f"unsupported Subtitle OCR language: {language}")
    return LANGUAGE_PROFILES[normalized]


def engine_for(profile: str) -> PaddleOCR:
    with ENGINE_LOCK:
        engine = ENGINES.get(profile)
        if engine is None:
            engine = PaddleOCR(
                lang=profile,
                ocr_version="PP-OCRv5",
                device="gpu:0",
                use_doc_orientation_classify=False,
                use_doc_unwarping=False,
                use_textline_orientation=False,
            )
            ENGINES[profile] = engine
        return engine


def result_fields(result: object) -> tuple[list[str], list[float]]:
    # PaddleOCR 3.x result objects expose a JSON-compatible payload. Keep this
    # adapter tolerant of the documented result wrappers, but never invent text
    # when the exact model detected none in a sampled subtitle crop.
    payload = result
    if hasattr(payload, "json"):
        payload = payload.json
        if callable(payload):
            payload = payload()
    if isinstance(payload, str):
        payload = json.loads(payload)
    if not isinstance(payload, dict):
        return [], []
    data = payload.get("res", payload)
    if not isinstance(data, dict):
        return [], []
    texts = data.get("rec_texts", data.get("text", []))
    scores = data.get("rec_scores", data.get("scores", []))
    if isinstance(texts, str):
        texts = [texts]
    if not isinstance(texts, list):
        texts = []
    if not isinstance(scores, list):
        scores = []
    clean_texts = [str(value).strip() for value in texts if str(value).strip()]
    clean_scores = [float(value) for value in scores[:len(clean_texts)] if isinstance(value, (int, float))]
    return clean_texts, clean_scores


app = FastAPI(title=f"LA Studio Subtitle OCR - {MODEL_NAME}", docs_url=None, redoc_url=None, openapi_url=None)


@app.exception_handler(Exception)
async def unhandled_exception(request: Request, error: Exception):
    detail = f"Subtitle OCR worker internal error: {type(error).__name__}: {str(error)[:300]}"
    print(detail, flush=True)
    return JSONResponse(status_code=500, content={"detail": detail})


@app.get("/health")
@app.get("/v1/health")
def health(_: None = Depends(authorize)):
    return {
        "ready": True,
        "device": "cuda",
        "gpu": GPU_NAME,
        "model": MODEL_ID,
        "variant": "fixed",
        "upstream_model": UPSTREAM_MODEL,
        "upstream_version": UPSTREAM_VERSION,
        "license": LICENSE,
        "worker_revision": WORKER_REVISION,
        "response_contract": RESPONSE_CONTRACT,
        "cpu_fallback": False,
    }


@app.get("/v1/capabilities")
def capabilities(_: None = Depends(authorize)):
    return {
        "contract_version": 1,
        "device": "cuda",
        "worker_revision": WORKER_REVISION,
        "capabilities": [{
            "id": "subtitle-ocr",
            "models": [{
                "id": MODEL_ID,
                "name": MODEL_NAME,
                "variant": "fixed",
                "upstream_model": UPSTREAM_MODEL,
                "upstream_version": UPSTREAM_VERSION,
                "license": LICENSE,
                "languages": ["vi", "ch", "japan", "korean", "en"],
                "device": "cuda",
                "loaded": True,
                "response_contract": RESPONSE_CONTRACT,
            }],
        }],
    }


@app.post("/v1/ocr/subtitles")
async def recognize_subtitle(
    model: str = Form(...),
    language: str = Form(...),
    file: UploadFile = File(...),
    _: None = Depends(authorize),
):
    require_exact_model(model)
    profile = resolve_profile(language)
    if file.content_type not in {"image/png", "application/octet-stream"}:
        raise HTTPException(status_code=415, detail="Subtitle OCR accepts only PNG crop frames")
    data = await file.read(MAX_UPLOAD_BYTES + 1)
    if not data or len(data) > MAX_UPLOAD_BYTES or not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise HTTPException(status_code=400, detail="Subtitle OCR crop must be a non-empty PNG no larger than 16 MiB")
    if not INFERENCE_SLOTS.acquire(blocking=False):
        raise HTTPException(status_code=429, detail="worker is busy; retry shortly")
    path = None
    try:
        with tempfile.NamedTemporaryFile(prefix="la-studio-subtitle-", suffix=".png", delete=False) as handle:
            handle.write(data)
            path = Path(handle.name)
        texts: list[str] = []
        scores: list[float] = []
        for result in engine_for(profile).predict(str(path)):
            current_texts, current_scores = result_fields(result)
            texts.extend(current_texts)
            scores.extend(current_scores)
        text = " ".join(texts).strip()
        confidence = sum(scores) / len(scores) if scores else 0.0
        return {"text": text, "confidence": max(0.0, min(1.0, confidence))}
    except HTTPException:
        raise
    except Exception as error:
        raise HTTPException(status_code=503, detail=f"PP-OCRv5 inference failed: {type(error).__name__}: {str(error)[:300]}") from error
    finally:
        if path is not None:
            path.unlink(missing_ok=True)
        INFERENCE_SLOTS.release()
'''


def lines(source: str) -> list[str]:
    return (dedent(source).strip() + "\n").splitlines(keepends=True)


def build_notebook() -> dict:
    worker_path = "/content/la_studio_subtitle_ocr_worker.py"
    writer = (
        "from pathlib import Path\n\n"
        f"WORKER = Path({worker_path!r})\n"
        f"WORKER.write_text({dedent(WORKER).strip()!r} + '\\n', encoding='utf-8')\n"
        "print('Worker source:', WORKER)\n"
    )
    launch = build_worker_launch(
        capability_label="Subtitle OCR",
        module="la_studio_subtitle_ocr_worker:app",
        port=3955,
        model_id=MODEL_ID,
        token_env="LA_STUDIO_COLAB_SUBTITLE_OCR_TOKEN",
        url_env="LA_STUDIO_COLAB_SUBTITLE_OCR_URL",
        model_env="LA_STUDIO_COLAB_SUBTITLE_OCR_MODEL",
        log_path="/content/la_studio_subtitle_ocr_worker.log",
        # The dedicated package directory is placed before Colab's mutable
        # global site-packages only for the worker process. It never invokes
        # the Colab ensurepip module.
        worker_environment={
            "PYTHONPATH": "/content/la_studio_subtitle_ocr_site",
            "PYTHONNOUSERSITE": "1",
        },
    )
    return {
        "cells": [
            {"cell_type": "markdown", "metadata": {}, "source": lines("""
                # LA Studio Subtitle OCR — PP-OCRv5 Multilingual 3.1

                This direct CUDA notebook is independent of API Gateway. It loads the
                PP-OCRv5 multilingual family (PaddleOCR 3.1.1, Apache-2.0) and accepts
                only cropped PNG subtitle frames, never a source video.

                1. Choose **Runtime → Change runtime type → GPU**.
                2. Run all cells.
                3. In Subtitle OCR choose **Colab GPU**, open Configure / check Colab,
                   and paste only the temporary URL and token printed below.
            """)},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": lines("""
                !nvidia-smi
                # Keep OCR out of Colab's mutable global site-packages. A
                # global install can leave Pillow 12's ImageText.py beside an
                # older PIL._typing.py, which causes the `_Ink` error reported
                # in this notebook. Do not create a venv: recent Colab Python
                # images can have a broken ensurepip bootstrap.
                import os
                import shutil
                import subprocess
                import sys
                from pathlib import Path

                BOOTSTRAP_REVISION = "subtitle-ocr-bootstrap-2026-08-12.11"
                print("LA Studio Subtitle OCR bootstrap:", BOOTSTRAP_REVISION)
                print("This revision uses a dedicated package directory; it never creates a venv or calls ensurepip.")

                OCR_SITE_PACKAGES = Path("/content/la_studio_subtitle_ocr_site")
                # Remove only the old app-owned bootstrap directories. This
                # makes Run all safe after a notebook revision that used a
                # broken ensurepip virtual environment, without touching any
                # user files in /content.
                shutil.rmtree(Path("/content/la_studio_subtitle_ocr_venv"), ignore_errors=True)
                shutil.rmtree(OCR_SITE_PACKAGES, ignore_errors=True)
                OCR_SITE_PACKAGES.mkdir(parents=True, exist_ok=True)
                BOOTSTRAP_ENV = os.environ.copy()
                BOOTSTRAP_ENV.pop("PYTHONPATH", None)
                BOOTSTRAP_ENV["PYTHONNOUSERSITE"] = "1"

                def bootstrap_pip(*arguments):
                    command = [sys.executable, "-m", "pip", *arguments]
                    result = subprocess.run(command, env=BOOTSTRAP_ENV,
                                            text=True, stdout=subprocess.PIPE,
                                            stderr=subprocess.STDOUT)
                    output = result.stdout or ""
                    if output:
                        print(output)
                    if result.returncode:
                        raise RuntimeError(
                            "LA Studio Subtitle OCR dependency bootstrap failed with exit code "
                            + str(result.returncode) + ".\\nCommand: " + " ".join(command)
                            + "\\n\\n---- pip output (last 12,000 characters) ----\\n"
                            + output[-12000:])

                def ocr_pip(*arguments):
                    # --target alone can still treat a globally installed
                    # requirement as satisfied on some Colab images. Force
                    # every package into this directory so the worker cannot
                    # combine a new ImageText.py with an old PIL._typing.py.
                    bootstrap_pip("install", "--target", str(OCR_SITE_PACKAGES),
                                  "--ignore-installed", *arguments)

                OCR_PYTHON = sys.executable
                OCR_ENV = os.environ.copy()
                OCR_ENV["PYTHONPATH"] = str(OCR_SITE_PACKAGES)
                OCR_ENV["PYTHONNOUSERSITE"] = "1"
                # Resolve the complete pinned stack in one pip transaction.
                # Installing Paddle first and then OCR extras in separate
                # --ignore-installed transactions allowed a later resolver to
                # overwrite a GPU/Pillow dependency in the target directory.
                # Paddle's CUDA index is retained as an extra source while
                # PyPI remains available for the rest of the fixed stack.
                ocr_pip("--no-cache-dir", "--upgrade", "--force-reinstall",
                        "--only-binary=:all:",
                        # PaddleX's OCR extras depend on the source-only
                        # GPUtil distribution. Keep every other dependency
                        # wheel-only, but permit this one small pure-Python
                        # package to build rather than making pip fail before
                        # the worker starts.
                        "--no-binary=GPUtil",
                        "paddlepaddle-gpu==3.1.0",
                        "paddleocr==3.1.1", "paddlex[ie,multimodal,ocr,trans]==3.1.0",
                        "Pillow==12.0.0", "fastapi==0.115.12", "uvicorn==0.34.3",
                        "python-multipart==0.0.20",
                        "--extra-index-url", "https://www.paddlepaddle.org.cn/packages/stable/cu118/")
            """)},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": lines("""
                # Fail in this explicit dependency probe rather than after the
                # service has started. This uses the same interpreter plus
                # package-directory environment that will launch Uvicorn.
                from textwrap import dedent

                probe = dedent(r'''
                from importlib.metadata import version
                import os
                import sys
                from pathlib import Path
                import PIL
                from PIL import ImageText
                from PIL._typing import _Ink
                import paddle
                import paddleocr
                import paddlex
                from paddleocr import PaddleOCR

                assert version("paddlepaddle-gpu") == "3.1.0", version("paddlepaddle-gpu")
                assert version("paddlex") == "3.1.0", version("paddlex")
                assert version("paddleocr") == "3.1.1", version("paddleocr")
                assert version("pillow") == "12.0.0", version("pillow")
                dedicated_site = str(Path(os.environ["LA_STUDIO_OCR_SITE"]).resolve())
                for package in (PIL, paddle, paddleocr, paddlex):
                    assert str(Path(package.__file__).resolve()).startswith(dedicated_site), (package.__name__, package.__file__)
                assert paddle.device.is_compiled_with_cuda(), "Choose a Colab GPU runtime; CPU fallback is disabled."
                paddle.device.set_device("gpu:0")
                print("Verified isolated OCR stack:", paddle.__version__, version("paddlex"), version("paddleocr"), version("pillow"))
                '''
                )
                OCR_ENV["LA_STUDIO_OCR_SITE"] = str(OCR_SITE_PACKAGES)
                subprocess.run([OCR_PYTHON, "-c", probe], check=True, env=OCR_ENV)
            """)},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": lines(writer)},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [],
             "source": lines(f"MODEL_ID = {MODEL_ID!r}\n" + launch)},
        ],
        "metadata": {
            "accelerator": "GPU",
            "colab": {"gpuType": "T4", "provenance": []},
            "kernelspec": {"display_name": "Python 3", "name": "python3"},
            "language_info": {"name": "python"},
            "la_studio": {
                "capability": "subtitle-ocr",
                "family_id": MODEL_ID,
                "upstream_model": "PaddlePaddle/PaddleOCR PP-OCRv5",
                "upstream_version": "PaddleOCR 3.1.1",
                "license": "Apache-2.0",
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
    target = NOTEBOOKS / NOTEBOOK
    target.write_text(json.dumps(build_notebook(), indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(target.relative_to(ROOT))


if __name__ == "__main__":
    main()
