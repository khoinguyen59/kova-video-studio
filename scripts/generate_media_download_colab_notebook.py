#!/usr/bin/env python3
"""Generate the dedicated Colab public-media downloader notebook.

This worker is deliberately separate from the CUDA model workers and API
Gateway.  The desktop sends a public HTTPS link to a token-authenticated Colab
worker, then retrieves only the completed file.  It never runs yt-dlp or a
browser profile on the desktop.
"""

from __future__ import annotations

import json
from pathlib import Path
from textwrap import dedent

from colab_worker_launch import build_worker_launch


ROOT = Path(__file__).resolve().parents[1]
NOTEBOOKS = ROOT / "notebooks"
NOTEBOOK = "LA_STUDIO_MEDIA_DOWNLOAD_YTDLP_COLAB.ipynb"
MODEL_ID = "yt-dlp-media-download"
WORKER_REVISION = "media-download-2026-08-14.1"
RESPONSE_CONTRACT = "media-download-jobs-v1"


def lines(source: str) -> list[str]:
    return (dedent(source).strip() + "\n").splitlines(keepends=True)


def worker_source() -> str:
    return r'''
    import ipaddress
    import os
    import re
    import secrets
    import socket
    import subprocess
    import sys
    import threading
    import time
    from pathlib import Path
    from urllib.parse import urlparse

    from fastapi import FastAPI, Header, HTTPException
    from fastapi.responses import FileResponse
    from pydantic import BaseModel, Field

    TOKEN = os.environ["LA_STUDIO_COLAB_MEDIA_DOWNLOAD_TOKEN"]
    MODEL_ID = "yt-dlp-media-download"
    WORKER_REVISION = "media-download-2026-08-14.1"
    RESPONSE_CONTRACT = "media-download-jobs-v1"
    # requires_cuda=False: this is an acquisition-only Colab CPU worker.
    # It never runs a local/GPU inference fallback.
    REQUIRES_CUDA = False
    DOWNLOAD_ROOT = Path("/content/la_studio_media_downloads")
    MAX_FILE_BYTES = 4 * 1024 * 1024 * 1024
    MAX_JOBS = 8
    JOBS = {}
    JOB_LOCK = threading.Lock()
    URL_PATTERN = re.compile(r"^https://[^\s]+$", re.IGNORECASE)

    app = FastAPI(title="LA Studio Colab media downloader", docs_url=None,
                  redoc_url=None, openapi_url=None)

    class DownloadRequest(BaseModel):
        url: str = Field(min_length=8, max_length=4096)

    def authorize(authorization: str | None) -> None:
        if authorization != "Bearer " + TOKEN:
            raise HTTPException(status_code=401, detail="invalid worker token")

    def public_url_or_error(value: str) -> str:
        raw = value.strip()
        parsed = urlparse(raw)
        if (not URL_PATTERN.fullmatch(raw) or parsed.scheme.lower() != "https"
                or not parsed.hostname or parsed.username or parsed.password):
            raise HTTPException(status_code=422, detail="only one public HTTPS media URL is accepted")
        host = parsed.hostname.rstrip(".").lower()
        if host == "localhost" or host.endswith(".local"):
            raise HTTPException(status_code=422, detail="local media URLs are not accepted")
        try:
            addresses = {record[4][0] for record in socket.getaddrinfo(host, 443, type=socket.SOCK_STREAM)}
        except socket.gaierror:
            raise HTTPException(status_code=422, detail="the public media host could not be resolved")
        if not addresses:
            raise HTTPException(status_code=422, detail="the public media host could not be resolved")
        for address in addresses:
            ip = ipaddress.ip_address(address)
            if (ip.is_private or ip.is_loopback or ip.is_link_local or ip.is_multicast
                    or ip.is_reserved or ip.is_unspecified):
                raise HTTPException(status_code=422, detail="the media URL must resolve to a public host")
        return raw

    def safe_name(path: Path) -> str:
        name = re.sub(r"[^A-Za-z0-9._-]+", "-", path.name).strip(".-")
        return (name or "downloaded-media")[:180]

    def job_view(job: dict) -> dict:
        directory = Path(job["directory"])
        received = sum(path.stat().st_size for path in directory.glob("*") if path.is_file())
        return {
            "state": job["state"],
            "received_bytes": min(received, MAX_FILE_BYTES),
            "total_bytes": job.get("total_bytes", -1),
            "file_name": job.get("file_name", ""),
            "detail": job.get("detail", ""),
        }

    def download_job(job_id: str, public_url: str) -> None:
        with JOB_LOCK:
            job = JOBS[job_id]
            job["state"] = "downloading"
        directory = Path(job["directory"])
        log_path = directory / "yt-dlp.log"
        command = [
            sys.executable, "-m", "yt_dlp", "--no-config", "--no-playlist",
            "--restrict-filenames", "--no-progress", "--socket-timeout", "30",
            "--retries", "3", "--fragment-retries", "3", "--max-filesize", "4G",
            "--output", str(directory / "%(title).100B-%(id)s.%(ext)s"), public_url,
        ]
        try:
            with log_path.open("w", encoding="utf-8", errors="replace") as log:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                        timeout=60 * 45, check=False)
            files = [path for path in directory.iterdir()
                     if path.is_file() and path.name != log_path.name]
            if result.returncode != 0 or len(files) != 1:
                raise RuntimeError("yt-dlp could not download this public media")
            output = files[0]
            if output.stat().st_size <= 0 or output.stat().st_size > MAX_FILE_BYTES:
                raise RuntimeError("the downloaded media is empty or exceeds the 4 GiB limit")
            with JOB_LOCK:
                job["state"] = "ready"
                job["file_path"] = str(output)
                job["file_name"] = safe_name(output)
                job["total_bytes"] = output.stat().st_size
                job["detail"] = ""
        except Exception:
            with JOB_LOCK:
                job["state"] = "failed"
                job["detail"] = "Colab could not download this public media. Check that the link is public and try again."

    @app.get("/health")
    @app.get("/v1/health")
    def health(authorization: str | None = Header(default=None)):
        authorize(authorization)
        return {
            "status": "ready", "ready": True, "device": "colab-cpu",
            "model": MODEL_ID, "variant": "fixed", "cpu_fallback": False,
            "worker_revision": WORKER_REVISION,
        }

    @app.get("/v1/capabilities")
    def capabilities(authorization: str | None = Header(default=None)):
        authorize(authorization)
        return {
            "contract_version": 1, "device": "colab-cpu",
            "worker_revision": WORKER_REVISION,
            "capabilities": [{"id": "media-download", "models": [{
                "id": MODEL_ID, "name": "Colab yt-dlp media download", "variant": "fixed",
                "loaded": True, "device": "colab-cpu", "cpu_fallback": False,
                "response_contract": RESPONSE_CONTRACT,
            }]}],
        }

    @app.post("/v1/media/downloads")
    def create_download(request: DownloadRequest, authorization: str | None = Header(default=None)):
        authorize(authorization)
        public_url = public_url_or_error(request.url)
        with JOB_LOCK:
            active = sum(job["state"] in {"queued", "downloading"} for job in JOBS.values())
            if active >= MAX_JOBS:
                raise HTTPException(status_code=429, detail="the Colab download queue is full; wait for a job to finish")
            job_id = secrets.token_hex(16)
            directory = DOWNLOAD_ROOT / job_id
            directory.mkdir(parents=True, exist_ok=False)
            JOBS[job_id] = {"state": "queued", "directory": str(directory), "created_at": time.time()}
        thread = threading.Thread(target=download_job, args=(job_id, public_url), daemon=True)
        thread.start()
        return {"job_id": job_id}

    @app.get("/v1/media/downloads/{job_id}")
    def download_status(job_id: str, authorization: str | None = Header(default=None)):
        authorize(authorization)
        with JOB_LOCK:
            job = JOBS.get(job_id)
            if job is None:
                raise HTTPException(status_code=404, detail="download job was not found")
            return job_view(job)

    @app.get("/v1/media/downloads/{job_id}/file")
    def download_file(job_id: str, authorization: str | None = Header(default=None)):
        authorize(authorization)
        with JOB_LOCK:
            job = JOBS.get(job_id)
            if job is None or job.get("state") != "ready":
                raise HTTPException(status_code=409, detail="download is not ready")
            path = Path(job.get("file_path", ""))
            name = job.get("file_name", "downloaded-media")
        if not path.is_file() or path.stat().st_size <= 0 or path.stat().st_size > MAX_FILE_BYTES:
            raise HTTPException(status_code=410, detail="completed media is no longer available")
        return FileResponse(path, media_type="application/octet-stream", filename=name,
                            headers={"Cache-Control": "no-store"})
    '''


