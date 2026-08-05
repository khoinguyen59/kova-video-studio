# AI agent response - Dubbing subtitle order and Direct Colab 90% repair

Date: 2026-08-06

## Result

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

No new EXE was packaged and no live Colab worker was run. The offscreen test
did not open a visible GUI. Live desktop/Colab acceptance remains manual work;
the existing `0.0.2.28` package does not contain this source-only repair.
