# AI agent response - Dubbing panes and Subtitle OCR bootstrap

Date: 2026-08-11

## Delivered source change

Commit `8d256ce fix: stabilize dubbing layout and ocr colab bootstrap` is
pushed directly to `origin/main`.

- Dubbing now uses fixed layout panes instead of a horizontal offscreen canvas:
  optional history and task controls are left of the central preview, task
  output/review is on the right, and the Timeline spans the lower workspace.
  Selecting a task consumes layout space rather than covering the preview.
- The header has two rows. Utility actions remain visible while the task rail
  scrolls independently, so labels such as **Workflow** are not truncated.
  The history, preview and timeline resize handles use continuous drag handlers.
- Languages and execution quality are requested in a popup immediately after
  selecting Automatic or step-by-step mode, or through **Project settings**.
  They no longer occupy a permanent lower panel.
- The Subtitle OCR notebook replaces the failing stdlib `ensurepip` venv path
  with `virtualenv==20.31.2`, then preserves the isolated exact Paddle OCR
  dependency stack. The generated verifier rejects the old bootstrap path.

## Evidence

- Generated exact-model notebook verification: **32/32 passed**.
- Fresh virtualenv bootstrap smoke: **passed** (isolated interpreter and pip).
- Changed Dubbing QML parser check: **passed**.
- Source contract checks and `git diff --check`: **passed**.

## Validation boundary

Full CTest, a desktop build, package creation, and live Colab acceptance were
not run for this batch. The test script stops before configuring because the
current machine has no Qt development kit (`Qt6Config.cmake` / `LA_QT` is
unavailable). This is recorded as **blocked**, not a passing test result. No
GUI, browser, or live Colab worker was opened.

No new EXE was created; the latest packaged candidate remains `0.0.6.3`.
