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

## 2026-08-15 - Per-task Colab upload visibility fix

- The per-task Colab artifact panel was moved from the narrow left task shelf
  into the active task review pane. It is now visible beside the result for
  Isolator, STT/Translate, TTS, Mix, and Export instead of requiring the user
  to discover a secondary scroll position.
- The Isolator panel accepts exactly `vocals.wav` and `background.wav`; the
  upload button remains disabled while the task is processing.
- Build and full CTest were rerun after the QML change: **39/39 passed**.

## 2026-08-15 - completed: direct upload for all Dubbing task outputs

### Delivered

- Added **Upload completed output** directly to Task Controls for every
  Dubbing result-producing task. It no longer requires the user to open
  **Show task result**, and it is not limited to voice isolation.
- The upload dialog lists the exact Colab folder, filename(s), and permitted
  format before opening the file picker. The accepted handoffs are Normalize
  (`normalized.wav`), Isolator (`vocals.wav` + `background.wav`), STT/OCR
  (`transcript.srt`/`ocr.srt`), review (`reviewed-transcript.srt`), Translate
  (`translated.srt`), TTS (`voice.wav`), Alignment (`timed-voice.wav`), Mix
  (`mix.wav`), and Export (`final.mp4`).
- Controller import follows the actual Dubbing state machine: files are placed
  in the active project cache and become the working audio, cue list, dubbed
  voice, preview, or final export consumed by the next real step. Names,
  extension allow-lists, cue validity, and the two-stem Isolator pair are
  validated before state advances.

### Evidence

- Rebuilt the MSVC test target with Qt 6.9.3 and ran full CTest:
  **39/39 passed, 0 failed**. `TestDubbingProject` covers each direct stage
  contract and verifies the hand-uploaded stereo voice bed passes through the
  real mixer as mono output.
- QML compilation/cache generation succeeded; QML lint exited 0 and reported
  only pre-existing unused-import/callback-property warnings. `git diff
  --check` and `graphify update .` were run.

### Boundary

No visible desktop GUI or live Colab worker was opened. This establishes the
controller/QML and artifact-handoff contract, but a user still needs to test
their own temporary Colab URL and files. No EXE was packaged for this
source-only request.

## 2026-08-15 - completed: internal package 0.0.7.1

### Delivered

- Packaged the current `main` source as the portable internal EXE
  `out/LA-Studio-0.0.7.1/LA-Studio-0.0.7.1.exe`.
- Source `LASTUDIO_VERSION`, FileVersion and ProductVersion were all verified
  as `0.0.7.1`.

### Evidence

- EXE SHA-256:
  `435BA385480DB098D3CCFB1BA7AEBDB0DB188C34C4B76C5E787C71D3EF455DDE`.
- Runtime staging inventory: **19/19** required artifacts, including Qt
  `qwindows`/`qoffscreen`, Runtime Host, FFmpeg/FFprobe, yt-dlp, Subtitle OCR,
  eSpeak and notices/licenses. Staged FFmpeg, FFprobe and yt-dlp launch checks
  passed.
- Shipped EXE headless QML smoke passed with exit `0` and generated a
  **19-action** Dubbing trace. The preceding source batch had full CTest
  **39/39 pass** and QML build/lint validation.

### Boundary

No visible desktop GUI or live Colab worker was started. The build is internal
only because its hash-verified eSpeak payload is unsigned.

## 2026-08-15 — completed: generalized Dubbing manual Colab handoff

### Delivered

- Applied the same handoff rule to every Dubbing task that produces an output,
  not just Isolator. Automatic worker transfer remains normal; a file selection
  is harmless until **Use uploaded output and continue** confirms it.
- Confirming valid output during the matching active transfer stops only that
  transfer and advances the next Dubbing task. A different running task cannot
  be cancelled by the current panel.
- Isolator has distinct required `vocals.wav` and `background.wav` inputs and
  documents `/content/la-studio-separation-jobs/<model-id>/<job-id>/`.
  `source.wav` is input-only. Other stages validate their own normalized WAV,
  subtitle/cue, TTS/timing/mix WAV, or final export contracts.
- Repaired the CTest OCR frame-runtime fixture by using .NET SHA-256 instead of
  an auto-loaded PowerShell `Get-FileHash` cmdlet.

### Evidence

- Targeted `TestDubbingProject`: **1/1 passed**.
- Full CTest: **39/39 passed** in **57.70 s**.
- QML lint exit 0 with only existing warnings; `git diff --check` passed.
- `graphify update .` was attempted but Graphify is not installed/on PATH, so
  no graph update is claimed.

### Boundary

No visible GUI or live Colab worker was launched. This is controller/QML/test
evidence; no EXE was packaged for the current source-only batch.
