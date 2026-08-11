# Bao cao tong hop LA Studio

Cap nhat: 2026-08-11

## 2026-08-11 - Dubbing eight-stage contract and OCR bootstrap `.8`

- Automatic Dubbing now exposes exactly eight stages from one C++ controller
  contract: Import/Download, Normalize, Isolator, Transcribe/STT,
  Alignment/Subtitle, Translate, TTS, and Export/Output. QML receives that
  same list, so the header cannot add a stale Subtitle/Alignment/Timing/Mix
  step. Translation review remains part of Translate; timing and conflict
  review remain part of Alignment/Subtitle; mix remains part of Export.
- Compact Dubbing header actions become accessible icon-only buttons below the
  available-width breakpoint instead of clipping labels. The center preview
  default is 1040 px and the timeline default is 340 px. The layout keeps task
  shelf, preview and inspector as non-overlapping layout participants.
- Current Subtitle OCR notebook bootstrap is
  `subtitle-ocr-bootstrap-2026-08-11.8`. It never creates a venv or invokes
  `ensurepip`; it uses the app-owned isolated target directory for the pinned
  Paddle/PaddleOCR/PaddleX/Pillow stack.
- Evidence: Python compile PASS, generated notebook verifier **32/32 PASS**,
  diff check PASS, Graphify update PASS, full CTest **39/39 PASS** in 57.04 s.
  No GUI, live Colab run, or new package is claimed.

## 2026-08-11 - Dubbing entry setup and compact-pane regression closure

- The Automatic entry flow is now covered end-to-end by the production QML
  smoke journey: **Automatic** opens the project setup dialog first, then its
  visible **Continue to preflight** action opens the task-specific Source &
  language preflight. This matches the intended UX: language pair and
  execution-quality policy are chosen once after selecting Automatic or
  step-by-step, not left permanently below the editor.
- The compact Dubbing smoke checks the selector where it is actually rendered:
  in the left task shelf on a wide workspace, or in the fixed right detail pane
  at the compact breakpoint. It continues to reject task controls, the video
  workspace and the right detail pane if they overlap or extend outside the
  workspace.
- The old statement that this checkout lacked a Qt development SDK is no
  longer true. The project-local Qt 6.9.3 SDK was used to build the test target
  and run full CTest: **39/39 passed** in 57.83 seconds. `qmllint`,
  `git diff --check`, and `graphify update .` also passed.
- No visible desktop GUI, live Colab worker, or new EXE was launched or
  packaged in this validation batch. The fresh Subtitle OCR notebook remains
  `notebooks/LA_STUDIO_SUBTITLE_OCR_PP_OCRV5_GPU.ipynb`, bootstrap revision
  `subtitle-ocr-bootstrap-2026-08-11.7`; a notebook that shows
  `venv.EnvBuilder` is an older copy and must be reopened from current source.

## 2026-08-11 - Dubbing fixed-pane toolbar and OCR single-transaction bootstrap

- Source commit `d955dd9` is pushed directly to `main`.  The Dubbing header
  has one scrolling task rail only; the fixed action cluster keeps the complete
  **Workflow** label available instead of letting it collapse to a fragment.
  Save, Export, Pause/Stop and task-control visibility are available from its
  overflow menu, rather than stealing video width.
- The central video pane defaults to `940 px`, has a `440 px` video minimum,
  and moves editing actions into a separate horizontal preview toolbar.  The
  source controls no longer fight the video-mode selector in one row.  At
  narrow widths History and task controls yield as real layout participants;
  they do not paint over the canvas.  The timeline drag target is now 28 px
  and uses press-relative pointer handling instead of an almost invisible
  decorative line.
- Language/target and quality selection continue to belong to the existing
  entry-mode Project Setup dialog after Automatic or Step-by-step is chosen;
  `DubbingProjectStatusPanel` is not instantiated by the page.
- Subtitle OCR bootstrap revision `subtitle-ocr-2026-08-11.7` removes only
  legacy app-owned bootstrap folders, then resolves the fixed Paddle/PaddleOCR/
  PaddleX/Pillow stack in one dedicated `pip --target` transaction.  It never
  creates a venv or invokes `ensurepip`, avoiding the reported failed venv
  bootstrap and later mixed dependency transactions.
- Evidence: changed QML parser PASS, Python compilation PASS, generated
  notebook verifier **32/32 PASS**, independent Dubbing/OCR source contract
  PASS, `git diff --check` PASS and Graphify AST update PASS.  CTest and a new
  package are blocked: MSVC is available only after developer-environment
  setup, but the machine has no Qt 6.9 development SDK / `Qt6Config.cmake`.
  No GUI, live Colab, CTest, EXE or package claim is made for this batch.

## 2026-08-11 - Dubbing layout-minimum correction

- Source commit `32ee731` corrects the remaining medium-width geometry hole:
  the task shelf yields below 1450 px and History below 1080 px.  The previous
  breakpoints could still leave the sum of real History/task/preview/review
  minima wider than the workspace, which lets Qt compress or clip the video.
- The non-overlap breakpoint contract and QML parser passed; the source is on
  `main`.  The Qt development SDK constraint remains unchanged, so CTest and
  a new package are still not claimed.

## 2026-08-11 - Compact Dubbing controls and complete OCR package isolation

- Source commit `771dcf3` adds a narrow-workbench contract: under 1000 px the
  left task shelf is not allowed to become an offscreen or overlapping pane.
  Its actual Run/Configure controls render in the right review pane, while the
  wide resizable History/task/Preview/review/Timeline editor remains intact.
  A smoke boundary rejects a review pane extending past the Dubbing workspace.
- The only language and execution-quality chooser remains Project Setup after
  Automatic or Step-by-step mode, or its explicit Project settings reopen;
  the bottom of the editor is still reserved for the full-width Timeline.
- Subtitle OCR now uses `pip --target --ignore-installed` for every pinned
  dependency.  The preflight rejects any launch unless Pillow, Paddle,
  PaddleOCR and PaddleX all import from
  `/content/la_studio_subtitle_ocr_site`, eliminating a remaining mixed global
  package path behind the reported Pillow `_Ink` failure.
- Evidence: QML parser, compact-layout and OCR source contracts, Python
  compilation, notebook verification **32/32**, diff check and Graphify AST
  update passed.  Full CTest/build remains blocked by the missing Qt
  development kit.  No EXE was produced.

## 2026-08-11 - Dubbing pane contract and Subtitle OCR no-venv bootstrap

- Source commit `5dddf99` keeps the Dubbing workbench as fixed, resizable
  task/preview/review panes with the Timeline below; new smoke guards reject
  pane overlap rather than treating painted-over content as valid.
- Header utility actions now have a dedicated horizontal strip so full labels
  remain reachable at narrow widths. Project languages and quality remain in
  the post-mode Project Setup dialog instead of a permanent bottom panel.
- `LA_STUDIO_SUBTITLE_OCR_PP_OCRV5_GPU.ipynb` no longer relies on either
  `venv.EnvBuilder` or `virtualenv`. The exact pinned PaddleOCR stack is
  installed into a dedicated package directory and the worker receives that
  directory through an explicit environment contract.
- Validation: generated notebooks 32/32, OCR source contract, Python compile,
  changed QML lint, diff check and Graphify AST update passed. Full CTest/build
  is blocked locally by the missing Qt development kit; no package was made.

## Baseline hien tai

| Muc | Trang thai |
| --- | --- |
| Latest packaged candidate | `0.0.6.3` |
| Artifact | `out/LA-Studio-0.0.6.3/LA-Studio-0.0.6.3.exe` |
| SHA-256 | `B3322735B67EEE453FA5549AB35CB5DC95D2E578B68A9BEC7BCDE25F1FDB3137` |
| Current source | `main` at `771dcf3`; the 0.0.6.3 package is unchanged and the latest Dubbing/Subtitle OCR fixes are source-only. |
| Distribution | Internal only; eSpeak MSI SHA-verified but unsigned |

