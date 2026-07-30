# LA Studio — hồ sơ bàn giao cho AI agent

**Cập nhật:** 2026-07-30  
**Workspace:** C:\Users\Nguyen Trong Khoi\Downloads\LA-STUDIO  
**Nhánh bắt buộc:** main  
**Baseline trước tài liệu này:** 8a983ed — fix: make remote workflow progress truthful  
**Remote:** origin → https://github.com/khoinguyen59/kova-video-studio.git  
**Version CMake hiện tại:** 0.0.2.0

Tài liệu này là điểm bắt đầu cho AI agent kế tiếp. Đọc cùng các tài liệu tham
chiếu trước khi sửa hoặc đóng gói. Không dùng số test pass để tuyên bố feature
đã chạy thật với người dùng.

## 1. Yêu cầu sản phẩm không được phá vỡ

LA Studio là app C++17/Qt 6/QML cho STT, TTS, voice cloning, voice design,
voice isolation, forced alignment, translation, LLM chat và video dubbing.

Người dùng muốn giữ đầy đủ tính năng nhưng giảm tải/cost local bằng hai đường
chạy **độc lập**:

| Route | Mục đích | Cấu hình | Điều cấm |
| --- | --- | --- | --- |
| API Gateway / 9Router | STT, TTS, Translation, LLM khi feature/model hỗ trợ | Gateway URL + API key ở Settings hoặc form feature nếu Settings chưa có | Không biến Gateway thành proxy/control plane của Colab |
| Direct Colab GPU | Heavy GPU inference theo feature và exact model | Worker URL + bearer token tạm thời, chỉ trong memory session | Không gửi job/token Colab qua Gateway; không fallback sang Gateway/local nếu Colab lỗi |
| Local Dev | CPU local khi user chủ động chọn | Model/runtime local đã cài | Không auto-download/chạy local GPU hoặc local model làm fallback trong Remote-first |

Gateway có thể dùng khi Colab chưa pair, và ngược lại. Không gộp route,
catalog, credentials, tokens, model state, request hoặc error.

Yêu cầu UI:

- Mọi nơi cần GPU phải có setup Colab ngay trong feature: model đã chọn, nút
  mở **đúng notebook của model**, URL/token, check connection và feedback.
- Feature hỗ trợ Gateway phải có lối nhập/cấu hình Gateway nếu user chưa điền
  Settings; không bắt user có key chỉ để mở UI.
- Backend có route/model/configuration nào thì UI phải hiển thị tương ứng.
- Không được tạo % bằng timer, graph weight, số stage hoặc mốc cố định. Không
  đo được thì ghi Working/Đang xử lý, không ghi 5%, 8%, 94% giả.
- Không package mới chỉ vì mock/unit pass; ưu tiên app không đơ/crash.

## 2. Quy tắc repo và version

- Commit **trực tiếp main** rồi push origin main. Không tạo branch/PR nếu user
  không yêu cầu thay đổi quy tắc này.
- Backup version-1 là bất biến: không sửa, build vào, xóa, format hay commit.
- graphify/ đang untracked và ngoài phạm vi; không add/sửa/xóa/commit.
- Không quét toàn máy tìm source. Không ghi URL tunnel, bearer token hay API
  key vào source, notebook, JSON tracked, log hoặc report.
- Không reset hard/checkout ghi đè/xóa rộng trong worktree bẩn.
- Version có bốn trường MAJOR.MINOR.RELEASE.BUILD: 0.0.0.1 → 0.0.0.9 →
  0.0.1.0. EXE phải là LA-Studio-<version>.exe; số này phải khớp app metadata,
  installer, staging manifest và tag, không phải tên folder.

## 3. Cấu trúc thư mục hiện tại

