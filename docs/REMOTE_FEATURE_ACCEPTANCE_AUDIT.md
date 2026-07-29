# Remote feature acceptance audit

**Status:** source implementation and automated contracts pass; live GPU/API
acceptance is still required before a new release package. This document is the
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
| Notebook source inventory | Pass for current source | 38 source notebooks are present, including exact-model workers for STT, TTS, Voice Clone, Voice Design, Forced Alignment, Voice Isolation, Translation and LLM Chat. The prior `0.0.0.5` package predates this inventory and is not acceptance evidence. |
| Notebook-to-feature mapping | Pass | STT, TTS, Clone, Design, Alignment, Isolation, Translation and Chat panels reference the intended notebook names. |
| Generated exact-model notebook freshness | Pass | `scripts/verify_generated_colab_notebooks.py` regenerates the 31 active exact-model notebooks into a temporary ignored directory and compares every file byte-for-byte. The remote surface gate and Windows CI/release workflows run this check, so a template/model mapping change cannot silently leave a stale committed notebook. |
| Exact-notebook dependency audit | Pass for the static compatibility gate | All 31 current exact-model notebooks were inspected. Only the four VieNeu v2/v3 routes explicitly replace Torch with 2.8.0; the Voice Clone and TTS variants now also replace torchvision with the matching 0.23.0 CUDA build and preflight the Transformer classes. The other 27 exact routes do not make that partial Torch upgrade. |
| Current source compile and test suite | Pass | `scripts/run_tests.ps1` completed with exit code **0**. `TestRemoteExecution` now has **31/31** checks, including asynchronous worker verification and the verified-GPU health contract for every exact-model notebook. |
| Current QML syntax gate | Pass | `scripts/lint_qml.ps1` completed with exit code **0** after all feature panels gained a visible Colab verification state. |
| Current remote UI contract gate | Pass | `scripts/verify_remote_feature_surface.ps1` verified **8/8** direct Colab routes, including notebook, URL/token fields, CUDA guard and endpoint surface. |
| Remote preflight transport contract | Pass | `tests/test_RemoteLivePreflightContract.ps1` uses isolated loopback workers for Gateway plus all 8 Colab capability groups. It verifies bearer-token isolation and proves that a worker is rejected when its health/capabilities advertise a model other than the configured exact model, or when the model is not a loaded CUDA worker. This is protocol evidence, not a substitute for a user-owned live Colab GPU run. |
| Desktop exact-model activation contract | Pass | `TestRemoteExecution` now rejects a worker when the selected model entry omits `loaded=true` or omits its own `device=cuda` proof, even if the worker or capability claims CUDA generally. The desktop therefore applies the same exact-model gate as the preflight script before activating a feature session. |
| Dubbing exact-route contract | Pass | **27** exact routes are mapped across isolation, STT, translation, TTS, clone and optional alignment; `TestDubbingProject` passed **52/52**. |
| Public notebook inventory | Pass | All **27/27** exact notebooks used by Dubbing are visible on public branch `main`. |
| Model-picker stability: open | Pass | Rebuilt executable opened the STT model picker immediately; its UI rendered CPU Whisper.cpp as compatible and CUDA unavailable without loading/downloading any model. |
| Model-picker stability: switching card | Pending rerun | The first automated switch attempt lost foreground focus to Codex; no state-changing selection is accepted as evidence. Rerun in an isolated LA Studio window is required. |
| Live Gateway/Colab inference | Not run | There is no active `LASTUDIO_*` credential/configuration in this session. A real Gateway key and active GPU Colab sessions are required; mock/local HTTP tests cannot replace this gate. |

The source has changed after the prior package while correcting the UI audit
findings below.  Therefore `0.0.0.5` is **not** the acceptance candidate and
must not be handed off as proof of these source changes until a fresh UI and
feature regression pass is complete.

### Latest notebook-contract correction

The four exact STT notebooks previously returned `/health` without
`ready=true`.  This was a real end-to-end defect: `ColabSession` correctly
rejects an unready worker, so a CUDA worker could never become active even
when its selected model and bearer token were valid.  Commit `20accfc` fixes
the generator and all four generated notebooks to return `ready=true`,
`device=cuda`, the GPU name, the exact model and `cpu_fallback=false`.
The `TestRemoteExecution` regression gate now checks those requirements,
plus the capability, exact model and feature endpoint, for all 31 active
model-specific notebooks.

