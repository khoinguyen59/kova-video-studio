# Subtitle OCR runtime manifest

LA Studio's Subtitle OCR feature runs the Tesseract command-line runtime on
the local CPU. It is intentionally **not** downloaded by the application, and
no OCR model is fetched when the user opens the feature or presses Run.

## Supplying a reviewed runtime

Use one of the following explicit deployment choices:

1. Place a reviewed `tesseract.exe` and its `tessdata` directory in
   `subtitle-ocr/` beside `LA-Studio-<version>.exe`.
2. Set the process environment variable `LASTUDIO_TESSERACT` to the absolute
   path of a reviewed `tesseract.exe`. Configure that runtime's data path
   (normally `TESSDATA_PREFIX`) according to the upstream Tesseract guidance.
3. Install a reviewed system Tesseract runtime that is available on `PATH`.

The app resolves only these local locations. It never runs a package manager,
opens a web page, or downloads a language file. Use **Refresh OCR runtime** in
the Subtitle OCR page after changing an installed runtime.

Each selected language must have its matching Tesseract trained-data file
(for example, `eng.traineddata`, `vie.traineddata`, or `chi_sim.traineddata`).
Before extracting video frames, LA Studio invokes `tesseract --list-langs` and
stops with an actionable error if the requested language is unavailable.

## Provenance and licensing

Tesseract OCR is an Apache-2.0 project. This package ships this manifest and
does **not** ship a Tesseract executable or trained-data files. Release owners
who add a reviewed runtime must include the exact upstream license texts,
version, source URL, and checksums in the release payload before distribution.

- Upstream project: <https://github.com/tesseract-ocr/tesseract>
- Upstream language data: <https://github.com/tesseract-ocr/tessdata>
- License: <https://www.apache.org/licenses/LICENSE-2.0>
