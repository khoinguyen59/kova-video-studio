#!/usr/bin/env python3
"""Thin offline adapter for PaddleOCR 3.7.0 PP-OCRv6 tiny.

The application owns video sampling, timestamps, deduplication and export.
This worker only invokes the official PaddleOCR API on already cropped PNG
frames, so it can be supplied by an isolated package runtime rather than a
global Python installation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import time
from pathlib import Path

ENGINE_ID = "paddleocr-ppocrv6-tiny"
ENGINE_VERSION = "3.7.0"
DET_MODEL = "PP-OCRv6_tiny_det"
REC_MODEL = "PP-OCRv6_tiny_rec"


def emit(value: dict) -> None:
    sys.stdout.write(json.dumps(value, ensure_ascii=False, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def cpu_seconds() -> float:
    usage = os.times()
    return float(usage.user + usage.system)


def peak_working_set_bytes() -> int:
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes

        class ProcessMemoryCounters(ctypes.Structure):
            _fields_ = [
                ("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD),
                ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t),
            ]
        counters = ProcessMemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        get_memory_info = ctypes.windll.psapi.GetProcessMemoryInfo
        get_memory_info.argtypes = (wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD)
        get_memory_info.restype = wintypes.BOOL
        current_process = ctypes.windll.kernel32.GetCurrentProcess
        current_process.restype = wintypes.HANDLE
        if get_memory_info(current_process(), ctypes.byref(counters), counters.cb):
            return int(counters.PeakWorkingSetSize)
    try:
        import resource
        maximum = int(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
        return maximum if os.name == "posix" and sys.platform == "darwin" else maximum * 1024
    except Exception:
        return 0


def configure_environment(cache_root: Path) -> None:
    # Must happen before importing PaddleX/PaddleOCR.  The package never uses
    # a user profile cache or asks the library to contact a model host.
    os.environ["PADDLE_PDX_CACHE_HOME"] = str(cache_root)
    os.environ["PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK"] = "True"
    os.environ["PADDLE_PDX_ENABLE_MKLDNN_BYDEFAULT"] = "False"
    os.environ["PADDLE_PDX_DISABLE_DEVICE_FALLBACK"] = "True"
    os.environ.setdefault("PADDLE_PDX_CPU_NUM_THREADS", "1")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def model_tree_sha256(cache_root: Path) -> str:
    files = sorted(path for path in cache_root.rglob("*") if path.is_file())
    if not files:
        raise RuntimeError("PaddleOCR model cache is empty")
    digest = hashlib.sha256()
    for path in files:
        relative = path.relative_to(cache_root).as_posix()
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(sha256_file(path).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def safe_relative_path(value: object, label: str) -> Path:
    candidate = Path(str(value or "").strip())
    if not candidate.parts or candidate.is_absolute() or ".." in candidate.parts:
        raise RuntimeError(f"PaddleOCR manifest has an unsafe {label}")
    return candidate


def require_sha256(value: object, label: str) -> str:
    result = str(value or "").strip().lower()
    if len(result) != 64 or any(character not in "0123456789abcdef" for character in result):
        raise RuntimeError(f"PaddleOCR manifest is missing a valid {label} SHA-256")
    return result


def verify_manifest(cache_root: Path, manifest_path: Path) -> None:
    """Verify the package-controlled adapter, interpreter and complete model tree.

    This runs only in the child worker, before Paddle imports or inference. It
    keeps expensive hashing off the desktop/UI thread and prevents an explicit
    development override from weakening the release package contract.
    """
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    engine = manifest.get("engine", {})
    models = manifest.get("models", {})
    runtime = manifest.get("runtime", {})
    worker = manifest.get("worker", {})
    if (manifest.get("schemaVersion") != 1
            or engine.get("id") != ENGINE_ID
            or engine.get("version") != ENGINE_VERSION
            or engine.get("upstreamRepository") != "https://github.com/PaddlePaddle/PaddleOCR"
            or len(str(engine.get("upstreamCommit", ""))) != 40
            or engine.get("license") != "Apache-2.0"
            or models.get("detection") != DET_MODEL
            or models.get("recognition") != REC_MODEL
            or runtime.get("delivery") != "bundled-isolated-python"
            or runtime.get("automaticDownload") is not False
            or worker.get("relativePath") != "paddle_ocr_worker.py"):
        raise RuntimeError("PaddleOCR manifest is incompatible with the pinned worker")
    root = manifest_path.resolve().parent
    expected_worker = (root / safe_relative_path(worker.get("relativePath"), "worker path")).resolve()
    expected_python = (root / safe_relative_path(runtime.get("pythonRelativePath"), "python path")).resolve()
    expected_models = (root / safe_relative_path(models.get("cacheLayout"), "model cache path")).resolve()
    actual_models = (cache_root.resolve() / "official_models").resolve()
    if expected_worker != Path(__file__).resolve() or expected_python != Path(sys.executable).resolve():
        raise RuntimeError("PaddleOCR runtime files do not match the package manifest location")
    if expected_models != actual_models:
        raise RuntimeError("PaddleOCR model cache location does not match the package manifest")
    if sha256_file(expected_worker) != require_sha256(worker.get("sha256"), "worker"):
        raise RuntimeError("PaddleOCR worker checksum does not match the package manifest")
    if sha256_file(expected_python) != require_sha256(runtime.get("pythonSha256"), "python runtime"):
        raise RuntimeError("PaddleOCR Python checksum does not match the package manifest")
    if model_tree_sha256(cache_root.resolve()) != require_sha256(models.get("treeSha256"), "model tree"):
        raise RuntimeError("PaddleOCR model tree checksum does not match the package manifest")


def ensure_models(cache_root: Path) -> None:
    root = cache_root / "official_models"
    required = (
        root / DET_MODEL / "inference.yml",
        root / DET_MODEL / "inference.pdiparams",
        root / REC_MODEL / "inference.yml",
        root / REC_MODEL / "inference.pdiparams",
    )
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise RuntimeError("Missing verified packaged PaddleOCR model files: " + ", ".join(missing))


def make_engine():
    from paddleocr import PaddleOCR

    return PaddleOCR(
        text_detection_model_name=DET_MODEL,
        text_recognition_model_name=REC_MODEL,
        use_doc_orientation_classify=False,
        use_doc_unwarping=False,
        use_textline_orientation=False,
        text_rec_score_thresh=0.0,
        # Paddle 3.3 Windows CPU can raise a oneDNN PIR conversion error.
        enable_mkldnn=False,
    )


def recognize(engine, frame_path: Path) -> tuple[str, float]:
    texts: list[str] = []
    scores: list[float] = []
    for result in engine.predict_iter(str(frame_path)):
        result_texts = result.get("rec_texts", [])
        result_scores = result.get("rec_scores", [])
        for index, raw_text in enumerate(result_texts):
            text = str(raw_text).strip()
            if not text:
                continue
            texts.append(text)
            try:
                scores.append(float(result_scores[index]))
            except (IndexError, TypeError, ValueError):
                scores.append(0.0)
    if not texts:
        return "", 0.0
    return " ".join(texts), sum(scores) / len(scores)


def health(cache_root: Path, manifest_path: Path) -> int:
    verify_manifest(cache_root, manifest_path)
    configure_environment(cache_root)
    ensure_models(cache_root)
    import paddleocr

    emit({"ok": True, "engineId": ENGINE_ID, "engineVersion": ENGINE_VERSION,
          "paddleocrVersion": getattr(paddleocr, "__version__", "unknown"),
          "manifestVerified": True})
    return 0


def run(request_path: Path, response_path: Path, cache_root: Path, manifest_path: Path) -> int:
    started = time.perf_counter()
    cpu_started = cpu_seconds()
    verify_manifest(cache_root, manifest_path)
    configure_environment(cache_root)
    ensure_models(cache_root)
    request = json.loads(request_path.read_text(encoding="utf-8"))
    if request.get("schemaVersion") != 1 or request.get("engineId") != ENGINE_ID:
        raise RuntimeError("Unsupported PaddleOCR request contract")
    frames = request.get("frames")
    if not isinstance(frames, list) or not frames:
        raise RuntimeError("PaddleOCR request did not contain cropped frames")
    engine = make_engine()
    results: list[dict] = []
    for item in frames:
        frame_hash = str(item.get("hash", "")).strip().lower()
        path = Path(str(item.get("path", "")))
        if not frame_hash or not path.is_file():
            raise RuntimeError("PaddleOCR request contains an unreadable frame")
        text, confidence = recognize(engine, path)
        results.append({"hash": frame_hash, "text": text, "confidence": confidence})
    payload = {"schemaVersion": 1, "engineId": ENGINE_ID, "engineVersion": ENGINE_VERSION,
               "manifestVerified": True,
               "results": results,
               "telemetry": {"elapsedMs": round((time.perf_counter() - started) * 1000),
                             "cpuSeconds": round(cpu_seconds() - cpu_started, 3),
                             "peakWorkingSetBytes": peak_working_set_bytes()}}
    response_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = response_path.with_suffix(response_path.suffix + ".tmp")
    temporary.write_text(json.dumps(payload, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    os.replace(temporary, response_path)
    emit({"ok": True, "count": len(results)})
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache-root", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--health", action="store_true")
    parser.add_argument("--request", type=Path)
    parser.add_argument("--response", type=Path)
    args = parser.parse_args()
    try:
        if args.health:
            return health(args.cache_root, args.manifest)
        if not args.request or not args.response:
            raise RuntimeError("--request and --response are required for recognition")
        return run(args.request, args.response, args.cache_root, args.manifest)
    except Exception as error:  # a stable machine-readable process boundary
        emit({"ok": False, "error": f"{type(error).__name__}: {error}"})
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
