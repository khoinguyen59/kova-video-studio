# Tri nho du an LA Studio

Muc dich: luu quyet dinh san pham, loi da khoanh vung va trang thai nhat quan de agent khong lam lai viec cu. Khong chep patch hay log dai.

## Hien trang

- Desktop local-first: STT, TTS, Voice Cloning, Voice Design, Alignment, Translation, Dubbing, LLM Chat, Download va Subtitle OCR.
- Dubbing UI contract (2026-08-11): Automatic Dubbing has exactly eight visible stages—Import/Download, Normalize, Isolator, Transcribe/STT, Alignment/Subtitle, Translate, TTS, Export/Output. Internal timing/review-conflicts belong to Alignment/Subtitle, review-translation belongs to Translate and mix belongs to Export; they must not be duplicated in the header. Header stage list must be controller-derived. At compact widths action labels change to icon-only controls with tooltip/accessibility rather than truncating. The fixed workspace has a task shelf, center preview and right inspector as layout items; panels push one another and must not overlay. Preview default is 1040 px; timeline default is 340 px. Source/target language and Fast/Adaptive/Custom policy remain a project-setup dialog step after Automatic or step-by-step selection, not a permanent lower strip. Full CTest 39/39 passed in 57.04 seconds after this change; no GUI or live Colab session was used.
- Subtitle OCR Colab current source is bootstrap revision `subtitle-ocr-bootstrap-2026-08-11.8`: no `venv.EnvBuilder` or `ensurepip`; pinned dependencies install into the app-owned `/content/la_studio_subtitle_ocr_site`. The generated notebook verifier is 32/32 PASS. A Colab traceback from `venv.EnvBuilder` proves the user opened an older notebook copy; reopen the current tracked notebook in a fresh runtime.
- API Gateway va Direct Colab la hai route doc lap. Token/URL Colab chi o memory session, khong persist vao project/report.
- Package noi bo moi nhat: `0.0.6.3` tai `out/LA-Studio-0.0.6.3/LA-Studio-0.0.6.3.exe`, SHA-256 `B3322735B67EEE453FA5549AB35CB5DC95D2E578B68A9BEC7BCDE25F1FDB3137`. QML lint, full CTest 39/39, portable artifact audit va hidden offscreen startup smoke PASS. Source hien co hotfix Subtitle OCR Colab chua nam trong EXE nay; manual GUI/live Colab luon la gate rieng.
- Source moi nhat sau `32ee731` giu tat ca Dubbing task/route nhung sua workbench pane co dinh: History/Task controls ben trai tuy chon, Preview o giua, task output ben phai va Timeline full-width ben duoi. Header chi cho task rail scroll; action cluster giu du nhan **Workflow**, action phu nam trong overflow menu. Preview default `940 px`, minimum video `440 px`; toolbar chinh sua tach khoi row trang thai video. Breakpoint duoc tinh theo tong minimum width that: task shelf yield duoi `1450 px`, History yield duoi `1080 px`, nen QML khong clip/offscreen hay overlay Preview o width desktop trung binh; Timeline co target resize thuc `28 px`. Language va execution quality phai hoi trong popup sau khi chon Automatic/step-by-step hoac tu **Project settings**, khong duoc nam thuong truc duoi Timeline. Automatic route smoke phai click popup setup that roi moi vao Source & language preflight; compact smoke phai kiem tra selector o task shelf hoac right detail pane tuy breakpoint, khong duoc doi mot control an co chu y thanh loi. Subtitle OCR Colab revision `subtitle-ocr-2026-08-11.7` dung dedicated package directory, xoa chi folder bootstrap cu cua app va resolve Paddle/PaddleOCR/PaddleX/Pillow trong mot `pip --target` transaction; khong dung `venv.EnvBuilder`, `virtualenv` hay `ensurepip`. QML lint, notebook verifier 32/32, diff check, Graphify update va full CTest **39/39 PASS (57.83 s)** bang project-local Qt 6.9.3. Khong co GUI/live Colab hoac EXE moi trong batch nay.
- `0.0.2.23` duoc giu nguyen nhung khong duoc chap nhan: audit bat thieu evidence trace route/model/worker truoc-sau. `0.0.2.24` bo sung trace va package moi, khong ghi de candidate cu.
- `0.0.2.21` khong duoc chap nhan cho Dubbing Automatic: gate/preflight co
  dead-end media va no-op Configure. `0.0.2.22` sua duong ingest truoc gate,
  state/action card va Review Fix, co production-shell offscreen trace 15
  actions. Subtitle OCR `link-input` smoke da sua theo local QML focus vi
  offscreen khong co native active window.

