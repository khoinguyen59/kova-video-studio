#!/usr/bin/env python3
"""One-tunnel coordinator for real LA Studio Dubbing Colab workers.

The coordinator deliberately does not implement inference itself.  It starts
the selected *exact* notebook workers on private loopback ports, waits for each
worker's real CUDA /health response, and exposes them through one authenticated
Cloudflare URL:

    /v1/unified/<capability>/<model>/<the normal worker route>

This keeps the direct per-model notebooks valid while allowing the optional
Unified Dubbing setup in the desktop app to use one URL and token.  A worker
that fails to install, load CUDA, or pass its normal health check prevents the
coordinator from becoming ready; it is never reported as a successful fake
route.
"""

from __future__ import annotations

import argparse
import ast
import asyncio
import json
import os
import re
import secrets
import shutil
import socket
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import httpx
import uvicorn
from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse, StreamingResponse


COORDINATOR_REVISION = "unified-dubbing-coordinator-2026-08-22.1"
LISTEN_HOST = "127.0.0.1"
LISTEN_PORT = 3960
TOKEN_ENVIRONMENTS = {
    "stt": "LA_STUDIO_COLAB_STT_TOKEN",
    "subtitle-ocr": "LA_STUDIO_COLAB_SUBTITLE_OCR_TOKEN",
    "translation": "LA_STUDIO_COLAB_TRANSLATION_TOKEN",
    "tts": "LA_STUDIO_COLAB_TTS_TOKEN",
    "voice-isolation": "LA_STUDIO_COLAB_SEPARATION_TOKEN",
    "forced-alignment": "LA_STUDIO_COLAB_ALIGNMENT_TOKEN",
    "llm": "LA_STUDIO_COLAB_LLM_TOKEN",
}
HOP_BY_HOP_HEADERS = {
    "connection", "keep-alive", "proxy-authenticate", "proxy-authorization",
    "te", "trailers", "transfer-encoding", "upgrade", "host",
}
SAFE_SLUG = re.compile(r"^[a-z0-9][a-z0-9._-]{0,127}$")


@dataclass(frozen=True)
class WorkerSpec:
    capability: str
    model: str
    notebook: Path


@dataclass
class RunningWorker:
    spec: WorkerSpec
    port: int
    process: subprocess.Popen[str]
    log_path: Path

    @property
    def base_url(self) -> str:
        return f"http://{LISTEN_HOST}:{self.port}"


def require_slug(value: str, label: str) -> str:
    if not isinstance(value, str) or not SAFE_SLUG.fullmatch(value):
        raise ValueError(f"{label} must be a lowercase model/capability slug")
    return value


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as candidate:
        candidate.bind((LISTEN_HOST, 0))
        return int(candidate.getsockname()[1])


