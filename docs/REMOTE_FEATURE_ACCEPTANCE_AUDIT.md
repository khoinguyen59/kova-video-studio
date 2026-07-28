# Remote feature acceptance audit

**Status:** not accepted for a new release build yet.  This document is the
feature-by-feature gate for the API Gateway + direct Colab GPU migration.  A
passing unit test, a successful package, or a CPU fallback is not considered
feature acceptance on its own.

**Scope:** preserve LA Studio's existing features while moving supported heavy
work to either an independent API Gateway route or a direct, CUDA-only Colab
worker.  Gateway credentials must never be sent to Colab; Colab URL/tokens
must never be sent through Gateway.

## Evidence collected

| Check | Result | Evidence |
| --- | --- | --- |
| Portable launch stability | Pass for the prior `0.0.0.5` package | The packaged EXE remained alive and responsive at 5, 10, 20, and 30 seconds (about 273 MB working set). |
| Existing automated suite | Pass before the new audit fixes | `ctest --test-dir out/build/windows-msvc-release --output-on-failure`: **34/34** tests passed, including all Colab runners, Gateway TTS, remote contract and QML route smoke. |
| Notebook source inventory | Pass for current source | 34 source notebooks are present, including exact-model workers for STT, TTS, Voice Clone, Voice Design, Forced Alignment and Voice Isolation. The prior `0.0.0.5` package predates this inventory and is not acceptance evidence. |
| Notebook-to-feature mapping | Pass | STT, TTS, Clone, Design, Alignment, Isolation, Translation and Chat panels reference the intended notebook names. |
| Current source compile and test suite | Pass | Rebuilt with the MSVC environment; `ctest --test-dir out/build/windows-msvc-release --output-on-failure` passed **34/34** after the stability changes. |
| Current remote UI contract gate | Pass | `scripts/verify_remote_feature_surface.ps1` verified **8/8** direct Colab routes, including notebook, URL/token fields, CUDA guard and endpoint surface. |
| Model-picker stability: open | Pass | Rebuilt executable opened the STT model picker immediately; its UI rendered CPU Whisper.cpp as compatible and CUDA unavailable without loading/downloading any model. |
| Model-picker stability: switching card | Pending rerun | The first automated switch attempt lost foreground focus to Codex; no state-changing selection is accepted as evidence. Rerun in an isolated LA Studio window is required. |
| Live Gateway/Colab inference | Not run | There is no active `LASTUDIO_*` credential/configuration in this session. A real Gateway key and active GPU Colab sessions are required; mock/local HTTP tests cannot replace this gate. |

The source has changed after the prior package while correcting the UI audit
findings below.  Therefore `0.0.0.5` is **not** the acceptance candidate and
must not be handed off as proof of these source changes until a fresh UI and
feature regression pass is complete.

## Feature route matrix