~~~text
LA-STUDIO/
├── .deps/                    dependency cache/provisioned dependencies
├── .github/                  CI workflows/GitHub metadata
├── .tools/                   Qt/tool runtime local
├── catalog-src/              catalog nguồn model/runtime
├── cmake/                    CMake modules và AppVersion template
├── data/                     generated catalog/schema/runtime data
├── docs/                     kế hoạch, audit, release gate, handoff
├── examples/                 example settings/input
├── graphify/                 UNTRACKED — ngoài phạm vi
├── i18n/                     localization assets
├── icons/                    app icons
├── include/                  public/runtime interface headers
├── Khoi/                     thư mục đã có trong source; không đụng nếu chưa rõ owner
├── notebooks/                38 ipynb; 31 exact-model workers được verify
├── out/                      build, test, stage, generated reports
├── qml/
│   ├── components/           reusable UI: Dubbing, STT, TTS, shared, popup
│   └── pages/                page/studio-level QML
├── resources/                Qt resources và Windows version metadata
├── scripts/                  build/package/test, notebook generator, verifier
├── src/
│   ├── alignment/            forced alignment
│   ├── api/                  built-in API service
│   ├── audio/                decode/playback/audio utilities
│   ├── controllers/          QML-facing/application controllers
│   ├── core/                 catalog/model/runtime/shared infrastructure
│   ├── dubbing/              project, media, workflow adapter/nodes
│   ├── inference/            inference abstractions
│   ├── llm/                  LLM feature
│   ├── remote/               Colab sessions and contract validation
│   ├── runtimehost/          runtime host protocol/process
│   ├── separation/           voice isolation
│   ├── stt/                  STT runners/workers
│   ├── subtitles/            subtitle utilities
│   ├── textnorm/             VietNorm and separate test target
│   ├── translation/          translation engine/runners
│   ├── tts/                  TTS engine/runners
│   └── workflows/            generic graph, executor, journal
├── tests/                    QtTest/CTest, loopback, QML/remote contracts
├── CMakeLists.txt            top-level version and targets
├── CMakePresets.json         CMake presets
├── README.md                 overview and feature matrix
└── vcpkg.json                C++ dependency manifest
~~~

Các file tác động trực tiếp đến lỗi mới nhất:

| File | Vai trò |
| --- | --- |
| src/workflows/WorkflowGraphRunner.* | Cấm graph weight thành % của active node |
| src/workflows/WorkflowExecutionAdapter.h | Boundary signal progress capability |
| src/dubbing/workflow/DubbingWorkflowAdapter.* | Forward DubbingJobRunner state/progress và map stage |
| src/dubbing/workflow/DubbingWorkflowNodes.cpp | Forward adapter progress sang executor |
| src/controllers/dubbing/DubbingController.* | Automatic setup/Dubbing state/progress |
| src/controllers/dubbing/DubbingSynthesisJob.* | Voice progress theo clip hoàn thành/worker report |
| qml/components/WorkflowPopup.qml | Activity UI và progressAvailable |
| qml/pages/DubbingPage.qml | Header/timeline Dubbing và Working state |
| qml/components/shared/WorkflowPipelineDialog.qml | Hide numeric bar khi unavailable |
| src/remote/ColabSession.cpp | Exact model/contract/revision and stale-worker rejection |
| scripts/generate_*_colab_notebooks.py | Generator; không sửa ipynb thủ công nếu có generator |

## 4. Feature và notebook status

31 exact-model notebook bindings đã được verifier xác nhận. notebooks/ có 38
ipynb vì còn notebook generic/legacy.

| Feature | Remote route | Colab exact-model |
| --- | --- | --- |
| STT | Gateway STT hoặc Direct Colab | 4: Whisper.cpp, Nemotron, Qwen3-ASR 0.6B, Qwen3-ASR 1.7B |
| TTS | Gateway TTS hoặc Direct Colab | 8, gồm Kokoro, VibeVoice, VieNeu v2/v3, Qwen3-TTS |
| Voice cloning | Direct Colab | 6; profile/generation phải có consent |
| Voice design | Direct Colab | 3 |
| Voice isolation | Direct Colab | Spleeter 2-stem, UVR Vocals |
| Forced alignment | Direct Colab | 4 exact aligner |
| Translation | Gateway hoặc Direct Colab | M2M-100 418M, MADLAD-400 3B, Hy-MT2 1.8B |
| LLM chat | Gateway hoặc Direct Colab | Qwen3.5 2B |
| Video dubbing | Compose selected route by stage | Import → normalize → separation → STT → translate → voice → timing/mix → export |

Desktop phải check /health và /v1/capabilities bằng token trước active. Worker
cần ready=true, CUDA/no CPU fallback, capability đúng, exact model đúng và
contract đúng. Dán URL/token không có nghĩa worker đã connected.

## 5. Lỗi đã điều tra và fix

### 5.1 Freeze khi chọn model STT

**Triệu chứng:** click model card khác trong Load Model, chưa confirm, UI CPU
cao rồi trắng/Not responding.

**Root cause:** QML binding visible: detailPanel.f ép QVariantMap metadata lớn
thành bool. Qt/QV4 chuyển map lồng nhau sang JavaScript, allocate/GC tight loop
trên GUI thread. Không phải CUDA, inference, download hoặc GPU local.

**Fix:** dùng scalar bool detailPanel.hasFamily thay mọi bool coercion của
detailPanel.f. Giữ pending selection: click card không commit model/runtime
trước confirm. QmlRouteSmoke để dialog mở rồi chọn tuần tự model qua event loop.
Xem docs/MODEL_SELECTION_FREEZE_ROOT_CAUSE_0.0.0.8.md.