## Lich su cap san pham

### 2026-08-11 - Dubbing medium-width non-overlap correction (unpackaged)

- `32ee731` dua compact task shelf threshold len `1450 px` va History threshold
  len `1080 px`. Hai gia tri tu tong min width pane thuc, khong la breakpoint
  tuy y; do do layout khong con co the ep hoac clip Preview o 1200--1440 px.
- QML parser va non-overlap source contract PASS; graphify update da chay. Qt
  SDK van thieu nen CTest/package/GUI acceptance khong duoc claim.

### 2026-08-11 - Dubbing fixed header/video workspace and OCR bootstrap transaction (unpackaged)

- `d955dd9` tach task rail scroll va action cluster co dinh o Dubbing header,
  bao dam nhan **Workflow** khong bi cat. Save/Export/Pause/Stop va toggle task
  controls nam trong overflow menu, khong tranh width voi Preview.
- Source panel co row trang thai video rieng va preview toolbar scroll ngang;
  canvas video default `940 px`, minimum `440 px`. History/task control yield
  theo layout o workspace hep, Timeline co `MouseArea` hit target `28 px` de
  resize thuc, khong con thanh decor kho keo.
- Notebook OCR `.7` in bootstrap revision, xoa legacy app-owned venv folder,
  va cai dat stack fixed trong mot transaction voi CUDA package index. Verifier
  bat buoc revision, cleanup legacy, CUDA index va dung mot install call.
- Changed QML parser, source contract, Python compile, notebook verifier 32/32,
  diff check va graphify update PASS. CMake reconfigure da nhan MSVC sau khi
  nap Build Tools, nhung dung o `Qt6Config.cmake` vi thieu Qt 6.9 development
  SDK. Khong co CTest, EXE/package, GUI hay live Colab PASS.

### 2026-08-11 - compact Dubbing controls and OCR package isolation (unpackaged)

- `771dcf3` giu pane wide resizable nhung them responsive contract thuc: khi
  workspace < 1000 px, left task shelf khong duoc ton tai offscreen; Run va
  Configure cua node hien trong right review pane compact. QML smoke kiem tra
  review pane khong the vuot khoi workspace.
- Notebook OCR ep moi package vao
  `/content/la_studio_subtitle_ocr_site` bang `--ignore-installed`; probe
  khang dinh `PIL`, Paddle, PaddleOCR va PaddleX deu den tu mot directory
  truoc CUDA worker. Muc tieu la chan lai state Pillow/Paddle tron global.
- QML parser, source contracts, Python compile, notebook verifier 32/32,
  diff check va graphify update PASS. Full CTest/build BLOCKED vi thieu Qt;
  khong co EXE, GUI hay live Colab PASS.

### 2026-08-11 - fixed Dubbing panes and Subtitle OCR bootstrap (unpackaged)

- `8d256ce` thay horizontal Flickable cua Dubbing bang layout co dinh. Khi
  chon task, left shelf va right result pane chiem dung layout width va day
  Preview, khong de/che Preview. Header co hai hang: utility actions va task
  rail scroll doc lap; khong cat label **Workflow**. Resize History/Preview/
  Timeline dung `DragHandler` de keo lien tuc, khong chi nhan trong thanh 18px.
- `DubbingProjectSetupDialog` chon source language, target language va
  Fast/Adaptive/Custom sau khi chon mode. Lower `DubbingProjectStatusPanel`
  khong con instantiate trong page; speaker/output chi duoc hien khi task can
  thay vi chiem cho thuong truc.
