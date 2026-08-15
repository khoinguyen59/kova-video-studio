# AI agent response — local download and resumable Dubbing projects

## 2026-08-14 — completed: package 0.0.6.9

### Delivered

- Replaced the media-download Colab worker route with a local CPU downloader.
  Download media now uses SHA-256-pinned `yt-dlp`; it does not ask for Colab
  URL/token, GPU, or API Gateway credentials.
- Accepted full public-share text and extracts its HTTPS media URL. A Netscape
  cookie file remains optional; LA Studio does not read browser cookies.
- Kept Colab only on actual model/GPU tasks. For media manually made in Colab,
  the UI tells the user to open the Colab **Files** sidebar, download the exact
  output printed by the notebook final cell, then select that local file.
- Added Dubbing project **New**, **Open**, **Save**, and **Save As**. Save As is
  atomic and reopening a `.ladub.json` restores project state for continued
  work, including recovery guidance for an interrupted workflow.

### Evidence

- Full CTest: **39/39 passed, 0 failed**, rebuilt with source version `0.0.6.9`.
- Generated exact-model notebooks: **32/32 passed**.
- Direct-Colab contract runner: **9 capability paths passed**.
- `graphify update .`, Python compilation, and `git diff --check`: passed.
- Internal portable package:
  `C:\Users\Nguyen Trong Khoi\Downloads\LA-STUDIO\out\LA-Studio-0.0.6.9\LA-Studio-0.0.6.9.exe`.
  FileVersion/ProductVersion are `0.0.6.9`; SHA-256 is
  `CDC449056C120B6F00CE562C24F163A44E49C1A88E1229438E5C37BF16B62361`.
  Required Qt, runtime-host, FFmpeg/FFprobe, `yt-dlp 2026.07.04`, and license
  artifacts were verified in the staged package.

### Boundary

No desktop GUI or live Colab session was opened. The successful tests prove
source, controller, offscreen-QML, generated-notebook, contract, and staged
runtime behavior. A user must still run a fresh Colab notebook to accept a
live temporary GPU worker. The package is internal-only because its verified
eSpeak payload is unsigned.

## 2026-08-15 - completed: project-first gate and package 0.0.7.0

### Delivered

- Added a global New/Open project gate before operational feature routes,
  including Dubbing. Settings and model browsing remain available before a
  project. Importing media through the controller now requires an existing
  project instead of creating one implicitly.
- Added explicit model chooser actions for local apply, independent Colab
  selection, and non-destructive close.

### Evidence

- Targeted Dubbing project regression: **98 passed, 0 failed, 5 skipped**.
- Offscreen QML route smoke: **3/3 passed**; full CTest: **39/39 passed**.
- QML lint exited 0 with existing warnings; `git diff --check` and
  `graphify update .` passed.
- Portable internal EXE:
  `C:\Users\Nguyen Trong Khoi\Downloads\LA-STUDIO\out\LA-Studio-0.0.7.0\LA-Studio-0.0.7.0.exe`
  (File/Product Version `0.0.7.0`, SHA-256
  `5B3B7876C80EC473C89A7ECC96E793545EEA3AAB19DAF6B98E5558C7EF88814E`).
  The staged package contains Qt/offscreen, runtime host, FFmpeg/FFprobe,
  yt-dlp, Tesseract, and the isolated PaddleOCR runtime. Package offscreen
  smoke exited 0.

### Boundary

Source commit `339cfa4` was pushed directly to `origin/main`. No visible GUI or
live Colab worker was used. This is an internal package only because the
hash-verified eSpeak payload is unsigned.

## 2026-08-15 - Dubbing OCR/Colab controls and per-task artifact handoff

### Delivered

- Transcribe/STT now exposes `STT`, `OCR`, and `STT + OCR`, an explicit Subtitle
  OCR model selector, a Local CPU/Colab GPU route, the exact notebook/model
  identity, and a **Set up OCR Colab** action in the Dubbing task controls and
  review panel. The selection is persisted through the workflow configuration.
- When `STT + OCR` is selected, the UI explains the source-language AI
  reconciliation stage before Translate. The request is gated until unresolved
  conflicts exist; review/accept/reject remains authoritative. Translate is
  documented as consuming the reviewed source transcript and its selected model.
- Added strict per-task output upload for isolation, STT/OCR, translation, TTS,
  mix, and export. Each task states its exact Colab folder and worker path,
  validates extensions and counts (isolation requires `vocals.wav` plus
  `background.wav`), imports subtitles/cues safely, and hands artifacts to the
  normal downstream step from the project cache.
- Wrapped the task controls in a vertical scroll area so OCR model/route and AI
  controls remain reachable in compact Dubbing layouts.

### Evidence

- Build succeeded: `out/build/windows-msvc-release/LA-Studio-0.0.7.0.exe`.
- Full CTest: **39/39 passed, 0 failed** (55.12 s), including strict artifact
  contract and Dubbing QML source regressions.
- QML parser/build validation passed. `qmllint` reports only the pre-existing
  unused-import and callback-property warnings; no Dubbing syntax error remains.
- `graphify update .` and `git diff --check` passed. No desktop GUI or live
  Colab worker was opened; evidence covers source/controller contracts,
  offscreen QML, build, and test fixtures.

### Boundary

No EXE package was created for this change because this request asked for the
Dubbing/OCR implementation and verification, not a new release package.
