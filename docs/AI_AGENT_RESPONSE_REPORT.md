# AI agent response - Dubbing pane contract and Subtitle OCR bootstrap

Date: 2026-08-11

## Delivered source change

Commit `5dddf99 fix: stabilize dubbing panes and OCR Colab runtime` is pushed
directly to `origin/main`.

- Dubbing keeps its CapCut-style editor order in one real layout: optional task
  controls on the left, a central preview, a task review/inspector on the
  right, and a full-width Timeline below. The shelf and preview have separate
  drag handles; the preview is larger by default.
- The QML smoke route now rejects four concrete geometry failures: shelf over
  its handle, shelf over preview, preview over its handle, and preview over
  the review pane. This prevents feature panels from being painted over the
  video instead of consuming layout space.
- Header utility actions live in a dedicated horizontally scrollable strip, so
  **Generate Final Dubbing**, **Colab setup**, and **Workflow** retain their
  complete labels rather than becoming clipped fragments. The obsolete hidden
  duplicate buttons were removed.
- The existing Project Setup dialog remains the only project-language and
  quality chooser after the user picks **Automatic** or **Step-by-step**;
  those choices no longer need a permanent lower panel.
- Subtitle OCR no longer creates a `venv` or invokes `ensurepip`. It installs
  the exact pinned OCR stack into `/content/la_studio_subtitle_ocr_site` and
  launches the worker with that directory first on `PYTHONPATH`. The preflight
  probe verifies Paddle loads from that dedicated directory, avoiding both the
  reported `ensurepip` failure and the mixed-Pillow `_Ink` import failure.

## Evidence

- Exact generated Colab notebooks: **32/32 verified** after regeneration.
- Subtitle OCR notebook source contract: **passed**; it contains no
  `venv.EnvBuilder`, `virtualenv`, or legacy virtual-environment worker path.
- Python generator compilation, changed QML parser checks, and `git diff
  --check`: **passed**.
- `graphify update .`: **completed** (AST update; no topology changes).

## Validation boundary

`run_tests.ps1 -Preset windows-msvc-release -NoBuild` remains blocked before
CTest because this machine has no Qt development kit (`LA_QT` / `Qt6Config.cmake`).
Consequently, no desktop build, package, GUI interaction, or live Colab worker
was claimed for this batch. The latest packaged candidate is still `0.0.6.3`.
