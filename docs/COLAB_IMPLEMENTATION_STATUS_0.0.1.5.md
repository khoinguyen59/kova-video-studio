# Colab exact-model implementation status — 0.0.1.5

**Source revision:** `88115b8` on `main`  
**Repository visibility while testing:** public, by explicit temporary approval  
**Release status:** not accepted for distribution; live GPU evidence is still required.

This is a feature-level implementation audit.  It distinguishes source and
loopback evidence from a real Colab GPU inference; the latter cannot be
substituted by a green unit-test count.

## Implemented model routing

| Capability | Exact direct-Colab models | UI and dispatch contract | API Gateway |
| --- | ---: | --- | --- |
| Speech to Text | 4 — Nemotron 3.5 ASR 0.6B, Whisper.cpp, Qwen3-ASR 0.6B, Qwen3-ASR 1.7B | The model-picker accepts one model, opens its matching notebook, and STT dispatch requires that exact verified worker. | Independent `/v1/audio/transcriptions` route. |
| Text to Speech | 8 — Kokoro, Kokoro Vietnamese, OmniVoice, Qwen3 CustomVoice 1.7B, VibeVoice 0.5B, VieNeu v2/v3 Turbo, VoxCPM2 | Model-specific defaults and notebook; synthesis checks the selected model before each request. | Independent `/v1/audio/speech` route. |
| Voice Cloning | 6 — OmniVoice, Qwen3 Base 0.6B/1.7B, VieNeu v2/v3 Turbo, VoxCPM2 | Consent, profile and generation carry the exact model; a different worker/model is rejected. | Not exposed because this source has no compatible adapter. |
| Voice Design | 3 — OmniVoice, Qwen3 VoiceDesign 1.7B, VoxCPM2 | Exact model/notebook/session binding before generation. | Not exposed because this source has no compatible adapter. |
| Forced Alignment | 4 — Canary CTC, MMS ONNX, Qwen3 0.6B, Wav2Vec2 Chinese | Exact model/notebook/session binding before alignment. | Not exposed because this source has no compatible adapter. |
| Voice Isolation | 2 — Spleeter 2-stem FP16, UVR Vocals FT | Exact model/notebook/session binding before separation. | Not exposed because this source has no compatible adapter. |
| Translation | 3 — M2M-100 418M, MADLAD-400 3B, Hy-MT2 1.8B | Exact model/notebook/session binding before translation. | Independent Gateway chat-completions route. |
| LLM Chat | 1 — Qwen3.5 2B | Exact model/notebook/session binding before chat streaming. | Independent Gateway chat-completions route. |

The total is **31 exact capability/model bindings**.  The current controller,
feature page, checked-in notebook, and live-acceptance manifest must all agree
for every one of them.  The `verify_colab_model_bindings.py` gate compares all
four sources and rejects an unmapped model, stale notebook, or dispatch path
without `hasVerifiedRoute(capability, model)`.

## Current UX and safety behavior

- Clicking a gallery card alone is a pending UI selection; it does not rebuild
  the studio or load a model locally.  `Select for Colab` is the explicit
  commit action.
- The notebook button always opens the selected notebook from GitHub `main`.
  Operational documentation is checked by the remote-surface gate so it cannot
  send a user to a retired development branch.
- Pairing calls `/health` and `/v1/capabilities` asynchronously.  It accepts
  only `ready=true`, CUDA, no CPU fallback, the selected capability, the exact
  selected model, and a loaded CUDA model entry.
- API Gateway credentials are never sent to Colab.  Colab URL/tokens are
  session-only and are never written to Settings or passed through Gateway.
- Local execution is forced to CPU by Settings.  The model gallery restricts
  Colab-enabled capabilities to local CPU runtime choices; it does not start a
  local CUDA/Vulkan inference path.
- Dubbing's primary node, Voice Clone, and optional Alignment dialogs now stay
  visible while the asynchronous worker verification runs.  They auto-close
  only after success, show a wrong-model/HTTP error on failure, and cancel the
  handshake when the user presses Cancel.

## Evidence gathered on this source

| Gate | Result | Scope |
| --- | --- | --- |
| Generated notebook freshness | Pass — 31/31 | Checked-in active notebooks match their generators byte-for-byte. |
| UI/controller/notebook/manifest binding audit | Pass — 31/31 | Exact mappings for all eight capability groups. |
| Remote feature-surface audit | Pass — 8/8 | URL/token UI, notebook, CUDA guard and endpoint contract. |
| QML lint | Pass | Including the Dubbing asynchronous verification dialogs. |
| Incremental LA Studio compile | Pass | Modified Dubbing QML was compiled and linked into `LA-Studio-0.0.1.5.exe`. |
| Headless QML route smoke | Pass — exit code 0 | Loads the compiled routes without opening a user-facing window. |
| Focused controller/runner regression | Pass — 10/10 CTest entries | STT, Dubbing, remote handshake and every Colab runner. |

## Still required: real GPU acceptance

No active user-owned Colab worker URL/token is available to this process, so
no model is marked **Live Colab verified** yet.  For each model intended for
use, open its exact notebook, run it on a Colab GPU, then run:

```powershell
python scripts/run_live_colab_acceptance.py `
  --config C:\Temp\la-studio-live-colab.json `
  --only capability:model `
  --report out\live-colab-capability-model.md
```

The live report passes only if it records CUDA health, the matching loaded
model entry, deliberate wrong-model rejection, and one real feature result.
After all intended underlying routes pass, Dubbing requires one complete
source-to-export run.  Only then can this implementation be called feature
accepted and packaged for release.
