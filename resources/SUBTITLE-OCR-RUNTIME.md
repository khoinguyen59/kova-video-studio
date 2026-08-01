# Subtitle OCR managed runtime

Subtitle OCR uses the CPU Tesseract command-line runtime. The portable package
ships an integrity-checked Tesseract executable built from the pinned vcpkg
source package. It is not downloaded when the page opens and no external
Windows installer is ever started. A user only chooses any missing language
packs.

## Install behaviour

- The package contains `runtime-manifest.json` with the Tesseract version,
  package-generated executable SHA-256 and a successful `tesseract --version`
  health check, plus each language-pack URL and SHA-256. The app validates the
  packaged executable before treating it as ready.
- The executable remains immutable package content. Language data is installed
  atomically under application data (`subtitle-ocr/runtime/tessdata`), never
  Program Files and never with administrator privileges.
- English (`eng`), Vietnamese (`vie`), Simplified Chinese (`chi_sim`),
  Traditional Chinese (`chi_tra`), Japanese (`jpn`) and Korean (`kor`) are
  individually selectable. A failed, cancelled or checksum-invalid language
  download never replaces an existing verified pack.
- Progress is shown only from actual received/total transfer bytes. Retry and
  cancel keep the previous runtime untouched.

`LASTUDIO_TESSERACT` is an advanced explicit override; if it is set, the page
identifies it as an external runtime and does not alter it. `PATH` is not used
as an implicit Tesseract selection source. For the bundled runtime the app sets
`TESSDATA_PREFIX` only on its own OCR worker processes so downloaded language
data remains in application data. A Tesseract `--list-langs` preflight still
blocks OCR before frame extraction if the selected language is unavailable.

## Provenance and licensing

Tesseract and `tessdata_fast` are Apache-2.0. This portable package ships the
runtime binary, package manifest and this notice; it does not bundle trained
data. See `runtime-manifest.json` for pinned provenance.

- Upstream engine: <https://github.com/tesseract-ocr/tesseract>
- Upstream language data: <https://github.com/tesseract-ocr/tessdata_fast>
- License: <https://www.apache.org/licenses/LICENSE-2.0>
