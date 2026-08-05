# Tri nho du an LA Studio

Muc dich: luu quyet dinh san pham, loi da khoanh vung va trang thai nhat quan de agent khong lam lai viec cu. Khong chep patch hay log dai.

## Hien trang

- Desktop local-first: STT, TTS, Voice Cloning, Voice Design, Alignment, Translation, Dubbing, LLM Chat, Download va Subtitle OCR.
- API Gateway va Direct Colab la hai route doc lap. Token/URL Colab chi o memory session, khong persist vao project/report.
- Package noi bo moi nhat: `0.0.2.28` tai `out/LA-Studio-0.0.2.28/LA-Studio-0.0.2.28.exe`, SHA-256 `63BA1B5B36A70039ADAB92FA5DB607E0556A3C5A7A55B515B116B516D02A4D92`. FileVersion/ProductVersion `0.0.2.28`, full CTest 39/39 va package audit PASS. Manual GUI/live Colab luon la gate rieng.
- `0.0.2.23` duoc giu nguyen nhung khong duoc chap nhan: audit bat thieu evidence trace route/model/worker truoc-sau. `0.0.2.24` bo sung trace va package moi, khong ghi de candidate cu.
- `0.0.2.21` khong duoc chap nhan cho Dubbing Automatic: gate/preflight co
  dead-end media va no-op Configure. `0.0.2.22` sua duong ingest truoc gate,
  state/action card va Review Fix, co production-shell offscreen trace 15
  actions. Subtitle OCR `link-input` smoke da sua theo local QML focus vi
  offscreen khong co native active window.

## Lich su cap san pham

### 2026-08-06 - Dubbing subtitle ordering and Spleeter progress truthfulness (source only)

- The visible workflow is now `Import/Download -> Normalize -> Isolator ->
  Transcribe/STT -> Translate -> Subtitle -> TTS -> Alignment -> Export/Output`.
  `review-transcript` remains the source-text quality gate under Transcribe;
  `review-translation` is the target subtitle gate after Translate; timing and
  conflict handling remain production nodes under Alignment after speech is
  synthesized.  Export continues to use target-language segment text.
- Direct Colab Spleeter reports worker finalization separately from artifact
  transfer.  At worker 90%, the Activity label says what the CUDA worker is
  doing; once ready, it exposes received/total bytes for vocals or background,
  not a false workflow percent.  A five-minute 90% finalization watchdog
  cancels the remote job and explicitly preserves the no-Local-fallback route.
- Added direct runner and controller regressions. Release source/test targets
  build; focused suites pass 2/2 and all 37 non-GUI CTest tests pass in 62.27s.
  QmlRouteSmoke was intentionally not run because it launches the desktop EXE;
  live Colab and desktop acceptance remain manual gates. No new package.

### 2026-08-06 - Spleeter cloudflared bootstrap repair (source only)

- The Spleeter Direct Colab worker completed its CUDA probe, then crashed
  before creating a tunnel because a new Colab runtime has no `cloudflared`
  executable and the launcher did not catch `FileNotFoundError`.
- `cloudflared_ready()` now handles that absence, `ensure_cloudflared()`
  installs the official package when needed, then confirms the executable is
  usable before starting a tunnel. Route separation remains intact: there is
  no Local or API fallback.
- The pinned Spleeter notebook is generated rather than hand-maintained. It
  locks worker source to immutable commit `2502485` and verifies the new
  launcher SHA-256. The source commits are `2502485` and `5440f94`.
- Validation: notebook generator 32/32, targeted separation 7/7 and full
  CTest 39/39 PASS in 133.83 seconds. No new package and no live Colab session
  were claimed as tested; a fresh notebook runtime is required.

### 2026-08-05 - 0.0.2.28 Direct Colab Spleeter CUDA hardening

- Evidence from `0.0.2.27` shows the reported `CUDNN_FE_HEURISTIC_QUERY_FAILED`
  is emitted by the exact Direct Colab Spleeter worker. It is not a Local GPU
  run and must not be fixed by silently switching model, route or device.
- `sherpa-onnx-spleeter-2stems-fp16` remains the exact model. The dedicated
  worker creates direct ONNX Runtime CUDA sessions with
  `cudnn_conv_algo_search=DEFAULT`, passes a bounded CUDA startup probe before
  exposing health, and separates long source audio in 20-second cores with
  1.5-second context. It sends measured segment progress and explicitly never
  starts a local fallback.
- Notebook `LA_STUDIO_SEPARATION_SPLEETER_2STEMS_GPU.ipynb` pins worker commit
  `f1b26005b6e3677db444ac12774ba3eaf9d9b204` and verifies template SHA-256.
  CMake now installs the worker templates into portable support docs; this was
  found by an audit and covered by `TestRemoteExecution`.
