# AI agent response - 0.0.6.2 internal package

Date: 2026-08-10

## Result

The internal portable package is available at:

`out/LA-Studio-0.0.6.2/LA-Studio-0.0.6.2.exe`

- SHA-256: `52B7B4228742C5C769F80C1EE9E315F55B6406FFBF32E0FE1F6FE7FFBFE05B45`
- FileVersion / ProductVersion: `0.0.6.2`
- Source commit on `main`: `9b39b3c fix: prioritize dubbing preview workspace`

The change is restricted to the Dubbing workspace layout. It keeps the
existing LA Studio features and adds these controls:

- **Open video** / **Replace video** is always available in the preview header.
- Selecting a local source collapses the link, cookie, and Chromium setup
  drawer deterministically; the drawer is still available through **Change /
  download source** when needed.
- The preview frame can be switched between **Fit source**, **16:9**, **9:16**,
  and **1:1**. The source remains uncropped and uses aspect-fit rendering.
- **Focus video** removes the timeline and project-control panel from the
  workspace temporarily. The new sliders button beside the History toggle
  shows or hides the lower Language & Voice, Dubbing Quality, Speakers, and
  Output panel independently.
- OCR and subtitle-preview coordinates now follow the selected preview frame,
  so changing the display ratio does not offset the scan region or subtitles.

Only three versioned portable folders are retained:
`LA-Studio-0.0.6.0`, `LA-Studio-0.0.6.1`, and `LA-Studio-0.0.6.2`.

## Verification performed

- QML lint: passed.
- Targeted media, remote, and offscreen-QML regressions: **4/4 passed**.
- Full CTest at version `0.0.6.2`: **39/39 passed**.
- `graphify update .`: completed after the source changes.
- Portable package staging completed. Independent artifact checks confirmed
  the versioned EXE, `LAStudioRuntimeHost.exe`, `qwindows.dll`, `qoffscreen.dll`,
  FFmpeg, FFprobe, yt-dlp, the Spleeter notebook, and remote-worker document.

## Limits of this verification

No visible desktop GUI, browser, live Douyin link, API Gateway, or Colab worker
was opened. The offscreen QML regression does use the production file-picker
boundary and verifies the post-selection drawer, preview-ratio logic, and
project-control toggle, but it is not a substitute for manually viewing a
real video in the packaged app. The package remains internal-only because its
eSpeak payload is SHA-256 verified but unsigned.
