"""Shared, self-contained launch cell for exact-model Colab workers.

The generated code deliberately lives inside every notebook: the desktop app
only receives a temporary tunnel URL and token, never a local dependency on
this script.  Keeping the launch protocol in one generator helper prevents a
fix for one Colab capability from leaving the others with stale process or
tunnel behaviour.
"""

from __future__ import annotations

import json
from textwrap import dedent


LAUNCH_REVISION = "launch-2026-08-06.1"


def build_worker_launch(
    *,
    capability_label: str,
    module: str,
    port: int,
    model_id: str,
    token_env: str,
    url_env: str,
    model_env: str,
    log_path: str,
    worker_python: str | None = None,
    isolate_python: bool = False,
    worker_environment: dict[str, str] | None = None,
    requires_cuda: bool = True,
) -> str:
    """Return a Colab code cell that launches one verified worker safely."""
    if not all((capability_label, module, model_id, token_env, url_env, model_env, log_path)):
        raise ValueError("Colab launch metadata must be complete")
    if not 1 <= port <= 65535:
        raise ValueError(f"Invalid Colab worker port: {port}")
    if isolate_python and not worker_python:
        raise ValueError("An isolated Colab worker must provide its Python executable")

    template = r'''
# LA Studio worker launch contract: __REVISION__
import json
import os
import queue
import re
import secrets
import signal
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

CAPABILITY_LABEL = __CAPABILITY_LABEL__
MODEL_ID = __MODEL_ID__
PORT = __PORT__
TOKEN_ENV = __TOKEN_ENV__
URL_ENV = __URL_ENV__
MODEL_ENV = __MODEL_ENV__
WORKER_LOG = Path(__LOG_PATH__)
WORKER_MODULE = __WORKER_MODULE__
WORKER_PYTHON = __WORKER_PYTHON__
WORKER_PYTHON_ISOLATED = __WORKER_PYTHON_ISOLATED__
WORKER_ENVIRONMENT = __WORKER_ENVIRONMENT__
REQUIRES_CUDA = __REQUIRES_CUDA__
STARTUP_TIMEOUT_SECONDS = 20 * 60
TUNNEL_TIMEOUT_SECONDS = 90
TOKEN = secrets.token_urlsafe(32)


def port_is_occupied(port: int) -> bool:
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.5):
            return True
    except OSError:
        return False


def process_cmdline(pid: int) -> str:
    """Read a Linux process command line without depending on psutil."""
    try:
        return Path(f"/proc/{pid}/cmdline").read_bytes().replace(b"\0", b" ").decode(
            "utf-8", errors="replace"
        ).strip()
    except (FileNotFoundError, PermissionError, ProcessLookupError):
        return ""


def all_processes():
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        pid = int(entry.name)
        command = process_cmdline(pid)
        if command:
            yield pid, command


def listening_processes(port: int) -> dict[int, str]:
    """Return PIDs listening on a local TCP port via /proc socket ownership."""
    target_port = f"{port:04X}"
    socket_inodes = set()
    for table_name in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            lines = Path(table_name).read_text(encoding="utf-8").splitlines()[1:]
        except FileNotFoundError:
            continue
        for line in lines:
            fields = line.split()
            if len(fields) < 10:
                continue
            local_address, state, inode = fields[1], fields[3], fields[9]
            if state == "0A" and local_address.rsplit(":", 1)[-1].upper() == target_port:
                socket_inodes.add(inode)
    if not socket_inodes:
        return {}

    listeners = {}
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            descriptors = (entry / "fd").iterdir()
        except (FileNotFoundError, PermissionError):
            continue
        for descriptor in descriptors:
            try:
                target = os.readlink(descriptor)
            except (FileNotFoundError, PermissionError, OSError):
                continue
            match = re.fullmatch(r"socket:\\[(\\d+)\\]", target)
            if match and match.group(1) in socket_inodes:
                pid = int(entry.name)
                listeners[pid] = process_cmdline(pid)
                break
    return listeners


def stop_pid(pid: int) -> None:
    if pid == os.getpid():
        return
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return
        time.sleep(0.2)
    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def reclaim_previous_la_studio_worker() -> None:
    """Stop only an older LA Studio worker/tunnel for this exact local port.

    Re-running a Colab cell keeps child processes alive.  The previous launch
    created a new token but aborted before it could replace the old worker,
    forcing users to destroy the whole GPU runtime.  We identify ownership by
    the exact generated module name and never terminate a foreign listener.
    """
    stopped = []
    for pid, command in listening_processes(PORT).items():
        if WORKER_MODULE in command and "uvicorn" in command:
            stop_pid(pid)
            stopped.append(f"worker PID {pid}")

    endpoint = f"http://127.0.0.1:{PORT}"
    for pid, command in all_processes():
        if ("cloudflared" in command and "tunnel" in command and endpoint in command):
            stop_pid(pid)
            stopped.append(f"tunnel PID {pid}")

    deadline = time.monotonic() + 12
    while port_is_occupied(PORT) and time.monotonic() < deadline:
        time.sleep(0.2)
    if stopped:
        print("Stopped previous LA Studio " + ", ".join(stopped) + ".")

    if port_is_occupied(PORT):
        listeners = listening_processes(PORT)
        foreign_pids = sorted(listeners) or ["unknown"]
        raise RuntimeError(
            f"Port {PORT} is occupied by a process that is not the previous LA Studio "
            f"{CAPABILITY_LABEL} worker (PID(s): {', '.join(map(str, foreign_pids))}). "
            "Choose a fresh Colab runtime rather than terminating an unrelated process."
        )


def worker_log_tail() -> str:
    try:
        return WORKER_LOG.read_text(encoding="utf-8", errors="replace")[-12000:]
    except FileNotFoundError:
        return "(worker log was not created)"


def stop_process(process) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()


reclaim_previous_la_studio_worker()

env = os.environ.copy()
env[TOKEN_ENV] = TOKEN
env["PYTHONUNBUFFERED"] = "1"
env.update(WORKER_ENVIRONMENT)
if WORKER_PYTHON_ISOLATED:
    # Do not let Colab's global site-packages or a notebook-level PYTHONPATH
    # bleed into a dedicated worker virtual environment.
    env.pop("PYTHONPATH", None)
    env["PYTHONNOUSERSITE"] = "1"
worker = None
tunnel = None

with WORKER_LOG.open("w", encoding="utf-8", buffering=1) as worker_output:
    worker = subprocess.Popen(
        [WORKER_PYTHON, "-m", "uvicorn", __MODULE__, "--host", "127.0.0.1", "--port", str(PORT)],
        cwd="/content",
        env=env,
        stdout=worker_output,
        stderr=subprocess.STDOUT,
    )
    worker_kind = "exact CUDA" if REQUIRES_CUDA else "dedicated Colab CPU"
    print(f"Starting {worker_kind} {CAPABILITY_LABEL} worker.")
    deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
    last_error = "worker has not answered /health yet"
    next_report = time.monotonic()
    while time.monotonic() < deadline:
        exit_code = worker.poll()
        if exit_code is not None:
            raise RuntimeError(
                f"The exact-model {CAPABILITY_LABEL} worker exited before becoming ready (exit code {exit_code}).\n\n"
                "---- LA Studio worker log (last 12,000 characters) ----\n" + worker_log_tail()
            )
        try:
            request = urllib.request.Request(
                f"http://127.0.0.1:{PORT}/health",
                headers={"Authorization": "Bearer " + TOKEN},
            )
            with urllib.request.urlopen(request, timeout=10) as response:
                health = json.loads(response.read().decode("utf-8"))
            if (response.status == 200
                    and health.get("ready") is True
                    and str(health.get("device", "")).lower()
                        == ("cuda" if REQUIRES_CUDA else "colab-cpu")
                    and str(health.get("model", "")).strip().lower() == MODEL_ID
                    and health.get("cpu_fallback") is False):
                print(worker_kind.title() + " worker is ready:", health)
                break
            last_error = "unexpected /health response: " + json.dumps(health, ensure_ascii=False)
        except urllib.error.HTTPError as error:
            last_error = f"/health returned HTTP {error.code}: " + error.read().decode("utf-8", errors="replace")[:1000]
        except Exception as error:
            last_error = f"/health is not ready: {type(error).__name__}: {error}"
        if time.monotonic() >= next_report:
            print(f"Waiting for the {worker_kind} worker...", last_error)
            next_report = time.monotonic() + 30
        time.sleep(2)
    else:
        stop_process(worker)
        raise RuntimeError(
            f"The {worker_kind} {CAPABILITY_LABEL} worker did not become ready within "
            f"{STARTUP_TIMEOUT_SECONDS // 60} minutes. Last health-check result: {last_error}\n\n"
            "---- LA Studio worker log (last 12,000 characters) ----\n" + worker_log_tail()
        )


def cloudflared_ready() -> bool:
    try:
        return subprocess.run(
            ["cloudflared", "--version"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            check=False,
        ).returncode == 0
    except OSError:
        return False


def ensure_cloudflared() -> None:
    if cloudflared_ready():
        return
    package_path = "/content/la-studio-cloudflared.deb"
    download = subprocess.run(
        [
            "curl", "--fail", "--location", "--retry", "4", "--retry-all-errors",
            "--output", package_path,
            "https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64.deb",
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if download.returncode != 0:
        detail = download.stdout[-1200:].strip() or "no download output"
        raise RuntimeError("Could not download cloudflared: " + detail)
    install = subprocess.run(
        ["dpkg", "-i", package_path], text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=False,
    )
    if install.returncode != 0 or not cloudflared_ready():
        detail = install.stdout[-1200:].strip() or "no installation output"
        raise RuntimeError("Could not install cloudflared: " + detail)


ensure_cloudflared()
tunnel = subprocess.Popen(
    ["cloudflared", "tunnel", "--url", f"http://127.0.0.1:{PORT}", "--no-autoupdate"],
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    bufsize=1,
)
tunnel_lines = queue.Queue()


def collect_tunnel_output() -> None:
    assert tunnel.stdout is not None
    for line in tunnel.stdout:
        tunnel_lines.put(line)


threading.Thread(target=collect_tunnel_output, daemon=True).start()
public_url = ""
recent_tunnel_lines = []
deadline = time.monotonic() + TUNNEL_TIMEOUT_SECONDS
while time.monotonic() < deadline and not public_url:
    if tunnel.poll() is not None:
        break
    try:
        line = tunnel_lines.get(timeout=1)
    except queue.Empty:
        continue
    recent_tunnel_lines.append(line.rstrip())
    recent_tunnel_lines = recent_tunnel_lines[-10:]
    print(line, end="")
    match = re.search(r"https://[^\s\"']+\.trycloudflare\.com", line)
    if match:
        # The desktop Check Colab action is the authoritative public endpoint,
        # bearer-token, capability, and exact-model verification.
        public_url = match.group(0)

if not public_url:
    stop_process(tunnel)
    stop_process(worker)
    tail = "\n".join(recent_tunnel_lines) or "(no cloudflared output)"
    raise RuntimeError(
        f"cloudflared did not publish a trycloudflare URL within {TUNNEL_TIMEOUT_SECONDS} seconds.\n"
        "---- cloudflared output ----\n" + tail
    )

os.environ[URL_ENV] = public_url
os.environ[TOKEN_ENV] = TOKEN
os.environ[MODEL_ENV] = MODEL_ID
print("\nLA Studio exact-model Colab worker is ready")
print(URL_ENV + "=" + public_url)
print(TOKEN_ENV + "=" + TOKEN)
print(MODEL_ENV + "=" + MODEL_ID)
print("Click Check Colab in the matching LA Studio feature before running it.")
'''
    replacements = {
        "__REVISION__": LAUNCH_REVISION,
        "__CAPABILITY_LABEL__": repr(capability_label),
        "__MODEL_ID__": repr(model_id.strip().lower()),
        "__PORT__": str(port),
        "__TOKEN_ENV__": repr(token_env),
        "__URL_ENV__": repr(url_env),
        "__MODEL_ENV__": repr(model_env),
        "__LOG_PATH__": repr(log_path),
        "__MODULE__": repr(module),
        "__WORKER_MODULE__": repr(module.split(":", 1)[0]),
        "__WORKER_PYTHON__": repr(worker_python) if worker_python else "sys.executable",
        "__WORKER_PYTHON_ISOLATED__": repr(bool(isolate_python)),
        "__WORKER_ENVIRONMENT__": json.dumps(worker_environment or {}, sort_keys=True),
        "__REQUIRES_CUDA__": repr(bool(requires_cuda)),
    }
    for placeholder, value in replacements.items():
        template = template.replace(placeholder, value)
    return dedent(template).strip() + "\n"