### VieNeu startup diagnostic correction

The VieNeu v2/v3 Voice Clone notebooks used to launch the model worker in the
background, wait six minutes for `/health`, then discard the only useful
diagnostic and show a generic CUDA-ready timeout.  That output did not prove a
GPU failure: it could also be a cold model download, a dependency/import
failure, or a model-load failure.  The shared Voice Clone/Voice Design startup
template now writes the worker output to `/content/la_studio_*_worker.log`,
fails immediately if the worker exits, waits up to 20 minutes for a cold CUDA
model load, prints the final 12,000 log characters on failure, and accepts
readiness only when `/health` confirms CUDA, no CPU fallback and the exact
selected model.  The regression test checks that every generated Voice Clone
notebook preserves this diagnostic contract.  A fresh live VieNeu Colab run is
still required to establish the remaining runtime-specific cause, if any.

Live Colab logs then identified a shared failure before either VieNeu model
could load: Transformers could not import `PreTrainedModel` for v3 or
`Qwen3ForCausalLM` for v2.  These are framework-import errors, not an out of
memory condition or a rejected model repository.  The VieNeu setup cell now
replaces any retained Colab torchvision build with the CUDA 12.8-compatible
`torchvision==0.23.0` paired with `torch==2.8.0`, then imports both classes
before it starts the worker.  This makes the PyTorch/Transformers environment
deterministic and stops at the installation cell with a full traceback if a
future Colab image changes a dependency again.

The same audit found the equivalent gap in the two TTS VieNeu notebooks, so
they now use the same explicit Torch/torchvision pair and import preflight.
All eight exact TTS notebooks now also retain their worker log, verify the
exact CUDA health response and permit a 20-minute cold start rather than
hiding any model initialization exception behind a six-minute generic timeout.
The retained legacy Voice Clone notebook creates its own fresh Python virtual
environment, so it cannot retain Colab's old torchvision build and is not on
the current exact-model UI route.

## Feature route matrix

| Feature | Direct Colab notebook and advertised CUDA model(s) | API Gateway route | Current UI route surface | Acceptance state |
| --- | --- | --- | --- | --- |
| Speech to Text | Four exact workers: Nemotron 3.5 ASR Streaming 0.6B, Whisper.cpp, Qwen3-ASR 0.6B and Qwen3-ASR 1.7B | `/v1/audio/transcriptions`; model selected from Gateway STT configuration | Gallery and Dubbing select the model before opening its exact notebook; independent URL/token and Gateway fields remain available | Desktop `0.0.1.1` submits a direct-Colab request to asynchronous `/v2/jobs/transcriptions` and polls its short status endpoint, avoiding Cloudflare's 120-second response limit. Exact source/QML/HTTP contracts pass; each model still needs live Colab GPU inference. |
| Text to Speech | Eight exact workers: Kokoro, Kokoro Vietnamese, OmniVoice, Qwen3 CustomVoice 1.7B, VibeVoice 0.5B, VieNeu v2/v3 Turbo and VoxCPM2 | `/v1/audio/speech` | Gallery and Dubbing select the model before opening its exact notebook; model-specific defaults are shown | Exact source/QML/HTTP contracts pass; each model still needs live Colab GPU inference. |
| Voice Cloning | Six exact-model notebooks for OmniVoice, Qwen3 Base 0.6B/1.7B, VieNeu v2/v3 Turbo and VoxCPM2; profile and generation requests both carry the model ID | No supported Gateway adapter in this codebase | Gallery action opens the exact notebook; settings show selected model, notebook, URL/token, consent and profile fields | Source contract, compile, QML and desktop HTTP tests pass. Startup failures now show the worker log; each model still needs a consented live Colab GPU generation. |
| Voice Design | Three exact-model notebooks for OmniVoice, Qwen3 VoiceDesign 1.7B and VoxCPM2 | No supported Gateway adapter in this codebase | Gallery action opens the exact notebook; settings show selected model/notebook and URL/token | Source contract, compile, QML and desktop HTTP tests pass; each model still needs live Colab GPU generation. |
| Forced Alignment | Four exact-model workers: Wav2Vec2 Chinese, Canary CTC, MMS ONNX and Qwen3 Forced Aligner 0.6B | No supported Gateway adapter in this codebase | Gallery selection opens the exact notebook; settings show selected model/notebook, model-valid language choices, URL/token and alignment options | Source contract, compile, QML and desktop HTTP tests pass; every model still needs a live Colab GPU audio + transcript test. |
| Voice Isolation | Two exact-model workers: Spleeter 2-stem FP16 and UVR Vocals FT | No supported Gateway adapter in this codebase | Gallery selection opens the exact notebook; settings show selected model/notebook, URL/token and output workflow | Source contract, compile, QML and desktop HTTP tests pass; both models still need a live Colab GPU stem-artifact test. |
| Translation | Three exact-model workers: M2M-100 418M, MADLAD-400 3B MT and Tencent Hy-MT2 1.8B | Gateway chat-completions route with strict JSON patch validation | Gallery selection opens the exact notebook; feature settings retain independent Gateway and Colab URL/token/model surfaces | Exact mapping, compile, QML, direct-worker HTTP and Gateway JSON-contract tests pass; each model still needs live Colab GPU translation. |
| LLM Chat | Exact `Qwen3.5 2B` worker | Gateway chat-completions route using the model ID configured in Settings or this feature | Gallery selection opens the Qwen3.5 notebook; feature settings retain independent Gateway and Colab URL/token/model surfaces | Exact mapping, compile, QML, streaming/cancel and Gateway tests pass; live Colab GPU streaming remains pending. |
| Video Dubbing | 27 exact routes across Separation, STT, optional Alignment, Translation, TTS and Voice Clone | Per-node Gateway option for compatible STT/translation/TTS paths; remote-first duration rewrite uses Gateway LLM | Per-node exact model selector; exact notebook; separate URL/token for primary worker, clone and optional alignment | Compile, 52 Dubbing tests, 34 CTest tests, QML and public-notebook inventory pass; complete live source-to-export workflow remains pending. |

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