- Notebook Subtitle OCR revision `subtitle-ocr-2026-08-11.6` bo ca stdlib venv
  bootstrap loi `ensurepip` va nhanh `virtualenv` cu. Exact Paddle stack duoc
  cai vao `/content/la_studio_subtitle_ocr_site`; worker va probe deu uu tien
  dung package directory nay. Verifier cam legacy `venv.EnvBuilder` va
  `virtualenv`.
- Verifier generated notebooks PASS 32/32; dedicated-package source contract PASS;
  QML parser PASS; diff check PASS. Build/CTest BLOCKED do Qt development kit
  khong con tren may. Khong mo GUI, browser, worker Colab song, va khong tao EXE.

### 2026-08-11 - Subtitle OCR Colab NCCL hotfix (unpackaged)

- Log Colab cho thay worker PP-OCRv5 chet truoc `/health`: sau khi Paddle nap
  NCCL, `import torch` cua `libtorch_cuda.so` doi symbol `ncclCommShrink` ma
  NCCL dang nap khong co. `Connection refused` chi la hau qua worker chua bind
  port, khong phai loi tunnel hay model OCR.
- Commit `adc7e04` bo PyTorch khoi exact OCR worker. Paddle tu dat `gpu:0` va
  lay ten GPU bang API Paddle; CPU fallback van bi cam. Verifier 32/32 va full
  CTest 39/39 PASS. Khong co live Colab/GPU test trong may local.
- Notebook moi co `WORKER_REVISION=subtitle-ocr-2026-08-11.2`. EXE 0.0.6.3
  khong bi ghi de; can candidate moi neu can dong goi hotfix vao app.

### 2026-08-11 - Subtitle OCR transitive Torch repair (unpackaged)

- Log sau `adc7e04` chung minh worker moi da khong import Torch truc tiep,
  nhung `PaddleOCR 3.1.1 -> PaddleX moi -> ModelScope -> Torch` van nap
  `libtorch_cuda.so` va vỡ NCCL. Day la nguyen nhan goc; `connection refused`
  la hau qua Uvicorn chua khoi dong.
- Commit `b26ba3a` ghim stack Colab vao Paddle GPU 3.1.0, PaddleOCR 3.1.1 va
  PaddleX 3.1.0 co extras OCR, force-reinstall khong cache, va probe import
  PaddleOCR/CUDA truoc worker. Audit wheel PaddleX 3.1.0 xac nhan model
  resolver khong import ModelScope.
- Verifier 32/32 PASS; CTest da in 39/39 passed nhung terminal wrapper het han
  sau khi in summary, nen khong co exit-code capture rieng. Khong co live
  Colab/GPU test va khong co package moi trong batch nay.

### 2026-08-11 - Subtitle OCR isolated Colab environment (unpackaged)

- Log tiep theo xac nhan pin/force-reinstall Pillow trong interpreter global
  cua Colab van khong du: `ImageText.py` van doc duoc nhung `_typing.py` active
  van cu. Khong duoc coi wheel audit tren may dev la bang chung cho global
  Colab site-packages.
- Commit `23e7d0d` tao lai virtual environment rieng
  `/content/la_studio_subtitle_ocr_venv` moi lan Run all. Pip, dependency
  probe va Uvicorn deu chay bang `.../bin/python`; `PYTHONPATH` va user-site
  bi loai bo. Probe assert `sys.prefix != sys.base_prefix`, import ca
  `ImageText`/`_Ink`, sau do moi import PaddleOCR/CUDA.
- Shared launcher duoc mo rong co worker Python explicit va isolation flag;
  tat ca notebook phu thuoc launcher duoc regenerate de verifier khong cho
  phep stale output. Verifier 32/32 PASS; generated probe syntax/environment
  PASS; clean local venv Pillow import PASS; full CTest da in 39/39 PASS trong
  batch. Van chua co live Colab GPU acceptance va khong co package moi.

### 2026-08-11 - Subtitle OCR Pillow package coherence repair (unpackaged)

