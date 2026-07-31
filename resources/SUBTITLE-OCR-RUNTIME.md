# Subtitle OCR managed runtime

Subtitle OCR uses the CPU Tesseract command-line runtime. It is not downloaded
when the page opens and it is not downloaded when **Run Subtitle OCR** is
pressed. A user must explicitly choose **Install app-managed runtime** and then
choose any missing language packs.

## Install behaviour

- The package contains `runtime-manifest.json` with the pinned HTTPS URL,
  version and SHA-256 for the Windows x64 Tesseract installer, plus each
  language pack. The app verifies SHA-256 before executing or activating data.
- The installer runs silently only after that click and targets application data
  (`subtitle-ocr/runtime`), never Program Files. It should not request admin
  rights. The new runtime is staged and atomically swapped only after a usable
  executable and manifest exist.
- English (`eng`), Vietnamese (`vie`), Simplified Chinese (`chi_sim`),
  Traditional Chinese (`chi_tra`), Japanese (`jpn`) and Korean (`kor`) are
  individually selectable. A failed, cancelled or checksum-invalid language
  download never replaces an existing verified pack.
- Progress is shown only from actual received/total transfer bytes. Retry and
  cancel keep the previous runtime untouched.

The app uses the app-owned runtime on later launches. `LASTUDIO_TESSERACT` is
an advanced explicit override; if it is set, the page identifies it as an
external runtime and does not alter it. `PATH` is not used as an implicit
Tesseract selection source. A Tesseract `--list-langs` preflight still blocks
OCR before frame extraction if the selected language is unavailable.

## Provenance and licensing

Tesseract and `tessdata_fast` are Apache-2.0. This portable package ships the
manifest and this notice; it does not bundle the Tesseract binary or trained
data. See `runtime-manifest.json` for pinned provenance.

- Upstream engine: <https://github.com/tesseract-ocr/tesseract>
- Upstream language data: <https://github.com/tesseract-ocr/tessdata_fast>
- License: <https://www.apache.org/licenses/LICENSE-2.0>
