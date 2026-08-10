# AI agent response — Subtitle OCR Pillow repair

Date: 2026-08-11

## Root cause

This is a second, independent Colab environment failure. `PIL/ImageText.py`
from Pillow 12 imports `_Ink`, but the runtime had an older
`PIL/_typing.py` without that symbol. Therefore Pillow had been partially
overwritten. The `ccache` warning is informational and is not the cause.

## Correction delivered

`c4689c1 fix: repair subtitle ocr pillow stack` is on `main`.

- The generated exact Subtitle OCR notebook force-reinstalls the coherent
  `Pillow==12.0.0` wheel after the fixed Paddle stack.
- Before importing PaddleOCR or starting Uvicorn, it imports both
  `PIL.ImageText` and `PIL._typing._Ink`, and asserts every pinned version.
- Notebook revision: `subtitle-ocr-2026-08-11.4`.

## Evidence

- Generated-notebook contract verification: **32/32 passed**.
- Pillow 12 wheel coherence check: **passed**.
- Full CTest: **39/39 passed, 0 failed** in 56.51 seconds.
- `graphify update .`: completed.

No GUI, live Colab GPU, or external worker was opened. No new EXE was built:
this is a notebook-only hotfix and portable `0.0.6.3` is unchanged.

## Required retry

Pull `main`, open
`notebooks/LA_STUDIO_SUBTITLE_OCR_PP_OCRV5_GPU.ipynb`, choose a GPU runtime,
then use **Runtime → Disconnect and delete runtime** and **Run all**. The
dependency cell itself now repairs Pillow. Before the worker starts, it must
print a line beginning:

`Verified Paddle-only OCR stack: ... 3.1.0 3.1.1 12.0.0`

If that line passes but the worker fails later, provide the new worker-log tail;
the reported `_Ink` import error cannot proceed past this new preflight.