| Feature | Direct Colab notebook and advertised CUDA model(s) | API Gateway route | Current UI route surface | Acceptance state |
| --- | --- | --- | --- | --- |
| Speech to Text | `LA_STUDIO_SPEECH_GPU.ipynb`; worker loads `faster-whisper-large-v3` | `/v1/audio/transcriptions`; model selected from Gateway STT configuration | Notebook, URL, token and Gateway fields are in STT settings | Needs live worker + Gateway smoke; Colab model is fixed by notebook and was not visible enough in UI. |
| Text to Speech | `LA_STUDIO_VOICE_GPU.ipynb`; `kokoro`, listed voices/languages | `/v1/audio/speech` | Separate Gateway and Colab URL/token/model/voice fields | Needs live smoke. Only Kokoro is covered by this Colab worker. |
| Voice Cloning | Six exact-model notebooks for OmniVoice, Qwen3 Base 0.6B/1.7B, VieNeu v2/v3 Turbo and VoxCPM2; profile and generation requests both carry the model ID | No supported Gateway adapter in this codebase | Gallery action opens the exact notebook; settings show selected model, notebook, URL/token, consent and profile fields | Source contract, compile, QML and desktop HTTP tests pass; each model still needs a consented live Colab GPU generation. |
| Voice Design | Three exact-model notebooks for OmniVoice, Qwen3 VoiceDesign 1.7B and VoxCPM2 | No supported Gateway adapter in this codebase | Gallery action opens the exact notebook; settings show selected model/notebook and URL/token | Source contract, compile, QML and desktop HTTP tests pass; each model still needs live Colab GPU generation. |
| Forced Alignment | Four exact-model workers: Wav2Vec2 Chinese, Canary CTC, MMS ONNX and Qwen3 Forced Aligner 0.6B | No supported Gateway adapter in this codebase | Gallery selection opens the exact notebook; settings show selected model/notebook, model-valid language choices, URL/token and alignment options | Source contract, compile, QML and desktop HTTP tests pass; every model still needs a live Colab GPU audio + transcript test. |
| Voice Isolation | Two exact-model workers: Spleeter 2-stem FP16 and UVR Vocals FT | No supported Gateway adapter in this codebase | Gallery selection opens the exact notebook; settings show selected model/notebook, URL/token and output workflow | Source contract, compile, QML and desktop HTTP tests pass; both models still need a live Colab GPU stem-artifact test. |
| Translation | `LA_STUDIO_LANGUAGE_GPU.ipynb`; `m2m100-418m` | Gateway chat-completions route with strict JSON patch validation | Independent Gateway and Colab URL/token/model fields | Needs live translation and error-contract tests. |
| LLM Chat | `LA_STUDIO_LANGUAGE_GPU.ipynb`; `qwen2.5-1.5b-instruct`, `qwen2.5-3b-instruct` | Gateway chat-completions route | Independent Gateway and Colab URL/token/model fields | Needs live streaming response/cancel test. |
| Video Dubbing | Reuses Separation, STT, Translation and TTS workers per node | Per-node Gateway option for compatible STT/translation/TTS paths | Per-node route selector plus inline Gateway/Colab dialog | Needs complete source-to-export live workflow after the individual routes pass. |

The specialist features correctly do **not** pretend to offer an API Gateway
option when this project has no compatible Gateway adapter.  Adding a disabled
or cosmetic API option would be a UX defect, not feature coverage.

## Notebook contract findings

The active exact-model notebooks and retained legacy notebooks were inspected.
The exact-model notebooks used by the current feature routes have the following
required properties:

- Rejects CPU fallback and checks CUDA before serving requests.
- Creates a fresh random bearer token for the current Colab runtime.
- Starts a local FastAPI worker, exposes it through a temporary HTTPS
  Cloudflare tunnel, and prints the corresponding URL and token in its final
  setup cell.
- Keeps Gateway URL/key out of the notebook and worker process.
- Exposes `/health` and `/v1/capabilities` alongside the feature endpoint.

The final printed variables are capability-specific, for example
`LA_STUDIO_COLAB_STT_URL` / `LA_STUDIO_COLAB_STT_TOKEN` for STT and
`LA_STUDIO_LANGUAGE_URL` / `LA_STUDIO_LANGUAGE_TOKEN` for the combined
Translation/Chat worker.  The user must still paste those values into the
matching studio; no token is persisted in Settings.

## UI/UX audit findings

### Fixed in source and compiled; UI regression is in progress

1. **Remote setup was locked behind a local model.** In the real `0.0.0.5`
   STT UI, the Colab and Gateway fields were visibly disabled when no local
   model was loaded.  The common `StudioShell` gate caused the same issue for
   STT, TTS, Voice Cloning, Voice Design, Voice Isolation, Translation and
   Alignment.  This contradicted the goal of avoiding local heavyweight model
   setup.  The relevant studios now set `settingsRequiresReady: false`; only
   inference controls remain gated until a selected provider is valid.

2. **“Open Colab” opened a blank Colab page.** The button now builds an exact
   `colab.research.google.com/github/khoinguyen59/kova-video-studio/...`
   link for the selected notebook on `codex/remote-inference`, rather than a
   generic home page.  The private repository must remain reachable to the
    signed-in GitHub account for this internal-build route to work.  The
    packaged notebook-folder button remains as an offline fallback.