- `ColabSeparationRunner` masks remote CUDA trace in the desktop dialog but
  writes raw detail to System Logs; QML delegate root/model-field diagnostics
  are repaired. Full CTest 39/39 PASS in 86.87s and portable package audit
  PASS. Live notebook/desktop acceptance is not claimed.

### 2026-08-05 - Follow-up Direct Colab code audit

- This was a source/protocol audit, not desktop evidence about the user's
  current EXE or Colab session. The direct runner returns before the Local
  resolver branch can execute, so a `colab-direct` Spleeter selection cannot
  invoke Local source separation.
- The old package's notebook embeds the heuristic `sherpa_onnx` CUDA worker.
  The fixed 0.0.2.28 notebook/worker uses a pinned revision, SHA checks,
  explicit DEFAULT cuDNN algorithm, startup probe and bounded chunks. A fresh
  0.0.2.28 notebook runtime, URL and token are mandatory; an existing 0.0.2.27
  Colab session remains old.
- Audit-only run: no source/package mutation. Targeted Dubbing/Direct
  Colab/remote/QML suites were 8/8 PASS and full CTest was 39/39 PASS. This
  evidence is not a claim of live Colab acceptance.

### 2026-08-05 - 0.0.2.27 Adaptive Direct Colab integrity

- Hai loi `free disk space` Local Qwen va runtime `llama-b10036...zip` thieu
  SHA-256 khong duoc sua bang cach bo checksum. Nguyen nhan la Adaptive
  Dubbing con hidden Local fallback va metadata Local cu co the song sot o
  nested parameters sau khi user da chon Direct Colab/API.
- Adaptive rewrite co route Direct Colab ro rang cho `qwen3.5-2b`, dung notebook
  `LA_STUDIO_LLM_QWEN3_5_2B_GPU.ipynb`, bat buoc exact verified
  `llm-chat/qwen3.5-2b` worker va hien la worker con cua Translate. Khong co
  local automatic download/fallback; API Gateway va Local van la route rieng.
- Moi remote reselect xoa Local runtime/files/signature o ca root va parameters;
  project luu route/model khong luu token/URL. Chon Direct Colab khong xoa API
  Gateway URL/credential va khong can thiệp controller LLM Chat doc lap.
- Regression legacy Local -> Direct, Direct reselect voi nested stale keys,
  five exact CUDA mock worker, persistence/secret/card/no-download PASS. QML
  lint PASS; targeted 7/7; full CTest 39/39 (72.18s); package audit 19 artifact
  PASS. `0.0.2.26` da stage truoc khi bat nested-key case nen khong duoc chap
  nhan; `0.0.2.27` la candidate hien tai.

### 2026-08-05 - 0.0.2.25 Direct Colab route and Activity correction

- Loi user report khong phai checksum sai: wizard da chon/verify Direct Colab
  nhung Automatic lai dung `remoteFirstMode` global va quay ve local Sherpa
  setup. Downloader dung da chan runtime asset thieu SHA-256. `ensureAutomaticModel`
  bay gio giu provider da luu theo tung node; Direct Colab exact worker va API
  Gateway khong the silently fallback sang local. Remote TTS cung khong load
  local runtime an.
- Activity map internal `model-setup`/production node ve 8 stage nguoi dung:
  title, vi tri nhu `Isolator (3/8)`, route va exact model. % chi hien khi
  worker/download gui counter that; khong dat % gia. `workflowChanged` refresh
  card ngay khi setup doi stage.
- Regression dung 4 exact-model Direct Colab mocks voi `remoteFirstMode=false`
  chung minh Automatic thoat setup ma khong enqueue Sherpa local, va Activity
  bao Isolator 3/8/Direct Colab. QML lint PASS; targeted 7/7, CTest 39/39
  PASS (36.02s), package offscreen QML smoke 18 trace event PASS. Artifact
  `0.0.2.25` da audit Qt/FFmpeg/RuntimeHost/runtime-license manifest; khong
  claim live Colab/manual desktop PASS.

### 2026-08-05 - 0.0.2.24 Automatic Dubbing eight-stage contract

- Automatic preflight dung mot presentation contract 8 stage: Import/Download
  (`media-input`), Normalize (`ingest`), Isolator (`source-separate`),
  Transcribe/STT (`transcribe`), Alignment/Subtitle (`review-transcript`,
  `fit-timing`, `review-conflicts`), Translate (`translate`,
  `review-translation`), TTS (`assign-voices`, `synthesize`) va
  Export/Output (`mix`, `export`). Timing/Mix khong con la user card nhung
  production node/algorithm van duoc giu.
- Preflight readiness khong con false Ready/default mo ho: Normalize hien ro
  local preprocessing va no model required sau khi media hop le; Isolator va
  tat ca model stage can route + exact model, Local runtime/model resolve,
  API Gateway credential rieng hoac Direct Colab exact worker verified.