def read_notebook(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def notebook_source(cell: dict[str, Any]) -> str:
    source = cell.get("source", "")
    return "".join(source) if isinstance(source, list) else str(source)


def metadata_for_notebook(path: Path) -> tuple[str, str] | None:
    metadata = read_notebook(path).get("metadata", {}).get("la_studio", {})
    capability = metadata.get("capability")
    model = metadata.get("family_id") or metadata.get("model_id")
    if not isinstance(capability, str) or not isinstance(model, str):
        return None
    return capability, model


def discover_exact_notebook(source_root: Path, capability: str, model: str) -> Path:
    for notebook in sorted((source_root / "notebooks").glob("*.ipynb")):
        metadata = metadata_for_notebook(notebook)
        if metadata == (capability, model):
            return notebook
    raise RuntimeError(
        f"No exact generated notebook exists for {capability}/{model}. "
        "Choose an exact model listed by the Dubbing app, then regenerate this notebook."
    )


def literal_worker_source(document: dict[str, Any]) -> str | None:
    """Return a static Path(...).write_text(<worker>) payload from a notebook."""
    def static_string(node: ast.AST) -> str | None:
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            return node.value
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
            left = static_string(node.left)
            right = static_string(node.right)
            return left + right if left is not None and right is not None else None
        return None

    for cell in document.get("cells", []):
        if cell.get("cell_type") != "code":
            continue
        try:
            tree = ast.parse(notebook_source(cell))
        except SyntaxError:
            continue
        for statement in ast.walk(tree):
            if not isinstance(statement, ast.Call) or not isinstance(statement.func, ast.Attribute):
                continue
            if statement.func.attr != "write_text" or not statement.args:
                continue
            value = static_string(statement.args[0])
            if value is not None and "FastAPI" in value:
                return value
    return None


def stt_worker_source(document: dict[str, Any]) -> str | None:
    """The STT generator declares its app directly instead of write_text()."""
    for cell in document.get("cells", []):
        source = notebook_source(cell)
        if "app = FastAPI" in source and "@app.get(\"/health\")" in source:
            return source
    return None


def worker_source_for(spec: WorkerSpec, source_root: Path) -> str:
    if spec.capability == "voice-isolation" and spec.model == "sherpa-onnx-spleeter-2stems-fp16":
        return (source_root / "notebooks" / "workers" /
                "LA_STUDIO_SEPARATION_SPLEETER_2STEMS_WORKER.py").read_text(encoding="utf-8")
    document = read_notebook(spec.notebook)
    source = literal_worker_source(document)
    if source is None and spec.capability == "stt":
        source = stt_worker_source(document)
        if source:
            source = re.sub(
                r"TOKEN\s*=\s*secrets\.token_urlsafe\(32\)",
                "TOKEN = os.environ['LA_STUDIO_COLAB_STT_TOKEN']",
                source,
            )
    if source is None:
        raise RuntimeError(
            f"Could not extract the exact worker source for {spec.capability}/{spec.model}. "
            "The notebook must keep a static FastAPI worker source."
        )
    return source


def install_shell_lines(document: dict[str, Any], runtime: Path) -> None:
    """Run only explicit package/artifact setup commands, never notebook launch cells."""
    for cell in document.get("cells", []):
        if cell.get("cell_type") != "code":
            continue
        source = notebook_source(cell)
        if "uvicorn" in source and ("cloudflared" in source or "tunnel" in source):
            continue
        for raw_line in source.splitlines():
            line = raw_line.strip()
            if line.startswith("%pip "):
                arguments = [sys.executable, "-m", "pip", *line[5:].strip().split()]
            elif line.startswith("!"):
                command = line[1:].strip()
                if command.startswith(("python ", "python3 ")) or "cloudflared" in command:
                    continue
                arguments = ["bash", "-lc", command]
            else:
                continue
            subprocess.run(arguments, cwd=runtime, check=True)


def run_ocr_bootstrap(document: dict[str, Any], runtime: Path) -> None:
    """Use the OCR notebook's isolated bootstrap rather than mixing Paddle globally."""
    for cell in document.get("cells", []):
        source = notebook_source(cell)
        if "OCR_SITE_PACKAGES" not in source or "BOOTSTRAP_REVISION" not in source:
            continue
        source = source.replace("!nvidia-smi", "subprocess.run(['nvidia-smi'], check=True)")
        bootstrap = runtime / "la_studio_ocr_bootstrap.py"
        bootstrap.write_text(source, encoding="utf-8")
        subprocess.run([sys.executable, str(bootstrap)], cwd=runtime, check=True)
        return
    raise RuntimeError("The selected Subtitle OCR notebook has no recognized isolated bootstrap cell")


def prepare_worker(spec: WorkerSpec, source_root: Path, runtime: Path) -> tuple[Path, dict[str, str]]:
    document = read_notebook(spec.notebook)
    if spec.capability == "subtitle-ocr":
        run_ocr_bootstrap(document, runtime)
    else:
        install_shell_lines(document, runtime)
    # Uvicorn imports a Python module, so exact model IDs such as
    # ``whisper.cpp`` and ``m2m100-418m`` cannot be used verbatim as names.
    module_stem = re.sub(r"[^A-Za-z0-9_]", "_", f"worker_{spec.capability}_{spec.model}")
    worker_path = runtime / f"{module_stem}.py"
    worker_path.write_text(worker_source_for(spec, source_root), encoding="utf-8")
    environment = os.environ.copy()
    environment["PYTHONUNBUFFERED"] = "1"
    environment["LA_STUDIO_UNIFIED_DUBBING_TOKEN"] = environment["LA_STUDIO_UNIFIED_DUBBING_TOKEN"]
    for token_environment in TOKEN_ENVIRONMENTS.values():
        environment[token_environment] = environment["LA_STUDIO_UNIFIED_DUBBING_TOKEN"]
    if spec.capability == "subtitle-ocr":
        isolated_site = runtime / "la_studio_subtitle_ocr_site"
        environment["PYTHONPATH"] = str(isolated_site)
        environment["PYTHONNOUSERSITE"] = "1"
    return worker_path, environment


def health_payload(base_url: str, token: str) -> dict[str, Any] | None:
    try:
        response = httpx.get(
            f"{base_url}/health", headers={"Authorization": f"Bearer {token}"}, timeout=10.0
        )
        response.raise_for_status()
        payload = response.json()
        return payload if isinstance(payload, dict) else None
    except (httpx.HTTPError, ValueError):
        return None


def wait_for_exact_health(worker: RunningWorker, token: str, timeout_seconds: float = 420.0) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if worker.process.poll() is not None:
            tail = worker.log_path.read_text(encoding="utf-8", errors="replace")[-12000:]
            raise RuntimeError(
                f"{worker.spec.capability}/{worker.spec.model} exited before readiness "
                f"(exit {worker.process.returncode}).\n{tail}"
            )
        payload = health_payload(worker.base_url, token)
        if payload and payload.get("ready") is True:
            returned_model = str(payload.get("model") or payload.get("family_id") or "")
            if returned_model and returned_model != worker.spec.model:
                raise RuntimeError(
                    f"Worker identity mismatch: expected {worker.spec.model}, got {returned_model}"
                )
            if str(payload.get("device", "cuda")).lower() != "cuda":
                raise RuntimeError(
                    f"{worker.spec.capability}/{worker.spec.model} is not CUDA-ready: {payload}"
                )
            return
        time.sleep(1.0)
    raise RuntimeError(
        f"Timed out waiting for actual CUDA health from {worker.spec.capability}/{worker.spec.model}. "
        f"See {worker.log_path}."
    )


class UnifiedCoordinator:
    def __init__(self, source_root: Path, runtime: Path, token: str):
        self.source_root = source_root
        self.runtime = runtime
        self.token = token
        self.workers: dict[tuple[str, str], RunningWorker] = {}

    def start(self, selections: list[dict[str, Any]]) -> None:
        if not selections:
            raise RuntimeError("UNIFIED_WORKERS is empty; configure at least one exact Dubbing model")
        self.runtime.mkdir(parents=True, exist_ok=True)
        for selection in selections:
            capability = require_slug(selection.get("capability"), "capability")
            model = require_slug(selection.get("model"), "model")
            key = (capability, model)
            if key in self.workers:
                continue
            notebook = discover_exact_notebook(self.source_root, capability, model)
            spec = WorkerSpec(capability, model, notebook)
            worker_path, environment = prepare_worker(spec, self.source_root, self.runtime)
            port = find_free_port()
            log_path = self.runtime / f"{capability}-{model}.log"
            with log_path.open("w", encoding="utf-8") as log:
                process = subprocess.Popen(
                    [sys.executable, "-m", "uvicorn", f"{worker_path.stem}:app",
                     "--host", LISTEN_HOST, "--port", str(port)],
                    cwd=self.runtime, env=environment, stdout=log, stderr=subprocess.STDOUT, text=True,
                )
            worker = RunningWorker(spec, port, process, log_path)
            wait_for_exact_health(worker, self.token)
            self.workers[key] = worker

    def worker_for(self, capability: str, model: str) -> RunningWorker:
        worker = self.workers.get((capability, model))
        if worker is None:
            raise HTTPException(
                status_code=404,
                detail=(f"{capability}/{model} was not prewarmed by this unified notebook. "
                        "Add that exact model to UNIFIED_WORKERS and run the notebook again."),
            )
        return worker

    def health(self) -> dict[str, Any]:
        result: list[dict[str, Any]] = []
        for worker in self.workers.values():
            payload = health_payload(worker.base_url, self.token)
            if not payload or payload.get("ready") is not True:
                raise HTTPException(status_code=503, detail=f"Worker lost readiness: {worker.spec}")
            result.append({"capability": worker.spec.capability, "model": worker.spec.model, "health": payload})
        return {"ready": True, "coordinator": COORDINATOR_REVISION, "workers": result}

    def stop(self) -> None:
        for worker in self.workers.values():
            if worker.process.poll() is None:
                worker.process.terminate()


COORDINATOR: UnifiedCoordinator | None = None
APP = FastAPI(title="LA Studio unified Dubbing coordinator")


def require_authorization(request: Request) -> None:
    expected = f"Bearer {os.environ['LA_STUDIO_UNIFIED_DUBBING_TOKEN']}"
    if not secrets.compare_digest(request.headers.get("authorization", ""), expected):
        raise HTTPException(status_code=401, detail="Invalid LA Studio unified session token")


@APP.get("/health")
async def coordinator_health(request: Request) -> JSONResponse:
    require_authorization(request)
    if COORDINATOR is None:
        raise HTTPException(status_code=503, detail="Coordinator has not finished prewarming exact workers")
    return JSONResponse(COORDINATOR.health())


@APP.get("/v1/capabilities")
async def capabilities(request: Request) -> JSONResponse:
    require_authorization(request)
    if COORDINATOR is None:
        raise HTTPException(status_code=503, detail="Coordinator has not finished prewarming exact workers")
    rows = [{"capability": worker.spec.capability, "model": worker.spec.model}
            for worker in COORDINATOR.workers.values()]
    return JSONResponse({"ready": True, "routes": rows})


@APP.api_route("/v1/unified/{capability}/{model}/{route:path}", methods=["GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"])
async def proxy(capability: str, model: str, route: str, request: Request) -> StreamingResponse:
    require_authorization(request)
    if COORDINATOR is None:
        raise HTTPException(status_code=503, detail="Coordinator has not finished prewarming exact workers")
    worker = COORDINATOR.worker_for(require_slug(capability, "capability"), require_slug(model, "model"))
    target = f"{worker.base_url}/{route.lstrip('/')}"
    if request.url.query:
        target += f"?{request.url.query}"
    headers = {key: value for key, value in request.headers.items() if key.lower() not in HOP_BY_HOP_HEADERS}
    client = httpx.AsyncClient(timeout=httpx.Timeout(connect=30.0, read=None, write=None, pool=30.0))
    try:
        upstream_request = client.build_request(request.method, target, headers=headers, content=request.stream())
        upstream = await client.send(upstream_request, stream=True)
    except httpx.HTTPError as error:
        await client.aclose()
        raise HTTPException(status_code=502, detail=f"Configured unified worker request failed: {error}") from error

    async def response_body():
        try:
            async for chunk in upstream.aiter_raw():
                yield chunk
        finally:
            await upstream.aclose()
            await client.aclose()

    response_headers = {key: value for key, value in upstream.headers.items() if key.lower() not in HOP_BY_HOP_HEADERS}
    return StreamingResponse(response_body(), status_code=upstream.status_code, headers=response_headers)


def ensure_cloudflared(runtime: Path) -> str:
    found = shutil.which("cloudflared")
    if found:
        return found
    destination = runtime / "cloudflared"
    subprocess.run([
        "curl", "--fail", "--location", "--retry", "3", "--output", str(destination),
        "https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64",
    ], check=True)
    destination.chmod(0o755)
    return str(destination)


def start_tunnel(runtime: Path) -> tuple[subprocess.Popen[str], str]:
    cloudflared = ensure_cloudflared(runtime)
    log_path = runtime / "unified-tunnel.log"
    log = log_path.open("w", encoding="utf-8")
    tunnel = subprocess.Popen(
        [cloudflared, "tunnel", "--url", f"http://{LISTEN_HOST}:{LISTEN_PORT}", "--no-autoupdate"],
        stdout=log, stderr=subprocess.STDOUT, text=True,
    )
    pattern = re.compile(r"https://[-a-z0-9]+\.trycloudflare\.com", re.IGNORECASE)
    deadline = time.monotonic() + 90.0
    while time.monotonic() < deadline:
        if tunnel.poll() is not None:
            raise RuntimeError(f"Cloudflare tunnel exited early:\n{log_path.read_text(encoding='utf-8', errors='replace')[-8000:]}")
        text = log_path.read_text(encoding="utf-8", errors="replace")
        match = pattern.search(text)
        if match:
            return tunnel, match.group(0)
        time.sleep(0.5)
    tunnel.terminate()
    raise RuntimeError("Timed out waiting for the verified public Cloudflare tunnel URL")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--runtime", default=Path("/content/la_studio_unified_dubbing"), type=Path)
    arguments = parser.parse_args()
    if not arguments.source_root.is_dir():
        raise RuntimeError(f"Source checkout does not exist: {arguments.source_root}")
    selections = json.loads(arguments.config.read_text(encoding="utf-8"))
    if not isinstance(selections, list):
        raise RuntimeError("Unified workers config must be a JSON list")
    os.environ.setdefault("LA_STUDIO_UNIFIED_DUBBING_TOKEN", secrets.token_urlsafe(32))
    global COORDINATOR
    COORDINATOR = UnifiedCoordinator(arguments.source_root, arguments.runtime, os.environ["LA_STUDIO_UNIFIED_DUBBING_TOKEN"])
    try:
        COORDINATOR.start(selections)
        tunnel, public_url = start_tunnel(arguments.runtime)
        print("\nLA Studio Unified Dubbing coordinator is ready.")
        print(f"LA_STUDIO_UNIFIED_DUBBING_URL={public_url}")
        print(f"LA_STUDIO_UNIFIED_DUBBING_TOKEN={os.environ['LA_STUDIO_UNIFIED_DUBBING_TOKEN']}")
        print("Paste these once in Dubbing > Project setup > Unified Colab (optional).")
        uvicorn.run(APP, host=LISTEN_HOST, port=LISTEN_PORT, log_level="info")
        tunnel.terminate()
    finally:
        if COORDINATOR is not None:
            COORDINATOR.stop()


if __name__ == "__main__":
    main()
