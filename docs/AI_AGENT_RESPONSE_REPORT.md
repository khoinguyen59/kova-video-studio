# AI agent response - compact Dubbing controls and isolated Subtitle OCR stack

Date: 2026-08-11

## Delivered source change

Commit `771dcf3 fix: adapt dubbing panes and harden OCR Colab` is pushed
directly to `origin/main`.

- Dubbing now has an explicit compact-workbench rule.  When the available
  editor width is below 1000 px, the left task shelf is removed from layout
  rather than clipped or painted over the preview.  The same node Run and
  Configure controls move into the already-visible review pane, whose compact
  minimum width is 240 px.  The normal wide layout keeps the resizable left
  shelf, central preview, right review pane, and full-width Timeline.
- The QML smoke contract now rejects a right review pane that extends outside
  the Dubbing workspace in addition to the earlier shelf/preview overlap
  checks.  This covers the narrow-window failure mode rather than merely
  checking that a hidden offscreen shelf exists in source.
- Project language pair and execution quality remain in the Project Setup
  dialog immediately after choosing Automatic or Step-by-step, or when the
  user deliberately reopens Project settings.  They are not restored as a
  permanent bottom panel.
- The Subtitle OCR notebook now installs every OCR dependency with
  `--ignore-installed` into its dedicated
  `/content/la_studio_subtitle_ocr_site` directory.  Its probe requires
  Pillow, Paddle, PaddleOCR, and PaddleX to all resolve from that one
  directory before it starts the CUDA worker.  This prevents mixed global and
  target package files such as a new `ImageText.py` with an old `PIL._typing`.

## Evidence

- QML parser check: passed for the changed Dubbing page and workflow header.
- Compact-layout source contract: passed, including the compact control owner,
  the conditional review width, and the workspace-boundary guard.
- Exact generated Colab notebooks: **32/32 verified** after regeneration.
- OCR dedicated-package source contract and Python generator compilation:
  passed.  The notebook contains neither `venv.EnvBuilder` nor `virtualenv`.
- `git diff --check` and `graphify update .`: passed.  Graphify reported no
  code-graph topology change; it also reported its pre-existing SQL parser and
  zero-node-source warnings.

## Validation boundary

`run_tests.ps1 -Preset windows-msvc-release -NoBuild` is blocked before CTest:
this machine has no Qt development kit (`LA_QT` / `Qt6Config.cmake`).  Therefore
there is no claim of a full CTest pass, desktop build, EXE package, visible GUI
acceptance, or live Colab acceptance for commit `771dcf3`.  The newest existing
package remains `0.0.6.3` and does not contain this source-only change.