def build_notebook() -> dict:
    launch = build_worker_launch(
        capability_label="media-download",
        module="la_studio_media_download_worker:app",
        port=8013,
        model_id=MODEL_ID,
        token_env="LA_STUDIO_COLAB_MEDIA_DOWNLOAD_TOKEN",
        url_env="LA_STUDIO_COLAB_MEDIA_DOWNLOAD_URL",
        model_env="LA_STUDIO_COLAB_MEDIA_DOWNLOAD_MODEL",
        log_path="/content/la_studio_media_download_worker.log",
        requires_cuda=False,
    )
    return {
        "cells": [
            {"cell_type": "markdown", "metadata": {}, "source": lines("""
            # LA Studio public-media downloader

            This dedicated **Colab CPU** worker downloads public HTTPS media links
            with yt-dlp, then lets LA Studio copy the completed file into its local
            media library. It does not run a GPU model, API Gateway, a desktop
            browser, desktop yt-dlp, or browser cookies.

            1. Run all cells once in this Colab runtime.
            2. Copy the temporary URL and token printed by the last cell.
            3. In LA Studio, choose **Download with Colab**, paste them, then press
               **Connect and check Colab downloader** before adding links.
            """)},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [],
             "source": lines("""
             %pip install -q --upgrade --no-cache-dir "fastapi==0.115.12" "uvicorn==0.34.3" "pydantic==2.10.6" "yt-dlp>=2025.08.22"
             """)},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [],
             "source": lines("from pathlib import Path\nPath('/content/la_studio_media_download_worker.py').write_text(" + repr(dedent(worker_source()).strip() + "\n") + ", encoding='utf-8')")},
            {"cell_type": "code", "execution_count": None, "metadata": {}, "outputs": [],
             "source": lines(launch)},
        ],
        "metadata": {
            "colab": {"provenance": []},
            "kernelspec": {"display_name": "Python 3", "name": "python3"},
            "language_info": {"name": "python"},
            "la_studio": {
                "capability": "media-download", "family_id": MODEL_ID,
                "upstream_model": "yt-dlp", "contract_version": 1,
                "device": "colab-cpu", "cpu_fallback": False,
                "worker_revision": WORKER_REVISION,
                "response_contract": RESPONSE_CONTRACT,
            },
        },
        "nbformat": 4, "nbformat_minor": 5,
    }


def main() -> None:
    NOTEBOOKS.mkdir(parents=True, exist_ok=True)
    target = NOTEBOOKS / NOTEBOOK
    target.write_text(json.dumps(build_notebook(), indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(target.relative_to(ROOT))


if __name__ == "__main__":
    main()
