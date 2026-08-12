# AI agent response — Dubbing workspace and Subtitle OCR bootstrap

## 2026-08-13 - Automatic Dubbing audit outcome

I audited only the current Automatic Dubbing A-B contract. The eight visible
stages, aggregate readiness source, Direct Colab/local separation, and wizard
route/model flow were already covered by controller and offscreen-QML
regressions. One real presentation defect remained: a stage card did not show
the exact model variant although Direct Colab verification is bound to that
variant. It is now shown as `fixed` for the immutable Colab notebook or as the
worker-provided value.

- Targeted Dubbing/controller/offscreen QML: **3/3 PASS**.
- Full CTest: **39/39 PASS** in 86.27 seconds.
- QML compiled in the normal target; standalone `qmllint` exited successfully
  with existing import/unqualified-access warnings.
- No GUI and no package/EXE were created. Source commit `45626f1` is pushed to
  `main`.

Date: 2026-08-11

## 2026-08-12 — latest response: Subtitle OCR Colab bootstrap `.13`

The requested notebook has been overwritten with the generated `.13` version:
`C:\\Users\\Nguyen Trong Khoi\\Downloads\\LA_STUDIO_SUBTITLE_OCR_PP_OCRV5_GPU.ipynb`
(SHA-256 `D2092912774A0E33421D77ACDACF9EDF13BC3B00BADAD41AD6B51802F5B7FFBB`).

### Actual failure and repair

- The previous installation still allowed PaddleOCR metadata to resolve
  `paddlex[ie,multimodal,ocr,trans]`. That unrelated chain requires source-only
  `GPUtil`, which is why wheel-only installation ended as a bare
  `CalledProcessError`.
- Bootstrap `.13` fetches the exact CPython 3.12 Linux CUDA 11.8
  `paddlepaddle-gpu==3.1.0` wheel directly, including its declared CUDA/NCCL
  dependencies. It no longer contacts the flaky Paddle package index.
- It installs only `paddlex[ocr]==3.1.0`, then
  `paddleocr==3.1.1 --no-deps`, keeping the working OCR dependency boundary.
- A real blank-image PP-OCRv5 CUDA inference must finish before `/health` can
  be ready. The worker is isolated from global packages and explicitly uses
  the BOS PaddleX model source.

### Evidence and next run

- Exact Linux/CPython 3.12 install transaction: the three install stages
  completed. Its only runtime stop was missing `libcuda.so.1` on a host without
  a GPU driver, which is expected outside Colab.
- Generated exact-model notebook verifier: **32/32 PASS**.
- Full CTest: **39/39 PASS** in 93.78 seconds.
- `git diff --check` and `graphify update .`: completed.
- Live Colab GPU execution is not claimed here. Start a fresh GPU runtime and
  run the replacement file. The first cell must print
  `subtitle-ocr-bootstrap-2026-08-12.13`; otherwise Colab is using an older
  downloaded copy.

## Completed

- Reduced Automatic Dubbing to the eight user-facing stages: **Import/Download**, **Normalize**, **Isolator**, **Transcribe/STT**, **Alignment/Subtitle**, **Translate**, **TTS**, and **Export/Output**. Internal timing, translation-review, and mix nodes remain in their owning visible stage; they are not extra header stages.
- The header now takes its stage list from the same controller contract as preflight, preventing a QML-only nine-stage list from drifting from the backend. At compact widths its action cluster uses icon-only controls with accessible labels/tooltips, so labels such as `Workflow` cannot be clipped to `Wor`.
- Kept Dubbing as a fixed-pane editor: the left task shelf and the right inspector are layout items that consume width and push the center workspace. The task rail is the only header area that scrolls. The preview defaults to 1040 px and the timeline to 340 px; the existing video aspect and focus controls remain available.
- Language pair and Fast/Adaptive/Custom quality are chosen in the existing project-setup dialog after Automatic or step-by-step is chosen. They are not a permanent strip beneath the editor.
- Regenerated the Subtitle OCR notebook. Bootstrap revision is now `subtitle-ocr-bootstrap-2026-08-11.8`; it does not call `venv.EnvBuilder` or `ensurepip`. It isolates its pinned OCR/Paddle stack under the app-owned `/content/la_studio_subtitle_ocr_site` directory.

## Validation evidence

- Python generator compilation: PASS.
- Generated-notebook verifier: **32/32 PASS**.
- `git diff --check`: PASS.
- `graphify update .`: PASS.
- Full project CTest with the project-local Qt 6.9.3 SDK: **39/39 PASS** in 57.04 seconds.

## Boundary

No visible desktop GUI, user session, live Colab worker, or new EXE package was opened or created. The notebook fix is source-level and generated-notebook validated; run the current tracked `notebooks/LA_STUDIO_SUBTITLE_OCR_PP_OCRV5_GPU.ipynb` in a **fresh** Colab runtime. A traceback showing `venv.EnvBuilder(... ensurepip ...)` identifies an older notebook copy, not revision `.8`.

## 2026-08-11 — fixed-pane Dubbing release candidate 0.0.6.4

### Delivered

- Preserved the eight Dubbing stages and the CapCut-style fixed panes already
  implemented in the workspace: task controls consume the left width, preview
  stays in the centre, a task review panel consumes the right width only after
  a task is selected, and the timeline spans the full lower workspace. Panels
  are layout items rather than overlays.
- Project language pair and Fast/Adaptive/Custom quality remain in the modal
  project-setup flow after the user chooses Automatic or step-by-step; they are
  not a permanent lower editor strip.
- Added a runtime QML regression assertion for the header's fixed action
  cluster. At normal width it requires the full `Workflow` label; at compact
  width it requires intentional icon-only controls with tooltips. This prevents
  a future regression from showing a clipped fragment such as `Wor` or letting
  the workflow rail overlap actions.
