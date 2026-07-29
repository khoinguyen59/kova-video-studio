# Text-to-Speech exact-model Colab audit

## Scope

This source milestone replaces the single fixed Kokoro notebook and manually
typed Colab model field with a catalog-driven model selection flow. Selecting
a TTS family in the model gallery now chooses one exact notebook, records the
same family ID in the controller, and sends that ID in `/v1/audio/speech`.

API Gateway TTS remains an independent route. No Gateway URL, API key, model,
or fallback logic is included in these notebooks.

## Model matrix

| LA Studio family | Notebook | Upstream GPU adapter |
| --- | --- | --- |
| `kokoro` | `LA_STUDIO_TTS_KOKORO_GPU.ipynb` | `hexgrad/Kokoro-82M`, official `KPipeline` |
| `kokoro-vietnamese` | `LA_STUDIO_TTS_KOKORO_VIETNAMESE_GPU.ipynb` | `contextboxai/Kokoro-Vietnamese`, CUDA ONNX Runtime |
| `omnivoice` | `LA_STUDIO_TTS_OMNIVOICE_GPU.ipynb` | `k2-fsa/OmniVoice`, official `OmniVoice.from_pretrained` |
| `qwen3-tts-1.7b-customvoice` | `LA_STUDIO_TTS_QWEN3_CUSTOMVOICE_1_7B_GPU.ipynb` | official Qwen3-TTS `generate_custom_voice` |
| `vibevoice` | `LA_STUDIO_TTS_VIBEVOICE_0_5B_GPU.ipynb` | Microsoft VibeVoice Realtime 0.5B streaming model |
| `vieneu-tts-v2-turbo` | `LA_STUDIO_TTS_VIENEU_V2_TURBO_GPU.ipynb` | official `Vieneu(mode="turbo_gpu")` |
| `vieneu-tts-v3-turbo` | `LA_STUDIO_TTS_VIENEU_V3_TURBO_GPU.ipynb` | official v3 Turbo PyTorch CUDA backend |
| `voxcpm2` | `LA_STUDIO_TTS_VOXCPM2_GPU.ipynb` | official `VoxCPM.from_pretrained` |

Every worker:

- aborts when CUDA is unavailable;
- loads one exact upstream family before reporting ready;
- advertises that family through `/v1/capabilities`;
- rejects a mismatched request model with HTTP 409;
- returns WAV only and performs finite/duration checks;
- uses a temporary bearer token and HTTPS Cloudflare tunnel;
- does not use or receive API Gateway credentials.

## UI and controller behavior

- The TTS model gallery exposes `Select for Colab` and
  `Select + open notebook`.
- The TTS settings panel shows the selected model and exact notebook; the
  free-form Colab model field was removed.
- Changing model clears an already paired TTS worker so a worker for the old
  model cannot look active for the new model.
- Default voice/language values are reset per family.
- Local runtime choices remain CPU-only in this Colab-enabled gallery flow.

## Automated evidence

- Notebook JSON and embedded worker Python syntax: pass for 8/8 notebooks.
- Exact model/notebook mapping unit test: pass for 8/8 models.
- Direct request contract test: pass.
- QML route smoke: pass.
- Remote execution regression: pass.
- `scripts/verify_remote_feature_surface.ps1`: must pass before commit.

## Evidence still requiring a real Colab runtime

Static checks cannot prove that current Colab images, GPU type, package
downloads, model licenses, and available VRAM complete a real inference. For
release acceptance, run each notebook on a Colab GPU, verify
`ready=true`, `device=cuda`, the exact family ID, and one short non-sensitive
TTS request. Record failures per model instead of substituting CPU or a
different model.

## Primary upstream references

- Qwen3-TTS: <https://github.com/QwenLM/Qwen3-TTS>
- Kokoro: <https://github.com/hexgrad/kokoro>
- Kokoro Vietnamese: <https://huggingface.co/contextboxai/Kokoro-Vietnamese>
- OmniVoice: <https://github.com/k2-fsa/OmniVoice>
- VibeVoice: <https://github.com/microsoft/VibeVoice>
- VieNeu-TTS: <https://github.com/pnnbao97/VieNeu-TTS>
- VoxCPM2: <https://github.com/OpenBMB/VoxCPM>
