# AI agent response — local download and resumable Dubbing projects

## 2026-08-22 — completed: internal portable package 0.0.7.6

### Delivered

- Built the requested portable internal EXE at
  `out/LA-Studio-0.0.7.6/LA-Studio-0.0.7.6.exe`.
- Source version, FileVersion, and ProductVersion match `0.0.7.6`.
  SHA-256: `2BBA1C68B321C47693FD9B60A3BAD73C070D17FC292D9AF34E1D41278EAB4A87`.
- The portable staging checks completed. Post-package audit verified both
  `qwindows.dll` and `qoffscreen.dll`, the bundled OCR manifest, Tesseract
  `5.5.1`, and the staged license directory (176 files).

### Boundary

The app GUI was not opened and no new live Colab session was run for this
package. This confirms the package layout and versioned binary, not a new
remote-GPU acceptance run.

## 2026-08-22 — completed: Dubbing recheck, no new package

### Fixed in source

- A zero-sample STT decode now ends the Dubbing job with a visible error and
  does not contact the remote worker. This removes the previously possible
  blocked task after "No audio data was decoded."
- Manual **Run STT now** and **Run Subtitle OCR now** can operate independently
  and concurrently because they use distinct workers. **Reconcile saved STT +
  OCR** remains a local-only final action and requires both saved sources.
- The static remote-feature verifier now follows Spleeter's real signed
  notebook/worker/launcher architecture instead of falsely demanding the
  launcher tunnel text in the bootstrap notebook itself.

### Evidence and boundary

- Release build without deployment succeeded; CTest **39/39**; generated
  notebooks **32/32**; exact bindings **31/31**; live acceptance contracts
  **9/9**; remote feature surface **8/8**; `git diff --check` clean. QML lint
  completed with exit 0 and five existing warnings outside this change.
- No visible GUI, no machine-control interaction, no fresh Colab worker, and
  no EXE package were produced for this recheck. A fresh URL/token plus real
  media is still required for external acceptance; this report does not claim
  that unrun Colab infrastructure was tested live.

## 2026-08-17 — completed: independent Dubbing STT/OCR control (0.0.7.5)

### What changed

- **STT**, **Subtitle OCR**, and **Reconcile saved STT + OCR** are now explicit
  Dubbing actions in the task shelf.
- The selected next action no longer determines whether either Colab worker can
  be selected, connected, or verified. STT and OCR retain independent model,
  route, notebook, URL, token, and connection state.
- Reconciliation is local-only. It never starts, configures, disconnects, or
  disables either worker; it is the final action after both saved results exist.
- A manual STT job no longer locks OCR setup. Only an Automatic Dubbing run
  freezes shared OCR scan/route settings.

### Verification and operator flow

- Full CTest: **39/39 passed** in 61.13 seconds; Dubbing, OCR controller, and
  offscreen QML-route regressions passed. `graphify update .` and
  `git diff --check` passed.
- Configure/connect STT and use **Run STT now**. Configure/connect Subtitle OCR
  and use **Run Subtitle OCR now**. Once both results are saved, use
  **Reconcile saved STT + OCR**. Manual media jobs remain serialized, but either
  worker can be configured while the other runs.
- Internal portable package: `out/LA-Studio-0.0.7.5/LA-Studio-0.0.7.5.exe`.
  Source, FileVersion, and ProductVersion are all `0.0.7.5`; SHA-256:
  `736983215DBCB18EF299BAD8B69BD7BBA4C4BFD0707E2A721DA66FF5745EB189`.
  The package manifest verified 19 runtime and 18 license artifacts; staged
  FFmpeg, FFprobe, yt-dlp, and the application's offscreen QML smoke exited
  successfully.
- No visible desktop window or live Colab session was opened. A fresh Colab
  session remains the acceptance check for temporary worker URL/token.

## 2026-08-16 — completed: valid FLAC STT recovery and package 0.0.7.4

### Root cause and repair

- The live desktop log showed a valid Direct Colab `vocals.flac` (45.6 MB,
  44.1 kHz stereo, ~15 minutes) reaching the STT stage. The old standalone
  `QAudioDecoder` completed with **0 samples**, which produced “No audio data
  was decoded.” It was not an empty artifact.
- Replaced that path with the shared asynchronous decoder. It validates the
  file, decodes off the UI thread, and falls back to the packaged FFmpeg path
  when Qt cannot decode FLAC; the STT session receives mono 16 kHz PCM.
- STT, OCR, and reconciliation are now distinct Dubbing actions. Run STT or OCR
  independently; reconciliation consumes their completed saved transcripts and
  does not start either model.
