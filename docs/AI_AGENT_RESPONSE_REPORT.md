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

## 2026-08-11 — fixed-pane Dubbing release candidate 0.0.6.4

### Delivered

- Preserved the eight Dubbing stages and the CapCut-style fixed panes already
  implemented in the workspace: task controls consume the left width, preview
  stays in the centre, a task review panel consumes the right width only after
  a task is selected, and the timeline spans the full lower workspace. Panels
  are layout items rather than overlays.
- Project language pair and Fast/Adaptive/Custom quality remain in the modal
  project-setup flow after the user chooses Automatic or step-by-step; they are
  not a permanent lower editor strip.
- Added a runtime QML regression assertion for the header's fixed action
  cluster. At normal width it requires the full `Workflow` label; at compact
  width it requires intentional icon-only controls with tooltips. This prevents
  a future regression from showing a clipped fragment such as `Wor` or letting
  the workflow rail overlap actions.
- Reissued the Subtitle OCR notebook as bootstrap revision
  `subtitle-ocr-bootstrap-2026-08-11.9`. It removes the legacy app venv before
  bootstrapping a dedicated `--target` package directory, never invokes
  `venv.EnvBuilder` or `ensurepip`, and keeps the pinned OCR/Paddle import
  checks. A Colab trace containing `VENV_DIR`/`ensurepip` is therefore a stale
  notebook copy and must be replaced with this tracked `.9` notebook in a
  fresh runtime.
- Bumped the four-field internal build version to `0.0.6.4` and changed the
  version regression from a stale literal to the required four-single-digit
  version invariant.

### Evidence

- QML lint: PASS.
- Generated exact-model notebooks: **32/32 PASS**.
- Python notebook generator compile and `git diff --check`: PASS.
- Headless full CTest, including the offscreen QML route smoke:
  **39/39 PASS** in 59.24 seconds.
- `graphify update .`: PASS.
- Portable internal package created at
  `out/LA-Studio-0.0.6.4/LA-Studio-0.0.6.4.exe`; package staging verified
  **19 required runtime artifacts**. SHA-256:
  `2367C0A735D20F6692C2EF6BCCF3EF22F097188C7C3C92175C6A728F3F0EC5EF`.
- The packaged OCR notebook exists at
  `out/LA-Studio-0.0.6.4/docs/colab-notebooks/LA_STUDIO_SUBTITLE_OCR_PP_OCRV5_GPU.ipynb`
  and contains revision `.9`.

### Boundary

No visible desktop app, interactive user session, or live Colab GPU worker was
opened for this run. The package is internal-only because its hash-verified
eSpeak payload is unsigned; it must not be promoted as a public distributable
release without signing remediation. All assertions above are source, offscreen
QML, generated-notebook, and staged-payload evidence.