## Source batch after 0.0.6.3 - fixed Dubbing panes and OCR bootstrap

- `8d256ce` keeps every LA Studio Dubbing task and route, but reorganizes the
  workbench into fixed layout participants: optional History and task shelf on
  the left, a central Preview, contextual task output on the right, and one
  full-width Timeline below. Selecting a task now consumes real layout width
  rather than overlaying the Preview or leaving an offscreen horizontal canvas.
- Project language pair and execution quality no longer occupy a permanent
  lower panel. They are chosen in `DubbingProjectSetupDialog` immediately
  after Automatic or step-by-step mode, or reopened through **Project
  settings**. The task rail is split into a separate scrollable row so action
  labels such as **Workflow** cannot be squeezed into fragments. History,
  Preview, and Timeline use `DragHandler`, so a resize continues after the
  pointer leaves the old tiny handle.
- The Subtitle OCR notebook no longer invokes a Colab venv bootstrap. It
  installs the exact Paddle/PaddleOCR/PaddleX/Pillow pins into a dedicated
  package directory and passes it to the worker in an explicit environment.
  Revision: `subtitle-ocr-2026-08-11.6`; both `venv.EnvBuilder` and
  `virtualenv` are rejected by the generator verifier.
- Evidence: the generated notebook verifier passed **32/32**; the dedicated
  package source contract, Python compile, changed `qmllint`, source contract
  checks and `git diff --check` passed. Full CTest/build is **blocked**, not
  passed: the current machine has no Qt development kit (`Qt6Config.cmake` /
  `LA_QT` unavailable), so no desktop/package or live Colab claim is made for
  this source batch. `graphify update .` completed its AST update.
- The source commit is pushed directly to `origin/main`. No new EXE was built.

## Candidate 0.0.6.3 - Dubbing workbench restructure

- The Dubbing workspace now separates the selectable left task shelf, central
  preview, contextual right review panel, and full-width lower Timeline. It
  keeps existing LA Studio tasks, routes, and panels rather than replacing the
  product workflow with a mock editor.
- QML lint passed; full CTest passed **39/39**. The package build succeeded,
  and a hidden offscreen package-startup smoke confirmed the staged Qt/QML
  payload could start without opening a user-visible app.
- Independent artifact audit verified `qwindows`/`qoffscreen`, RuntimeHost,
  FFmpeg/FFprobe, yt-dlp, Douyin helper, Subtitle OCR, PaddleOCR, and relevant
  Colab notebooks. Live browser/Colab/API and visual desktop acceptance were
  intentionally not run.
- The package is `out/LA-Studio-0.0.6.3/LA-Studio-0.0.6.3.exe`. Four portable
  candidate folders are presently present (`0.0.6.0`--`0.0.6.3`), so the
  requested three-candidate retention limit still needs cleanup.

## Post-package Subtitle OCR notebook hotfix

- The original `adc7e04` removal of a direct Torch import did not cover the
  later Colab log: PaddleOCR 3.1.1 was resolving a newer PaddleX, which
  imported ModelScope and therefore Torch indirectly. That is the real path
  behind `libtorch_cuda.so: undefined symbol: ncclCommShrink`; connection
  refused is only the resulting worker startup failure.
- `b26ba3a` pins the notebook's complete tested dependency set to
  `paddlepaddle-gpu==3.1.0`, `paddleocr==3.1.1`, and
  `paddlex[ie,multimodal,ocr,trans]==3.1.0`. It force-reinstalls this set and
  runs a real `from paddleocr import PaddleOCR` CUDA probe before starting the
  server. The exact PaddleX 3.1.0 wheel was inspected: its model resolver does
  not import ModelScope.
- The generated-notebook verifier is now a regression gate for those exact
  pins plus the import probe; it passed **32/32**. Full CTest printed
  **39/39 passed**; the outer terminal deadline occurred only after CTest had
  printed that completed summary, so its shell exit code was not captured.
- A later live Colab failure exposed a separate, mixed Pillow installation:
  Pillow 12's `PIL/ImageText.py` was present while the older
  `PIL/_typing.py` lacked `_Ink`. `c4689c1` force-reinstalls the coherent
  `Pillow==12.0.0` wheel after the Paddle OCR extras and probes both
  `ImageText` and `_Ink` before importing PaddleOCR. The notebook revision is
  now `subtitle-ocr-2026-08-11.4`.
- The generated-notebook gate now requires that exact Pillow install and both
  import probes. It passed **32/32**; a direct wheel-content check confirmed
  that Pillow 12's `ImageText.py` and `PIL._typing._Ink` match. Full CTest
  completed with **39/39 passed, 0 failed** in 56.51 seconds.
- The next live log proved that force-reinstalling Pillow into Colab's global
  interpreter did not guarantee that its active `PIL` directory was coherent.
  `23e7d0d` therefore creates a cleared, dedicated
  `/content/la_studio_subtitle_ocr_venv`; installation, Paddle/Pillow probe,
  and Uvicorn all use only that interpreter. `PYTHONPATH` and Python user-site
  are removed for each of those processes, and the probe asserts that it is
  inside the venv.
- The shared launcher was regenerated across its affected notebooks so they
  stay in sync. Generated notebook verification remains **32/32 passed**;
  the generated isolated probe compiles, the shared-launch isolation contract
  passes, and a clean local venv imported Pillow 12's `ImageText` and `_Ink`.
- The correction is on `main`, but is not retroactively inside the staged
  `0.0.6.3` portable artifact. It still requires a fresh Colab runtime for
  live acceptance.

## Candidate 0.0.6.1 - Dubbing preview workspace package

- Created only after the user removed old package folders. The retained
  candidates are `LA-Studio-0.0.6.0` and `LA-Studio-0.0.6.1`; no existing
  candidate was overwritten and the count remains below the three-folder cap.
- CMake default, generated application resource version, EXE FileVersion and
  EXE ProductVersion are all `0.0.6.1` (`718f2e6`).
- QML lint passed; targeted media/remote/offscreen route regression passed
  **4/4**; full CTest passed **39/39** in 57.83 seconds.
- The portable package audit passed both staging and license manifests with
  **19 required artifacts**. The independently checked folder contains Qt
  `qwindows`/`qoffscreen`, RuntimeHost, FFmpeg/FFprobe, yt-dlp, Subtitle OCR,
  PaddleOCR and the Colab notebooks.
- This is a package and automated/offscreen validation only. The app, browser,
  live Douyin URL, API Gateway and Colab worker were not opened.

## Source batch after 0.0.6.0 - Dubbing preview workspace

- Studied the matching OpenCut repository at
  `C:/Users/Nguyen Trong Khoi/Downloads/OpenCut-reference` (remote
  `OpenCut-app/OpenCut`, commit `4d8c49e`). Its separate Browser/Preview/
  Inspector upper workspace and bottom Timeline informed the layout only;
  no OpenCut code or replacement workflow was imported.
- The actual LA Studio Dubbing panel retains the existing media queue,
  Chromium/cookie controls, Direct Colab/API Gateway routes, OCR, and exports.
  Once a source exists, its source setup is a `160 px` scrollable panel so it
  cannot shrink the video to a strip. The central Preview default widened to
  `860 px` (minimum `620 px`) and has larger 16:9-oriented bounds.
- **Focus video** temporarily hides History, the stage workspace, and node
  inspector. A dedicated horizontal divider resizes the independent Timeline
  from `96` to `360 px`; all layout controls are source/QML controls, not a
  mock overlay.
