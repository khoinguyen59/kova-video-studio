#!/usr/bin/env python3
"""Generate the hardened, pinned Spleeter Direct-Colab notebook.

The Spleeter CUDA worker is intentionally fetched from one immutable Git
revision and checked by SHA-256.  Keeping that small notebook in a generator
prevents the checked-in notebook from silently drifting away from its lock.
"""

from __future__ import annotations

import json
from pathlib import Path
from textwrap import dedent


ROOT = Path(__file__).resolve().parents[1]
NOTEBOOKS = ROOT / "notebooks"
NOTEBOOK = "LA_STUDIO_SEPARATION_SPLEETER_2STEMS_GPU.ipynb"
MODEL_ID = "sherpa-onnx-spleeter-2stems-fp16"
UPSTREAM_MODEL = "k2-fsa/sherpa-onnx-spleeter-2stems-fp16"
ARTIFACT_URL = (
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/"
    "source-separation-models/sherpa-onnx-spleeter-2stems-fp16.tar.bz2"
)

# This lock is updated only after the referenced worker templates have been
# committed.  The notebook must never download a moving branch such as main.
WORKER_COMMIT = "0982aadd001d91aae8c1c829bcfef7f85d5c6704"
WORKERS = {
    "la_studio_separation_worker.py": (
        "notebooks/workers/LA_STUDIO_SEPARATION_SPLEETER_2STEMS_WORKER.py",
        "307861926e13ff9849b04594074b573b1da063b1791c56dc2f502ab64991c5af",
    ),
    "la_studio_separation_launcher.py": (
        "notebooks/workers/LA_STUDIO_SEPARATION_SPLEETER_2STEMS_LAUNCHER.py",
        "3a1fb3ecde9d6ddf299839291d24515a23934d29f6859bb86196529d722a16d1",
    ),
}


def source_lines(source: str) -> list[str]:
    return (dedent(source).strip() + "\n").splitlines(keepends=True)


def make_notebook() -> dict:
    worker_rows = "\n".join(
        f'    "{destination}": (\n'
        f'        "{relative_path}",\n'
        f'        "{checksum}"),'
        for destination, (relative_path, checksum) in WORKERS.items()
    )
    worker_download = "\n".join((
        "from hashlib import sha256",
        "from pathlib import Path",
        "from urllib.request import urlopen",
        "",
        f'MODEL_ID = "{MODEL_ID}"',
        f'WORKER_COMMIT = "{WORKER_COMMIT}"  # audited exact worker revision',
        "WORKERS = {",
        worker_rows,
        "}",
        "for destination, (relative_path, expected_sha256) in WORKERS.items():",
        '    url = f"https://raw.githubusercontent.com/khoinguyen59/kova-video-studio/{WORKER_COMMIT}/{relative_path}"',
        "    payload = urlopen(url, timeout=60).read()",
        "    actual_sha256 = sha256(payload).hexdigest()",
        "    if actual_sha256 != expected_sha256:",
        '        raise RuntimeError(f"Worker integrity check failed for {relative_path}: {actual_sha256}")',
        "    Path('/content', destination).write_bytes(payload)",
        "print('Downloaded verified exact-model CUDA worker templates.')",
    ))
    return {
        "cells": [
            {
                "cell_type": "markdown",
                "metadata": {},
                "source": source_lines(
                    f'''
                    # LA Studio voice-isolation - Spleeter 2-stem FP16

                    This notebook runs exactly `{MODEL_ID}` from the declared k2-fsa artifact on the temporary **Colab GPU worker**. It never uses API Gateway or a local LA Studio model.

                    The worker performs a CUDA startup probe before it prints a URL. It also sends long audio as bounded, overlapping segments, so the Spleeter FP16 CUDA convolution plan remains within the verified shape.

                    1. Choose **Runtime -> Change runtime type -> GPU**.
                    2. Run all cells. The final cell must print `startup probe: passed`.
                    3. Copy the printed URL and token to Dubbing -> Colab setup, then press **Check Colab**.
                    '''
                ),
            },
            {
                "cell_type": "code",
                "execution_count": None,
                "metadata": {},
                "outputs": [],
                "source": source_lines(
                    f'''
                    !nvidia-smi
                    %pip install -q --upgrade --no-cache-dir "onnxruntime-gpu==1.21.0" "kaldi-native-fbank" "soundfile==0.13.1" "fastapi==0.115.12" "uvicorn==0.34.3" "python-multipart==0.0.20"

                    import torch
                    if not torch.cuda.is_available():
                        raise RuntimeError('No Colab CUDA GPU is available. Select Runtime > Change runtime type > GPU, then restart and Run all.')
                    print('Colab CUDA:', torch.cuda.get_device_name(0))

                    !wget -q --show-progress -O /content/spleeter.tar.bz2 {ARTIFACT_URL}
                    !tar -xjf /content/spleeter.tar.bz2 -C /content
                    '''
                ),
            },
            {
                "cell_type": "code",
                "execution_count": None,
                "metadata": {},
                "outputs": [],
                "source": source_lines(worker_download),
            },
            {
                "cell_type": "code",
                "execution_count": None,
                "metadata": {},
                "outputs": [],
                "source": source_lines("!python /content/la_studio_separation_launcher.py"),
            },
        ],
        "metadata": {
            "accelerator": "GPU",
            "colab": {"gpuType": "T4", "provenance": []},
            "kernelspec": {"display_name": "Python 3", "name": "python3"},
            "language_info": {"name": "python"},
            "la_studio": {
                "capability": "voice-isolation",
                "family_id": MODEL_ID,
                "upstream_model": UPSTREAM_MODEL,
                "contract_version": 1,
                "device": "cuda",
                "cpu_fallback": False,
                "worker_contract": "spleeter-cuda-safe-20260816.1",
                "worker_templates": [
                    "workers/LA_STUDIO_SEPARATION_SPLEETER_2STEMS_WORKER.py",
                    "workers/LA_STUDIO_SEPARATION_SPLEETER_2STEMS_LAUNCHER.py",
                ],
                "artifact_url": ARTIFACT_URL,
            },
        },
        "nbformat": 4,
        "nbformat_minor": 5,
    }


def main() -> None:
    NOTEBOOKS.mkdir(parents=True, exist_ok=True)
    target = NOTEBOOKS / NOTEBOOK
    target.write_text(json.dumps(make_notebook(), indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(target.relative_to(ROOT))


if __name__ == "__main__":
    main()
