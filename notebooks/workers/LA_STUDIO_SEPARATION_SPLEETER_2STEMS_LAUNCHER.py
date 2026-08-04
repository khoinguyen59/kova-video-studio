"""Launch the exact Spleeter Direct Colab worker and a temporary tunnel."""

import json
import os
import re
import secrets
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


WORKER_CONTRACT = "spleeter-cuda-safe-20260805.1"
MODEL_ID = "sherpa-onnx-spleeter-2stems-fp16"
CAPABILITY_LABEL = "Voice Isolation"
PORT = 3924
TOKEN_ENV = "LA_STUDIO_COLAB_SEPARATION_TOKEN"
URL_ENV = "LA_STUDIO_COLAB_SEPARATION_URL"
MODEL_ENV = "LA_STUDIO_COLAB_SEPARATION_MODEL"
WORKER_LOG = Path("/content/la_studio_separation_worker.log")
TUNNEL_LOG = Path("/content/la_studio_separation_tunnel.log")
STARTUP_TIMEOUT_SECONDS = 20 * 60
TUNNEL_TIMEOUT_SECONDS = 90


def port_is_occupied(port: int) -> bool:
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.5):
            return True
    except OSError:
        return False


def tail(path: Path, limit: int = 12000) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")[-limit:]
    except FileNotFoundError:
        return "(log was not created)"


def stop(process: subprocess.Popen | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()


def ensure_cloudflared() -> None:
    if subprocess.run(["cloudflared", "--version"], stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL, check=False).returncode == 0:
        return
    package_path = "/content/la-studio-cloudflared.deb"
    download = subprocess.run(
        ["curl", "--fail", "--location", "--retry", "4", "--retry-all-errors",
         "--output", package_path,
         "https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64.deb"],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
    )
    if download.returncode != 0:
        raise RuntimeError("Could not download cloudflared: " + (download.stdout[-1200:] or "no output"))
    install = subprocess.run(["dpkg", "-i", package_path], text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    if install.returncode != 0:
        raise RuntimeError("Could not install cloudflared: " + (install.stdout[-1200:] or "no output"))


if port_is_occupied(PORT):
    raise RuntimeError(
        f"Port {PORT} is occupied by an earlier Colab worker. Use Runtime > Disconnect and delete runtime, "
        "then Run all once for this exact model."
    )

token = secrets.token_urlsafe(32)
environment = os.environ.copy()
environment[TOKEN_ENV] = token
environment["PYTHONUNBUFFERED"] = "1"
with WORKER_LOG.open("w", encoding="utf-8", buffering=1) as worker_output:
    worker = subprocess.Popen(
        [sys.executable, "-m", "uvicorn", "la_studio_separation_worker:app",
         "--host", "127.0.0.1", "--port", str(PORT)],
        cwd="/content", env=environment, stdout=worker_output, stderr=subprocess.STDOUT,
        start_new_session=True,
    )

print(f"Starting exact CUDA {CAPABILITY_LABEL} worker; it must pass its bounded Spleeter startup probe.")
deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
last_error = "worker has not answered /health yet"
while time.monotonic() < deadline:
    if worker.poll() is not None:
        raise RuntimeError(
            f"The exact-model worker exited before becoming CUDA-ready (exit code {worker.returncode}).\n\n"
            "---- worker log ----\n" + tail(WORKER_LOG)
        )
    try:
        request = urllib.request.Request(f"http://127.0.0.1:{PORT}/health",
                                         headers={"Authorization": "Bearer " + token})
        with urllib.request.urlopen(request, timeout=10) as response:
            health = json.loads(response.read().decode("utf-8"))
        if (response.status == 200 and health.get("ready") is True
                and str(health.get("device", "")).lower() == "cuda"
                and str(health.get("model", "")).strip().lower() == MODEL_ID
                and health.get("cpu_fallback") is False
                and health.get("startup_probe") == "passed"):
            print("Exact CUDA worker passed startup probe:", health)
            break
        last_error = "unexpected /health response: " + json.dumps(health, ensure_ascii=False)
    except urllib.error.HTTPError as error:
        last_error = f"/health returned HTTP {error.code}: " + error.read().decode("utf-8", errors="replace")[:1000]
    except Exception as error:
        last_error = f"/health is not ready: {type(error).__name__}: {error}"
    time.sleep(2)
else:
    stop(worker)
    raise RuntimeError(
        f"The exact-model worker did not become CUDA-ready within {STARTUP_TIMEOUT_SECONDS // 60} minutes. "
        f"Last check: {last_error}\n\n---- worker log ----\n" + tail(WORKER_LOG)
    )

ensure_cloudflared()
with TUNNEL_LOG.open("w", encoding="utf-8", buffering=1) as tunnel_output:
    tunnel = subprocess.Popen(
        ["cloudflared", "tunnel", "--url", f"http://127.0.0.1:{PORT}", "--no-autoupdate"],
        stdout=tunnel_output, stderr=subprocess.STDOUT, start_new_session=True,
    )

public_url = ""
deadline = time.monotonic() + TUNNEL_TIMEOUT_SECONDS
while time.monotonic() < deadline and not public_url:
    if tunnel.poll() is not None:
        break
    match = re.search(r"https://[^\s\"']+\.trycloudflare\.com", tail(TUNNEL_LOG, 4000))
    if match:
        public_url = match.group(0)
        break
    time.sleep(1)

if not public_url:
    stop(tunnel)
    stop(worker)
    raise RuntimeError(
        f"cloudflared did not publish a trycloudflare URL within {TUNNEL_TIMEOUT_SECONDS} seconds.\n"
        "---- cloudflared log ----\n" + tail(TUNNEL_LOG, 4000)
    )

print("\nLA Studio exact-model Colab worker is ready")
print(URL_ENV + "=" + public_url)
print(TOKEN_ENV + "=" + token)
print(MODEL_ENV + "=" + MODEL_ID)
print("Click Check Colab in the matching LA Studio feature before running it.")