- QML lint PASS; targeted media/remote/offscreen route regression **4/4
  PASS**; full CTest **39/39 PASS**. The first QML smoke caught an invalid
  tooltip property and passed after its direct repair. `graphify update .`
  ran after editing. No visible GUI, live video, browser, Douyin, API, or
  Colab worker was opened.
- This source batch is delivered in the audited `0.0.6.1` candidate above.

## Candidate 0.0.6.0 - internal version carry and Dubbing workspace package

- Version identifiers now have exactly four single-digit fields. The carry rule
  is enforced consistently by CMake, build/package scripts and release-tag
  validation: `0.0.0.9` carries to `0.0.1.0`; values such as `0.0.2.40` are
  rejected before configure/package.
- The prior Dubbing layout batch is included: source/download setup collapses
  after a source is loaded, the video workspace has a bounded useful height,
  OCR edit mode exposes its scan area without source controls covering it, and
  current running activity remains observable without locking unrelated setup.
- The manually retained package folders are exactly three:
  `LA-Studio-0.0.2.39`, `LA-Studio-0.0.2.40`, and `LA-Studio-0.0.6.0`.

### Evidence 0.0.6.0

- PowerShell syntax checks passed for build, package and release-version
  scripts. `v0.0.6.0` was accepted; `v0.0.2.40` and CMake
  `LASTUDIO_VERSION=0.0.2.40` were rejected with the carry-rule diagnostic.
- QML lint passed. Targeted media/remote/offscreen QML regression passed
  **4/4**; full CTest passed **39/39**.
- Portable package audit passed: FileVersion/ProductVersion `0.0.6.0`, SHA
  above, Qt `qwindows`/`qoffscreen`, RuntimeHost, FFmpeg/FFprobe, yt-dlp
  `2026.07.04`, Tesseract `5.5.1`, and the prepared PaddleOCR health manifest
  are present and verified. The staging and license manifests both passed
  `19 required artifacts` during packaging.
- No visible desktop GUI, user browser, live Douyin URL or live Colab worker
  was opened. Those remain manual acceptance checks.

## Candidate 0.0.2.36 - explicit Douyin cookie retry

- The app never reads Chrome/browser cookies. The user may choose a readable
  Netscape tab-separated cookie file from the Import/Download screen or the
  Dubbing source panel; it is kept in session memory only.
- Before invoking yt-dlp, the service copies the selected file to a private
  short-lived temp file (maximum 16 MiB, owner-only permissions). The temp
  copy is removed after resolver success/failure/cancel/destruction and is not
  included in project, settings, history, output metadata or logs.
- The default resolver remains `--no-cookies`. When yt-dlp reports Douyin's
  fresh-cookie diagnostic, the item is marked `needs-auth`, keeps its source
  link only in memory, and shows **Retry with cookies**. After the user selects
  cookies and retries successfully, the URL and cookie path are cleared.

### Evidence 0.0.2.36

- Targeted media suite PASS, including resolver argument contract, temporary
  cookie lifecycle, actionable fresh-cookie error, and controller retry from
  `needs-auth` to downloaded media.
- QML lint PASS. Full CTest **39/39 PASS** after the final test changes.
- Portable package audit PASS: FileVersion/ProductVersion `0.0.2.36`, SHA above,
  yt-dlp `2026.07.04`, FFmpeg `N-125829-gfe953596e9-20260728`, qwindows and
  qoffscreen present, RuntimeHost present, and 19/19 staging/license artifacts.
- No browser/GUI control and no live Douyin/Colab request was performed. A
  real Douyin account-exported Netscape cookie and current public-link access
  remain manual acceptance requirements.

## Candidate 0.0.2.35 - shared links and independent Dubbing actions

- The Dubbing input accepts a full copied Douyin/TikTok-style share message.
  It extracts only each embedded `http(s)` URL before validation/download;
  captions, short codes and “copy this link” text are neither persisted nor
  sent to the resolver. Plain one-link-per-line input is retained.
- **Add link(s) to download queue** now only queues/downloads media. It does
  not open an action dialog or commit a workflow. After items become
  downloaded, **Downloaded media & actions** lets the user tick any subset and
  run exactly one of Import/Normalize, Isolator, STT, Translate, TTS/Voice or
  Export/Output. The previous all-in-one batch remains an explicit advanced
  choice.
- Later actions restore the selected item's saved Dubbing project/artifacts;
  they do not clear other items or silently run following stages. Export uses
  the existing generated `voice.wav` and records the actual exported media
  path. Missing prerequisites are shown as exact errors, not silently rerun.
- Fixed the public-video adapter lookup: the portable package puts `yt-dlp.exe`
  beside the application, while FFmpeg remains in `media-tools`. The runtime
  locator now follows that real layout, fixing the `not-ready` state caused by
  a correctly staged adapter being invisible to the app.

### Evidence 0.0.2.35

- New loopback regression embeds a public URL inside share-text and verifies
  that exactly one staged download occurs. A second controller regression
  imports two items, selects only one for a later action, and proves the
  unselected item's saved project is not rerun or cleared. Existing package
  lookup and QML/control regressions were updated for the real UI.
- Targeted `TestMediaIngestService` and `TestDubbingProject` PASS; QML lint
  PASS; full CTest **39/39 PASS** in 57.98 seconds. `graphify update .` ran
  after source edits.
- Portable audit confirmed source/FileVersion/ProductVersion `0.0.2.35`, the
  SHA above, Qt `qwindows`/`qoffscreen`, RuntimeHost, FFmpeg/FFprobe and
  root-level `yt-dlp` (`2026.07.04`). Staging/runtime and license manifests
  both passed at 19 required artifacts.
- No visible GUI or live Douyin/Colab/API request was run. Live platform
  availability still depends on the public URL/service and the user-configured
  route; it is not claimed by the loopback regression.

## Candidate 0.0.2.34 - Dubbing workspace queue controls

- Fixed the placement fault in `0.0.2.33`: the real Dubbing Import/Download
  screen now has **Queue & batch settings** immediately beside **Add link(s)
  to media queue**. Adding links opens that queue automatically. The dialog
  permits selecting downloaded videos, selecting Isolate/STT/Translate/Voice
  tasks, choosing either execution order, viewing real item state, errors and
  output paths, then starting the selected batch.
- These controls call the same existing `DubbingController::startMediaQueue`
  contract. The two orders remain real controller scheduling: finish one item
  end-to-end, or finish each selected production stage across all selected
  items. Configured Direct Colab, API Gateway and explicit Local routes are
  preserved; no Local fallback was added.
- Added visible drag handles to the actual three-pane `DubbingPage` layout:
  one between History and Preview and one between Preview and the step panel.
  This corrects the prior generic `StudioShell`-only implementation, which
  cannot affect the Dubbing workspace.

### Evidence 0.0.2.34

- Regression explicitly reads the real Dubbing source panel/dialog/page and
  asserts the in-workspace button, both execution modes, task start contract,
  output labels and both resize handles. Targeted `TestMediaIngestService`
  PASS (6.14 s); QML lint PASS.
- Fresh full CTest completed **39/39 PASS**; `LastTest.log` contains 39 passed,
  zero failed test entries, including offscreen `QmlRouteSmoke`.
- `graphify update .` ran after source changes. Portable audit: source,
  FileVersion and ProductVersion are `0.0.2.34`; SHA is above. Direct checks
  confirmed `qwindows`, `qoffscreen`, FFmpeg, FFprobe, RuntimeHost, Subtitle
  OCR runtime manifest and Tesseract 5.5.1.
- No desktop GUI or live remote worker was opened. The exact manual gate is to
  add one or more links, open **Queue & batch settings**, choose the desired
  order/tasks, drag both handles, and run with the user's configured worker.

