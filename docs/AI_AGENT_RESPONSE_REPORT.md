# AI agent response - 0.0.2.22 packaged

Date: 2026-08-04

## Completed repair

- Automatic Dubbing previously evaluated readiness before it offered a media
  ingest path. That made missing media look like generic downstream `Blocked`
  state, while visible Configure buttons for non-configurable nodes were dead.
- Page 1 now has production Browse and URL import paths wired to the existing
  Dubbing ingest controller. Preflight supplies actionable states and actions:
  `Needs input`, `Needs setup`, `Blocked by previous stage`, `Ready`, and
  `No configuration required`. Review Fix returns to the correct page/control.
- Production-shell offscreen Dubbing interaction trace:
  `out/build/windows-msvc-tests/dubbing-qml-interaction-trace.json` records 15
  ordered UI/controller actions from entry gate through Review. Native file
  selection is injected only at the production file-picker boundary; no GUI,
  model workload or live Colab worker was opened.
- Subtitle OCR route smoke was then fixed. Its URL field correctly enabled
  Import, but `activeFocus` was false because `QT_QPA_PLATFORM=offscreen` has
  no native active window. The check now verifies portable QML local focus,
  while retaining desktop active-focus compatibility.

## Validation and package

- QML lint: PASS.
- Targeted QML route smoke: PASS.
- Full CTest: **39/39 PASS** (70.78 seconds).
- Source version check: `v0.0.2.22` matches `LASTUDIO_VERSION 0.0.2.22`.
- Portable internal package audit: PASS for staging and license manifests,
  Qt Windows/offscreen plugins, FFmpeg/FFprobe, Tesseract, Paddle OCR and
  internal eSpeak runtime. The unsigned eSpeak MSI is SHA-verified and remains
  internal-only.

## Artifact and Git

- EXE: `out/LA-Studio-0.0.2.22/LA-Studio-0.0.2.22.exe`
- FileVersion/ProductVersion: `0.0.2.22` / `0.0.2.22`
- SHA-256: `E71F98802368577B16B28EDAAE807A70216ADD6A7CFE5DCE611A055D993CE2E4`
- Source/tests committed and pushed directly to `main`: `355be6c`
  (`fix: repair dubbing preflight and OCR smoke`).

Manual desktop interaction and live Colab execution remain separate user-side
acceptance checks; neither is claimed by the offscreen or package evidence.
