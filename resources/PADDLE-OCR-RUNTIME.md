# PaddleOCR CPU runtime

Subtitle OCR uses the upstream PaddleOCR Python API through the small
`paddle_ocr_worker.py` adapter. The adapter owns no recognition model code: LA
Studio supplies already cropped subtitle frames, while PaddleOCR performs
multilingual text detection and recognition.

## Package contract

- The portable package contains an isolated Python runtime, the pinned
  PaddleOCR dependencies, PP-OCRv6 tiny detection and recognition models, and
  this adapter. It never probes a system Python or downloads models at runtime.
- `runtime-manifest.json` pins the upstream repository/commit, engine version,
  worker SHA-256, isolated Python SHA-256, and a deterministic SHA-256 over
  the complete shipped model cache.
- Before OCR starts, the child worker validates that manifest and checks the
  exact model cache. The application accepts no local Paddle response unless
  the worker reports `manifestVerified: true`; it does not silently switch to
  Tesseract.
- The runtime is built with CPU-only PaddlePaddle. GPU use remains the separate
  Direct Colab route and is never an implicit fallback.

## Provenance and licensing

- Upstream OCR: <https://github.com/PaddlePaddle/PaddleOCR>
- Pinned upstream commit: `2661c7c0ef5c613e8f93c6e93b2e052399f0f854`
- Pinned upstream release: `v3.7.0`
- Engine/model profile: `PaddleOCR 3.7.0`, `PP-OCRv6_tiny_det` and
  `PP-OCRv6_tiny_rec`
- PaddleOCR, PaddlePaddle and PaddleX are Apache-2.0. The package stages the
  installed Python distributions' license material and metadata under
  `licenses/paddle-ocr-python/`.
- The isolated interpreter is CPython. Its exact archive and executable hashes
  are recorded in the runtime manifest used for the candidate package.