### 5.2 Runtime package và normalize

| Triệu chứng | Phát hiện/fix |
| --- | --- |
| no Qt platform plugin could be initialized | Portable layout phải có Qt DLL + platforms/windows plugin đúng vị trí; không ép offscreen plugin để test portable |
| Qt6Multimedia.dll was not found | Deploy Qt Multimedia đầy đủ, không chỉ EXE |
| QML smoke loader 0xc0000135 | Stage libcurl.dll và zlib1.dll cạnh EXE |
| FFmpeg and FFprobe are required for media import | Normalize tạo working audio trước separation/STT; FFmpeg/FFprobe runtime chưa stage, không phải mock feature |

Commit 2971738 sửa staging FFmpeg không redundant extraction. Không thay
normalizer bằng fallback silent.

Save trong Dubbing chỉ lưu project JSON. Xuất video: Export góc trên phải →
Dubbed video → Export video → chọn path. Default cạnh project theo dạng
project-dubbed.mp4 (hoặc wav theo input). Generate Final Dubbing không tự là
thao tác chọn output path.

### 5.3 Colab worker, retry và tunnel

| Triệu chứng | Điều cần giữ |
| --- | --- |
| Port 8000 address already in use khi Run all lại | Launcher STT rerun-safe, dừng/reuse worker cũ; commits e738150 và 8aa58c8 |
| wget cloudflared lỗi/tunnel timeout | Chỉ publish URL sau health qua public tunnel verified; có diagnostic/timeout; commits de6027d và 93d39b0 |
| VieNeu v2/v3 CUDA worker không ready | Strict CUDA/exact-model check, dependency/model diagnostic; không fallback CPU/giả ready; vẫn cần live GPU evidence |
| STT worker cũ/sai model được nhận | ColabSession reject stale contract/revision và wrong exact model; commit 1beae89 |
| M2M empty text/payload, HTTP 503 và dừng flow | Translation v3 retry element trống một lần; còn trống thì source-preserving needs-review patch + diagnostic, tiếp tục segment. Desktop reject notebook v2 cũ. Commits c06a71e và bef254c |

### 5.4 Fake progress Dubbing/Activity

**Triệu chứng:** model-setup đứng 5%, header Import · 8%, voice/TTS giữ 5% sau
clip trước, 94%/95% suy diễn từ stage/clip.

**Nguyên nhân:**

1. DubbingController hard-code 5/8 và map download vào khoảng 5–20.
2. WorkflowGraphRunner dùng orderIndex/order.size làm % active node.
3. DubbingWorkflowAdapter chưa forward runner progress sang graph.
4. DubbingSynthesisJob map per-clip/index thành % tổng giả.
5. Service phát 5/35/65/90 phase marker dù không có measurement.

**Fix 8a983ed:**

- Thêm progressAvailable cho WorkflowGraphRunner/DubbingController.
- Chỉ show numeric khi active executor có intermediate measurement 1–99. Giá trị
  0 là new request lifecycle và 100-only là completion; cả hai hiện Working.
- Forward progress qua WorkflowExecutionAdapter → DubbingWorkflowAdapter →
  CapabilityNodeExecutor → graph, map import/source-separation/transcription/
  translation/tts/fit-timing/mix/export.
- Automatic setup chỉ show byte received/total thật; thiếu total là unavailable.
- Voice remote tính theo clip đã hoàn thành + worker intermediate report; request
  clip mới reset availability, không giữ % clip trước; không claim 100 trước
  timing/finalization.
- Activity popup, Dubbing header/timeline/pipeline hide bar khi unavailable.
- Xóa marker giả ở media ingest/tool, Gateway STT/Translation, Colab
  alignment/separation và remote TTS UI liên quan.
- Regression mới: TestWorkflowGraph::exposesOnlyActiveNodeMeasuredProgress.

Nếu % giả quay lại, trace chuỗi này; không thêm timer:
DubbingJobRunner::stateChanged → DubbingWorkflowAdapter::progress →
CapabilityNodeExecutor::progress → WorkflowGraphRunner → DubbingController → QML.

## 6. Test evidence và giới hạn

Đã chạy sau code progress mới:

~~~powershell
ctest --test-dir out\build\windows-msvc-tests --output-on-failure
# 35/35 passed, 0 failed

python scripts\verify_generated_colab_notebooks.py
# 31/31 exact-model notebook contract verified

python scripts\verify_colab_model_bindings.py
# 31/31 controller/UI/notebook mapping verified
~~~

Lần full CTest đầu thiếu VietNormUnitTests vì chỉ build LAStudioUnitTests. Sau
khi build explicit target VietNormUnitTests, full CTest chạy lại mới đạt 35/35.
Không được báo full green nếu executable test này chưa tồn tại.

