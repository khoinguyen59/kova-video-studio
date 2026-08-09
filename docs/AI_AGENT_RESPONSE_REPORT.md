# AI agent response - 0.0.2.33 batch order and responsive controls

Date: 2026-08-09

## Delivered

The Dubbing media queue now presents two explicit processing choices before a
batch starts:

- **Complete one video, then next**: runs all selected tasks for the first
  item, then starts the next item. This retains the former end-to-end serial
  behaviour.
- **Complete each step for all videos**: runs the current real Dubbing stage
  for every selected item before advancing the selected queue to the next
  stage. For example, all selected items ingest first, then all run STT, then
  translation, then voice/mix when those tasks were selected.

Both modes use the existing production `DubbingJobRunner` and retain a
separate Dubbing project per media item. They preserve the configured route:
Direct Colab remains Direct Colab, API Gateway remains API Gateway, and Local
is used only when the user explicitly chose Local. No route is silently
substituted. Existing artifacts remain per-item under:

`C:\Users\<user>\.lastudio\dubbing\batch-output\<queue-item-id>\`

Depending on the selected tasks, that directory contains real
`source.srt`, `translated.srt`, `vocals.wav`, `background.wav`, `voice.wav`
and `project.ladub.json` outputs.

TTS and Voice Cloning Examples now include more usable text presets: short
and long Vietnamese, short and long English, and bilingual TTS. Voice-clone
examples do not provide a fake reference audio file and do not bypass the
existing consent/reference workflow.

The shared dark theme now has stronger text contrast and an application palette
for native controls. Studio left/history and right/settings rails have visible
drag handles with bounded widths. The left application navigation can now be
expanded or collapsed; when expanded, each function name is visible instead of
requiring hover-only discovery.

## Regression and package evidence

- Added controller regression for the stage-by-stage queue. With two real WAV
  inputs, both complete real ingest before either reaches an intentionally
  unavailable real STT dependency. The error terminates each item instead of
  leaving a permanently running queue.
- Targeted `TestMediaIngestService`: PASS.
- QML lint: PASS.
- Fresh complete CTest: **39/39 passed** in 57.06 seconds, including QML route
  smoke under offscreen Qt.
- Ran `graphify update .` after source changes.
- Portable internal package:
  [LA-Studio-0.0.2.33.exe](C:\Users\Nguyen%20Trong%20Khoi\Downloads\LA-STUDIO\out\LA-Studio-0.0.2.33\LA-Studio-0.0.2.33.exe)
  - FileVersion/ProductVersion: `0.0.2.33`
  - SHA-256: `B1B591F103740BB6A528112C4C1953B34CA010F8E231563C613790725C533626`
  - Staging manifest: **19/19** required runtime/license artifacts.
  - Qt `qwindows` and `qoffscreen`, FFmpeg, FFprobe, RuntimeHost and Tesseract
    5.5.1 were present; FFmpeg/FFprobe/Tesseract version commands completed.

## Scope not claimed as tested

No visible GUI was opened or controlled, and no live API Gateway/Direct Colab
job was called. Therefore the following remain manual acceptance checks:

1. Switch between both batch orders with several downloaded videos and verify
   the visible order matches the selected label.
2. Drag the left and settings pane handles, then expand/collapse the global
   navigation at the user's normal display scale.
3. Run a batch using the user's temporary Direct Colab or API credentials and
   verify the configured remote route receives the work.

## Source delivery

- Source/test commit: `217b8f7 feat: add batch order and responsive studio controls`
- Branch: `main`
- Source has been pushed to `origin/main`.
