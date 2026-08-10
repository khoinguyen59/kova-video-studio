# AI agent response — 0.0.6.3 package and Subtitle OCR notebook hotfix

Date: 2026-08-11

## Result

The portable internal package is available at:

`out/LA-Studio-0.0.6.3/LA-Studio-0.0.6.3.exe`

- SHA-256: `B3322735B67EEE453FA5549AB35CB5DC95D2E578B68A9BEC7BCDE25F1FDB3137`
- FileVersion / ProductVersion: `0.0.6.3`
- Source commit on `main`: `6219edc fix: rework dubbing workbench layout`

This package contains the Dubbing workbench restructure: the task shelf and
review panel are no longer permanently overlaid on the central preview,
Timeline is an independent full-width lower workspace, and System Logs remain
accessible only from Settings. The existing LA Studio processing features and
routes are retained. A lightweight QML preview harness is included for future
layout checks without a full production package build.

## Post-package Subtitle OCR notebook hotfix

The generated `LA_STUDIO_SUBTITLE_OCR_PP_OCRV5_GPU.ipynb` worker no longer
imports PyTorch. It sets and verifies `gpu:0` entirely through Paddle before
serving PP-OCRv5. This removes the observed Colab dynamic-link conflict where
Paddle's NCCL runtime was loaded before `libtorch_cuda.so`, leaving the Torch
symbol `ncclCommShrink` unresolved. The worker revision is now
`subtitle-ocr-2026-08-11.2`; source commit: `adc7e04` on `main`.

This source/notebook fix is deliberately **not** inside the already-created
`0.0.6.3` EXE. Open the updated notebook from GitHub or this source tree for
the immediate Colab retry; a later package candidate is required to ship it
inside the desktop artifact.

## Verification performed

- QML lint: passed.
- Full CTest at version `0.0.6.3`: **39/39 passed**.
- Exact-model notebook generator/byte-for-byte verifier: **32/32 passed**;
  it also asserts Subtitle OCR has no Torch CUDA import and verifies Paddle
  establishes `gpu:0` before startup.
- `graphify update .`: completed after source changes.
- Portable staging passed its build-time manifest checks.
- Independent package audit verified the versioned executable,
  `LAStudioRuntimeHost.exe`, Qt `qwindows.dll` / `qoffscreen.dll`, FFmpeg,
  FFprobe, yt-dlp, the managed Douyin helper, Subtitle OCR, PaddleOCR, and the
  Spleeter / OmniVoice Colab notebooks.
- The staged application passed a five-second hidden offscreen Qt/QML startup
  smoke check. It was then stopped by the test; no visible desktop GUI was
  opened.

## Limits and retention

No visible GUI, browser, live video, Douyin link, API Gateway, or Colab worker
was opened; live-service and visual acceptance remain separate checks.
The package is internal-only because its eSpeak payload is SHA-256 verified but
unsigned. The current `out` directory still contains four candidates
(`0.0.6.0` through `0.0.6.3`); the requested three-version retention cleanup
was not completed during this packaging pass.
