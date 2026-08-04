# AI agent response - 0.0.2.24 packaged

Date: 2026-08-05

## Outcome

Automatic Dubbing now uses one controller-owned, eight-stage presentation
contract. `0.0.2.22` is superseded. The first `0.0.2.23` package is preserved
but rejected because its interaction trace did not yet prove route/model/worker
state transitions; it was not overwritten. The accepted internal candidate is
`0.0.2.24`.

| User stage | Production nodes retained internally |
| --- | --- |
| Import/Download | `media-input` |
| Normalize | `ingest` |
| Isolator | `source-separate` |
| Transcribe/STT | `transcribe` |
| Alignment/Subtitle | `review-transcript`, `fit-timing`, `review-conflicts` |
| Translate | `translate`, `review-translation` |
| TTS | `assign-voices`, `synthesize` |
| Export/Output | `mix`, `export` |

`Timing/Mix` no longer appear as user cards, headers or review rows. Their
production behavior remains inside Alignment/Subtitle and Export/Output.
Normalize explicitly shows automatic local preprocessing/effective format and
`No model required`; it is not Ready without valid media. Isolator, STT,
Translate and TTS require an exact route/model and the matching readiness:
resolved Local runtime/model, configured API Gateway, or verified Direct Colab
capability/model worker. There is no `workflow default` success state.

Configure is now an inline, wizard-owned sub-dialog. Route is chosen before the
matching model picker; Save/Apply/Cancel preserve the current preflight card.
Direct Colab alone creates exact worker cards on the wizard Colab page; Local
and API Gateway do not. URL and token stay in session memory.

## Validation

- QML lint: PASS.
- Targeted offscreen QML route smoke: 2/2 PASS.
- Full CTest: **39/39 PASS** in 25.57 seconds.
- Controller readiness matrix covers no media, valid media, Local without/with
  resolved runtime-model, and Direct Colab without/with an asynchronously
  verified exact worker.
- Production-shell trace:
  `out/build/windows-msvc-tests/dubbing-qml-interaction-trace.json` has 18
  ordered events. It clicks Configure for all 8 stages and records exact state
  transitions: `Local/No model/0 workers -> Direct Colab/exact models/2 workers
  -> Local/exact models/0 workers`.
- Graphify code graph update: PASS. It reported no topology change after the
  final trace-only adjustment; known non-code/parser warnings remain outside
  this delivery scope.

## Package audit

- EXE: `out/LA-Studio-0.0.2.24/LA-Studio-0.0.2.24.exe`
- Source/FileVersion/ProductVersion: `0.0.2.24` / `0.0.2.24` / `0.0.2.24`
- SHA-256: `4254932A08D3FD2D44E2D924328FD9F67C0CCAF8EE48B3E1D1C2170A9FB32319`
- Package staging and license manifests: PASS (19 required artifacts).
- Verified staged: Qt `qwindows`, Core/Gui/Qml/Quick/Multimedia, FFmpeg/FFprobe,
  Tesseract 5.5.1, Paddle worker/manifest, eSpeak NG and third-party notices.
- Internal-only caveat: eSpeak MSI is SHA-verified but unsigned. The package is
  not a distributable release.

Source/tests were committed and pushed directly to `main` as `6ef65b8`
(`fix: enforce eight-stage dubbing preflight`). No GUI or live Colab notebook/
tunnel/worker was opened, so desktop interaction and live-service acceptance
remain user-side manual gates.