- Reissued the Subtitle OCR notebook as bootstrap revision
  `subtitle-ocr-bootstrap-2026-08-11.9`. It removes the legacy app venv before
  bootstrapping a dedicated `--target` package directory, never invokes
  `venv.EnvBuilder` or `ensurepip`, and keeps the pinned OCR/Paddle import
  checks. A Colab trace containing `VENV_DIR`/`ensurepip` is therefore a stale
  notebook copy and must be replaced with this tracked `.9` notebook in a
  fresh runtime.
- Bumped the four-field internal build version to `0.0.6.4` and changed the
  version regression from a stale literal to the required four-single-digit
  version invariant.

### Evidence

- QML lint: PASS.
- Generated exact-model notebooks: **32/32 PASS**.
- Python notebook generator compile and `git diff --check`: PASS.
- Headless full CTest, including the offscreen QML route smoke:
  **39/39 PASS** in 59.24 seconds.
- `graphify update .`: PASS.
- Portable internal package created at
  `out/LA-Studio-0.0.6.4/LA-Studio-0.0.6.4.exe`; package staging verified
  **19 required runtime artifacts**. SHA-256:
  `2367C0A735D20F6692C2EF6BCCF3EF22F097188C7C3C92175C6A728F3F0EC5EF`.
- The packaged OCR notebook exists at
  `out/LA-Studio-0.0.6.4/docs/colab-notebooks/LA_STUDIO_SUBTITLE_OCR_PP_OCRV5_GPU.ipynb`
  and contains revision `.9`.

### Boundary

No visible desktop app, interactive user session, or live Colab GPU worker was
opened for this run. The package is internal-only because its hash-verified
eSpeak payload is unsigned; it must not be promoted as a public distributable
release without signing remediation. All assertions above are source, offscreen
QML, generated-notebook, and staged-payload evidence.

## 2026-08-12 - completed: Dubbing compactness, timeline containment, OCR bootstrap

### Delivered

- Reduced the fixed Dubbing header to 52 px and the status strip to 46 px.
  At constrained widths, header actions are deliberately icon-only with
  accessible labels/tooltips, rather than partially rendered text.
- Merged source-mode controls into the 40 px preview toolbar: **Original** and
  **Dubbed** now share a row with the preview title and source actions. The
  preview title uses compact letter spacing.
- Bound Dubbing timeline resizing to the lower workspace, defaulting to 300 px
  and never exceeding the available editor height. Runtime QML regression now
  rejects any timeline/video overlap. Compact task action panes clip and stack
  controls so model/Colab/lifecycle buttons cannot overflow their panel.
- Made Chinese (`zh`) the default source language for a new Dubbing project and
  every relevant persisted/workflow/controller fallback.
- Regenerated `LA_STUDIO_SUBTITLE_OCR_PP_OCRV5_GPU.ipynb` as bootstrap
  `subtitle-ocr-bootstrap-2026-08-12.10`. This corrects the actual fresh-Colab
  failure: generated code used `ocr_pip("install", ...)` even though `ocr_pip`
  already inserts `install`, resulting in `pip install ... install ...`. The
  generator now calls `ocr_pip("--no-cache-dir", ...)`; verification rejects
  the old form.

### Verification and package

- Generator and generated-notebook verifier: **32/32 PASS**.
- Full CTest: **39/39 PASS** in 61.53 seconds, including offscreen QML route
  smoke, Dubbing timeline/layout contracts, media queue, remote execution, and
  OCR controller/runtime tests.
- `graphify update .` and `git diff --check`: PASS.
- Portable internal payload staged at
  `out/LA-Studio-0.0.6.5/LA-Studio-0.0.6.5.exe`. Product Version and File
  Version are `0.0.6.5`; SHA-256 is
  `40015BFB9C9E44321785BDD20AD61E1673A631514C9EFD3641BAE74941780A84`.
  Package staging verified 19 required runtime artifacts. The packaged OCR
  notebook exists and carries revision `.10`.

### Boundary

No desktop GUI, user session, or live Colab GPU worker was opened. This is an
internal package only: its eSpeak payload hash is verified but unsigned, so it
must not be promoted to a public distributable release without signing
remediation.

## 2026-08-12 - completed: 0.0.6.6 Dubbing resize/layout and OCR bootstrap

### Delivered

- Reworked Dubbing editor geometry so the video workspace, 28 px resize target,
  and full-width timeline use separate `ColumnLayout` rows. The timeline now
  takes only real available height and cannot cover the video workspace.
- Updated the Subtitle OCR generator and notebook to bootstrap revision `.11`.
  The reported `CalledProcessError` came from wheels-only resolution rejecting
  source-only `GPUtil`, a transitive PaddleX dependency. Only `GPUtil` is now
  allowed to build from source; the rest stays wheels-only. Failures expose the
  exact pip command and final install log tail.

### Verification and package

- Generated notebook verifier: **32/32 PASS**.
- Full CTest: **39/39 PASS** in 57.97 seconds, including offscreen Dubbing QML
  geometry coverage.
- `graphify update .` completed; staging validated 19 required package files.
- Portable internal executable:
  `out/LA-Studio-0.0.6.6/LA-Studio-0.0.6.6.exe`
  (Product/File Version `0.0.6.6`, SHA-256
  `EDEB7877AB397648ED643A5DC06FF30DADB19382EAF2640473C008CF602F78C1`).

### Boundary

No desktop GUI or live Google Colab GPU session was opened. Run the fresh
Subtitle OCR notebook in a new Colab runtime; if the upstream image changes,
the notebook now exposes the resolver output necessary to diagnose it. The
package remains for internal use because the verified eSpeak runtime is unsigned.