- Log Colab sau pin Paddle stack cho thay mot loi doc lap: `PIL/ImageText.py`
  cua Pillow 12 import `_Ink`, nhung `PIL/_typing.py` tren runtime lai la file
  cu va khong co ky hieu nay. Day la Pillow bi ghi de mot phan, khong phai loi
  `ccache`, CUDA hay PaddleOCR model.
- Commit `c4689c1` force-reinstall `Pillow==12.0.0` sau PaddleOCR/PaddleX extras
  va truoc probe worker. Probe import ca `PIL.ImageText` va
  `PIL._typing._Ink` truoc `PaddleOCR`, nen runtime lai bi tron se fail som o
  dependency gate thay vi lam worker/Uvicorn bi chet.
- Generated notebooks 32/32 PASS; direct audit wheel Pillow 12 PASS;
  `graphify update .` completed; full CTest 39/39 PASS in 56.51s. Khong co
  live Colab/GPU test va khong co EXE moi, vi day chi la notebook hotfix.

### 2026-08-11 - 0.0.6.3 internal package

- Commit `6219edc` reworks the Dubbing workbench without removing LA Studio
  capabilities: task controls can live in the left shelf, the central preview
  is no longer permanently covered by node configuration, contextual detail
  stays on the right, and the Timeline is a dedicated full-width lower area.
  System Logs are Settings-only.
- QML lint and a fresh complete CTest run passed **39/39**. Package staging
  verified its own manifests; an independent audit confirmed the versioned
  application, Qt platform plugins, RuntimeHost, FFmpeg/FFprobe, yt-dlp,
  managed Douyin helper, OCR runtimes, and relevant Colab notebooks. A hidden
  offscreen launch remained alive for five seconds before deliberate shutdown.
- The package is internal-only because its eSpeak payload remains SHA-verified
  but unsigned. There are currently four candidate folders in `out`, so the
  requested maximum of three is not yet restored.

### 2026-08-10 - 0.0.6.1 internal package

- User da giai phong slot package; candidate `0.0.6.1` duoc tao moi, khong ghi
  de candidate cu. Hai folder con lai la `LA-Studio-0.0.6.0` va
  `LA-Studio-0.0.6.1`, nam duoi gioi han toi da ba phien ban.
- CMake default va regression version da dong bo o `0.0.6.1` trong commit
  `718f2e6`. EXE co dung FileVersion/ProductVersion va SHA o phan Hien trang.
- Package portable da xac minh 19 staging artifact va 19 license artifact;
  qwindows/qoffscreen, RuntimeHost, FFmpeg/FFprobe, yt-dlp, Subtitle OCR,
  PaddleOCR va Colab notebooks deu ton tai. eSpeak van la payload noi bo
  SHA-verified nhung unsigned.
- Khong mo GUI, browser, video hay remote worker; package audit khong thay the
  manual desktop/live service acceptance.

### 2026-08-10 - Dubbing preview workspace source batch (sau 0.0.6.0)

- OpenCut tham khao duoc giu ngoai source LA Studio tai
  `C:/Users/Nguyen Trong Khoi/Downloads/OpenCut-reference`, remote
  `OpenCut-app/OpenCut`, commit `4d8c49e`. Chi tham khao bo cuc Browser /
  Preview / Inspector va Timeline doc lap; khong copy code hay thay the tinh
  nang, workflow, route Colab/API cua LA Studio.
- DubbingSourceMediaPanel gioi han vung source/download/Chromium thanh
  ScrollView toi da 160 px sau khi co source. Video canvas giu minimum/
  preferred 16:9 thuc dung; source setup, media queue, cookie va Chromium van
  mo lai bang **Change / download source**.
- DubbingPage co Preview rong mac dinh 860 px, minimum 620 px, **Focus
  video** de an tam History/step/inspector (khong doi project), va handle doc
  lap cho Timeline 96--360 px. Commit source/test: `b86eb90` tren `main`.
