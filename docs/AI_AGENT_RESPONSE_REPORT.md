# AI agent response - 0.0.2.18 transcript reconciliation

Date: 2026-08-03

## Completed

The active request in `AI_AGENT_REQUEST.md` is completed in source commit
`3db38db` on `main`.

- Added one persisted transcript-source selector for STT only, OCR only and
  STT+OCR in both Dubbing Transcribe and Direct Colab setup.
- Persisted the actual STT provider/model; reload, readiness, cards and
  validation use that project route rather than a stale workflow template.
- Added evidence-preserving STT/OCR reconciliation: ask/prefer policies,
  manual and batch decisions, conflict blocking before Translate, and source
  text/provenance retained after reload.
- Added structured-capability gating for AI reconciliation. Plain translation
  models cannot be treated as an LLM; suggestions remain pending explicit
  accept/reject and never change the execution provider implicitly.
- Corrected the test runner to invoke CTest fixtures, and corrected packaging
  to use the controlled prepared PaddleOCR runtime by default when no override
  is supplied.

## Evidence

- `TestDubbingProject`: 82 passed, 0 failed, 5 skipped.
- Full CTest: 39/39 PASS in 64.21 seconds. This includes the Subtitle OCR
  runtime fixture, remote contract and QML smoke.
- Production code-only OCR E2E on
  `C:/Users/Nguyen Trong Khoi/Downloads/1.mp4`: PASS in 599867 ms.
  PaddleOCR 3.7.0 processed 1125 frames and produced 430 matching Standalone
  and Dubbing cues; Dubbing reused the completed artifact, no Tesseract
  fallback was used, and no child worker remained alive.
- E2E artifacts:
  `out/ocr-e2e-new/standalone-zh-Hans.srt`,
  `out/ocr-e2e-new/dubbing-zh-Hans.srt`,
  `out/ocr-e2e-new/transcript-zh-Hans.txt`, and
  `out/ocr-e2e-new/OCR_TEST_RESULT.md`.

## Package

- Executable:
  `out/LA-Studio-0.0.2.18/LA-Studio-0.0.2.18.exe`
- FileVersion/ProductVersion: `0.0.2.18`
- SHA-256:
  `2FC962F21721E285927ADD7F6201A11FA9DA46D10242BA4787A4015F02CD8381`
- Audit PASS: 26 required runtime/license artifacts, Qt `qwindows.dll`,
  FFmpeg/FFprobe, and isolated PaddleOCR health with `ok=true` and
  `manifestVerified=true`.
- Internal-only caveat remains: the eSpeak MSI is hash verified but unsigned.

## Still manual/live only

No GUI was opened. Live Direct Colab worker/notebook/tunnel behavior and
desktop interaction acceptance remain unverified; automated contracts and
offscreen smoke do not represent a live-worker or manual desktop PASS.
