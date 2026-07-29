#!/usr/bin/env python3
"""Fail when UI/controller/notebook exact-model bindings drift apart.

This is deliberately stricter than a file-exists check.  A GPU request can be
correct only when the model chosen in the UI is mapped by its controller to the
same notebook listed in the live acceptance manifest.  It also verifies that
controllers reject unmapped models and check the active Colab capability/model
route before dispatching work.
"""

from __future__ import annotations

import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = ROOT / "docs" / "LIVE_COLAB_ACCEPTANCE_TEMPLATE.json"


@dataclass(frozen=True)
class Binding:
    capability: str
    controller: Path
    page: Path
    controller_qml_name: str


BINDINGS = (
    Binding("stt", Path("src/controllers/stt/SttSessionController.cpp"), Path("qml/pages/SttPage.qml"), "AppController.sttSession"),
    Binding("tts", Path("src/controllers/tts/ColabTtsController.cpp"), Path("qml/pages/TtsPage.qml"), "AppController.colabTts"),
    Binding("voice-cloning", Path("src/controllers/tts/ColabVoiceCloneController.cpp"), Path("qml/pages/VoiceCloningPage.qml"), "AppController.colabVoiceClone"),
    Binding("voice-design", Path("src/controllers/tts/ColabVoiceDesignController.cpp"), Path("qml/pages/VoiceDesignPage.qml"), "AppController.colabVoiceDesign"),
    Binding("forced-alignment", Path("src/controllers/alignment/ColabAlignmentController.cpp"), Path("qml/pages/AlignmentPage.qml"), "AppController.colabAlignment"),
    Binding("voice-isolation", Path("src/controllers/separation/ColabVoiceIsolatorController.cpp"), Path("qml/pages/VoiceIsolatorPage.qml"), "AppController.colabVoiceIsolator"),
    Binding("translation", Path("src/controllers/translation/TranslationController.cpp"), Path("qml/pages/TranslationPage.qml"), "AppController.translation"),
    Binding("llm-chat", Path("src/controllers/llm/LlmChatController.cpp"), Path("qml/pages/LlmPage.qml"), "AppController.llmChat"),
)


def read(relative: Path) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def function_body(source: str, function_name: str) -> str:
    match = re.search(rf"\b{re.escape(function_name)}\s*\([^)]*\)\s*const\s*\{{", source)
    if not match:
        raise ValueError(f"missing {function_name}()")
    start = match.end()
    depth = 1
    for offset, character in enumerate(source[start:], start):
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return source[start:offset]
    raise ValueError(f"unterminated {function_name}()")


def controller_pairs(source: str) -> set[tuple[str, str]]:
    body = function_body(source, "notebookForColabModel")
    pairs = set(re.findall(
        r'normalized\s*==\s*QStringLiteral\("([^"]+)"\)\s*\)\s*'
        r'return\s+QStringLiteral\("([^"]+\.ipynb)"\)',
        body,
    ))
    if not pairs:
        raise ValueError("notebookForColabModel() has no exact-model mappings")
    return pairs


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def main() -> int:
    errors: list[str] = []
    try:
        workers = json.loads(TEMPLATE.read_text(encoding="utf-8"))["workers"]
    except (OSError, KeyError, json.JSONDecodeError) as error:
        print(f"Cannot read live acceptance template: {error}", file=sys.stderr)
        return 2

    template_pairs = {
        binding.capability: {
            (item["model"].strip().lower(), item["notebook"])
            for item in workers if item.get("capability") == binding.capability
        }
        for binding in BINDINGS
    }
    total = 0
    for binding in BINDINGS:
        try:
            source = read(binding.controller)
            mapped_pairs = controller_pairs(source)
        except (OSError, ValueError) as error:
            fail(errors, f"{binding.capability}: cannot read controller mapping: {error}")
            continue

        total += len(mapped_pairs)
        expected_pairs = template_pairs[binding.capability]
        if mapped_pairs != expected_pairs:
            missing = sorted(expected_pairs - mapped_pairs)
            extra = sorted(mapped_pairs - expected_pairs)
            if missing:
                fail(errors, f"{binding.capability}: controller lacks manifest bindings: {missing}")
            if extra:
                fail(errors, f"{binding.capability}: controller has stale bindings: {extra}")

        if "notebookForColabModel(normalized).isEmpty()" not in source:
            fail(errors, f"{binding.capability}: controller does not reject unmapped models")
        route_check = f'hasVerifiedRoute(QStringLiteral("{binding.capability}")'
        if route_check not in source:
            fail(errors, f"{binding.capability}: dispatch does not verify its exact active Colab route")

        page_source = read(binding.page)
        if f"{binding.controller_qml_name}.selectColabModel(familyId)" not in page_source:
            fail(errors, f"{binding.capability}: page does not select the controller model before opening Colab")
        if f"{binding.controller_qml_name}.colabNotebookFile" not in page_source:
            fail(errors, f"{binding.capability}: page does not use the controller's exact notebook file")
        if "ColabNotebookUrls.forNotebookFile" not in page_source:
            fail(errors, f"{binding.capability}: page bypasses the shared Colab URL helper")

        for model, notebook in mapped_pairs:
            notebook_path = ROOT / "notebooks" / notebook
            if not notebook_path.is_file():
                fail(errors, f"{binding.capability}/{model}: mapped notebook is missing: {notebook}")
                continue
            try:
                notebook_json = json.loads(notebook_path.read_text(encoding="utf-8"))
                notebook_source = "\n".join(
                    "".join(cell.get("source", []))
                    for cell in notebook_json.get("cells", [])
                    if isinstance(cell, dict)
                ).lower()
            except (OSError, json.JSONDecodeError) as error:
                fail(errors, f"{binding.capability}/{model}: unreadable notebook: {error}")
                continue
            if model not in notebook_source:
                fail(errors, f"{binding.capability}/{model}: notebook does not declare its exact model")

    manifest_total = sum(len(pairs) for pairs in template_pairs.values())
    if total != manifest_total:
        fail(errors, f"binding cardinality differs: controllers={total}, manifest={manifest_total}")
    if len({(item.get("capability"), item.get("model")) for item in workers}) != len(workers):
        fail(errors, "live acceptance manifest contains duplicate capability/model workers")

    if errors:
        print("Exact Colab model binding audit failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print(f"Exact Colab model bindings verified: {total}/{manifest_total} controller/UI/notebook routes.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