- QML lint PASS; targeted media/remote/offscreen QML PASS 4/4; fresh full
  CTest PASS 39/39. Smoke ban dau bat loi `Button.toolTip` va da sua sang
  attached ToolTip truoc khi gate PASS. Khong mo GUI, browser, video hay
  worker song. Batch nay da duoc dong goi thanh `0.0.6.1` sau khi user giai
  phong mot slot.

### 2026-08-10 - 0.0.6.0 internal version carry

- Version noi bo dung bon truong mot chu so va carry tai 9: `0.0.0.9` thanh
  `0.0.1.0`. CMake, `build.ps1`, `package.ps1` va
  `verify_release_version.ps1` deu tu choi `0.0.2.40`; source default va
  package da dong bo o `0.0.6.0`.
- Regression them contract cho version policy vao `TestRemoteExecution` va
  giu cac regression Dubbing layout hien tai. QML lint PASS, targeted 4/4
  PASS, full CTest 39/39 PASS. Package portable verified FileVersion,
  ProductVersion, SHA, Qt plugins, RuntimeHost, FFmpeg, yt-dlp, Tesseract va
  PaddleOCR health manifest. Khong mo GUI hay goi service song.

### 2026-08-09 - 0.0.2.34 Dubbing workspace queue controls

- Phat hien sai vi tri UX cua `0.0.2.33`: cac lua chon batch nam o Download
  route rieng, trong khi user dang lam viec o Dubbing Import/Download. Dubbing
  source panel nay co nut **Queue & batch settings** ngay canh nut them link;
  them link se mo queue. Dialog hien item da tai, checkbox chon item, task
  Isolate/STT/Translate/Voice, hai thu tu chay va output/error that su.
- Dialog dung lai `DubbingController::startMediaQueue`, khong them mock hay
  fallback. `per-media` hoan thanh mot video truoc; `stage-by-stage` hoan
  thanh cung production stage tren moi video da chon truoc khi tien buoc. Route
  Direct Colab/API Gateway/Local cua tung stage duoc giu nguyen.
- DubbingPage la layout rieng, khong phai StudioShell. Hai handle keo gian dung
  duoc them giua History/Preview va Preview/step workspace; handle generic
  truoc do khong the hien o Dubbing. Regression, QML lint, full CTest 39/39,
  graph update va portable audit da PASS; GUI/live route chua duoc tuyen bo.

### 2026-08-09 - 0.0.2.33 batch order and responsive UI

- Batch Dubbing now has two durable scheduling choices: per-media end-to-end
  and stage-by-stage across selected media. The latter stores each item's
  project state between real runner stages; it does not simulate a queue or
  alter the selected Local/API Gateway/Direct Colab route.
- TTS and Voice Cloning include broader short/long Vietnamese and English
  example text, plus a bilingual TTS example. Generic voice-clone examples do
  not include reference audio and therefore cannot skip user permission or a
  real reference selection.
- Shared Theme/Main palette uses high-contrast text roles. StudioShell left and
  settings rails are drag-resizable (240--480 px), and Sidebar can expand to
  show labels. These interactions are source/QML-tested, not a claim of
  observed desktop GUI behaviour.
- Regression includes two production ingest stages completing across two items
  before an intentionally unavailable STT stage starts; item failures finish
  without a stuck queue. Targeted media test and QML lint pass; full CTest is
  39/39 in 57.06s. Package `0.0.2.33` audit verified source/PE version, SHA,
  19 artifacts, Qt platform plugins, FFmpeg/FFprobe and Tesseract 5.5.1.

### 2026-08-09 - 0.0.2.32 Dubbing media batch queue

- Download va Dubbing source panel nhan nhieu public link, moi link mot dong,
  va tai serial bang worker rieng. Input URL chi ton tai trong RAM trong luc
  tai; khong persist vao project, history, settings hay output manifest.
- Nguoi dung chon nhieu media da tai roi xep hang Dubbing: Isolator, STT,
  Translate va Voice/clone voice. Item chay bang DubbingJobRunner production
  theo thu tu, dung dung route/model da cau hinh (Local chi khi da chon ro,
  API Gateway va Direct Colab doc lap); khong co local fallback an.