- Updated the generated PP-OCRv5 Colab bootstrap with its missing compatibility
  `langchain` dependency. No live Colab session was run by this delivery.

### Verification and handoff

- CTest: **39/39 passed**. Exact-model notebook verifier: **32/32 passed**.
- Packaged offscreen QML smoke: exit `0`.
- Internal EXE: `out/LA-Studio-0.0.7.4/LA-Studio-0.0.7.4.exe`.
- SHA-256:
  `F179A90B98C6517EFE3017939F50F3F5D6B0D9068958C283C5F63512A6536555`.

### Boundary

The package has been checked without opening a visible window. A new Colab
runtime remains the user's acceptance step for its temporary GPU worker.

## 2026-08-16 — completed: FLAC Colab isolation transport and package 0.0.7.3

### Delivered

- Direct Colab Isolator now uses lossless FLAC by default for `vocals` and
  `background`, with WAV retained as an explicit compatibility selection.
- Updated Spleeter and UVR worker/notebook contracts to publish
  `artifact_format` and `artifacts_ready`. The app now visibly reports that
  Colab has created both stems before it starts downloading them.
- Moved large stem preview decode off the UI thread, rejects truncated FLAC
  before decode, and preserves both new FLAC and old WAV Voice Clone cache
  files.
- Built portable internal EXE:
  `out/LA-Studio-0.0.7.3/LA-Studio-0.0.7.3.exe`.

### Evidence

- Source tests: **39/39 passed, 0 failed**; Python worker/generator compile
  passed.
- Package audit: **19/19 runtime** and **18/18 license** artifacts present.
- Source/FileVersion/ProductVersion: `0.0.7.3`.
- SHA-256:
  `AD99D4145471491FD36C623C1FFCA661DAD1ED5CFA0854B47F8DDD85A798E313`.
- Packaged offscreen QML smoke: exit `0`; FFmpeg, FFprobe and yt-dlp staged
  launch checks passed.

### Boundary

No visible desktop UI or live Colab runtime was started. The package is for
internal use because the verified eSpeak payload remains unsigned. Graphify was
attempted but its CLI is not available on PATH.

## 2026-08-15 — completed: package 0.0.7.2

### Delivered

- Built and staged the portable internal executable:
  `out/LA-Studio-0.0.7.2/LA-Studio-0.0.7.2.exe`.
- Source `LASTUDIO_VERSION`, FileVersion, and ProductVersion are all
  `0.0.7.2`.

### Evidence

- SHA-256:
  `CE196C06379490BBA22D0ACD6300F53A4EA3B6353F0C7A4F715D649B59C514ED`.
- Package audit found all required runtime items; staged FFmpeg, FFprobe, and
  yt-dlp launch checks passed.
- The packaged EXE passed the offscreen QML smoke with exit `0`.

### Boundary

No visible GUI or live Colab worker was started. `graphify update .` was
attempted but the Graphify CLI is unavailable on PATH.

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

## 2026-08-22 — implemented: opt-in Unified Dubbing Colab connection contract

### Delivered

- Added an opt-in **Unified Dubbing Colab** card in Direct Colab setup. A user
  may enter one coordinator URL and one temporary bearer token once.
- The controller expands that URL into a separate verified route for every
  Dubbing stage currently selected as **Direct Colab**:
  `/v1/unified/<capability>/<model>`. Each stage still receives its exact
  capability/model handshake.
- Existing individual Direct Colab workers, API Gateway routes, and Local
  routes are unchanged. API or Local stages are deliberately excluded from the
  unified connection transaction.
- If any stage cannot start verification, the transaction rolls back every
  earlier stage and clears its pending state rather than leaving a partially
  connected workflow.

### Evidence

- `unifiedDubbingColabIsOptInAndKeepsIndependentRoutes` ran against a real
  loopback HTTP coordinator fixture: it verified distinct STT and Translation
  unified paths and confirmed a TTS API route was untouched. Exit code: `0`.
- Rebuilt the production `LAStudio` MSVC target successfully (exit code `0`).
- `git diff --check` passed and `graphify update .` was run.

### Boundary

This change intentionally does **not** pretend that the existing per-model
notebooks are one combined worker. They are separate exact-model runtimes,
including incompatible Torch/Paddle dependencies. The new app option is ready
for a genuine coordinator that launches/proxies those isolated workers under
the documented paths; such a notebook must be implemented and live-tested
before this option can be advertised as a replacement for the individual
notebooks.