The two Voice Isolation workers expose their immutable model family IDs and
the exact public `k2-fsa/sherpa-onnx` GitHub Release asset as `artifact_url`.
They do not claim that those ONNX assets are Hugging Face repositories.  The
artifact URLs were checked as reachable before this audit update.

The final printed variables are capability-specific, for example
`LA_STUDIO_COLAB_STT_URL` / `LA_STUDIO_COLAB_STT_TOKEN` for STT,
`LA_STUDIO_COLAB_TRANSLATION_URL` / `LA_STUDIO_COLAB_TRANSLATION_TOKEN`
for Translation and `LA_STUDIO_COLAB_CHAT_URL` /
`LA_STUDIO_COLAB_CHAT_TOKEN` for Chat. The user must still paste those values
into the matching studio; no token is persisted in Settings.

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
   link for the selected notebook on `main`, rather than a
   generic home page. The repository is temporarily public for Colab testing
   and must be returned to private after live acceptance. The packaged
   notebook-folder button remains as an offline fallback.

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

### Fixed source-level connection gate

1. **Direct Colab pairing is now verified, not optimistic.** The shared
   `ColabSession` first enters `checking`, calls `/health`, then calls
   `/v1/capabilities` with the temporary bearer token. It requires
   `ready=true`, CUDA (and no CPU fallback), `contract_version=1`, the selected
   capability, the exact selected model, a CUDA-backed model entry, and a
   loaded model. Only then does `active=true` and the feature route activate.
   A failed, stale, wrong-model, wrong-capability, CPU, or unreachable worker
   stays inactive and presents a useful error without exposing the token.

2. **Feature UI now exposes the true connection state.** STT, TTS, Voice
   Clone, Voice Design, Isolation, Alignment, Translation, Chat and all
   Dubbing worker dialogs display `checking`, verified CUDA/model, or failure
   state next to their URL/token fields; actions are disabled while checking.
   Remote Execution tests cover valid CUDA, CPU rejection, wrong model,
   missing capability, stale request replacement, and all required UI surfaces.

### Must be live-verified before acceptance

1. **No external live evidence exists yet.** A real Colab GPU session and a
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

The source-level candidate is compiled, unit-tested and QML-linted. Do **not**
accept or package a release candidate until the real Gateway and real GPU Colab
live tests have evidence for each route that the user intends to use. Missing
credentials are an external-test blocker, not a reason to mark a feature
complete.