35/35 là regression source/mock/loopback/QML-offscreen. Nó **không** chứng minh
packaged desktop UI flow hay live GPU inference cho mọi model. Trước đây báo cáo
33/33 passed đã không phản ánh lỗi user tìm thấy; không lặp lại.

Chỉ dùng các nhãn:

- **Regression passed:** unit/source/mock/loopback pass.
- **Packaged desktop verified:** portable/staged clean layout và UI thật pass.
- **Live Colab verified:** notebook GPU thật, exact model, output thật, report lưu.
- **Feature accepted:** desktop + live model gate đủ cho feature.

## 7. Checklist khi sửa từng feature

1. Liệt kê model, backend route và capability.
2. Kiểm UI thật: model selector, notebook đúng model, URL/token, Gateway field,
   Check Colab, error/cancel/progress; xác nhận không freeze.
3. Chạy notebook exact model trên Colab GPU, Run all rồi pair URL/token tạm thời.
4. Verify health, capabilities, CUDA, exact model và wrong-model reject.
5. Chạy input thật nhỏ; kiểm text/patch/timestamp/WAV/video output.
6. Test token sai, tunnel hết hạn, worker/model sai, HTTP 500/503, empty output,
   cancel. Không fallback route/crash/fake progress.
7. Dubbing phải qua import → normalize → separation → STT → translate → voice →
   timing/mix → export; mỗi stage có UI/config/output rõ.
8. Viết regression, compile và full CTest trước package.

## 8. Build, package và live gate còn lại

- Đọc docs/BUILD.md, docs/RELEASE.md và docs/TEST_EVIDENCE_AND_RELEASE_GATE.md.
- scripts\bootstrap.bat -SkipDeploy là dev build, không đủ package validation.
- AllowUnsignedEspeakForInternalBuild chỉ dành internal test, không phải release.
- Portable layout cần Qt plugin/DLL, Qt Multimedia, libcurl.dll, zlib1.dll,
  FFmpeg/FFprobe và eSpeak DLL/data. EXE đơn lẻ không đủ.
- Không package chỉ để thử. Khi đủ gate, đặt LA-Studio-<version>.exe ở nơi user
  dễ tìm, giữ kèm runtime folder đúng scope.

Live Colab dùng environment variables, không commit secret:

~~~powershell
python scripts\generate_live_colab_acceptance_template.py --output C:\Temp\la-studio-live-colab.json
$env:LASTUDIO_LIVE_TTS_KOKORO_URL = 'https://...trycloudflare.com'
$env:LASTUDIO_LIVE_TTS_KOKORO_TOKEN = 'temporary-token'
python scripts\run_live_colab_acceptance.py --config C:\Temp\la-studio-live-colab.json --only tts:kokoro --report out\live-colab-kokoro.md
~~~

Live-pass yêu cầu health ready/CUDA, capability/model đúng, wrong-model HTTP 409
và inference thật trả output hợp lệ.

## 9. Tài liệu cần đọc tiếp

- README.md — overview, feature matrix, remote-first/version policy.
- docs/COLAB_GPU_GATEWAY_MIGRATION_PLAN.md — Gateway và Colab độc lập.
- docs/REMOTE_WORKERS.md — remote session/exact-worker contract/security.
- docs/DUBBING_REMOTE_EXECUTION_AUDIT.md — Dubbing routes/model setup.
- docs/MODEL_SELECTION_FREEZE_ROOT_CAUSE_0.0.0.8.md — UI freeze root cause.
- docs/STT_COLAB_UPLOAD_RELIABILITY_0.0.1.3.md — STT worker lifecycle.
- docs/TRANSLATION_LLM_COLAB_EXACT_MODEL_AUDIT.md — Translation.
- docs/TTS_COLAB_EXACT_MODEL_AUDIT.md và docs/VOICE_COLAB_EXACT_MODEL_AUDIT.md.
- docs/ALIGNMENT_VOICE_ISOLATION_COLAB_EXACT_MODEL_AUDIT.md.
- docs/LIVE_COLAB_ACCEPTANCE.md và docs/TEST_EVIDENCE_AND_RELEASE_GATE.md.

## 10. Handoff conclusion

Source hiện đã có fix model-selection freeze, strict exact-model Colab guard,
notebook retry/tunnel hardening, runtime packaging dependency fixes và Dubbing
progress trung thực thay vì 5%/8% giả. Việc quan trọng tiếp theo là live Colab
và clean packaged UI acceptance theo từng feature/model, không phải build thêm
EXE hoặc đổi % cho đẹp. Luôn báo rõ evidence nào đã chạy và evidence nào chưa.

