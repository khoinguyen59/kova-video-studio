# AI agent response - 0.0.2.21 Dubbing entry and automatic setup

Date: 2026-08-04

## Completed

- Dubbing entry is blocked by a modal choice: Automatic or Step-by-step. It
  cannot be bypassed by Escape, outside click, or a close button; leaving
  Dubbing is the only non-selection action.
- The choice is persisted with the project without altering existing segments,
  artifacts, node settings, or workflow resume state. Step-by-step enters the
  first valid Import state for a new project.
- Automatic opens a setup wizard before workspace entry. It uses persisted
  source/target language as the single source of truth, validates missing
  required language in place, shows active route/model/variant/language state,
  restricts Colab setup to active Direct Colab nodes, and invalidates approval
  after relevant configuration changes.

## Evidence

- Targeted Dubbing controller/QML regression: 5 passed, 0 failed.
- QML lint PASS. Full CTest: 39/39 PASS in 31.97 seconds.
- No desktop GUI or live Colab worker was opened or controlled.

## Package

- Executable: `out/LA-Studio-0.0.2.21/LA-Studio-0.0.2.21.exe`
- FileVersion/ProductVersion: `0.0.2.21`
- SHA-256: `CDEF6AA0D54A7BE50F8F7F04DC157532AE8F2E96BE1E59B4D1E37E4B4F5431CE`
- Portable audit PASS: qwindows/qoffscreen, RuntimeHost, FFmpeg/FFprobe,
  Tesseract and the packaged PaddleOCR health check (`ok=true`,
  `manifestVerified=true`) verified.

## Still manual/live only

Interactive desktop flow and a live Direct Colab worker/notebook remain manual
acceptance gates. Automated controller/QML/offscreen checks do not replace
them.