## Candidate 0.0.2.33 - batch execution order and responsive controls

- The media queue has two explicit, real execution modes: **Complete one
  video, then next** retains end-to-end serial processing; **Complete each
  step for all videos** completes the same production stage for every selected
  media item before moving the queue forward.  This is controller scheduling,
  not a UI-only choice.  Direct Colab/API Gateway/Local routing remains the
  already selected route per stage, with no implicit Local fallback.
- The second mode retains one independent Dubbing project per item. It writes
  the same real per-item artifacts (`source.srt`, `translated.srt`,
  `vocals.wav`, `background.wav`, `voice.wav`, `project.ladub.json`) and keeps
  a failed item isolated while other queued items can proceed.
- TTS and Voice Cloning now have a larger shared Examples picker: short and
  long Vietnamese, short and long English, plus bilingual TTS. Voice clone
  examples only fill text/settings; they never invent a reference audio or
  bypass the existing consent requirement.
- Contrast was raised through the shared theme and application palette so
  native controls do not use dark system text on dark surfaces. Studio left
  and settings rails now have visible drag handles and bounded widths. The
  application navigation can expand/collapse; expanded navigation shows route
  names instead of requiring hover-only discovery.

### Evidence 0.0.2.33

- Added a real-controller stage-by-stage regression: two WAV items both finish
  production ingest before either starts the unavailable real STT dependency;
  both failures then terminate cleanly. It also asserts the exposed execution
  mode. Existing serial queue regression remains in the same suite.
- Targeted `TestMediaIngestService` PASS; QML lint PASS; fresh full CTest:
  **39/39 PASS** in 57.06 seconds. `graphify update .` completed after source
  changes.
- Portable internal package audit: FileVersion/ProductVersion `0.0.2.33`, SHA
  above, staging manifest **19/19** required runtime/license artifacts,
  `qwindows`/`qoffscreen`, FFmpeg, FFprobe and Tesseract 5.5.1 verified.
- No visible desktop GUI or live Colab/API job was opened. Resizing, expanded
  navigation and live remote execution therefore remain manual acceptance
  checks, not claimed automated passes.

## Candidate 0.0.2.32 - Dubbing media batch queue

- Download accepts one public media link per line and resolves each link serially
  into LA Studio-owned staging. URL input is short-lived memory only; it is
  removed when a download ends and is not written into project/history.
- Several downloaded items may be selected for a serial Dubbing batch. Each
  uses a separate project and the existing real ingest, separation, STT,
  translation, synthesis and mix workers. A configured Direct Colab/API route
  stays that route; no Local fallback is introduced.
- Outputs are real files under `~/.lastudio/dubbing/batch-output/<id>`:
  `source.srt`, `translated.srt`, `voice.wav`, `vocals.wav`,
  `background.wav` and `project.ladub.json` where applicable. The UI displays
  the exact paths per item.
- A worker error marks only its item failed, records its actual error and
  advances the next item. Progress uses completed items plus active runner
  progress; it is not a fixed 5%/8% value.

### Evidence 0.0.2.32

- Loopback integration downloads two real WAV payloads serially, verifies both
  staged files and proves temporary URL query text is absent afterwards. A
  second regression makes the real STT dependency fail for two selected items
  and verifies that the queue terminates without an indefinitely running item.
- Fresh full CTest after rebuilding the test binary: **39/39 PASS** (57.71 s).
  Release packaging recompiles the Download page and Dubbing source panel to
  Qt AOT C++.
- Portable audit: FileVersion/ProductVersion `0.0.2.32`, SHA above, staging
  manifest **19/19** required runtime/license artifacts, and direct checks for
  Qt `qwindows`/`qoffscreen`, FFmpeg, FFprobe and Tesseract 5.5.1.
- No visible GUI, live API Gateway or live Direct Colab job was opened. They
  require a user temporary worker/credential and remain manual acceptance
  gates; they are not represented as a local pass.

## Candidate 0.0.2.31 - OmniVoice clone reuse in TTS

- Voice Clone now shows **Voice name for TTS reuse** adjacent to the reference
  input, rather than hiding the naming action in a side panel. The entry is
  persisted only after a successful Direct Colab clone; failed/cancelled work
  never creates a reusable voice.
- A verified Direct Colab OmniVoice cloning worker removes TTS's empty-model
  block by selecting the OmniVoice family configuration. It does **not** copy
  the clone URL/token into the separate generic TTS session, does not download
  a Local model and does not start a second Colab notebook.
- TTS Settings visibly exposes **Reuse cloned OmniVoice** with the saved voice
  list, an explicit permission check and **Use cloned OmniVoice in TTS**. That
  route invokes the existing exact Voice Clone worker with the saved reference
  audio and transcript, so the generated speech genuinely uses the cloned
  profile rather than merely labelling a normal TTS request as cloned.
- Validation was intentionally scoped to this change: MSVC rebuilt the
  changed QML into AOT C++, `voiceCloneOmniVoiceIsReusableInTtsWithoutLocalFallback`
  passed (3 Qt test checks including init/cleanup), and portable package staging
  verified 19 required runtime/license artifacts. No visible GUI or live Colab
  worker was opened; the user asked not to rerun already-stable features.

## Post-package source fix: safe shared Colab port recovery

- The generic exact-model Colab launcher no longer immediately fails when a
  previous LA Studio worker owns its port. It recovers only the old worker
  identified by its exact generated module plus `uvicorn`, and the matching
  Cloudflare tunnel for that port.
- A listener not owned by the same LA Studio worker is preserved. The notebook
  reports its PID and tells the user to choose a fresh runtime; it never kills
  an unrelated Colab process.
- This includes `LA_STUDIO_VOICE_CLONE_OMNIVOICE_GPU.ipynb` on port `3923` and
  removes the old instruction to destroy the entire runtime. Generated notebook
  verification is `32/32`; targeted remote/Voice Clone tests are `2/2`; full
  offscreen CTest is `39/39` in 78.24 seconds. This is not a claim of a live
  Colab or visible-GUI acceptance run. No new EXE was packaged.

## Candidate 0.0.2.29 - portable internal package

- The Dubbing flow and Direct Colab progress repair were packaged as a new
  portable candidate; no earlier candidate was overwritten.
- The package executable, embedded `FileVersion` and embedded
  `ProductVersion` are all `0.0.2.29`. SHA-256 is
  `3D37B2DE11575EE265C2FAAB43B20DE8185557FEB587FAC74654E66656EBC2D7`.
- Package staging verified the 19 required runtime/license artifacts. An
  independent post-package check confirmed the Windows and offscreen Qt
  platforms, FFmpeg/FFprobe, RuntimeHost, bundled Subtitle OCR manifest,
  Spleeter notebook/worker templates and licenses are present.
- This remains an internal build: the eSpeak NG MSI is SHA-verified but not
  signed. The portable audit and offscreen CTest do not claim a visible desktop
  launch or live-Colab success.

## Post-package Dubbing flow and Direct Colab progress repair (source only)

- The Dubbing presentation contract now exposes the user-visible dependency
  order `Import/Download -> Normalize -> Isolator -> Transcribe/STT ->
  Translate -> Subtitle -> TTS -> Alignment -> Export/Output`.  The old
  `Alignment/Subtitle` label incorrectly combined two different concerns and
  appeared before translation.  Source-language transcript review remains
  under Transcribe/STT; target-language subtitle review is now its own stage
  after Translate; timing/conflict work is under Alignment after TTS.
- Direct Colab Spleeter no longer leaves a vague `90%` status.  The desktop
  reports the worker phase while CUDA is writing stems, then uses the actual
  byte counter of the current vocals/background artifact while it downloads.
  That artifact percentage is explicitly not presented as a whole-workflow
  percentage.  A worker that remains at 90% without becoming ready is cancelled
  after five minutes with an actionable error and never starts a Local fallback.
