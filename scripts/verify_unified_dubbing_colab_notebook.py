#!/usr/bin/env python3
"""Static contract checks for the optional Unified Dubbing Colab notebook."""

from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "scripts" / "generate_unified_dubbing_colab_notebook.py"
NOTEBOOK = ROOT / "notebooks" / "LA_STUDIO_UNIFIED_DUBBING_GPU.ipynb"
COORDINATOR = ROOT / "notebooks" / "workers" / "LA_STUDIO_UNIFIED_DUBBING_COORDINATOR.py"


def load_generator():
    spec = importlib.util.spec_from_file_location("unified_generator", GENERATOR)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_coordinator():
    spec = importlib.util.spec_from_file_location("unified_coordinator", COORDINATOR)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    # dataclasses resolves postponed annotations through sys.modules while the
    # module body is being evaluated.
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def main() -> None:
    module = load_generator()
    expected = json.dumps(module.make_notebook(), ensure_ascii=False, indent=2) + "\n"
    actual = NOTEBOOK.read_text(encoding="utf-8")
    assert actual == expected, "Generated notebook drift: run generate_unified_dubbing_colab_notebook.py"
    notebook = json.loads(actual)
    metadata = notebook["metadata"]["la_studio"]
    assert metadata["role"] == "unified-dubbing-coordinator"
    sources = "\n".join("".join(cell.get("source", [])) for cell in notebook["cells"])
    for required in (
        "UNIFIED_WORKERS", "LA_STUDIO_UNIFIED_DUBBING_URL=",
        "/v1/unified/{capability}/{model}/{route:path}", "httpx.AsyncClient",
        "wait_for_exact_health", "Cloudflare tunnel", "LA_STUDIO_UNIFIED_DUBBING_TOKEN",
    ):
        assert required in sources, f"Missing unified contract: {required}"
    coordinator = load_coordinator()
    defaults = json.loads(next(
        source for source in ("".join(cell.get("source", [])) for cell in notebook["cells"])
        if "UNIFIED_WORKERS" in source
    ).split("UNIFIED_WORKERS = ", 1)[1].split("\n                CONFIG_PATH", 1)[0])
    for selection in defaults:
        capability = selection["capability"]
        model = selection["model"]
        exact_notebook = coordinator.discover_exact_notebook(ROOT, capability, model)
        worker_source = coordinator.worker_source_for(
            coordinator.WorkerSpec(capability, model, exact_notebook), ROOT
        )
        compile(worker_source, str(exact_notebook), "exec")
    print("Unified Dubbing notebook contract: PASS")


if __name__ == "__main__":
    main()
