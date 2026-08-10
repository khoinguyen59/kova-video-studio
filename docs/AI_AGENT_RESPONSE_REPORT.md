# AI agent response - 0.0.6.1 internal package

Date: 2026-08-10

## Result

The new internal portable package is available at:

`out/LA-Studio-0.0.6.1/LA-Studio-0.0.6.1.exe`

- SHA-256: `5C32FC68A1F7CBAF50873DFADC7F5D23B150B9AF77E6A9B54AEA40F9117CD39E`
- FileVersion: `0.0.6.1`
- ProductVersion: `0.0.6.1`
- Source/version commit on `main`: `718f2e6 build: advance internal candidate to 0.0.6.1`

This package includes the completed Dubbing preview workspace source batch:
the central 16:9 video canvas is wider by default, loaded-source controls are
bounded and scrollable, **Focus video** temporarily clears adjacent workspaces,
and the Timeline has its own height drag handle. Existing Dubbing functions,
OCR, media queue, Chromium/cookie controls, Direct Colab, API Gateway and
exports remain in the product.

Only two package folders are retained: `LA-Studio-0.0.6.0` and
`LA-Studio-0.0.6.1`. No older candidate was overwritten.

## Verification

- QML lint passed.
- Targeted media/remote/offscreen-QML regression: **4/4 passed**.
- Full CTest: **39/39 passed** (57.83 seconds).
- Package staging and license audits both passed with **19 required artifacts**.
- Independent artifact checks found Qt `qwindows.dll` and `qoffscreen.dll`,
  `LAStudioRuntimeHost.exe`, FFmpeg/FFprobe, yt-dlp, Subtitle OCR/PaddleOCR
  manifests, the Spleeter Colab notebook and third-party notices.

The package is internal-only: its eSpeak payload is SHA-256 verified but
unsigned. No GUI, browser, live Douyin URL, API Gateway or Colab worker was
opened, so those remain manual acceptance checks rather than claimed passes.