- Regression added a stuck-at-90 worker and verifies the remote cancellation,
  no-local-fallback message, phase notifications, and artifact transfer bytes.
  The presentation regression verifies the nine-stage order and that
  `review-translation` belongs to Subtitle.
- Validation: release source and both unit-test targets build successfully;
  targeted `TestDubbingProject` and `TestColabSeparationRunner` pass 2/2; full
  CTest passes 39/39 in 82.58 seconds, including deployment and
  `QmlRouteSmoke` under `QT_QPA_PLATFORM=offscreen`.  The first smoke attempt
  did reveal and the source fixed a stale expectation that Subtitle must expose
  a Configure button.  Offscreen regression is not desktop or live-Colab
  acceptance. No package or live Colab run is claimed for this source-only
  repair.

## Post-package source fix: Spleeter Colab cloudflared bootstrap

- Root cause of the reported notebook failure: after the exact CUDA startup
  probe passed, the Spleeter launcher called `subprocess.run(["cloudflared",
  "--version"])` without handling a missing executable. A fresh Colab runtime
  raises `FileNotFoundError` there, so the existing install path never ran.
- The launcher now treats an absent binary as not installed, downloads and
  installs Cloudflare Tunnel, then verifies its executable before opening the
  temporary tunnel. It still has no Local or API Gateway fallback.
- The Spleeter notebook is now generated and checked for drift. It fetches
  both exact worker templates from immutable source commit `2502485` and
  verifies their SHA-256 values, including the fixed launcher hash.
- Validation: generated exact-model notebooks 32/32 PASS; targeted
  `TestColabSeparationRunner` 7/7 PASS; full CTest 39/39 PASS in 133.83
  seconds. This is source/offscreen evidence only: a new live Colab runtime
  and its public tunnel still need manual acceptance. No new EXE was packaged.

## Batch 0.0.2.28: Direct Colab Spleeter CUDA hardening

- The `CUDNN_FE_HEURISTIC_QUERY_FAILED` evidence was traced to the Direct
  Colab Spleeter worker, not a Local route: `0.0.2.27` log records an exact
  `sherpa-onnx-spleeter-2stems-fp16` Direct Colab run and the Colab container
  emitted the cuDNN failure.
- The same exact upstream FP16 Spleeter ONNX artifact is retained. The new
  worker uses explicit ONNX Runtime CUDA provider options with
  `cudnn_conv_algo_search=DEFAULT`, has a CUDA startup probe before health can
  pass, and separates long source audio as 20-second cores with 1.5-second
  context. It reports actual segment progress and has no local fallback.
- The notebook pins an immutable source revision and SHA-256 checks worker
  templates. The first package audit found templates absent from staged docs;
  the CMake install rule and regression now stage them, and the final package
  audit verifies both files.
- QML fixes remove the reported delegate `root` reference and undefined model
  field update. Full CTest **39/39 PASS** in 86.87 seconds. Portable audit
  verifies version, hash, Qt, FFmpeg, RuntimeHost, OCR manifests, notebook and
  worker templates. Live Colab/manual desktop testing remains a separate gate.

## Post-package Dubbing code verification

- Code audit, not desktop/live-service evidence: the `colab-direct` runner
  sends the request to `ColabSeparationRunner` and returns before the Local
  resolver branch. A Direct Colab Spleeter selection therefore cannot execute
  the Local source-separation path.
- Package `0.0.2.27` contains the older inline `sherpa_onnx` CUDA worker. The
  audited `0.0.2.28` package contains the pinned, SHA-checked worker templates
  with a startup probe, explicit DEFAULT cuDNN search and bounded 20-second
  chunks. A new URL/token from that notebook is required; an old Colab runtime
  cannot be upgraded by reconnecting it.
- No source change or package was made for this verification. Targeted
  Dubbing/Direct-Colab/remote/QML tests passed **8/8** in 28.85 seconds and
  full CTest passed **39/39** in 72.18 seconds. Live Colab remains a manual
  acceptance gate.

## Batch 0.0.2.27: Adaptive Dubbing Direct Colab route integrity

- The two reported errors (`could not determine free disk space` for a local
  Qwen directory and a missing SHA-256 for `llama-b10036...zip`) were symptoms
  of one route-integrity bug, not disk/checksum faults. Adaptive Dubbing still
  had a hidden Local LLM setup path, and old Local runtime metadata could
  survive below the visible remote configuration.
- Adaptive rewrite now has an explicit Direct Colab option: exact model
  `qwen3.5-2b`, notebook `LA_STUDIO_LLM_QWEN3_5_2B_GPU.ipynb`, and an exact
  `llm-chat/qwen3.5-2b` verified worker. It is displayed as a subordinate
  Translate worker, never as a ninth production stage. Preflight blocks with a
  concrete setup/check action instead of downloading a Local model.
- Changing or reselecting any API Gateway/Direct Colab route clears all
  Local-only runtime, files and configuration-signature keys both at the
  persisted root and in nested parameters. The selected remote route/model is
  durable on project reopen. Direct Colab also does not overwrite the separate
  API Gateway URL/credential, and no Colab URL/token is saved in project JSON.
- The adaptive Dubbing selection no longer mutates the independent standalone
  LLM Chat controller. API Gateway, Direct Colab and explicit Local remain
  separate choices.
- Regression includes a legacy Local-to-Colab project, a repeat Direct-Colab
  selection containing stale nested runtime fields, five exact CUDA-worker
  contracts, reopen persistence, secret exclusion, the Translate worker card
  and the assertion that automatic setup never downloads the default Adaptive
  LLM. QML lint PASS; targeted Dubbing/remote/QML tests 7/7 PASS; full CTest
  39/39 PASS in 72.18 seconds.
- Portable package audit PASS: source/FileVersion/ProductVersion `0.0.2.27`,
  SHA above, Qt Windows/offscreen plugins, FFmpeg/FFprobe, RuntimeHost,
  Tesseract/Paddle manifests, license inventory and the Qwen Direct Colab
  notebook were verified. Candidate `0.0.2.26` was staged before the nested
  metadata regression was found; it is retained but not accepted.

## Batch 0.0.2.25: Direct Colab route preservation and truthful Activity stage

- Root cause of the reported Automatic Dubbing failure: after the wizard had
  verified a selected Direct Colab worker, `DubbingController` still entered
  the local automatic setup path whenever the unrelated global
  `remoteFirstMode` preference was false. It therefore tried to install the
  local Sherpa runtime. The secure downloader correctly refused that catalog
  asset because it has no SHA-256; the route selection was what was wrong.
- Automatic setup now resolves the saved provider per model node. A verified
  exact Direct Colab capability/model or configured API Gateway is retained;
  neither can silently enqueue or load a local runtime. Remote TTS is likewise
  left on its selected route. Local remains explicit and the downloader's
  checksum guard remains enabled.
- Activity no longer leaks `model-setup` or a generic `Running dubbing
  workflow` row. It maps internal nodes to the existing eight user stages and
  reports the current title, for example `Dubbing - Isolator`,
  `Isolator (3/8)`, plus the exact selected route/model. A numeric percentage
  is only rendered when a download or worker emits a real counter; otherwise
  `Working` is retained rather than inventing a fixed 5% or 8%.
- Regression: one new controller/Activity test starts four independently
  verified exact-model Direct Colab mock workers while `remoteFirstMode=false`.
  It proves that Automatic leaves setup without a local Sherpa download and
  that Activity reports Isolator stage 3/8 on Direct Colab.
