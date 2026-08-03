# AI agent response - 0.0.2.20 Dubbing and Voice Clone Isolator

Date: 2026-08-04

## Completed

- Dubbing renders nine production-backed stages without migrating or deleting
  durable workflow IDs. Alignment/Subtitle opens the existing subtitle
  review/editor and can route to Alignment Studio.
- Voice Cloning exposes `Clean reference audio with Isolator`. Local and
  Direct Colab reuse the existing Isolator route/model; with it enabled clone
  receives only a validated Vocals WAV. Original and Background are preview
  artifacts, not fallbacks. Cache identity includes source fingerprint and
  Isolator route/model configuration.
- Fixed real integration defects found by regression: synchronous state-reset
  signals could finish reference cleanup before the worker started, and Direct
  Colab separation did not propagate its existing loopback-test-only flag to
  the worker runner. QML route/model formatting was corrected.

## Evidence

- QML lint PASS.
- Targeted `TestDubbingProject`, `TestColabSeparationRunner`,
  `PrepareQmlRouteSmokeRuntime`, `QmlRouteSmoke`: 4/4 PASS.
- Full CTest: 39/39 PASS in 69.47 seconds.
- No desktop GUI or live Colab worker was opened or controlled.

## Package

- Executable: `out/LA-Studio-0.0.2.20/LA-Studio-0.0.2.20.exe`
- FileVersion/ProductVersion: `0.0.2.20`
- SHA-256: `338B035A711BE68743EFC98F500FA4A36C2EA9712F264C0F63C5751464BB2F7B`
- Portable package audit PASS: qwindows/qoffscreen, runtime host, FFmpeg,
  OCR/Tesseract, Paddle health (`ok=true`, `manifestVerified=true`) and
  third-party notices present. Internal-only: eSpeak MSI is hash-verified but
  unsigned.

## Still manual/live only

Desktop interactive acceptance and a live Direct Colab worker/notebook remain
manual checks. Automated offscreen and loopback integration coverage passed,
but neither is claimed as a live Colab or manual desktop pass.