- Moi item co output folder rieng `~/.lastudio/dubbing/batch-output/<id>`:
  `source.srt`, `translated.srt`, `voice.wav`, `vocals.wav`,
  `background.wav`, project copy. Voice WAV dung configured TTS/saved cloned
  voice; khong tao clone identity moi cho tung video vi viec do can consent va
  ten profile ro rang o Voice Cloning Studio.
- Regression integration tai 2 WAV qua loopback, xoa URL sau download, va cho
  2 worker STT loi thuc te tiep tuc queue khong treo. Full CTest 39/39 PASS;
  package audit version/hash/runtime PASS. Live remote worker va GUI khong
  duoc tuyen bo da test.

### 2026-08-09 - 0.0.2.31 OmniVoice clone reuse in TTS

- Trong Voice Clone, `Voice name for TTS reuse` nam ngay duoi Saved reference
  voices. Ten va reference audio duoc luu thanh reusable preset chi sau khi
  Direct Colab clone thanh cong; error/cancel khong tao preset ma.
- Khi exact OmniVoice clone worker da verified, TTS tu dong chon family
  `omnivoice` de bo empty setup state. Clone URL/token van thuoc route
  `voice-cloning`; khong bi copy sang generic `tts`, khong local-download va
  khong mo notebook thu hai.
- TTS Settings co section `Reuse cloned OmniVoice`: chon preset, check quyen
  su dung va nut `Use cloned OmniVoice in TTS`. Generate goi
  `ColabVoiceCloneController::cloneVoice` voi reference da luu, vi vay day la
  clone/profile request that su, khong phai normal TTS dat nhan clone.
- Targeted Qt regression + QML AOT compilation PASS; package portable stage
  audit 19 artifacts PASS. Khong claim GUI hien thi hay live Colab acceptance.

### 2026-08-06 - Shared Colab launcher safe port recovery (source only)

- Root cause of the OmniVoice error on port `3923`: the generic launcher
  treated every existing listener as a fatal collision, including the previous
  LA Studio worker left alive by a notebook rerun.
- Before starting a replacement worker, the launcher now uses `/proc` to match
  the exact generated worker module and `uvicorn`, then terminates only that
  old worker and its matching Cloudflare tunnel. A foreign listener is retained
  and reported with its PID; no unrelated process is killed.
- Generator verification is 32/32; focused remote/Voice Clone tests pass 2/2
  and the full headless CTest passes 39/39 in 78.24 seconds. No live Colab or
  visible GUI test, and no new package, is claimed.

### 2026-08-06 - 0.0.2.29 portable internal package

- Package moi cho Dubbing subtitle-order va Direct Colab 90% repair duoc tao
  tai `out/LA-Studio-0.0.2.29/LA-Studio-0.0.2.29.exe`; khong ghi de candidate
  cu. FileVersion/ProductVersion deu la `0.0.2.29`; SHA-256 la
  `3D37B2DE11575EE265C2FAAB43B20DE8185557FEB587FAC74654E66656EBC2D7`.
- Package script da stage/audit 19 runtime/license artifacts. Kiem tra doc lap
  xac nhan qwindows/qoffscreen, FFmpeg/FFprobe, RuntimeHost, Subtitle OCR,
  Spleeter notebook/worker va license deu co mat. eSpeak MSI SHA-verified
  nhung unsigned, vi vay chi la internal distribution.
- Full CTest 39/39 da PASS truoc package. Khong mo GUI hien thi va khong claim
  live Colab/manual desktop acceptance.

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
  build; focused suites pass 2/2 and full CTest passes 39/39 in 82.58s,
  including the offscreen QmlRouteSmoke deployment check.  This remains
  automated/offscreen evidence only; live Colab and desktop acceptance remain
  manual gates. No new package.

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

### 2026-08-09 - 0.0.2.35 shared-link Dubbing library

- A copied social share message is normalized at the controller boundary:
  `enqueueMediaLinks()` extracts only embedded HTTP(S) URLs before any remote
  validation, and removes each short-lived source URL after download. This is
  test-covered with share text around a loopback URL; it is not a live-Douyin
  claim.