- Evidence: QML lint PASS; targeted Dubbing/remote/QML tests 7/7 PASS; full
  CTest 39/39 PASS in 36.02 seconds; package QML smoke PASS with 18 interaction
  trace events. Portable package source/FileVersion/ProductVersion `0.0.2.25`,
  Qt Windows/offscreen plugins, Multimedia, FFmpeg/FFprobe, RuntimeHost and
  staging/license manifests (19 required artifacts) were verified.

## Batch 0.0.2.24: Automatic Dubbing eight-stage contract

- `0.0.2.22` is superseded and `0.0.2.23` is deliberately not accepted: its
  first package was produced before the interaction trace contained route/model/
  worker state transitions. It is preserved but is not release evidence.
- Automatic Dubbing now has one controller-owned presentation contract with
  exactly eight unique stages: Import/Download, Normalize, Isolator,
  Transcribe/STT, Alignment/Subtitle, Translate, TTS and Export/Output. QML
  consumes that aggregate contract; it does not hide or deduplicate internal
  nodes by title.

| Presentation stage | Production nodes retained internally |
| --- | --- |
| Import/Download | `media-input` |
| Normalize | `ingest` |
| Isolator | `source-separate` |
| Transcribe/STT | `transcribe` |
| Alignment/Subtitle | `review-transcript`, `fit-timing`, `review-conflicts` |
| Translate | `translate`, `review-translation` |
| TTS | `assign-voices`, `synthesize` |
| Export/Output | `mix`, `export` |

- Timing/Mix are no longer user cards or headers. Their production behavior
  remains in Alignment/Subtitle and Export/Output respectively. Normalize
  explicitly reports local preprocessing, effective source/working format and
  `No model required`; it is Ready only with valid source media. Model stages
  cannot report Ready from a template/default: Local uses the configuration
  resolver, API Gateway needs its own configured URL/key, and Direct Colab
  needs a verified exact capability/model worker.
- Configure stays inside the preflight wizard as an owned dialog. Route is
  selected before its picker, Save/Apply/Cancel preserve the wizard context,
  and Direct Colab stages alone appear on its worker page. Changing route or
  exact model clears the prior worker state; URL/token remain session-memory
  only.
- Regression evidence: QML lint PASS; targeted QML route/offscreen smoke 2/2
  PASS; full CTest **39/39 PASS** in 25.57 seconds. The controller readiness
  matrix covers missing/valid media, Local without/with resolved runtime-model,
  Direct Colab without/with an asynchronously verified exact worker. The
  production-shell trace contains 18 ordered events, including the exact
  `Local -> Direct Colab (2 workers) -> Local (0 workers)` route/model states.
- Portable internal package audit PASS: source/FileVersion/ProductVersion
  `0.0.2.24`, staging and license manifests (19 required artifacts), Qt
  platform/QML/Multimedia DLLs, FFmpeg/FFprobe, Tesseract/Paddle runtime and
  eSpeak runtime verified. No GUI or live Colab worker was opened; those remain
  manual acceptance gates.

## Batch 0.0.2.22: Dubbing Automatic repair and clean package

- `0.0.2.21` remains rejected for the user-reported Automatic dead-end. The
  current source repairs the gate-level root cause: preflight now offers real
  source Browse/URL import through the existing ingest controller, distinguishes
  source input from downstream blocking, removes no-op Configure controls and
  routes actionable Configure/Fix controls to production dialogs/pages.
- The production-shell offscreen interaction trace records 15 actions from
  Dubbing entry through Review, including the real Browse request, file-picker
  boundary injection, all displayed Configure buttons, no-Direct-Colab skip and
  Review/Fix. It is generated at
  `out/build/windows-msvc-tests/dubbing-qml-interaction-trace.json`.
- The `Subtitle OCR` smoke failure was a portable-test defect, not a failed URL
  route: the valid URL enabled Import, but the offscreen platform cannot own a
  native active window and therefore reports `activeFocus=false`. The test now
  verifies QML local `focus` (and still accepts desktop `activeFocus`), so it
  tests the real keyboard-focus contract without inventing OS focus.
- Targeted Dubbing regression, QML lint and full CTest are **39/39 PASS**.
  Portable internal package audit verified the staging and license manifests,
  FileVersion/ProductVersion `0.0.2.22`, Qt runtime, FFmpeg/FFprobe,
  Tesseract/Paddle OCR and eSpeak internal runtime. This is not a claim of
  manual desktop or live Colab acceptance.

## Batch 0.0.2.21: Dubbing entry gate and automatic setup

- Opening Dubbing now requires an explicit Automatic or Step-by-step choice.
  The modal cannot be escaped, clicked outside, or closed into the workspace;
  the only non-selection path leaves Dubbing. Existing project mode is saved
  without deleting transcripts, artifacts, node configuration, or resume state.
- Automatic opens a five-page setup wizard before the workspace: source and
  target language, stages/routes/models, Direct Colab workers, review, start.
  Language fields update persisted project SSoT and missing fields keep focus
  on the required control. Each stage card and review item show the actual
  route, model, variant, language, readiness, and block reason.
- Direct Colab setup is shown only for active Direct Colab nodes. Approval is
  single-use and is invalidated by source/target language, route, model,
  variant, media, or worker changes.

### Evidence 0.0.2.21

- Targeted Dubbing controller/QML regression: 5 passed, 0 failed. QML lint
  PASS. Full CTest: 39/39 PASS in 31.97 seconds.
- Portable package audit PASS: FileVersion/ProductVersion `0.0.2.21`, Qt
  Windows/offscreen plugins, RuntimeHost, FFmpeg/FFprobe, Tesseract, and a
  staged PaddleOCR health check (`ok=true`, `manifestVerified=true`) verified.

## Batch 0.0.2.20: Dubbing stage clarity and Voice Clone reference Isolator

- Dubbing now presents exactly nine production-backed UI stages: Import/Download,
  Normalize, Isolator, Transcribe/STT, Alignment/Subtitle, Translate, TTS,
  Timing/Mix and Export/Output. The presentation mapping retains durable
  workflow node IDs, project data and resume/rerun behavior.
- Alignment/Subtitle opens the existing subtitle editor and routes to Alignment
  Studio; it is not a placeholder. Voice Cloning has `Clean reference audio
  with Isolator`, reusing the existing Local or Direct Colab route/model.
- Enabled cleanup only exposes decoded Vocals to clone/train, preserves
  Original/Background for preview, caches by source/configuration fingerprint,
  and reports real progress/cancel/retry/error through Activity.

### Evidence 0.0.2.20

- QML lint PASS; targeted Dubbing, Colab separation and offscreen QML smoke:
  4/4 PASS. Full CTest: 39/39 PASS in 69.47 seconds.
- Package audit PASS: FileVersion/ProductVersion `0.0.2.20`, SHA above,
  staged Qt Windows/offscreen plugins, runtime host, FFmpeg/FFprobe,
  OCR/Tesseract, Paddle health (`ok=true`, `manifestVerified=true`) and
  third-party notices verified.

## Batch 0.0.2.19: Direct Colab / Dubbing preflight / Activity

- Direct Colab now carries an exact `capability + model + variant` contract.
  Every current exact-model notebook declares `variant: fixed`; Local CPU
  files/quantization are not presented as Colab configuration. Replacing URL,
  token, model or variant clears verification immediately.
- `ColabSessionStatus` is the shared Check/Disconnect/status surface for STT,
  TTS, cloning, design, alignment, separation, translation, OCR, LLM and
  Dubbing nodes. It shows the sanitized worker URL, capability/model/variant
  and verification time; token remains session memory only.
- Automatic Dubbing opens a preflight wizard (automatic or step-by-step,
  routes/models, only required workers, Check, Review, one-use approval).
  Any configuration change invalidates the approval.
