# AI agent response - 0.0.6.0 internal package

Date: 2026-08-10

## Result

Created the internal portable package:

- [LA-Studio-0.0.6.0.exe](C:/Users/Nguyen%20Trong%20Khoi/Downloads/LA-STUDIO/out/LA-Studio-0.0.6.0/LA-Studio-0.0.6.0.exe)
- SHA-256: `9768C4C7990B6FD1164EEFC455683B4DECFBA4DABB778A8152FD6B02D5F31736`
- FileVersion/ProductVersion: `0.0.6.0` / `0.0.6.0`

The version scheme is now enforced in the build toolchain. A version must use
exactly four single digits and carry at `9`: `0.0.0.9` then `0.0.1.0`.
`0.0.2.40` is rejected by CMake, the build/package scripts, and release-tag
validation. This package includes the current Dubbing workspace improvements:
source controls can collapse after choosing media, the video/OCR area retains
a useful size, and other setup controls remain accessible while a job is
visible in Activity.

## Verification

- PowerShell parsing: build, package, and release-version scripts passed.
- Version behavior: `v0.0.6.0` accepted; `v0.0.2.40` and CMake version
  `0.0.2.40` rejected as intended.
- QML lint: passed.
- Targeted media/remote/offscreen QML regression: **4/4 passed**.
- Full CTest: **39/39 passed**.
- Portable package audit: passed. It verified root EXE/RuntimeHost, Qt
  `qwindows` and `qoffscreen`, FFmpeg/FFprobe, yt-dlp `2026.07.04`, Tesseract
  `5.5.1`, Colab notebook payloads, and the prepared PaddleOCR health
  manifest. Package staging and license manifests both reported 19 required
  artifacts.

## Retention and scope

There are exactly three remaining package folders:
`LA-Studio-0.0.2.39`, `LA-Studio-0.0.2.40`, and `LA-Studio-0.0.6.0`.
No GUI, browser, live Douyin, or live Colab worker was opened for this build.
The package is internal-only because the staged eSpeak component is
SHA-verified but unsigned.