- Configure mo sub-dialog thuoc wizard, khong dieu huong ra workspace; route
  chon truoc picker. Worker page chi hien Direct Colab stage, URL/token session
  memory only; doi route/model huy worker verification cu.
- Regression co readiness matrix va QML shell trace 18 event. Trace ghi exact
  `Local/No model/0 workers -> Direct Colab/exact models/2 workers ->
  Local/exact models/0 workers`, ben canh click Configure cho ca 8 stage.
- QML lint PASS, QmlRouteSmoke 2/2 PASS, full CTest 39/39 PASS (25.57s).
  Package portable audit PASS: PE/source version 0.0.2.24, SHA o tren, 19
  required staged artifacts, Qt/FFmpeg/Tesseract/Paddle/eSpeak va licenses.
  Khong mo GUI hay live Colab; do van la manual gate.

### 2026-08-04 - 0.0.2.22 Dubbing Automatic repair

- Source & language preflight co Browse local va URL import production truoc
  khi danh gia workflow; missing media chi la `Needs input`, khong lam mat ly
  do cua cac node sau. Configure chi hien o node co action, Review Fix quay
  dung page/field. Trace offscreen di tu Dubbing entry den Review ghi 15 thao
  tac QML/controller thuc.
- `QmlRouteSmoke` Subtitle OCR link input da fail sai do `activeFocus` phu
  thuoc native active window ma platform offscreen khong co. Test bay gio kiem
  tra local QML focus va Import enable; full CTest 39/39 PASS.
- Package portable noi bo audit PASS; FileVersion/ProductVersion `0.0.2.22`,
  SHA va runtime/license manifests da duoc xac minh. Khong mo GUI hay xac
  minh live Colab.

### 2026-08-04 - 0.0.2.21 Dubbing entry and automatic setup

- Dubbing is now protected by a mandatory, non-dismissible entry gate. The
  persisted Automatic/Step choice is durable project state only; changing it
  cannot destroy prior workflow artifacts or configuration.
- Automatic setup owns the route before workspace entry: source/target
  language SSoT, active stage route/model/variant cards, Direct Colab-only
  worker checks, review, and one-use approval. Language/route/model/worker
  changes invalidate approval. Step-by-step starts at Import for a new project.
- Regression: QML lint, targeted Dubbing gate/preflight coverage and full CTest
  39/39 PASS. Portable package audit verified version, Qt, media/OCR runtimes
  and Paddle health. No GUI or live Colab acceptance was claimed.

### Truoc 2026-07-30

- Xay dung cac studio cot loi va pipeline Dubbing, local/remote runtime host, subtitle/timing/export va CapCut Draft.

### 2026-07-30 den 2026-08-01

- Them exact-model Colab notebooks/routes, harden retry/tunnel/capability/model/progress, public media ingest va saved voice reuse.
- Hoan thien Subtitle OCR, Dubbing STT/OCR/STT+OCR fusion, subtitle editor/export/timing/CapCut Draft.
- Sua false-ready OCR language, frame extraction safe-end, ROI interaction va media controls; bundle Tesseract 5.5.1 va Direct Colab OCR.

### 2026-08-02 — 0.0.2.16 PaddleOCR baseline

- Bundle PaddleOCR 3.7.0 PP-OCRv6 tiny Local CPU: isolated Python, worker, model cache, manifest/license va package health check.
- Sua staging runtime, UTF-8 manifest BOM va license path; 39/39 CTest PASS.

### 2026-08-02 — 0.0.2.17 Dubbing batch

- Workflow Dubbing co metadata chung cho node role/resource va badge dynamic `Nen dung Colab`; UI/QML doc metadata cung controller/runtime thay vi hard-code theo ten model.
- Voice trong Dubbing thanh TTS. `ttsVoiceId` luu built-in/saved voice, migrate `cloneVoicePresetId` cu; saved preset duoc validate/loc compatibility. Voice Cloning authoring chi nam o studio doc lap.
- Direct Colab cache profile saved voice trong session. API gateway khong thay the saved voice im lang.
- OCR ROI production duoc dua vao Dubbing preview cho OCR/STT+OCR, normalized/persisted/handoff, drag + 8 resize handles, crop preview va controls duoi video.
- Candidate 0.0.2.17 da audit version/hash, Qt platform, FFmpeg, PaddleOCR worker/model/manifest va health check; 39/39 CTest va packaged QML smoke PASS.

### 2026-08-02 - C4 saved voice local correction (source `1e05fb2`)

