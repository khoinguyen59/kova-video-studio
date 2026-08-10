#!/usr/bin/env python3
"""App-owned Chromium session for Douyin public-media imports.

This helper deliberately uses a profile owned by LA Studio.  It never reads a
user's Chrome/Edge/Firefox profile and it never prints cookies, URLs containing
signed query data, or page content.  Playwright is an optional dependency: the
application keeps the explicit Netscape-cookie/yt-dlp path when this helper is
not configured.
"""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
import re
import sys
import time
from pathlib import Path
from typing import Iterable
from urllib.parse import urlparse
from urllib.request import Request, urlopen


MAX_DOWNLOAD_BYTES = 2 * 1024 * 1024 * 1024
LOGIN_TIMEOUT_SECONDS = 300
DOUYIN_HOSTS = {
    "douyin.com",
    "www.douyin.com",
    "v.douyin.com",
}
AUTH_COOKIE_NAMES = {"sessionid", "sessionid_ss", "sid_guard", "uid_tt"}


def emit(event: str, **fields: object) -> None:
    payload = {"event": event, **fields}
    # Keep stdout machine-readable and intentionally omit all sensitive data.
    print(json.dumps(payload, ensure_ascii=False, separators=(",", ":")), flush=True)


def fail(message: str, code: int = 2) -> int:
    emit("error", message=message)
    return code


def validate_source(value: str) -> str:
    parsed = urlparse(value)
    if parsed.scheme.lower() != "https" or parsed.username or parsed.password:
        raise ValueError("The browser session accepts only an HTTPS Douyin URL.")
    host = (parsed.hostname or "").lower().rstrip(".")
    if host not in DOUYIN_HOSTS and not host.endswith(".douyin.com"):
        raise ValueError("The browser session accepts only a Douyin URL.")
    return value


def is_safe_media_url(value: str) -> bool:
    parsed = urlparse(value)
    if parsed.scheme.lower() != "https" or parsed.username or parsed.password:
        return False
    host = (parsed.hostname or "").strip().lower().rstrip(".")
    if not host or host in {"localhost", "localhost.localdomain"} or host.endswith(".local"):
        return False
    try:
        return ipaddress.ip_address(host).is_global
    except ValueError:
        return True


def safe_profile(path: str) -> Path:
    profile = Path(path).expanduser().resolve()
    if str(profile) in {"", "."} or profile.name in {"", ".", ".."}:
        raise ValueError("The Chromium profile directory is invalid.")
    profile.mkdir(parents=True, exist_ok=True)
    return profile


def cookie_header(cookies: Iterable[dict]) -> str:
    pairs = []
    for item in cookies:
        name = str(item.get("name", "")).strip()
        value = str(item.get("value", ""))
        if name:
            pairs.append(f"{name}={value}")
    return "; ".join(pairs)


def has_authenticated_session(cookies: Iterable[dict]) -> bool:
    return any(str(item.get("name", "")).lower() in AUTH_COOKIE_NAMES for item in cookies)


def stream_download(url: str, cookies: Iterable[dict], referer: str, output: Path) -> None:
    headers = {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/136 Safari/537.36",
        "Referer": referer,
        "Accept": "video/*,*/*;q=0.8",
    }
    header = cookie_header(cookies)
    if header:
        headers["Cookie"] = header
    request = Request(url, headers=headers)
    written = 0
    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        with urlopen(request, timeout=120) as response, output.open("wb") as target:
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                written += len(chunk)
                if written > MAX_DOWNLOAD_BYTES:
                    raise ValueError("The Douyin media response exceeds the 2 GiB import limit.")
                target.write(chunk)
    except Exception:
        try:
            output.unlink(missing_ok=True)
        except OSError:
            pass
        raise
    if written <= 0:
        output.unlink(missing_ok=True)
        raise ValueError("The Douyin media response was empty.")


def candidate_urls(page, responses: list[str]) -> list[str]:
    values: list[str] = []
    values.extend(responses)
    try:
        values.extend(page.locator("video").evaluate_all(
            "els => els.map(e => e.currentSrc || e.src || '').filter(Boolean)"))
    except Exception:
        pass
    try:
        values.extend(page.evaluate(
            "Array.from(performance.getEntriesByType('resource')).map(e => e.name)"))
    except Exception:
        pass
    result: list[str] = []
    seen: set[str] = set()
    for value in values:
        if not isinstance(value, str) or not is_safe_media_url(value):
            continue
        if value in seen or ".m3u8" in value.lower() or "blob:" in value.lower():
            continue
        # Douyin's CDN URLs do not always end in .mp4, so retain media-like
        # responses and URLs with a video extension while rejecting page APIs.
        if not (re.search(r"\.(mp4|mov|webm)(?:$|[?#])", value, re.I)
                or "video" in value.lower() or "playwm" in value.lower()
                or "playno" in value.lower()):
            continue
        seen.add(value)
        result.append(value)
    return result