- Voice Clone now uses a real keyboard-focusable consent checkbox, visible
  required-field reasons and responsive settings scroll. Shared settings
  toggles now have focus, descriptions, model metadata range/default hints and
  explicit Advanced affordance.
- Activity covers real long-running feature controllers. It shows route/model/
  variant/errors and renders `Working` when no measured counter exists;
  Dubbing counts only downloads started by that run, eliminating unrelated
  fixed 5%/8% reports.

### Root causes fixed

- The prior remote contract checked family/model but did not retain variant.
- QML offscreen smoke found an invalid chained `String.arg()` call and an
  unscoped `Repeater` index in the new preflight dialog; both were corrected.
- The package's pinned bsdtar dependency emitted a CMake developer warning
  that PowerShell treated as an error despite a successful exit code; the
  scoped third-party configure now suppresses only developer warnings and
  retains the exit-code gate.

### Evidence 0.0.2.19

- QML lint PASS.
- Targeted `TestRemoteExecution`, `TestDubbingProject`, and offscreen
  `QmlRouteSmoke`: 4/4 PASS.
- Generated notebooks: 32/32 PASS; exact controller/UI/notebook bindings:
  31/31 PASS.
- Full CTest: 39/39 PASS in 68.36 seconds.
- Portable package audit PASS: FileVersion/ProductVersion `0.0.2.19`, staged
  runtime manifest (19 required artifacts), Qt Windows/offscreen plugins,
  FFmpeg, eSpeak, Subtitle OCR/Paddle manifests and licenses present.

## Batch 0.0.2.18: Transcript source, STT/OCR reconciliation and package completion

- Dubbing Transcribe va Dubbing Direct Colab setup dung cung `transcriptConfiguration.transcriptSource`: STT only, OCR only, va STT+OCR. Route/model STT duoc persist rieng trong project va readiness sau reload dung route thuc te, khong doc template cu.
- Colab cards/check-all chi yeu cau worker cho nguon dang active va route Direct Colab. Nguon inactive hien `Not used`; Local khong block Colab setup. Cau hinh STT/OCR da luu duoc giu khi doi mode.
- Fusion giu STT/OCR text, confidence va provenance. Default `ask` de conflict pending; co prefer STT/OCR, batch resolution va manual final text. Conflict chua resolve chan Translate va workflow readiness.
- AI reconciliation chi duoc bat khi route co khai bao `supportsStructuredReconciliation`; M2M100/NLLB dich thuong khong duoc coi la LLM. Suggestion dung source language, timestamp/context/confidence, luon pending user accept/reject va khong ghi de evidence.
- `scripts/run_tests.ps1` dung CTest fixture thay vi goi aggregate EXE truc tiep. `scripts/package.ps1` tu dung runtime PaddleOCR da duoc controlled-prepared neu khong truyen override, roi van xac minh manifest/hash/health.

## Bang chung 0.0.2.18

- Targeted `TestDubbingProject`: **82 passed, 0 failed, 5 skipped**.
- Full CTest tren current transcript-reconciliation source: **39/39 PASS**, 64.21 giay, bao gom Subtitle OCR fixture, Remote Live Preflight contract va packaged QML smoke. Package configure sau do da truyen version `0.0.2.18` va artifact duoc audit rieng.
- OCR production code-only E2E tren `C:/Users/Nguyen Trong Khoi/Downloads/1.mp4`: **PASS** trong 599867 ms; PaddleOCR 3.7.0 PP-OCRv6 tiny, 1125 sampled/recognized frames, 430 cues o ca Standalone va Dubbing, Dubbing reuse cache, Tesseract fallback = false, child processes alive = 0. Artifact: `out/ocr-e2e-new/standalone-zh-Hans.srt`, `dubbing-zh-Hans.srt`, `transcript-zh-Hans.txt`, `OCR_TEST_RESULT.md`.
- Package audit: FileVersion/ProductVersion `0.0.2.18`; 26 required runtime/license artifacts co mat, bao gom `platforms/qwindows.dll`, FFmpeg/FFprobe va isolated PaddleOCR runtime. Worker health `ok=true`, `manifestVerified=true`.
- Khong mo GUI. Live Direct Colab worker/notebook/tunnel va manual desktop acceptance van **chua xac minh**.

## Batch 0.0.2.17: Dubbing ro rang resource, TTS va OCR ROI

- Workflow Dubbing dung metadata runtime-backed cho vai tro node, route va resource badge. GPU-heavy/GPU-required hien dung `Nen dung Colab`; node CPU khong bi gan canh bao chung chung.
- Dubbing Voice duoc doi thanh TTS: chon built-in hoac saved Voice Cloning preset, persist `ttsVoiceId`, migrate project cu, chan preset missing/invalid/incompatible va khong co UI tao clone trong Dubbing.
- Direct Colab saved voice reuse profile theo session; API Gateway va Colab van doc lap. Khong co fallback im lang va khong luu URL/token vao project/report.
- C4 da duoc sua trong source `1e05fb2`: Local Dubbing saved voice chi chay tren Qwen3-TTS persistent profile, goi native `crispasr_session_set_voice()` mot lan cho moi profile signature va sau do goi TTS cho moi segment. Runtime Local clone-per-request bi chan ro rang; khong clone lai theo segment.
- Dubbing OCR/STT+OCR dung ROI normalized that cua Subtitle OCR, 8 handles, letterbox-aware content rect, preview crop, persistence/handoff; STT an ROI.
- PaddleOCR 0.0.2.16 baseline duoc giu nguyen. Candidate 0.0.2.17 stage Qt, FFmpeg, eSpeak va bundled PaddleOCR 3.7.0 PP-OCRv6 tiny; health check manifest PASS.

## Bang chung

- Targeted Dubbing/workflow/OCR/Colab/QML tests PASS.
- Full CTest sau sua C4: **39/39 PASS**; targeted Dubbing/validator/Colab/QML: **6/6 PASS**.
- EXE audit cua artifact lich su `0.0.2.17`: FileVersion/ProductVersion `0.0.2.17`, SHA-256 o tren, `platforms/qwindows.dll`, FFmpeg/FFprobe, Paddle worker/runtime/model/manifest deu co; worker health `manifestVerified=true`.
- Khong ghi de artifact `0.0.2.17`; do ten candidate nay da ton tai truoc C4, current source chua co package moi. Khong mo GUI nguoi dung.

## Manual acceptance con mo

- Desktop ROI pointer/resize voi video letterbox/rotation that, file picker va responsive UI.
- Live Direct Colab worker/notebook/tunnel va audio/video output that.
- Live Qwen3 saved-voice audio/profile reuse va Direct Colab worker that van can manual acceptance; automated source regression PASS nhung khong thay the live audio acceptance.

## Quy uoc tai lieu

- `AI_AGENT_REQUEST.md`: chi yeu cau chua xong do nguoi dung/agent cap nhat.
- `AI_AGENT_RESPONSE_REPORT.md`: chi report moi nhat.
- `PROJECT_MEMORY.md`: lich su cap san pham ngan gon.
- Bang chung chi tiet cu tra trong Git history; khong tao report theo version rieng.

## Batch 0.0.2.30: Voice Clone reference Isolator and reliable packaging

- Voice Clone now owns a separate Direct Colab Spleeter session/controller for
  reference cleanup. Its setup and Run action remain inline; only the cached
  vocals artifact may feed the clone request, with no standalone-tab detour or
  second upload.
- Standalone Isolator Export WAV now uses `FileDialog.selectedFile`, validates
  the `.wav` destination, reports the exact saved path and surfaces failures.
- Direct Colab clone profiles may omit the reference transcript. Generated
  exact notebooks use model-specific audio-only profile paths; Local Qwen3
  retains the transcript constraint its runtime requires.
