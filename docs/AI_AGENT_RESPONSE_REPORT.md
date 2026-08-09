# AI agent response - 0.0.2.32 multi-media Dubbing queue

Date: 2026-08-09

## Delivered

The Download page and the Dubbing source panel now accept **multiple public
links**, one per line, in a multi-line input. The downloader processes them in
serial so it does not start several large transfers at once. Each successful
download becomes a selectable queue item.

Select any number of downloaded items, choose one or more batch tasks, then
press **Run selected batch**:

- **Isolate audio** writes `vocals.wav` and `background.wav`.
- **STT to source.srt** writes the source transcript SRT.
- **Translate to translated.srt** runs required STT first and writes target
  text SRT.
- **Voice / cloned voice to WAV** runs required STT and translation first,
  then runs the configured TTS route and writes `voice.wav`.

Every item runs through the real production `DubbingJobRunner`, not a mock or
fallback worker. The Dubbing configuration is copied to an independent project
for each selected media item. Thus Direct Colab stays Direct Colab, API Gateway
stays API Gateway, and Local runs only when the user explicitly configured it.
The batch UI reports per-item state/progress and lists the exact output paths.

Artifacts are stored in:

`C:\Users\<user>\.lastudio\dubbing\batch-output\<queue-item-id>\`

Each completed item also includes `project.ladub.json` in its output directory.
Temporary submitted media URLs are erased after success, failure or cancel and
are never persisted to a project, history, settings, log or output manifest.

## Failure and queue behaviour

One failed item no longer holds the batch in a false running state. The exact
worker error is attached to that item, then the next queued item starts. Cancel
marks active/pending work cancelled and preserves the pre-existing Dubbing
project after the queue finishes. Progress is calculated from real runner
progress and terminal items; no fixed 5%/8% progress values were added.

Voice output uses the selected configured TTS voice, including an already saved
and consented clone profile. This batch does not silently create a distinct
voice-clone identity per video: creating identities requires explicit consent
and naming in Voice Cloning Studio.

## Validation

- Recompiled changed controller/QML/tests with MSVC in the proper Visual Studio
  environment. `MediaDownloadPage.qml` and `DubbingSourceMediaPanel.qml` were
  compiled by the Qt AOT path.
- Added real loopback integration coverage for two serial public downloads;
  it verifies both staged files and verifies temporary URL text is removed.
- Added a real-worker regression with two selected media items whose STT
  dependency intentionally cannot start. Both items finish as failed and the
  queue terminates; neither remains running.
- Fresh complete CTest run: **39/39 passed** in 57.71 seconds.
- Ran `graphify update .` after source changes.

This is automated/offscreen and package evidence. I did **not** open the GUI
or use a live API Gateway/Direct Colab worker, because those require your
temporary credentials/URL and must remain a separate manual acceptance step.
No success of live remote inference is claimed here.

## Package audit

Package:

`C:\Users\Nguyen Trong Khoi\Downloads\LA-STUDIO\out\LA-Studio-0.0.2.32\LA-Studio-0.0.2.32.exe`

- FileVersion: `0.0.2.32`
- ProductVersion: `0.0.2.32`
- SHA-256: `CBABA45A673D4B8FE4AFE38FCE30946E63159C78440E5483BC7F482EE60F8F7F`
- Package staging manifest: **19/19 required runtime/license artifacts**.
- Independent audit found Qt Windows/offscreen platforms, FFmpeg, FFprobe,
  Tesseract 5.5.1, RuntimeHost, Colab notebook and license payloads.

This is an internal package. The eSpeak NG MSI is hash-verified but unsigned,
so it must not be distributed as a public release.

## Source delivery

- Source/test commit: `f0ca7f4 feat: add serial dubbing media batches`
- Branch: `main`
- Source has been pushed to `origin/main`.
