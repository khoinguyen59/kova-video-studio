# AI agent response - 0.0.2.28 packaged

Date: 2026-08-05

## Outcome

`0.0.2.27` called the Direct Colab Spleeter worker correctly; the reported
`CUDNN_FE_HEURISTIC_QUERY_FAILED` came from that remote worker while it sent a
long source through one FP16 ONNX/CUDA convolution. No local model or local GPU
was started for that job.

`0.0.2.28` keeps the exact upstream
`sherpa-onnx-spleeter-2stems-fp16` artifact and Direct Colab route. Its
audited worker creates ONNX Runtime CUDA sessions with the documented
`cudnn_conv_algo_search=DEFAULT`, performs a bounded CUDA startup probe before
exposing a URL, and processes long media in 20-second cores with 1.5-second
context. The desktop consumes only worker-reported progress and converts a
remote CUDA trace into an actionable message while retaining the full detail in
System Logs. It never falls back to a local model.

The Spleeter notebook pins the worker revision
`f1b26005b6e3677db444ac12774ba3eaf9d9b204` and checks both template SHA-256
values before launching it. The portable package now includes both worker
templates under `docs/colab-notebooks/workers`; an audit caught that omission
in the first staging attempt and the package was rebuilt after the CMake rule
and regression were added.

Two unrelated QML diagnostics on the same route were also corrected: the
Colab setup delegate no longer references an out-of-scope `root`, and the
Dubbing quality dialog updates its own `modelField`.

## Validation

- Python syntax and notebook JSON: PASS.
- Targeted Direct Colab separation, Dubbing, remote-contract and QML smoke:
  PASS.
- Full CTest: **39/39 PASS** in **86.87 seconds**.
- Regression now verifies exact model contract, CUDA-safe worker configuration,
  immutable notebook pin, no local fallback, concise CUDA failure handling and
  portable installation of worker templates.
- Graphify was updated after the code changes; graph outputs remain untracked.

## Package audit

- EXE: `out/LA-Studio-0.0.2.28/LA-Studio-0.0.2.28.exe`
- Source/FileVersion/ProductVersion: `0.0.2.28` / `0.0.2.28` / `0.0.2.28`
- SHA-256: `63BA1B5B36A70039ADAB92FA5DB607E0556A3C5A7A55B515B116B516D02A4D92`
- Verified: Qt Windows and offscreen plugins, FFmpeg/FFprobe, RuntimeHost,
  Subtitle OCR and Paddle runtime manifests, Spleeter notebook, both audited
  worker templates and the immutable worker revision.
- Internal-only caveat: eSpeak MSI is SHA-verified but unsigned.

## Live acceptance still required

No GUI or live Colab session was opened by the agent. In `0.0.2.28`, open the
Spleeter notebook, select a Colab GPU runtime, Run all, wait for `startup
probe: passed`, paste the printed URL/token into Dubbing -> Colab setup and
press Check Colab. Then run the reported long source through Isolator. A live
failure should include the final Colab traceback from the new notebook; it must
not be represented as an automated pass.

Source/test commits were pushed directly to `main`:
`f1b2600`, `166f245`, and `07ebcd1`.
