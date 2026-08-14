#!/usr/bin/env python3
"""Generate a secret-free live-acceptance configuration for every exact worker.

The output is a template only: URLs and bearer tokens are referenced through
unique environment-variable names and are never written to the file.  Run one
notebook at a time, set the matching two variables, and pass --only to
run_live_colab_acceptance.py for that exact capability/model pair.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
NOTEBOOKS = ROOT / "notebooks"


def normalized_environment_part(value: str) -> str:
    return re.sub(r"[^A-Z0-9]+", "_", value.upper()).strip("_")


def environment_name(capability: str, model: str, suffix: str) -> str:
    return "LASTUDIO_LIVE_{}_{}_{}".format(
        normalized_environment_part(capability),
        normalized_environment_part(model),
        suffix,
    )


def defaults_for(capability: str, model: str) -> dict[str, Any]:
    """Return a safe, small request accepted by the live runner."""
    if capability == "stt":
        return {
            "audio_path": "REPLACE_WITH_SHORT_SPEECH_AUDIO.wav",
            "language": "en",
            "job_timeout_seconds": 900,
        }
    if capability == "tts":
        voices = {
            "kokoro": ("af_heart", "en"),
            "kokoro-vietnamese": ("diem_trinh", "vi"),
            "qwen3-tts-1.7b-customvoice": ("Aiden", "en"),
            "vibevoice": ("carter", "en"),
            "vieneu-tts-v2-turbo": ("Phạm Tuyên", "vi"),
            "vieneu-tts-v3-turbo": ("Phạm Tuyên", "vi"),
        }
        voice, language = voices.get(model, ("auto", "auto"))
        return {
            "text": "LA Studio live model validation.",
            "voice": voice,
            "language": language,
        }
    if capability == "translation":
        return {
            "source_language": "en",
            "target_language": "vi",
            "text": "LA Studio live model validation.",
        }
    if capability == "llm-chat":
        return {"prompt": "Reply with the single word ready.", "max_tokens": 16}
    if capability == "voice-design":
        return {
            "text": "LA Studio live model validation.",
            "voice_description": "A calm studio narrator.",
            "language": "en",
        }
    if capability == "forced-alignment":
        return {
            "audio_path": "REPLACE_WITH_SHORT_SPEECH_AUDIO.wav",
            "transcript": "hello world",
            "language": "en",
        }
    if capability == "voice-isolation":
        return {"audio_path": "REPLACE_WITH_SHORT_MUSIC_AUDIO.wav", "job_timeout_seconds": 900}
    if capability == "voice-cloning":
        return {
            "audio_path": "REPLACE_WITH_SHORT_REFERENCE_AUDIO.wav",
            "reference_text": "Xin chào, đây là kiểm tra giọng nói.",
            "text": "LA Studio live voice cloning validation.",
            "language": "vi",
            "job_timeout_seconds": 900,
        }
    if capability == "subtitle-ocr":
        return {
            "image_path": "REPLACE_WITH_SHORT_SUBTITLE_CROP.png",
            "language": "en",
        }
    raise ValueError(f"Unsupported capability in notebook metadata: {capability}")


def workers_from_notebooks() -> list[dict[str, Any]]:
    workers: list[dict[str, Any]] = []
    for notebook in sorted(NOTEBOOKS.glob("*.ipynb")):
        data = json.loads(notebook.read_text(encoding="utf-8"))
        metadata = data.get("metadata", {}).get("la_studio", {})
        capability = str(metadata.get("capability", "")).strip().lower()
        model = str(metadata.get("family_id", "")).strip().lower()
        if not capability or not model:
            continue
        worker: dict[str, Any] = {
            "capability": capability,
            "model": model,
            "notebook": notebook.name,
            "url_env": environment_name(capability, model, "URL"),
            "token_env": environment_name(capability, model, "TOKEN"),
            "request_timeout_seconds": 180,
        }
        worker.update(defaults_for(capability, model))
        workers.append(worker)
    return workers


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True,
                        help="JSON template path to create or compare")
    parser.add_argument("--check", action="store_true",
                        help="Fail if --output differs from the generated template")
    args = parser.parse_args()

    payload = {"workers": workers_from_notebooks()}
    rendered = json.dumps(payload, ensure_ascii=False, indent=2) + "\n"
    if args.check:
        if not args.output.is_file() or args.output.read_text(encoding="utf-8") != rendered:
            print(f"Live acceptance template is stale: {args.output}")
            return 1
        print(f"Live Colab acceptance template verified: {len(payload['workers'])} workers.")
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    print(f"Live Colab acceptance template generated: {args.output} ({len(payload['workers'])} workers).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