3. **Choosing a model could white-screen the application.** The root blocking
   path was the first Intel display-adapter query (`EnumDisplayDevices`) on the
   GUI thread. On this machine it delayed the first `Hardware Detection
   Finished` log entry by roughly 16 seconds immediately after model-picker
   initialization. GPU inventory, including the Intel fast path, now runs in a
   worker thread and delivers its result through the event queue. The picker
   starts with CPU compatibility, then refreshes when advisory GPU data
   arrives. This does not start GPU inference: local execution remains
   CPU-only and CUDA was unavailable in the tested UI.

4. **Empty selection resets rebuilt the entire picker.** The gallery emitted an
   empty `initialSelectedFiles` map whenever it highlighted a family. The native
   model previously cleared the map and synchronously refreshed all catalog,
   file and runtime checks even when nothing changed. It now compares the
   resolved selection and skips that no-op reset. The Voice Cloning target
   language selector and the native-window visibility binding loop found in
   application logs were also corrected. These corrections compile and pass
   the full automated suite; a focused card-switch UI rerun remains required.

### Must be fixed or live-verified before acceptance

1. **Connection status is optimistic.** `connectColab()` implementations
   validate URL/token syntax and immediately select the route, but do not
   prove `/health` is reachable, CUDA is available, or the required capability
   is advertised before the UI calls it “Using Colab GPU”.  Gateway selection
   likewise validates configuration but does not preflight its model catalog
   at the feature level.  This is a release blocker: the app needs an
   asynchronous per-feature preflight state (`checking`, `ready`, `failed`)
   and must not enable execution after a failed preflight.

2. **Model coverage is partial, not universal.** The bundled Colab workers
   cover the models in the table above.  They do not automatically run every
   local catalog family: most visibly, the Colab TTS worker only supports
   Kokoro while the app also advertises Qwen3, VieNeu, VibeVoice, VoxCPM2 and
   OmniVoice families.  Each remaining family must either gain a tested Colab
   adapter, gain a tested Gateway adapter, or be explicitly labelled
   CPU-local-only rather than appearing as a GPU-ready choice.

3. **Feature-level model discovery needs to drive the controls.** The worker
   capabilities endpoint advertises model IDs, but the STT Colab form does not
   yet show its fixed `faster-whisper-large-v3` model and other feature forms
   still rely partly on manually typed defaults.  After preflight, the UI must
   display only model/voice/language values accepted by that specific worker.

4. **No external live evidence exists yet.** A real Colab GPU session and a
   configured 9Router/API Gateway are not present in this workspace.  The
   required tests cannot be substituted by a mock: the actual notebook must
   run, print URL/token, return `ready=true`, `device=cuda`, the expected
   capability and models, then complete one safe feature request.

## Required acceptance sequence per feature

For every row in the route matrix, perform and record all of these steps:

1. Start the app with no local model, then confirm the feature's remote setup
   panel, exact notebook button, URL field and token field are enabled.
2. Open the exact notebook, select Colab GPU, run all cells, and copy the
   printed URL/token.  Confirm the worker reports CUDA and the expected model
   through `/health` and `/v1/capabilities`.
3. Paste URL/token into that feature only.  Confirm a failed health or
   capability check leaves its action disabled and gives a useful error;
   confirm a passing check enables exactly that route without changing any
   other feature's session.
4. Where a Gateway adapter exists, enter URL/key/model in Settings and test
   the feature-level inline fields when Settings is incomplete.  Verify that
   Gateway works with Colab disconnected and does not alter the Colab URL or
   token.
5. Run one representative inference with a non-sensitive sample, cancel one
   in-flight request where supported, then test worker expiry/disconnect and
   recovery.  Check that the window remains responsive throughout.
6. For Dubbing, repeat the checks per node, then run one complete
   source-to-export flow only after the four underlying routes pass.

## Gate to authorize the next package

Do **not** build or package a new candidate until the source-level fixes above
have automated contract checks and a Windows UI regression pass.  Do **not**
accept a candidate until the real Gateway and real GPU Colab live tests have
evidence for each route that the user intends to use.  Missing credentials are
an external-test blocker, not a reason to mark a feature complete.
