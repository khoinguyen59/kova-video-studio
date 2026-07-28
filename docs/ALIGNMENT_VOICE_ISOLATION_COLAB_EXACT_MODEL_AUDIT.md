# Forced Alignment and Voice Isolation exact-model Colab audit

**Source status:** implemented and locally verified.

**Live GPU status:** pending a real Colab GPU session. Passing compile, QML,
controller and notebook-source tests is not a substitute for completing one
real inference request per model.

## Exact route matrix

| Feature | Gallery model ID | Notebook opened by the UI | Worker implementation |
| --- | --- | --- | --- |
| Forced Alignment | `wav2vec2-aligner-zh` | `LA_STUDIO_ALIGNMENT_WAV2VEC2_ZH_GPU.ipynb` | CrispASR Wav2Vec2 Chinese aligner, CUDA build |
| Forced Alignment | `canary-ctc-aligner` | `LA_STUDIO_ALIGNMENT_CANARY_CTC_GPU.ipynb` | CrispASR Canary CTC aligner, CUDA build |
| Forced Alignment | `mms-forced-aligner-onnx` | `LA_STUDIO_ALIGNMENT_MMS_ONNX_GPU.ipynb` | MMS 300M ONNX FP16 with `CUDAExecutionProvider` |
| Forced Alignment | `qwen3-forced-aligner-0.6b` | `LA_STUDIO_ALIGNMENT_QWEN3_0_6B_GPU.ipynb` | Qwen3 ForcedAligner 0.6B on CUDA |
| Voice Isolation | `sherpa-onnx-spleeter-2stems-fp16` | `LA_STUDIO_SEPARATION_SPLEETER_2STEMS_GPU.ipynb` | sherpa-onnx Spleeter vocals/accompaniment FP16, CUDA provider |
| Voice Isolation | `sherpa-onnx-uvr-vocals-ft` | `LA_STUDIO_SEPARATION_UVR_VOCALS_GPU.ipynb` | sherpa-onnx UVR-MDX-NET Vocals FT, CUDA provider |

The generic `LA_STUDIO_ALIGNMENT_GPU.ipynb` and
`LA_STUDIO_SEPARATION_GPU.ipynb` files remain for legacy and Dubbing
compatibility. The Alignment and Voice Isolation galleries no longer open
those generic workers.

## Desktop behavior

- Selecting a gallery card sets the exact model before opening the notebook.
- The Colab button opens the matching notebook from the public GitHub branch.
- The settings panel displays both the selected model ID and notebook name.
- Changing the model cancels the active request and clears the prior worker
  URL and token, preventing one model from accidentally calling another
  model's temporary worker.
- Every inference request carries the selected model ID. A late response from
  an older worker session is ignored.
- Alignment language choices are constrained by the selected implementation:
  Chinese-only for Wav2Vec2, the supported European set for Canary, the
  official 11-language set for Qwen3 and the multilingual set for MMS.

## Worker contract

Each generated notebook:

- refuses CPU fallback and requires CUDA before starting;
- advertises only its bound model from `/v1/capabilities`;
- rejects a different request model with HTTP 409;
- generates a new bearer token and exposes the worker through a temporary
  Cloudflare URL;
- contains no API Gateway URL, key, forwarding or fallback path.

## Verification performed

- Six generated notebook worker programs parse as valid Python.
- All six define the exact model adapter before FastAPI routes are created.
- The MSVC release target compiled successfully as
  `out/build/windows-msvc-release/LA-Studio-0.0.0.9.exe`.
- `TestRemoteExecution`, `TestColabAlignmentRunner`,
  `TestColabSeparationRunner` and `QmlRouteSmoke` passed 4/4 in offscreen mode.
- `scripts/verify_remote_feature_surface.ps1` passed 8/8 direct Colab route
  checks.

## Remaining live acceptance

For every row in the route matrix, start the notebook with a Colab GPU, verify
`/health` reports `ready=true` and `device=cuda`, verify the capability returns
only the expected model, then complete one safe real request and inspect the
returned timestamps or output audio artifacts. Until that evidence exists,
this feature group is source-complete but not release-accepted.
