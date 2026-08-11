# AI agent response — Dubbing workspace and Subtitle OCR bootstrap

Date: 2026-08-11

## Completed

- Reduced Automatic Dubbing to the eight user-facing stages: **Import/Download**, **Normalize**, **Isolator**, **Transcribe/STT**, **Alignment/Subtitle**, **Translate**, **TTS**, and **Export/Output**. Internal timing, translation-review, and mix nodes remain in their owning visible stage; they are not extra header stages.
- The header now takes its stage list from the same controller contract as preflight, preventing a QML-only nine-stage list from drifting from the backend. At compact widths its action cluster uses icon-only controls with accessible labels/tooltips, so labels such as `Workflow` cannot be clipped to `Wor`.
- Kept Dubbing as a fixed-pane editor: the left task shelf and the right inspector are layout items that consume width and push the center workspace. The task rail is the only header area that scrolls. The preview defaults to 1040 px and the timeline to 340 px; the existing video aspect and focus controls remain available.
- Language pair and Fast/Adaptive/Custom quality are chosen in the existing project-setup dialog after Automatic or step-by-step is chosen. They are not a permanent strip beneath the editor.
- Regenerated the Subtitle OCR notebook. Bootstrap revision is now `subtitle-ocr-bootstrap-2026-08-11.8`; it does not call `venv.EnvBuilder` or `ensurepip`. It isolates its pinned OCR/Paddle stack under the app-owned `/content/la_studio_subtitle_ocr_site` directory.

## Validation evidence

- Python generator compilation: PASS.
- Generated-notebook verifier: **32/32 PASS**.
- `git diff --check`: PASS.
- `graphify update .`: PASS.
- Full project CTest with the project-local Qt 6.9.3 SDK: **39/39 PASS** in 57.04 seconds.

## Boundary

No visible desktop GUI, user session, live Colab worker, or new EXE package was opened or created. The notebook fix is source-level and generated-notebook validated; run the current tracked `notebooks/LA_STUDIO_SUBTITLE_OCR_PP_OCRV5_GPU.ipynb` in a **fresh** Colab runtime. A traceback showing `venv.EnvBuilder(... ensurepip ...)` identifies an older notebook copy, not revision `.8`.
