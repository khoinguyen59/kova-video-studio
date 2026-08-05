# Bao cao tong hop LA Studio

Cap nhat: 2026-08-05

## Baseline hien tai

| Muc | Trang thai |
| --- | --- |
| Latest packaged candidate | `0.0.2.29` |
| Artifact | `out/LA-Studio-0.0.2.29/LA-Studio-0.0.2.29.exe` |
| SHA-256 | `3D37B2DE11575EE265C2FAAB43B20DE8185557FEB587FAC74654E66656EBC2D7` |
| Current source | `main`; full CTest 39/39 PASS before packaging. Version/source/package consistency verified for 0.0.2.29. |
| Distribution | Internal only; eSpeak MSI SHA-verified but unsigned |

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
