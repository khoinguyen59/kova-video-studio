# Voice Cloning and Voice Design exact-model Colab audit

Date: 2026-07-29

## Result

The desktop source now binds the model selected in the capability gallery to
one exact Colab notebook and one exact worker model. A model change clears the
old temporary Colab session; Voice Cloning also clears the old remote profile.
This prevents a URL from a previously selected worker from being reused for a
different model.

API Gateway is not read, forwarded to, or required by either direct Colab
worker. This preserves the independent routing requirement.

## Voice Cloning matrix

| Catalog model | Exact notebook |
| --- | --- |
| `omnivoice` | `LA_STUDIO_VOICE_CLONE_OMNIVOICE_GPU.ipynb` |
| `qwen3-tts-0.6b-base` | `LA_STUDIO_VOICE_CLONE_QWEN3_BASE_0_6B_GPU.ipynb` |
| `qwen3-tts-1.7b-base` | `LA_STUDIO_VOICE_CLONE_QWEN3_BASE_1_7B_GPU.ipynb` |
| `vieneu-tts-v2-turbo` | `LA_STUDIO_VOICE_CLONE_VIENEU_V2_TURBO_GPU.ipynb` |
| `vieneu-tts-v3-turbo` | `LA_STUDIO_VOICE_CLONE_VIENEU_V3_TURBO_GPU.ipynb` |
| `voxcpm2` | `LA_STUDIO_VOICE_CLONE_VOXCPM2_GPU.ipynb` |

Both `/v2/jobs/profile` and `/v2/jobs/generation` carry the exact model ID.
Every worker rejects a mismatched model with HTTP 409, requires explicit
consent, validates a 3–30 second reference, returns an opaque profile ID, and
keeps profile/model state only inside the temporary Colab process.

## Voice Design matrix

| Catalog model | Exact notebook |
| --- | --- |
| `omnivoice` | `LA_STUDIO_VOICE_DESIGN_OMNIVOICE_GPU.ipynb` |
| `qwen3-tts-1.7b-voicedesign` | `LA_STUDIO_VOICE_DESIGN_QWEN3_1_7B_GPU.ipynb` |
| `voxcpm2` | `LA_STUDIO_VOICE_DESIGN_VOXCPM2_GPU.ipynb` |

Each worker exposes `/v1/audio/voice_designs`, advertises only its loaded model,
requires CUDA, rejects CPU fallback, and rejects any mismatched model ID.

## Upstream implementation basis

- OmniVoice: `create_voice_clone_prompt`, reusable clone prompts, and
  `generate(..., instruct=...)`.
- Qwen3-TTS Base: `create_voice_clone_prompt` and `generate_voice_clone`.
- Qwen3-TTS VoiceDesign: `generate_voice_design`.
- VieNeu v2 Turbo: CUDA Turbo reference encoding and embedding-conditioned
  inference.
- VieNeu v3 Turbo: CUDA/PyTorch reference encoding and 48 kHz inference.
- VoxCPM2: reference/prompt WAV cloning and parenthesized natural-language
  Voice Design instructions.

The notebook generator pins the same upstream revisions used by the audited TTS
workers and records the exact upstream model in notebook metadata.

## Verification completed

- Generated notebook JSON and embedded Python worker source parse successfully:
  6/6 Voice Cloning and 3/3 Voice Design.
- CUDA-only, token, capability, endpoint, model mismatch and Gateway-separation
  static checks pass.
- Windows MSVC release compilation passes, including QML ahead-of-time compile.
- `TestColabVoiceCloneRunner` passes the profile/generation HTTP contract and
  verifies the model is present in both requests.
- `TestColabVoiceDesignRunner` passes the direct Voice Design HTTP contract.
- Both controller tests verify all catalog-to-notebook mappings and verify that
  changing model discards the previous temporary worker.
- `QmlRouteSmoke` passes offscreen.
- `verify_remote_feature_surface.ps1` passes 8/8 direct Colab feature routes.

## Not yet claimed

No live Colab GPU session URL/token was available during this source audit.
Therefore this report does not claim that model downloads and real inference
completed on a Colab GPU. Each of the nine notebooks still requires a live
Run-all, `/health`, `/v1/capabilities`, one real audio generation, mismatch
rejection, and cancellation/error-path check before final product acceptance.