- Local Dubbing saved voice khong duoc clone lai theo segment. Chi Qwen3-TTS duoc phep vi native runtime co `crispasr_session_set_voice()` de dat va reuse profile theo signature trong session; backend clone-per-request bi chan truoc Run TTS voi huong dan chon Qwen3-TTS hoac Direct Colab.
- Internal saved-profile fields chi di qua Dubbing-to-TTS boundary, khong persist vao project/UI va duoc mask trong log. API Gateway va Direct Colab van tach biet; Direct Colab cache profile cu khong thay doi.
- Test targeted 6/6 va full CTest 39/39 PASS. Artifact `0.0.2.17` tao truoc correction khong bi ghi de va khong dai dien current source; can ten candidate moi duoc phep de package.

### 2026-08-03 - 0.0.2.18 transcript reconciliation

- `transcriptSource` la single source of truth cho STT only/OCR only/STT+OCR o Transcribe va Direct Colab setup. STT provider/model thuc te duoc persist/reload; cards, readiness va Colab validation chi danh gia nguon active va selected route.
- STT/OCR fusion luu day du evidence. Policy `ask` default giu conflict pending; prefer STT/OCR, batch/manual resolution, AI suggestion accept/reject deu preserve original evidence. Conflict unresolved chan Translate.
- AI reconciliation yeu cau capability structured explicit; model dich thuong khong duoc dung lam LLM va khong co fallback route im lang.
- OCR E2E production tren `1.mp4` PASS: 1125 Paddle frames, 430 matched Standalone/Dubbing cues, cache reuse, khong Tesseract fallback hay child-process leak; artifact o `out/ocr-e2e-new`.
- Package portable `0.0.2.18` audit FileVersion/ProductVersion, SHA, Qt/FFmpeg/PaddleOCR manifest-health va license/runtime inventory PASS. Package script tu resolve `out/paddle-ocr-runtime-ready` neu khong duoc truyen override, tranh loi empty binding.

### 2026-08-03 - 0.0.2.19 Direct Colab and workflow readiness

- Direct Colab session now verifies and retains exact capability, model and
  variant. All current exact-model notebooks advertise the fixed variant;
  Local CPU file variants do not leak into the Colab route. Replacing any
  connection/model/variant input invalidates the old verification.
- One shared Colab status component exposes Check connection, Disconnect,
  sanitized worker identity and verified timestamp across all Direct Colab
  studios and Dubbing nodes. Token/URL stay in session memory and are absent
  from project persistence, logs and handoff reports.
- Automatic Dubbing requires a configuration/review/check preflight and a
  fresh one-use approval. Step-by-step remains supported with the same route
  readiness rule.
- Voice Clone consent is a real focusable checkbox. Required controls show a
  local reason; settings are scrollable at narrow sizes. Shared toggles and
  metadata controls now make descriptions, ranges/defaults and Advanced state
  explicit.
- Activity uses real controller lifecycle and only measured progress. It no
  longer borrows unrelated download progress for Dubbing or displays invented
  5%/8% values. QML smoke caught and the source fixed an invalid status string
  formatter and missing Repeater index before packaging.
- Release validation: QML lint, targeted Colab/Dubbing/QML smoke, notebook
  contract 32/32, binding audit 31/31 and full CTest 39/39 PASS. Portable
  package `0.0.2.19` passed staged runtime/license audit. Pinned bsdtar
  packaging suppresses CMake developer warnings only; non-zero configure/build
  exit codes still fail packaging.

### 2026-08-04 - 0.0.2.20 Dubbing and Voice Clone reference cleanup

- Dubbing presentation is a non-destructive mapping over durable workflow
  nodes: nine named stages retain legacy project node IDs, artifacts and
  resume/rerun semantics. Alignment/Subtitle routes to the existing subtitle
  review/editor and Alignment Studio.
- Voice Cloning may clean its reference with the existing Isolator. The
  enabled path only exposes a decoded Vocals stem to clone/train, preserves
  original/background for preview, records route/model activity, supports
  cancellation/retry and invalidates cache when source/model/route changes.
- Direct Colab separation propagates the existing loopback-test-only transport
  flag to its runner; production remains HTTPS-only. A synchronous state-reset
  ownership race in reference cleanup was fixed and covered by integration
  regression.
- `0.0.2.20` package audit passed with PE FileVersion/ProductVersion,
  SHA-256, Qt/FFmpeg/OCR/Paddle runtime and license inventory verified.

## Quy tac duy tri

- Doc `AI_AGENT_REQUEST.md` truoc task; sau task cap nhat response latest, summary cumulative va memory nay khi da co bang chung.
- Package mot candidate sau khi batch va full gate PASS; khong ghi de candidate cu.
- Bao cao ro automated/package PASS khac voi GUI/live service manual acceptance.
- Khi user dua evidence FAIL, dua dung loi do vao request va sua theo regression truoc package tiep theo.
