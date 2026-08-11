# AI agent response — Subtitle OCR isolated Colab repair

Date: 2026-08-11

## What the new log proved

The previous Pillow force-reinstall ran in Colab's mutable global Python. The
same `_Ink` error proves that this global environment could still resolve a
mixed `PIL` directory. The `ccache` warning remains unrelated.

## Correction delivered

Commit `23e7d0d fix: isolate subtitle ocr colab environment` is on `main`.

- Every **Run all** now creates a cleared dedicated venv at
  `/content/la_studio_subtitle_ocr_venv`.
- Paddle, PaddleOCR, PaddleX, Pillow, FastAPI and Uvicorn are installed inside
  it; the old Colab global `PIL` is never reused.
- The preflight and Uvicorn worker both use the venv Python. `PYTHONPATH` and
  user-site packages are removed, and preflight asserts it is in the venv.
- The probe imports `ImageText` and `_Ink`, then PaddleOCR and CUDA, before it
  allows the worker to start.

## Evidence

- Notebook contract verification: **32/32 passed**.
- Generated venv probe syntax/environment contract: **passed**.
- Clean local virtual environment: Pillow 12 imported `ImageText` and `_Ink`:
  **passed**.
- Full CTest printed **39/39 passed, 0 failed** in 58.38 seconds.
- No live Colab GPU worker or desktop GUI was opened.

No EXE was created: this is a Colab-notebook-only repair.

## Required retry

Pull `main`, open the current
`notebooks/LA_STUDIO_SUBTITLE_OCR_PP_OCRV5_GPU.ipynb`, choose a GPU runtime,
then **Runtime → Disconnect and delete runtime → Run all**. This notebook no
longer relies on global Pillow. It must print:

`Verified isolated OCR stack: ... 3.1.0 3.1.1 12.0.0`

Only after that line should it start the worker. If it fails, attach the full
output of the venv installation/probe cell; it will identify the isolated
package stage rather than a global Colab import.
