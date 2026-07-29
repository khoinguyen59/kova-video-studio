#!/usr/bin/env python3
"""Fail when checked-in exact-model Colab notebooks drift from their generators.

The generators remain the source of truth. This verifier imports each one,
redirects its output to a temporary directory below out/, and compares every
generated notebook byte-for-byte with the tracked notebook. It never writes to
the source notebooks.
"""

from __future__ import annotations

import importlib.util
import io
import json
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
    "generate_language_colab_notebooks.py",
)
EXPECTED_EXACT_NOTEBOOKS = 31


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
                metadata = json.loads(generated.read_text(encoding="utf-8")) \
                    .get("metadata", {}).get("la_studio", {})
                capability = str(metadata.get("capability", "")).strip().lower()
                model = str(metadata.get("family_id", "")).strip().lower()
                if not capability or not model:
                    mismatches.append(f"notebook metadata has no exact worker identity: {generated.name}")
                else:
                    generated_workers.add((capability, model, generated.name))
                if capability == "stt":
                    worker_source = "".join(
                        "".join(cell.get("source", []))
                        for cell in json.loads(generated.read_text(encoding="utf-8")).get("cells", [])
                        if cell.get("cell_type") == "code"
                    )
                    if "await prune_finished_jobs()" in worker_source:
                        mismatches.append(
                            f"STT worker awaits synchronous prune_finished_jobs: {generated.name}"
                        )
                    if "def prune_finished_jobs()" not in worker_source:
                        mismatches.append(f"STT worker has no finished-job pruning function: {generated.name}")
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
