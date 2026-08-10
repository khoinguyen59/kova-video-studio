# AI agent response — 0.0.6.3 internal package

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

## Verification performed

- QML lint: passed.
- Full CTest at version `0.0.6.3`: **39/39 passed**.
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
