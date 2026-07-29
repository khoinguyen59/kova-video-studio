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
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[1]
NOTEBOOKS = ROOT / "notebooks"
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

    if len(generated_names) != EXPECTED_EXACT_NOTEBOOKS:
        mismatches.append(
            f"expected {EXPECTED_EXACT_NOTEBOOKS} exact-model notebooks from generators, got {len(generated_names)}"
        )
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
