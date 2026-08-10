# AI agent response — Subtitle OCR transitive Torch repair

Date: 2026-08-11

## Root cause confirmed

The reported log is from the newer notebook: it no longer imports Torch
directly. It instead fails on this transitive chain:

`PaddleOCR 3.1.1 → unconstrained newer PaddleX → ModelScope → Torch`.

When ModelScope imports the preinstalled Colab Torch stack after Paddle has
loaded its NCCL runtime, Torch fails with
`libtorch_cuda.so: undefined symbol: ncclCommShrink`. The later connection
refusal is simply because Uvicorn never reached its `/health` bind.

## Source correction

Commit `b26ba3a fix: pin subtitle ocr colab paddle stack` is on `main`.
It changes the generated exact-model notebook to force-reinstall the bounded
Paddle-only stack:

- `paddlepaddle-gpu==3.1.0`
- `paddleocr==3.1.1`
- `paddlex[ie,multimodal,ocr,trans]==3.1.0`

PaddleOCR 3.1.1 specifies only a lower PaddleX bound. The inspected PaddleX
3.1.0 wheel has no ModelScope import in its official-model resolver, unlike
the version shown in the failure trace. The notebook now also runs the real
`from paddleocr import PaddleOCR` and CUDA probe before it starts the worker;
it reports the exact installed versions immediately if setup remains wrong.
The worker revision is `subtitle-ocr-2026-08-11.3`.

## Evidence

- Regenerated notebook verification: **32/32 passed**.
- `graphify update .`: completed.
- Full CTest printed **39/39 passed, 0 failed**. The outer terminal deadline
  occurred only after that completion summary, so its process exit code was
  not separately captured.
- No desktop GUI, live Colab runtime, or external worker was opened.

## Required live retry

Open the regenerated
`notebooks/LA_STUDIO_SUBTITLE_OCR_PP_OCRV5_GPU.ipynb` from `main`, select a
GPU runtime, then use **Runtime → Disconnect and delete runtime** before
**Run all**. A previous runtime can retain the newer PaddleX package. The
second cell must print `Verified Paddle-only OCR stack: ... 3.1.0 3.1.1`
before the worker is launched.

No new EXE was packaged: this is a Colab-notebook-only correction, and the
existing `0.0.6.3` artifact is unchanged.
