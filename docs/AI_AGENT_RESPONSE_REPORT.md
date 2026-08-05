# AI agent response - safe Colab port recovery (source-only)

Date: 2026-08-06

## Result

The reported `Port 3923 is already occupied` failure in
`LA_STUDIO_VOICE_CLONE_OMNIVOICE_GPU.ipynb` was caused by the shared launcher
refusing every existing listener before it could replace an old LA Studio
worker. Re-running the notebook therefore required destroying the entire
Colab runtime.

The shared launcher now inspects Linux `/proc` ownership and, before launch,
only stops a previous worker when its command line has both the exact generated
worker module and `uvicorn`. It also stops the matching prior Cloudflare tunnel
for the same local port. It never kills an unknown listener: if the port stays
occupied by a different process, the notebook reports the PID and tells the
user to use a fresh runtime.

This repair is generated into every shared-launcher exact-model notebook,
including `LA_STUDIO_VOICE_CLONE_OMNIVOICE_GPU.ipynb`. The OmniVoice regression
also asserts port `3923`, the exact `la_studio_voice_clone_worker` module, the
new safe reclaim path, and removal of the old "disconnect and delete runtime"
message.

## Validation

- Generated Colab notebooks: `32/32` verified byte-for-byte against their
  generators.
- Targeted headless tests: `TestRemoteExecution` and
  `TestColabVoiceCloneRunner`, `2/2` PASS.
- Full headless CTest: `39/39` PASS in `78.24` seconds.
- No live Colab runtime or visible desktop GUI was opened; a fresh manual
  OmniVoice notebook run remains the acceptance step for tunnel availability.

## Package result

## Package result

The new portable candidate is ready:

`C:\Users\Nguyen Trong Khoi\Downloads\LA-STUDIO\out\LA-Studio-0.0.2.29\LA-Studio-0.0.2.29.exe`

- FileVersion/ProductVersion: `0.0.2.29`.
- SHA-256: `3D37B2DE11575EE265C2FAAB43B20DE8185557FEB587FAC74654E66656EBC2D7`.
- The prior 0.0.2.28 candidate was not overwritten.
- Staging audit passed for 19 required runtime/license artifacts; independent
  checks confirmed Qt Windows/offscreen platforms, FFmpeg/FFprobe, RuntimeHost,
  bundled Subtitle OCR, Spleeter notebooks/workers and licenses.

## Included source repair

The Dubbing flow was presenting a misleading combined
`Alignment/Subtitle` stage before translation.  It now presents the actual
target-language dependency order:

`Import/Download -> Normalize -> Isolator -> Transcribe/STT -> Translate -> Subtitle -> TTS -> Alignment -> Export/Output`.

Source transcript review remains under Transcribe/STT.  The new Subtitle stage
is the post-Translate review of target text, and the subtitle editor states that
Export uses that target text for subtitle files and burn-in.  Alignment now
only covers the post-TTS timing/conflict work.

## Direct Colab Isolator repair

- At 90%, the Activity label now shows the actual Direct Colab worker phase,
  such as writing CUDA stems.
- When the worker becomes ready, the Activity row switches to the current
  artifact (`vocals` or `background`) and shows its actual received/total byte
  progress.  This is not labelled or used as a whole-workflow percentage.
- A worker that remains at 90% while finalizing for five minutes is cancelled
  remotely and returns a specific error.  It cannot switch to a Local model.
- The transfer/cancellation/phase contract has regression coverage, including
  a deterministic worker stuck at 90%.

## Validation

- Release source and `LAStudioUnitTests` build: PASS.
- `VietNormUnitTests` build: PASS.
- Targeted CTest: `TestDubbingProject` and `TestColabSeparationRunner`, 2/2
  PASS.
- Full CTest: 39/39 PASS in 82.58 seconds, including deployment and
  `QmlRouteSmoke` under `QT_QPA_PLATFORM=offscreen`.
- QML was compiled into the release resource as part of the build. A first
  offscreen smoke run exposed a stale test assumption that Subtitle has a
  Configure button; the source smoke list was corrected, then the full
  offscreen CTest suite passed.

## Not claimed

No live Colab worker was run. The offscreen test did not open a visible GUI.
Live desktop/Colab acceptance remains manual work. This is an internal package:
the eSpeak NG MSI is SHA-verified but unsigned and must not be treated as a
distributable public release.