- Shared-Qt packaging explicitly skips CMake's static-plugin import scan and
  continues to deploy QML with `windeployqt --qmldir qml`. This removes the
  Windows scanner stall without omitting dynamic QML deployment.

### Evidence 0.0.2.30

- Focused Voice Clone, Remote Execution, Direct Colab Separation, Source
  Separation and Studio Capability suites: all PASS.
- Full headless CTest: **39/39 PASS** in 67.74 seconds. Exact Colab notebooks:
  **32/32 verified**.
- Internal portable candidate audited: FileVersion/ProductVersion `0.0.2.30`,
  SHA-256 `5C70D8194621DF613DC64EF6777C324D68D67C6D73121FB0ED2C0860F8C8F3EB`,
  staged runtime/license inventory 19/19 and staged offscreen smoke PASS.
- No visible GUI or live Colab worker was opened; live audio/service acceptance
  remains a manual gate.

### 2026-08-10 - dedicated Douyin Chromium session

- The fresh-cookie failure was not treated as a reason to scrape a user's
  existing browser. `scripts/douyin_browser_session.py` now runs a separate,
  app-owned Playwright Chromium profile at `~/.lastudio/douyin-browser-profile`.
- Import/Download and the Dubbing source panel expose setup/check/disable
  controls. A successful check is required before a Douyin download can use
  the browser worker. The worker navigates the page, captures a real video
  resource and streams it to app staging with session cookies/referer; it does
  not print or persist cookies or signed URLs.
- The existing explicit Netscape cookie + yt-dlp path remains available and
  the browser route never silently falls back to Local inference or a browser
  cookie import. Package staging now includes the helper script; Playwright and
  Chromium remain explicit user-installed dependencies.
- Regression: helper argument/privacy contract, targeted media/QML checks,
  QML lint and full CTest **39/39 PASS**. `graphify update .` completed.
- No new versioned EXE was packaged and no live authenticated Douyin download
  was claimed. Manual acceptance requires installing Playwright/Chromium,
  signing in through the managed profile and pressing Check connection.

### 2026-08-10 - Download route scope correction

- Standalone Download now only downloads and lists media. Dubbing task
  checkboxes, execution order, and processing outputs were removed from this
  route; the Dubbing media queue remains the owner of those actions.
- Replaced the misleading queue-only label with a visible **Download** action
  and added a prominent **Set up Chromium** card with connection/disable
  controls.
- QML lint PASS; targeted checks 5/5 PASS; full CTest **39/39 PASS**.
- Internal portable candidate `0.0.2.37` was staged and version metadata was
  verified. It is not a distributable release because the eSpeak payload is
  unsigned.

### 2026-08-10 - Playwright interpreter auto-selection

- The managed Douyin service now probes Python candidates on `PATH` and uses
  the first interpreter that can import Playwright, avoiding a false failure
  when the first system Python lacks the dependency.
- Candidate `0.0.2.38` was staged with matching version metadata and SHA-256;
  full CTest 39/39 and QML lint passed. It is internal-only due to unsigned
  eSpeak.

### 2026-08-10 - 0.0.6.2 Dubbing preview workspace

- Loaded-source setup is now collapsed deterministically at the real
  file-picker boundary. The compact setup drawer remains explicitly available,
  while **Open video** / **Replace video** remains visible in the preview
  header.
- The Dubbing preview supports Fit source, 16:9, 9:16 and 1:1 display frames
  without crop or stretch. OCR ROI, subtitle overlay and playback controls use
  the selected frame rather than the entire panel.
- **Focus video** clears the timeline and lower project controls; the new
  header sliders toggle independently shows/hides the lower controls. Existing
  Dubbing routes and features were not removed.
- QML lint, targeted media/remote/offscreen-QML checks (4/4) and full CTest
  (39/39) passed. `graphify update .` completed. Portable internal package
  `out/LA-Studio-0.0.6.2/LA-Studio-0.0.6.2.exe` has File/Product Version
  `0.0.6.2` and SHA-256
  `52B7B4228742C5C769F80C1EE9E315F55B6406FFBF32E0FE1F6FE7FFBFE05B45`.
- Source commit `9b39b3c` was pushed directly to `main`. No GUI or live remote
  service was opened; visual acceptance with a real packaged video remains a
  manual gate.

### 2026-08-11 - 0.0.6.4 fixed-pane Dubbing header and OCR bootstrap

- The Dubbing workspace retains its fixed-pane arrangement: left task rail,
  central aspect-aware preview, right task review panel only after selection,
  and a full-width lower timeline. Project language/quality are selected in
  the post-mode project-setup dialog rather than a permanent lower strip.
- A runtime QML smoke contract now proves the header has one flexible workflow
  rail and one fixed action cluster. It accepts labelled controls at normal
  width and intentional, tooltip-equipped icon-only controls at compact width,
  preventing clipped `Workflow` fragments or header overlap.
- The generated Subtitle OCR Colab notebook is now bootstrap revision `.9`.
  It never creates a Python venv or calls `ensurepip`; it uses an app-owned
  `--target` package directory. Any Colab traceback showing `VENV_DIR` or
  `ensurepip` is an older notebook copy and requires reopening the tracked
  notebook in a fresh runtime.
- Evidence: QML lint PASS; exact generated notebooks **32/32**; full offscreen
  CTest **39/39 PASS** in 59.24 seconds; graph update PASS. Portable internal
  `out/LA-Studio-0.0.6.4/LA-Studio-0.0.6.4.exe` staged with 19 required runtime
  artifacts and SHA-256
  `2367C0A735D20F6692C2EF6BCCF3EF22F097188C7C3C92175C6A728F3F0EC5EF`.
- The package has not been visibly launched and no live Colab worker was
  claimed. It remains internal-only because the verified eSpeak payload is
  unsigned.

### 2026-08-12 - 0.0.6.5 Dubbing layout and OCR bootstrap correction

- The Dubbing header is 52 px and intentionally compacts action controls to
  icon-only buttons with tooltips before text can be clipped. The status strip
  is 46 px rather than a large permanent banner.
- The preview toolbar is a single 40 px row: `VIDEO PREVIEW`, source actions,
  and the **Original / Dubbed** selector share that row. The selector is no
  longer a second row. The title's letter spacing was reduced for a denser
  editor treatment.
- Timeline resizing is bounded to its lower workspace. A QML smoke check now
  fails if the timeline overlaps the video workspace; the initial height is
  300 px and its maximum is calculated from the available editor height.
- Compact task settings panels clip their contents and stack model, Colab, and
  action controls within the pane instead of overflowing into the preview.
  The default Dubbing source language is Chinese (`zh`) in the project model,
  workflow fallback paths, controller, and setup dialog.
- Subtitle OCR notebook revision `.10` fixes the reported bootstrap crash: the
  generated call is now `ocr_pip("--no-cache-dir", ...)`, so the actual command
  contains exactly one `pip install`, rather than the invalid duplicated
  `pip install ... install ...` invocation.
- Evidence: generated exact-model notebooks **32/32 PASS**; full CTest
  **39/39 PASS** in 61.53 s, including the offscreen QML route smoke;
  `graphify update .` and `git diff --check` passed. Production portable
  package staged with 19 required runtime artifacts at
  `out/LA-Studio-0.0.6.5/LA-Studio-0.0.6.5.exe`; Product/File Version are
  `0.0.6.5`, SHA-256
  `40015BFB9C9E44321785BDD20AD61E1673A631514C9EFD3641BAE74941780A84`.
- The packaged OCR notebook is revision `.10`. No visible desktop app or live
  Colab worker was opened for this run. The package remains internal-only
  because its hash-verified eSpeak payload is unsigned.