- The Dubbing media queue is now a downloaded-media library. Download and
  later production actions are separate: a selected subset may run only
  Import/Normalize, Isolator, STT, Translate, TTS/Voice or Export/Output.
  Later actions restore the selected item's saved project and preserve other
  items' outputs. Full multi-stage batching remains an explicit advanced mode.
- The portable `yt-dlp.exe` resides beside `LA-Studio-*.exe`, unlike FFmpeg
  under `media-tools`. `MediaRuntimeLocator` now mirrors the staged layout;
  the root adapter is discovered instead of reporting a false not-ready state.
- Source/test commit `5e80743` is on `main`. Targeted media and Dubbing
  regressions, QML lint and full CTest 39/39 passed. Candidate `0.0.2.35`
  is internally packaged/audited; no GUI or live remote acceptance was run.

## Quy tac duy tri

- Doc `AI_AGENT_REQUEST.md` truoc task; sau task cap nhat response latest, summary cumulative va memory nay khi da co bang chung.
- Package mot candidate sau khi batch va full gate PASS; khong ghi de candidate cu.
- Bao cao ro automated/package PASS khac voi GUI/live service manual acceptance.
- Khi user dua evidence FAIL, dua dung loi do vao request va sua theo regression truoc package tiep theo.

### 2026-08-09 - 0.0.2.30 Voice Clone / Isolator completion

- Voice Clone reference cleanup has an independent Direct Colab Spleeter
  session and controller. The inline card no longer navigates to standalone
  Isolator; verified vocals are cached and passed directly into clone without a
  re-upload. Session isolation is covered by Remote Execution regression.
- Export WAV uses `selectedFile`, extension normalization, controller-result
  checking and an exact save/failure message. Direct Colab reference transcript
  is optional for models that support audio-only profile creation; Local Qwen3
  correctly remains transcript-required.
- CMake skips only the shared-Qt static-plugin import scanner, which had been
  stalling Windows package configuration. Dynamic QML deployment remains via
  `windeployqt --qmldir qml` and was included in package verification.
- Validation: full CTest **39/39 PASS**, notebooks **32/32 verified**, staged
  package smoke PASS. Portable internal candidate `0.0.2.30` is at
  `out/LA-Studio-0.0.2.30/LA-Studio-0.0.2.30.exe`; eSpeak remains the
  hash-verified unsigned internal-only component. No visible GUI or live
  Colab run was claimed.

### 2026-08-09 - 0.0.2.36 explicit Douyin cookie retry

- yt-dlp `2026.07.04` still reports `Fresh cookies (not necessarily logged in)
  are needed` for the tested Douyin short link; no current upstream extractor
  fix was found. The app therefore keeps the no-cookie default and exposes an
  explicit user-selected Netscape cookie file instead of reading Chrome.
- `RemoteMediaImportService` validates a readable tab-separated file, copies
  it to a private 16 MiB-bounded `QTemporaryFile` only for the page resolver,
  passes `--cookies` only in that run, then removes the temp copy on every
  terminal path. The source path is not persisted or logged.
- Dubbing queue items with this diagnostic use `downloadState=needs-auth` and
  retain their source URL only in memory. The UI offers **Retry with cookies**;
  successful retry clears the URL and cookie configuration. Generic failures
  still discard their source URL and remain non-selectable.
- Regression coverage includes resolver arguments, temp-file cleanup, the
  actionable error, and controller retry. QML lint and full CTest 39/39 pass.
  Internal portable candidate `0.0.2.36` is hash-audited; live Douyin access
  remains an explicit manual gate.

### 2026-08-10 - Douyin browser-session route

- Added `DouyinBrowserSessionService` and
  `scripts/douyin_browser_session.py`. The helper owns a persistent Chromium
  profile under LA Studio data, uses Playwright page JavaScript to discover a
  video resource, and streams the response into app staging.
