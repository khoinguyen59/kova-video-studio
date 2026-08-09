# AI agent response - 0.0.2.34 Dubbing workspace controls

Date: 2026-08-09

## What changed

The screenshot exposed a real placement error: batch controls were reachable
only from the separate Download route, not from the Dubbing Import/Download
workspace the user was using. This candidate corrects that placement.

- In Dubbing Import/Download, **Queue & batch settings** is now next to
  **Add link(s) to media queue**. Adding link(s) also opens the queue directly.
- The queue dialog contains downloaded-item selection, Isolate/STT/Translate/
  Voice tasks, real status/error/output paths, and **Run selected batch**.
- The two choices are visible in that dialog:
  - **Complete one video, then next** runs selected tasks end-to-end per item.
  - **Complete each step for all videos** runs one selected production stage
    across the selected items before continuing to the next stage.
- Both choices call the existing production controller; Direct Colab, API
  Gateway and explicit Local selection remain separate and no fallback was
  introduced.
- Dubbing now also has two visible draggable rails: History/Preview and
  Preview/step workspace. This is separate from the generic StudioShell rails
  because DubbingPage owns a different layout.

## Validation

- Targeted `TestMediaIngestService`: PASS (6.14 seconds).
- QML lint: PASS.
- Full CTest: **39/39 PASS**; the completed CTest log records 39 passed and
  zero failed test entries, including offscreen QML route smoke.
- `graphify update .`: completed after source changes.
- Portable internal package:
  [LA-Studio-0.0.2.34.exe](C:/Users/Nguyen%20Trong%20Khoi/Downloads/LA-STUDIO/out/LA-Studio-0.0.2.34/LA-Studio-0.0.2.34.exe)
  - FileVersion/ProductVersion: `0.0.2.34`
  - SHA-256: `5F86C2715A8F32F842EBADCAE3118547CB3CFB846E7206D04DE777B712DE178C`
  - `qwindows`, `qoffscreen`, FFmpeg, FFprobe, LAStudioRuntimeHost, Subtitle
    OCR runtime manifest and Tesseract 5.5.1 were directly verified.

## Manual acceptance still required

No visible desktop GUI or live Colab/API job was opened. In `0.0.2.34`, add
one or more public links in Dubbing, press **Add link(s) to media queue** or
**Queue & batch settings**, wait for each selected item to download, choose
tasks/order, then press **Run selected batch**. Drag either thin divider to
resize its neighboring panel. A real configured worker is needed to verify a
remote production run.

## Source delivery

- Source/test commit: `ed03461 fix: expose dubbing batch controls in workspace`
- Branch: `main`; source was pushed to `origin/main`.
