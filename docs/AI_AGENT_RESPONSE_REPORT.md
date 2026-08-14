# AI agent response — local download and resumable Dubbing projects

## 2026-08-14 — completed: package 0.0.6.9

### Delivered

- Replaced the media-download Colab worker route with a local CPU downloader.
  Download media now uses SHA-256-pinned `yt-dlp`; it does not ask for Colab
  URL/token, GPU, or API Gateway credentials.
- Accepted full public-share text and extracts its HTTPS media URL. A Netscape
  cookie file remains optional; LA Studio does not read browser cookies.
- Kept Colab only on actual model/GPU tasks. For media manually made in Colab,
  the UI tells the user to open the Colab **Files** sidebar, download the exact
  output printed by the notebook final cell, then select that local file.
- Added Dubbing project **New**, **Open**, **Save**, and **Save As**. Save As is
  atomic and reopening a `.ladub.json` restores project state for continued
  work, including recovery guidance for an interrupted workflow.

### Evidence

- Full CTest: **39/39 passed, 0 failed**, rebuilt with source version `0.0.6.9`.
- Generated exact-model notebooks: **32/32 passed**.
- Direct-Colab contract runner: **9 capability paths passed**.
- `graphify update .`, Python compilation, and `git diff --check`: passed.
- Internal portable package:
  `C:\Users\Nguyen Trong Khoi\Downloads\LA-STUDIO\out\LA-Studio-0.0.6.9\LA-Studio-0.0.6.9.exe`.
  FileVersion/ProductVersion are `0.0.6.9`; SHA-256 is
  `CDC449056C120B6F00CE562C24F163A44E49C1A88E1229438E5C37BF16B62361`.
  Required Qt, runtime-host, FFmpeg/FFprobe, `yt-dlp 2026.07.04`, and license
  artifacts were verified in the staged package.

### Boundary

No desktop GUI or live Colab session was opened. The successful tests prove
source, controller, offscreen-QML, generated-notebook, contract, and staged
runtime behavior. A user must still run a fresh Colab notebook to accept a
live temporary GPU worker. The package is internal-only because its verified
eSpeak payload is unsigned.

## 2026-08-15 - completed: project-first gate and package 0.0.7.0

### Delivered

- Added a global New/Open project gate before operational feature routes,
  including Dubbing. Settings and model browsing remain available before a
  project. Importing media through the controller now requires an existing
  project instead of creating one implicitly.
- Added explicit model chooser actions for local apply, independent Colab
  selection, and non-destructive close.

### Evidence

- Targeted Dubbing project regression: **98 passed, 0 failed, 5 skipped**.
- Offscreen QML route smoke: **3/3 passed**; full CTest: **39/39 passed**.
- QML lint exited 0 with existing warnings; `git diff --check` and
  `graphify update .` passed.
- Portable internal EXE:
  `C:\Users\Nguyen Trong Khoi\Downloads\LA-STUDIO\out\LA-Studio-0.0.7.0\LA-Studio-0.0.7.0.exe`
  (File/Product Version `0.0.7.0`, SHA-256
  `5B3B7876C80EC473C89A7ECC96E793545EEA3AAB19DAF6B98E5558C7EF88814E`).
  The staged package contains Qt/offscreen, runtime host, FFmpeg/FFprobe,
  yt-dlp, Tesseract, and the isolated PaddleOCR runtime. Package offscreen
  smoke exited 0.

### Boundary

Source commit `339cfa4` was pushed directly to `origin/main`. No visible GUI or
live Colab worker was used. This is an internal package only because the
hash-verified eSpeak payload is unsigned.