- Browser setup/check/disable is exposed in both Download and Dubbing source
  UI. The C++ resolver enables the browser path only after a successful
  authenticated-session check; otherwise the normal yt-dlp/no-cookie or
  explicitly selected Netscape-cookie paths remain unchanged.
- No Chrome/Edge/Firefox profile import, `--cookies-from-browser`, cookie
  logging, or signed-URL persistence is permitted. Packaging stages the helper
  but intentionally does not install Python/Playwright/Chromium for the user.
- Tests after final source changes: QML lint PASS, targeted media/QML 3/3
  PASS, full CTest 39/39 PASS, and graphify update completed. No new EXE was
  packaged; live authenticated Douyin acceptance remains manual.

### 2026-08-10 - Download page is download-only

- `qml/pages/MediaDownloadPage.qml` no longer owns Dubbing action selection.
  It only downloads public links, manages the app-owned Douyin Chromium
  session/cookies, lists downloaded files, and hands off to Dubbing.
- The visible primary action is `Download`; processing controls remain in
  `qml/components/dubbing/DubbingMediaQueueDialog.qml`.
- After updating the affected regression expectations: QML lint PASS, targeted
  5/5 PASS, full CTest 39/39 PASS.
- Internal portable `0.0.2.37` was staged with matching file/product version
  metadata and SHA-256 audit; it remains internal-only because eSpeak is
  unsigned.

### 2026-08-10 - Playwright interpreter selection

- `DouyinBrowserSessionService` no longer trusts only the first `python` on
  PATH. It probes candidates for `import playwright`, preserves explicit
  `LASTUDIO_DOUYIN_PYTHON`, and reports the selected interpreter when the
  dependency is missing.
- Portable internal `0.0.2.38` was staged; QML lint and full CTest 39/39 pass.

### 2026-08-10 - 0.0.6.2 preview-first Dubbing workspace

- Dubbing's source drawer now collapses immediately after a source is accepted
  through the production picker. The preview header exposes Open/Replace video
  and explicit ratio choices (Fit source, 16:9, 9:16, 1:1).
- The display frame uses aspect-fit only; OCR ROI, subtitle overlay and media
  controls are frame-relative. Focus video hides the timeline and lower
  project controls, and a header sliders toggle restores/hides those controls
  without changing any project settings.
- Regressions include the live QML source-selection boundary plus frame-ratio
  and lower-panel-toggle contracts. QML lint, targeted 4/4 and full CTest 39/39
  passed. Graphify was updated. Internal portable package 0.0.6.2 is staged
  under `out/LA-Studio-0.0.6.2`; it was not launched visibly and no live remote
  service was claimed.

### 2026-08-11 - 0.0.6.4 Dubbing header and Subtitle OCR bootstrap

- The Dubbing UI contract is fixed panes, not overlaying drawers: a selected
  task consumes left/right layout width around the central preview, while the
  timeline spans the lower workspace. Language pair and execution quality are
  delayed to the modal setup flow selected after Automatic or step-by-step.
- `DubbingWorkflowHeader.qml` owns a runtime smoke layout check for the
  non-scrolling action cluster and workflow rail. Compact controls are
  intentional icon-only controls with tooltips; normal controls retain their
  full label, including `Workflow`.
- Subtitle OCR Colab was regenerated as `.9`; no `venv.EnvBuilder` or
  `ensurepip` remains. Bootstrap is isolated with pip `--target` and an
  explicit import/readiness probe. Stale Colab copies with `VENV_DIR` must be
  discarded and reopened in a fresh runtime.
- Validation: QML lint PASS, generated notebooks 32/32 PASS, full CTest 39/39
  PASS in 59.24 s, and graphify update completed. Portable internal package
  0.0.6.4 is staged at `out/LA-Studio-0.0.6.4/LA-Studio-0.0.6.4.exe` with 19
  required runtime artifacts; SHA-256
  `2367C0A735D20F6692C2EF6BCCF3EF22F097188C7C3C92175C6A728F3F0EC5EF`.
- No interactive desktop or live Colab session was opened. Package stays
  internal-only pending signed eSpeak remediation.