def import_playwright():
    try:
        from playwright.sync_api import sync_playwright  # type: ignore
    except Exception as exc:  # pragma: no cover - depends on user runtime
        raise RuntimeError(
            "Playwright is not installed. Install it in the configured Python environment "
            "with 'python -m pip install playwright' and 'python -m playwright install chromium'."
        ) from exc
    return sync_playwright


def run_login(profile: Path, timeout: int) -> int:
    sync_playwright = import_playwright()
    with sync_playwright() as playwright:
        context = playwright.chromium.launch_persistent_context(
            user_data_dir=str(profile), headless=False, accept_downloads=False,
            args=["--disable-blink-features=AutomationControlled"],
        )
        try:
            page = context.pages[0] if context.pages else context.new_page()
            page.goto("https://www.douyin.com/", wait_until="domcontentloaded", timeout=60000)
            emit("login_started")
            deadline = time.time() + max(10, min(timeout, LOGIN_TIMEOUT_SECONDS))
            verified = False
            while time.time() < deadline and context.pages:
                cookies = context.cookies("https://www.douyin.com/")
                if has_authenticated_session(cookies):
                    if not verified:
                        emit("ready")
                        verified = True
                    # Give the browser a moment to finish writing its profile.
                    time.sleep(2)
                    return 0
                time.sleep(1)
            return fail("No Douyin session was saved before the browser session timed out.")
        finally:
            context.close()


def run_check(profile: Path, url: str, timeout: int) -> int:
    validate_source(url)
    sync_playwright = import_playwright()
    with sync_playwright() as playwright:
        context = playwright.chromium.launch_persistent_context(
            user_data_dir=str(profile), headless=True, accept_downloads=False,
            args=["--disable-blink-features=AutomationControlled"],
        )
        try:
            page = context.pages[0] if context.pages else context.new_page()
            page.goto(url, wait_until="domcontentloaded", timeout=timeout)
            cookies = context.cookies("https://www.douyin.com/")
            if not has_authenticated_session(cookies):
                return fail("The managed Chromium profile has no verified Douyin session.")
            emit("ready")
            return 0
        except Exception:
            return fail("The managed Chromium session could not open the Douyin page.")
        finally:
            context.close()


def run_download(profile: Path, url: str, output: Path, timeout: int) -> int:
    validate_source(url)
    sync_playwright = import_playwright()
    responses: list[str] = []
    with sync_playwright() as playwright:
        context = playwright.chromium.launch_persistent_context(
            user_data_dir=str(profile), headless=True, accept_downloads=False,
            args=["--disable-blink-features=AutomationControlled"],
        )
        try:
            page = context.pages[0] if context.pages else context.new_page()

            def on_response(response) -> None:
                try:
                    content_type = (response.headers.get("content-type") or "").lower()
                    if content_type.startswith("video/") or "octet-stream" in content_type:
                        responses.append(response.url)
                except Exception:
                    pass

            page.on("response", on_response)
            page.goto(url, wait_until="domcontentloaded", timeout=timeout)
            page.wait_for_timeout(2500)
            cookies = context.cookies("https://www.douyin.com/")
            if not has_authenticated_session(cookies):
                return fail("The managed Chromium profile has no verified Douyin session.")
            candidates = candidate_urls(page, responses)
            if not candidates:
                return fail("The Douyin page did not expose a downloadable video URL.")
            for media_url in candidates:
                try:
                    stream_download(media_url, cookies, page.url, output)
                    emit("downloaded", path=str(output.resolve()))
                    return 0
                except Exception:
                    continue
            return fail("The Douyin video URL could not be downloaded with the managed session.")
        except Exception:
            return fail("The managed Chromium session could not download this Douyin page.")
        finally:
            context.close()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--mode", choices=("login", "check", "download"), required=True)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--url")
    parser.add_argument("--output")
    parser.add_argument("--timeout-ms", type=int, default=60000)
    args = parser.parse_args(argv)
    try:
        profile = safe_profile(args.profile)
        timeout = max(10000, min(args.timeout_ms, 120000))
        if args.mode == "login":
            return run_login(profile, timeout // 1000)
        if not args.url:
            return fail("A Douyin URL is required for this browser operation.")
        if args.mode == "check":
            return run_check(profile, args.url, timeout)
        if not args.output:
            return fail("An output path is required for browser download.")
        return run_download(profile, args.url, Path(args.output), timeout)
    except ValueError as exc:
        return fail(str(exc))
    except RuntimeError as exc:
        return fail(str(exc))
    except Exception:
        return fail("The managed Chromium operation failed unexpectedly.")


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
