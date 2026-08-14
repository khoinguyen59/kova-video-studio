#!/usr/bin/env python3
"""Fail when checked-in exact-model Colab notebooks drift from their generators.

The generators remain the source of truth. This verifier imports each one,
redirects its output to a temporary directory below out/, and compares every
generated notebook byte-for-byte with the tracked notebook. It never writes to
the source notebooks.
"""

from __future__ import annotations

import ast
import importlib.util
import io
import json
import re
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[1]
NOTEBOOKS = ROOT / "notebooks"
LIVE_ACCEPTANCE_TEMPLATE = ROOT / "docs" / "LIVE_COLAB_ACCEPTANCE_TEMPLATE.json"
GENERATORS = (
    "generate_stt_colab_notebooks.py",
    "generate_tts_colab_notebooks.py",
    "generate_voice_colab_notebooks.py",
    "generate_alignment_separation_colab_notebooks.py",
    "generate_spleeter_safe_colab_notebook.py",
    "generate_language_colab_notebooks.py",
    "generate_subtitle_ocr_colab_notebook.py",
)
EXPECTED_EXACT_NOTEBOOKS = 32


def load_generator(path: Path, index: int) -> ModuleType:
    spec = importlib.util.spec_from_file_location(f"lastudio_colab_generator_{index}", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Could not import generator: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not hasattr(module, "NOTEBOOKS") or not hasattr(module, "main"):
        raise RuntimeError(f"Generator does not expose NOTEBOOKS and main(): {path}")
    return module


def main() -> int:
    if not NOTEBOOKS.is_dir():
        raise RuntimeError(f"Notebook directory is missing: {NOTEBOOKS}")

    generated_names: set[str] = set()
    generated_workers: set[tuple[str, str, str]] = set()
    mismatches: list[str] = []
    out_root = ROOT / "out"
    out_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="verify-colab-notebooks-", dir=out_root) as temporary_root:
        temporary_notebooks = Path(temporary_root) / "notebooks"
        for index, filename in enumerate(GENERATORS):
            generator = load_generator(ROOT / "scripts" / filename, index)
            generator.NOTEBOOKS = temporary_notebooks
            # Generators normally list every written notebook. Keep this CI
            # verifier concise while still surfacing its own actionable error.
            with redirect_stdout(io.StringIO()):
                generator.main()

        for generated in sorted(temporary_notebooks.glob("*.ipynb")):
            generated_names.add(generated.name)
            tracked = NOTEBOOKS / generated.name
            if not tracked.is_file():
                mismatches.append(f"missing tracked notebook: {generated.name}")
                continue
            if generated.read_bytes() != tracked.read_bytes():
                mismatches.append(f"notebook is stale; regenerate it: {generated.name}")
            try:
                document = json.loads(generated.read_text(encoding="utf-8"))
                metadata = document.get("metadata", {}).get("la_studio", {})
                capability = str(metadata.get("capability", "")).strip().lower()
                model = str(metadata.get("family_id", "")).strip().lower()
                if not capability or not model:
                    mismatches.append(f"notebook metadata has no exact worker identity: {generated.name}")
                else:
                    generated_workers.add((capability, model, generated.name))
                worker_source = "".join(
                    "".join(cell.get("source", []))
                    for cell in document.get("cells", [])
                    if cell.get("cell_type") == "code"
                )
                executable_source = "".join(
                    line for line in worker_source.splitlines(keepends=True)
                    if not line.lstrip().startswith(("%", "!"))
                )
                try:
                    compile(executable_source, generated.name, "exec")
                except SyntaxError as error:
                    mismatches.append(
                        f"Exact worker notebook Python syntax is invalid: {generated.name}: {error}"
                    )
                else:
                    syntax_tree = ast.parse(executable_source, generated.name)
                    synchronous_functions = {
                        node.name for node in ast.walk(syntax_tree)
                        if isinstance(node, ast.FunctionDef)
                    }
                    for node in ast.walk(syntax_tree):
                        if not isinstance(node, ast.Await) or not isinstance(node.value, ast.Call):
                            continue
                        function = node.value.func
                        if isinstance(function, ast.Name) and function.id in synchronous_functions:
                            mismatches.append(
                                f"Exact worker awaits synchronous function '{function.id}': "
                                f"{generated.name}:{node.lineno}"
                            )
                if capability == "stt":
                    if "await prune_finished_jobs()" in worker_source:
                        mismatches.append(
                            f"STT worker awaits synchronous prune_finished_jobs: {generated.name}"
                        )
                    if "def prune_finished_jobs()" not in worker_source:
                        mismatches.append(f"STT worker has no finished-job pruning function: {generated.name}")
                    if "def ensure_cloudflared()" not in worker_source \
                            or "Reusing the existing local LA Studio STT worker" not in worker_source:
                        mismatches.append(
                            f"STT worker is not safe to rerun in an existing Colab runtime: {generated.name}"
                        )
                    if "WORKER_REVISION" not in worker_source \
                            or worker_source.count('"worker_revision": WORKER_REVISION') < 2:
                        mismatches.append(
                            f"STT worker does not expose its revision from health and capabilities: {generated.name}"
                        )
                    if "Check Colab action is the" not in worker_source \
                            or "Cloudflare tunnel URL created. Verify it with Check Colab in LA Studio." not in worker_source:
                        mismatches.append(
                            f"STT worker does not delegate public tunnel verification to the desktop check: {generated.name}"
                        )
                    if '"wget", "-q", "-O"' in worker_source:
                        mismatches.append(
                            f"STT worker still overwrites cloudflared on every rerun: {generated.name}"
                        )
                elif model == "sherpa-onnx-spleeter-2stems-fp16":
                    if not re.search(r'WORKER_COMMIT = "[0-9a-f]{40}"', worker_source) \
                            or 'WORKER_COMMIT = "main"' in worker_source \
                            or "la_studio_separation_launcher.py" not in worker_source:
                        mismatches.append(
                            f"Spleeter notebook has no immutable, checked worker launcher: {generated.name}"
                        )
                    launcher = ROOT / "notebooks" / "workers" / "LA_STUDIO_SEPARATION_SPLEETER_2STEMS_LAUNCHER.py"
                    try:
                        launcher_source = launcher.read_text(encoding="utf-8")
                    except OSError as error:
                        mismatches.append(f"Spleeter launcher cannot be read: {error}")
                    else:
                        if "def cloudflared_ready() -> bool:" not in launcher_source \
                                or "except OSError:" not in launcher_source \
                                or "or not cloudflared_ready()" not in launcher_source:
                            mismatches.append(
                                f"Spleeter launcher is not safe when cloudflared is absent: {generated.name}"
                            )
                else:
                    if "LA Studio worker launch contract:" not in worker_source \
                            or "def port_is_occupied" not in worker_source \
                            or "Click Check Colab in the matching LA Studio feature before running it." not in worker_source:
                        mismatches.append(
                            f"Exact worker does not use the hardened shared launch contract: {generated.name}"
                        )
                    if "/content/cloudflared.deb" in worker_source:
                        mismatches.append(
                            f"Exact worker still uses the unsafe legacy cloudflared installer: {generated.name}"
                        )
                if capability == "translation":
                    if 'RESPONSE_CONTRACT = "translation-patches-v3"' not in worker_source \
                            or '"response_contract": RESPONSE_CONTRACT' not in worker_source \
                            or "make_translation_patches" not in worker_source \
                            or "retry_empty_translations" not in worker_source \
                            or "later segments continued" not in worker_source \
                            or "NONLEXICAL_UTTERANCES" not in worker_source:
                        mismatches.append(
                            f"Translation worker lacks the retry-and-continue patch contract: {generated.name}"
                        )
                if capability == "subtitle-ocr":
                    if "import torch" in worker_source or "torch.cuda" in worker_source:
                        mismatches.append(
                            f"Subtitle OCR worker imports Torch and can conflict with Paddle NCCL: {generated.name}"
                        )
                    required_stack_markers = (
                        'paddlepaddle_gpu-3.1.0-{python_tag}-{python_tag}-linux_x86_64.whl',
                        '"paddleocr==3.1.1"',
                        '"paddlex[ocr]==3.1.0"',
                        '"PyYAML==6.0.2"',
                        '"typing-extensions==4.15.0"',
                        '"Pillow==12.0.0"',
                        'OCR_SITE_PACKAGES = Path("/content/la_studio_subtitle_ocr_site")',
                        'BOOTSTRAP_REVISION = "subtitle-ocr-bootstrap-2026-08-12.13"',
                        'shutil.rmtree(Path("/content/la_studio_subtitle_ocr_venv"), ignore_errors=True)',
                        'shutil.rmtree(OCR_SITE_PACKAGES, ignore_errors=True)',
                        'bootstrap_pip("install", "--target", str(OCR_SITE_PACKAGES),',
                        '"--ignore-installed", "--disable-pip-version-check",',
                        '"--retries", "4", "--timeout", "120", *arguments)',
                        'LA Studio Subtitle OCR dependency bootstrap failed with exit code',
                        '---- pip output (last 12,000 characters) ----',
                        'OCR_PYTHON = sys.executable',
                        'OCR_ENV["PYTHONPATH"] = str(OCR_SITE_PACKAGES)',
                        'OCR_ENV["PYTHONNOUSERSITE"] = "1"',
                        '"PADDLE_PDX_MODEL_SOURCE": "BOS"',
                        'probe = dedent(r"""',
                        'probe_result = subprocess.run(',
                        '---- OCR probe output (last 12,000 characters) ----',
                        'for package in (PIL, paddle, paddleocr, paddlex):',
                        'assert str(Path(package.__file__).resolve()).startswith(dedicated_site)',
                        'assert version("paddlex") == "3.1.0"',
                        'assert version("pillow") == "12.0.0"',
                        'from PIL import Image, ImageText',
                        'from PIL._typing import _Ink',
                        'from paddleocr import PaddleOCR',
                        'PADDLE_GPU_WHEEL = (',
                        'PADDLE_GPU_WHEEL)',
                        'Verified isolated PP-OCRv5 CUDA inference:',
                    )
                    if any(marker not in worker_source for marker in required_stack_markers):
                        mismatches.append(
                            f"Subtitle OCR notebook does not pin and probe the Paddle-only 3.1.0 stack: {generated.name}"
                        )
                    if 'paddlex[ie,multimodal,ocr,trans]' in worker_source:
                        mismatches.append(
                            f"Subtitle OCR notebook still installs unrelated PaddleX LLM/document extras: {generated.name}"
                        )
                    if '"--extra-index-url", "https://www.paddlepaddle.org.cn/packages/stable/cu118/"' in worker_source:
                        mismatches.append(
                            f"Subtitle OCR notebook still resolves Paddle through the timeout-prone package index: {generated.name}"
                        )
                    if ('venv.EnvBuilder' in worker_source
                            or "'ensurepip'" in worker_source
                            or '"ensurepip"' in worker_source):
                        mismatches.append(
                            f"Subtitle OCR notebook still contains a broken Colab venv/ensurepip bootstrap: {generated.name}"
                        )
                    if '"-m", "virtualenv"' in worker_source or 'virtualenv==20.31.2' in worker_source:
                        mismatches.append(
                            f"Subtitle OCR notebook still depends on a virtualenv bootstrap: {generated.name}"
                        )
                    if worker_source.count('ocr_pip("--no-cache-dir"') != 3 \
                            or 'ocr_pip("install"' in worker_source \
                            or '"--no-deps", "paddleocr==3.1.1"' not in worker_source \
                            or '"--no-deps", PADDLE_GPU_WHEEL' in worker_source:
                        mismatches.append(
                            f"Subtitle OCR notebook must isolate the base runtime from PaddleOCR's broad dependency extras: {generated.name}"
                        )
                    if ('@app.on_event("startup")' not in worker_source
                            or 'PP-OCRv5 CUDA startup inference passed for profile en' not in worker_source
                            or 'Image.new("RGB", (640, 160), "white")' not in worker_source):
                        mismatches.append(
                            f"Subtitle OCR worker must complete real CUDA startup inference before reporting ready: {generated.name}"
                        )
                    if "paddle.device.set_device(\"gpu:0\")" not in worker_source \
                            or "paddle.device.cuda.get_device_name(0)" not in worker_source:
                        mismatches.append(
                            f"Subtitle OCR worker does not prove its Paddle CUDA device before serving: {generated.name}"
                        )
                if model == "vibevoice" and 'SUPPORTED_LANGUAGES = ["en"]' not in worker_source:
                    mismatches.append(
                        f"VibeVoice Realtime must advertise its upstream English-only capability: {generated.name}"
                    )
            except (OSError, json.JSONDecodeError) as error:
                mismatches.append(f"notebook metadata cannot be read: {generated.name}: {error}")

    if len(generated_names) != EXPECTED_EXACT_NOTEBOOKS:
        mismatches.append(
            f"expected {EXPECTED_EXACT_NOTEBOOKS} exact-model notebooks from generators, got {len(generated_names)}"
        )
    if not LIVE_ACCEPTANCE_TEMPLATE.is_file():
        mismatches.append(f"live acceptance template is missing: {LIVE_ACCEPTANCE_TEMPLATE.name}")
    else:
        try:
            template = json.loads(LIVE_ACCEPTANCE_TEMPLATE.read_text(encoding="utf-8"))
            workers = template.get("workers")
            if not isinstance(workers, list):
                raise ValueError("top-level 'workers' must be an array")
            template_workers: set[tuple[str, str, str]] = set()
            environment_names: set[str] = set()
            for index, worker in enumerate(workers):
                if not isinstance(worker, dict):
                    mismatches.append(f"live acceptance worker {index} is not an object")
                    continue
                capability = str(worker.get("capability", "")).strip().lower()
                model = str(worker.get("model", "")).strip().lower()
                notebook = str(worker.get("notebook", "")).strip()
                url_env = str(worker.get("url_env", "")).strip()
                token_env = str(worker.get("token_env", "")).strip()
                if not all((capability, model, notebook, url_env, token_env)):
                    mismatches.append(f"live acceptance worker {index} has an incomplete exact-worker identity")
                    continue
                template_workers.add((capability, model, notebook))
                for environment_name in (url_env, token_env):
                    if environment_name in environment_names:
                        mismatches.append(f"live acceptance environment variable is reused: {environment_name}")
                    environment_names.add(environment_name)
            if template_workers != generated_workers:
                missing = sorted(generated_workers - template_workers)
                extra = sorted(template_workers - generated_workers)
                if missing:
                    mismatches.append(f"live acceptance template is missing exact workers: {missing}")
                if extra:
                    mismatches.append(f"live acceptance template has unknown exact workers: {extra}")
        except (OSError, ValueError, json.JSONDecodeError) as error:
            mismatches.append(f"live acceptance template cannot be read: {error}")
    if mismatches:
        raise RuntimeError("\n".join(mismatches))

    print(f"Generated exact-model Colab notebooks verified: {len(generated_names)}/{EXPECTED_EXACT_NOTEBOOKS}.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # Keep CI failure concise and actionable.
        print(f"Generated Colab notebook verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)
