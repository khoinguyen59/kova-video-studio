#!/usr/bin/env python3
"""Generate the one-tunnel optional Unified Dubbing Colab coordinator notebook."""

from __future__ import annotations

import json
from pathlib import Path
from textwrap import dedent


ROOT = Path(__file__).resolve().parents[1]
NOTEBOOK = ROOT / "notebooks" / "LA_STUDIO_UNIFIED_DUBBING_GPU.ipynb"
COORDINATOR = ROOT / "notebooks" / "workers" / "LA_STUDIO_UNIFIED_DUBBING_COORDINATOR.py"
SOURCE_REPOSITORY = "https://github.com/khoinguyen59/kova-video-studio.git"
# The coordinator only needs the already-generated exact workers.  Keep their
# source checkout immutable so regenerating this notebook never creates drift
# simply because a later LA Studio commit exists.
SOURCE_COMMIT = "96a2fe9"


def source_lines(source: str) -> list[str]:
    return (dedent(source).strip() + "\n").splitlines(keepends=True)


def make_notebook() -> dict:
    commit = SOURCE_COMMIT
    coordinator = COORDINATOR.read_text(encoding="utf-8")
    default_workers = [
        {"capability": "voice-isolation", "model": "sherpa-onnx-spleeter-2stems-fp16"},
        {"capability": "stt", "model": "whisper.cpp"},
        {"capability": "subtitle-ocr", "model": "pp-ocrv5-multilingual-3.1"},
        {"capability": "translation", "model": "m2m100-418m"},
        {"capability": "tts", "model": "kokoro"},
        {"capability": "forced-alignment", "model": "mms-forced-aligner-onnx"},
    ]
    return {
        "cells": [
            {"cell_type": "markdown", "metadata": {}, "source": source_lines("""
                # LA Studio Unified Dubbing Coordinator (optional)

                This is a real one-URL, one-token coordinator for the selected Dubbing models. It starts each selected **exact CUDA worker** privately, verifies its ordinary `/health`, then exposes it through one Cloudflare tunnel. It never substitutes local CPU or API Gateway routes.

                Keep the normal per-model notebooks if you prefer them. This notebook is optional and does not replace those routes.
            """)},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": source_lines(f"""
                import subprocess
                from pathlib import Path

                SOURCE_REPOSITORY = {SOURCE_REPOSITORY!r}
                SOURCE_COMMIT = {commit!r}
                SOURCE_ROOT = Path('/content/la-studio-unified-source')
                subprocess.run(['rm', '-rf', str(SOURCE_ROOT)], check=True)
                subprocess.run(['git', 'clone', '--no-checkout', SOURCE_REPOSITORY, str(SOURCE_ROOT)], check=True)
                subprocess.run(['git', '-C', str(SOURCE_ROOT), 'checkout', '--detach', SOURCE_COMMIT], check=True)
                subprocess.run(['python3', '-m', 'pip', 'install', '--quiet', '--upgrade', 'fastapi==0.115.12', 'uvicorn==0.34.3', 'httpx==0.28.1'], check=True)
                print('Pinned LA Studio source:', SOURCE_COMMIT)
            """)},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": source_lines(f"""
                import json
                from pathlib import Path

                # Keep only the exact models you intend to select in LA Studio.
                # Add another supported capability/model pair here before starting the notebook.
                UNIFIED_WORKERS = {json.dumps(default_workers, indent=4)}
                CONFIG_PATH = Path('/content/la_studio_unified_workers.json')
                CONFIG_PATH.write_text(json.dumps(UNIFIED_WORKERS, indent=2), encoding='utf-8')
                print('Will prewarm:', ', '.join(f"{{row['capability']}}/{{row['model']}}" for row in UNIFIED_WORKERS))
            """)},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": source_lines(f"""
                from pathlib import Path
                COORDINATOR_PATH = Path('/content/la_studio_unified_dubbing_coordinator.py')
                COORDINATOR_PATH.write_text({coordinator!r}, encoding='utf-8')
                print('Wrote coordinator:', COORDINATOR_PATH)
            """)},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [], "source": source_lines("""
                # This cell remains running while LA Studio uses the unified worker.
                # It prints one URL and one token only after every selected exact CUDA worker is healthy.
                !python3 /content/la_studio_unified_dubbing_coordinator.py \\
                    --source-root /content/la-studio-unified-source \\
                    --config /content/la_studio_unified_workers.json
            """)},
        ],
        "metadata": {
            "kernelspec": {"display_name": "Python 3", "language": "python", "name": "python3"},
            "language_info": {"name": "python", "version": "3.x"},
            "la_studio": {
                "role": "unified-dubbing-coordinator",
                "contract_version": 1,
                "route_template": "/v1/unified/<capability>/<model>/<worker-route>",
                "source_commit": commit,
            },
        },
        "nbformat": 4,
        "nbformat_minor": 5,
    }


def main() -> None:
    NOTEBOOK.write_text(json.dumps(make_notebook(), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(NOTEBOOK)


if __name__ == "__main__":
    main()
