# Điều phối báo cáo hiện hành — bắt buộc đọc trước khi làm tiếp

## Cập nhật mới nhất — OCR runtime bundle và độ tin cậy QML smoke (2026-08-01)

### Commit đã push trực tiếp `main`

- `bcc7cfc` — `fix(ocr): bundle verified Tesseract runtime`
- Remote đã xác minh: `origin/main` chứa commit này. Không có branch/PR trung gian.
- Chưa tạo package `0.0.2.10` trong cập nhật này. Vì vậy chưa có đường dẫn EXE,
  File/Product version hoặc SHA-256 mới để báo cáo; không được gọi OCR runtime
  manual gate là PASS trước khi package đó được tạo và người dùng nghiệm thu.

### Điều tra lỗi thật trên package 0.0.2.9

Ảnh người dùng xác nhận đường cũ đã tải/verify tới bước launch, sau đó lỗi
`Verified Tesseract installer could not be started`. Log của 0.0.2.9 chưa ghi
đủ dữ liệu create-process để quy hồi tố một nguyên nhân Windows duy nhất. Kiểm
tra cache cho thấy installer còn có hash khớp bản pin; không có
`Zone.Identifier`, nhưng chữ ký upstream University Mannheim không còn được
xác thực tại thời điểm kiểm tra. Vì thiếu diagnostic launch của lượt lỗi cũ,
không được gọi đó là bằng chứng chắc chắn của SmartScreen, quyền hay mạng.

Thay vì tiếp tục phụ thuộc một installer ngoài, sửa mới loại bỏ đường launch đó:

- `vcpkg.json`, `scripts/package.ps1`, `scripts/runtime_helpers.ps1` provision
  Tesseract `5.5.1` từ `vcpkg:tesseract` trong portable package, bao gồm binary,
  DLL phụ thuộc và giấy phép.
- `resources/subtitle-ocr-runtime-manifest.json` chuyển sang schema 2, delivery
  `bundled-vcpkg`; package tạo SHA-256 thực của binary và health-check
  `tesseract --version` trước khi publish manifest.
- `SubtitleOcrRuntimeService` không còn khởi chạy installer trong flow public.
  Refresh/health check xác minh manifest, hash và executable bundled; yêu cầu
  install/repair khi package hỏng báo lỗi repair rõ ràng, không yêu cầu quyền
  admin hay tắt bảo mật.
- `SubtitleOcrRuntimeLocator` ưu tiên runtime bundled đã verify hơn executable
  managed cũ; biến môi trường test/explicit vẫn là ngoại lệ có chủ ý. Regression
  xác nhận bản bundled thắng legacy executable, tránh chọn nhầm state cũ.
- Tessdata/language pack vẫn app-managed; `TESSDATA_PREFIX` chỉ được truyền vào
  OCR worker process, không ghi biến môi trường hệ thống.
- Card `SubtitleOcrPage.qml` mô tả rõ engine, route **Local CPU**, không cần GPU
  hoặc Colab, bundle status và repair runtime; language action chỉ mở khi runtime
  bundled sẵn sàng.

### Sửa kèm tính toàn vẹn regression UI

Trong khi chạy smoke thật, hai lỗi vốn bị che đã được phát hiện và sửa:

1. `DubbingSubtitleEditor.qml` dùng `parent.label` sau khi item bị reparent trong
   `data`, gây `TypeError: Cannot read property 'label' of null`; nay dùng owner
   `fieldRoot` ổn định.
2. `QmlRouteSmoke` cũ kiểm hai pane ẩn của `StackLayout` cùng lúc nên tạo kết
   quả mơ hồ; test nay kiểm từng tab sau event-loop turn. `Logger::init()` từng
   ghi đè message handler của smoke, khiến warning QML không làm test fail; nay
   logger có observer và warning/binding error route thật làm smoke đỏ.

### Kiểm chứng đã chạy

| Hạng mục | Bằng chứng | Trạng thái |
| --- | --- | --- |
| Tesseract package source | Build vcpkg cô lập `tesseract 5.5.1`; `tesseract --version` thực thi thành công | PASS CLI, không mở desktop GUI |
| OCR locator/service/controller | `TestSubtitleOcrRuntimeService`, `TestSubtitleOcrController` | PASS |
| QML route | `PrepareQmlRouteSmokeRuntime`, `QmlRouteSmoke`, gồm regression warning và Dubbing tab | PASS offscreen, không thay cho visual/manual |
| Full suite | `ctest --test-dir out\\build\\windows-msvc-tests --output-on-failure -j 1` | **38/38 PASS, 0 FAIL** (47.57 s) |
| Graph | `graphify update .` sau source change | Đã chạy; output generated vẫn untracked/không commit |

Các kiểm thử trên là source/CLI/offscreen. Chúng **không** chứng minh package
desktop mới, OCR video thật, video picker/drag-drop thật hay chất lượng visual.

### Gate tiếp theo bắt buộc cho package 0.0.2.10

1. Đọc lại `docs/AI_AGENT_REQUEST.md` ngay trước packaging để bảo đảm không có
   lệnh mới; build portable với `-Version 0.0.2.10`, không đụng package 0.0.2.9.
2. Xác minh manifest bundle trong stage: `tesseract.exe`, SHA khớp manifest,
   health-check, DLL phụ thuộc và `licenses/tesseract/LICENSE`; kiểm version và
   SHA-256 EXE rồi ghi chính xác vào report này.
3. Người dùng tự mở đúng EXE mới (agent không điều khiển GUI), vào Subtitle OCR
   và xác nhận card ghi Local CPU/No GPU or Colab required, runtime **Ready**,
   language English/Vietnamese/Chinese cài/refresh qua app-data và OCR một video
   thật. Nếu package/binary thiếu, ghi FAIL cụ thể; không fallback installer cũ.

Các file request/handoff, `VoiceLibraryDialog.qml`, `.agents/`, Graphify,
`out/` và temporary logs không thuộc commit `bcc7cfc`.

Tài liệu này là **điểm vào duy nhất** cho AI agent tiếp theo. Không được coi phần báo cáo 0.0.2.7 bên dưới là trạng thái mới nhất.

## Thứ tự đọc bắt buộc cho AI tiếp theo

Trước khi audit, sửa, build, package hoặc kết luận bất kỳ hạng mục nào, phải đọc đầy đủ theo thứ tự:

1. `C:/Users/Nguyen Trong Khoi/Downloads/LA-STUDIO/docs/AI_AGENT_RESPONSE_REPORT_0.0.2.8.md`
2. `C:/Users/Nguyen Trong Khoi/Downloads/LA-STUDIO/docs/AI_AGENT_RESPONSE_REPORT_0.0.2.9.md`
3. `C:/Users/Nguyen Trong Khoi/Downloads/LA-STUDIO/docs/AI_AGENT_REQUEST.md`
4. `C:/Users/Nguyen Trong Khoi/Downloads/LA-STUDIO/docs/AI_AGENT_HANDOFF_2026-07-30.md`
5. File này và `git status --short`.

Hai report version là báo cáo bằng chứng chi tiết, không được bỏ qua hoặc thay thế bằng tổng kê CTest:

- `AI_AGENT_RESPONSE_REPORT_0.0.2.8.md`: ma trận A–M, package 0.0.2.8 và ranh giới manual.
- `AI_AGENT_RESPONSE_REPORT_0.0.2.9.md`: audit lại A–G, regression transcript/media controls, package 0.0.2.9 và checksum.

## Trạng thái sau report 0.0.2.9 (2026-08-01)

Các commit sau đã được push thẳng lên `origin/main`; chưa tạo package mới vì full audit/chạy full regression chưa kết thúc:

- `04bc435` — import Subtitle OCR không có `Content-Length` có BusyIndicator indeterminate; smoke Home chạy 1024×720, 1280×800, 1600×900 và kiểm tra card 09/10 + scroll.
- `0641650` — CapCut draft chỉ gán stem “source vocals” sau khi source separation tạo cả vocals và background; không gán nhãn sai analysis mono.
- `da67f9a` — ASS burn-in giữ custom normalized x/y, chuyển thư mục font tuỳ chỉnh cho FFmpeg `fontsdir`, và fail sớm nếu font file đã cấu hình bị mất.
- `149a689` — OCR runtime preflight lại file/size/SHA trước launch, giữ cache installer đã verify cho Retry, ghi QProcess/working-directory/signature diagnostics, health-check `tesseract --version` trước `Ready`, và thêm UI Open diagnostics/Clean failed download.
- `67063f3` — launch installer/health-check qua Windows short path nếu NTFS cung cấp alias, vẫn giữ long Unicode canonical path để hash/cleanup/diagnostic; khắc phục path cache có khoảng trắng/Unicode mà không giả định 8.3 luôn bật.
- `7c1544d` — Rendered MP4 tôn trọng lựa chọn source reviewed (STT/OCR/import) hoặc translated target text, và `maxWidth` được map thành ASS horizontal margins. CapCut vẫn giữ cả original/dubbed sidecar và target→source fallback cho editable text.
- `865898f` — Rendered burn-in thực thi `lineSpacing`: text được Qt bọc theo `maxWidth`, mỗi visual line thành dialogue ASS có `pos`/bước dọc riêng; MP4 burn-in dùng encoder `mpeg4` có trong FFmpeg staged thay vì `libx264` không có trong runtime internal.
- `b6ed4ad` — Dubbing workspace không còn cố nhét History/source/review/inspector vào chiều rộng ngắn: viewport ngang có scrollbar khi cần, và smoke buộc chứng minh panel ngoài viewport còn reachable.
- `ad9decb` — CapCut editable draft copy custom subtitle font vào `assets/fonts/`, thay tham chiếu font cục bộ bằng asset trong draft và validator chặn media/font material trỏ ra ngoài draft. Đồng thời các thao tác bị từ chối khi Dubbing đang chạy dùng non-fatal busy diagnostic: không được hủy worker, reset stage hay làm mất tiến độ.

Regression đã chạy cho các thay đổi trên: `TestSubtitleOcrController`, `TestSubtitleOcrRuntimeService`, `TestDubbingProject`, `TestMediaToolService`, `PrepareQmlRouteSmokeRuntime`, `QmlRouteSmoke` đều PASS ở lượt chăm sóc tương ứng. Đây **không** thay cho full CTest/package, không được gọi là release hoàn tất, và không được gọi live GUI/Colab/CapCut PASS.

Sau `865898f`, full CTest với FFmpeg/FFprobe staged chạy **38/38 CTest target PASS, 0 CTest FAIL** (44.48 giây). Cần đọc đúng mức evidence: một subcase eSpeak trong `TestDubbingProject` tự `QSKIP` khi sandbox test không có eSpeak runtime; nó không liên quan line-spacing/burn-in và không được dùng để gọi runtime eSpeak hoặc toàn feature đã accepted. Chưa có package mới từ kết quả này.

Sau `b6ed4ad`, `PrepareQmlRouteSmokeRuntime` và `QmlRouteSmoke` PASS offscreen ở 1024×720, 1280×800, 1600×900. Đây chứng minh route load/resize/binding không warning và scrollbar reachability contract, nhưng visual/pointer acceptance vẫn manual.

Sau `ad9decb`, `TestDubbingProject` PASS riêng sau build Debug và full CTest chạy **38/38 CTest target PASS, 0 CTest FAIL** (44.39 giây). Regression mới tạo một draft có font tùy chỉnh, xác nhận font được copy vào `assets/fonts/subtitle-font.ttf`, cả `draft_content.json` lẫn manifest không còn giữ path font gốc; đồng thời mô phỏng worker đang ở `translation`, 42% để xác nhận Preview/Apply timing bị từ chối mà worker vẫn giữ stage/progress. CTest/offscreen không phải bằng chứng CapCut import, visual UI, hay Colab live PASS.

Audit source tiếp theo đã hoàn thành cho H/I, J, L và M ở mức code + regression hiện có:

- H/I: `DubbingPage.qml` lưu đúng ba mode `stt`, `ocr`, `stt+ocr`; `DubbingController` persist configuration/OCR parameter; `DubbingJobRunner` dùng shared `SubtitleOcrController`. Combined mode fail nếu một nguồn fail, không fallback im lặng. `DubbingTranscriptFusionService` giữ provenance, conflict/evidence và bắt reviewer chọn STT/OCR. Các ca `normalizesOcrOnly…`, `sttOnly…`, `combined…WithoutFallback`, `combined…FailureWithoutSttFallback`, `reviewerMustResolveFusionConflictExplicitly` là regression logic; không thay cho OCR/STT live.
- J: `MediaControlsAutoHide.qml` có delay 2000 ms, trạng thái playback/pointer/focus/menu/seek và được dùng bởi Dubbing source media + Subtitle OCR. QML route smoke xác nhận instantiate/binding; hover, keyboard và hide/reappear thật vẫn là manual acceptance.
- L: `DubbingTimingService` dùng duration đã đo, shift word timestamps khi ripple, giữ intentional overlap, controller invalidate mix/export preview cũ, persist và undo. `resolvesGlobalTimingConflictsWithRippleAndUndo` xác nhận các contract này; `ad9decb` bổ sung guard backend để UI lock không phải lớp bảo vệ duy nhất.
- M: UI tách `Rendered Video (MP4)` và `Editable CapCut Draft`. `CapCutDraftExporter` copy original media/audio, stems chỉ khi valid, generated clips, text track và SRT sidecars; nó validate JSON/material/track trước atomic publish. Trạng thái vẫn đúng là `structurally-validated-manual-import-pending`; chưa được nói là CapCut import đã PASS.

Riêng `149a689`/`67063f3` đã chạy lại `TestSubtitleOcrRuntimeService`, `TestSubtitleOcrController`, `TestMediaIngestService`, `PrepareQmlRouteSmokeRuntime` và `QmlRouteSmoke` PASS. `7c1544d` đã chạy `TestDubbingProject`, `TestMediaToolService`, `PrepareQmlRouteSmokeRuntime` và `QmlRouteSmoke` PASS. `865898f` chạy lại bốn target trên; `TestDubbingProject::preservesConfiguredLineSpacingInBurnInAss` xác nhận khoảng cách dòng tăng theo cấu hình, còn `TestMediaToolService::rendersLineSpacedAssWithStagedFfmpeg` tạo video/audio ngắn, render ASS hai dòng qua FFmpeg staged/libass và xác minh MP4 kết quả. Lỗi Windows gốc trước đó không có QProcess diagnostic trong log và đã xóa installer đã hash-verify, nên không thể kết luận hồi tố là lỗi mạng hay quyền. Cần user chạy bản package kế tiếp để xác nhận process-launch thật; không gọi gate đó PASS trước bằng chứng mới.

Audit K đã đóng khoảng trống kỹ thuật về line spacing trong `865898f`: không gán nhầm vào ASS `Spacing` (character spacing), mà mỗi line ASS được đặt theo bước dọc proportional sau khi app Qt bọc theo `maxWidth`; custom font được đăng ký để phép đo/wrap dùng đúng family khi app GUI hoạt động. Test headless chỉ dùng newline tác giả nhập để không truy cập font database khi không có `QGuiApplication`; kiểm thử FFmpeg staged xác minh libass render được file ASS phát sinh. Đây là **regression/CLI render PASS**, không phải visual acceptance: người dùng vẫn cần xác nhận trên package mới về typography, wrap HiDPI, màu/position và CapCut import thật.

Sau khi đọc, AI tiếp theo phải tiếp tục audit các requirement còn chưa có evidence phù hợp hoặc manual acceptance, không lặp lại code audit H/I/J/L/M đã ghi ở trên. Giữ N ở trạng thái manual pending cho đến khi có launch/install thật ở package mới; giữ K/M ở manual pending cho visual typography và CapCut import. Nếu phát hiện A–G có regression mới thì sửa kèm test; không được chỉ nói “baseline đã xong” để bỏ qua. Package baseline vẫn là `0.0.2.9`; không đóng gói chỉ vì CTest xanh.

---

# Báo cáo thực hiện — LA Studio 0.0.2.7

Ngày kiểm chứng: 2026-08-01
Phạm vi: hoàn tất các mục A–G của `docs/AI_AGENT_REQUEST.md`, dựa trên baseline 0.0.2.6.

## Commit và phạm vi

- Source, regression và version đã được commit/push trực tiếp lên `origin/main`: `7bb6b0b` — `fix: complete responsive subtitle OCR workflow`.
- Báo cáo này được commit/push riêng sau source commit.
- Không stage/commit: `docs/AI_AGENT_REQUEST.md`, `docs/AI_AGENT_HANDOFF_2026-07-30.md`, `qml/components/shared/VoiceLibraryDialog.qml`, `.agents/`, Graphify, `out/`, log test hoặc dữ liệu nhạy cảm.
- Source/package mới là `0.0.2.7`; không đè hoặc tái dùng `0.0.2.6`.

## Root cause và sửa layout Subtitle OCR

Nguyên nhân của giao diện chồng/cắt là trang cũ ghép `anchors.fill` với layout của cùng vùng, dùng `RowLayout` kéo chiều cao và card có sizing ngầm không phù hợp. Không có một viewport scroll có ownership rõ ràng, nên preview, runtime, language list và action có thể tranh cùng không gian.

`SubtitleOcrPage.qml` được tổ chức lại thành:

1. Source media: chọn local, kéo-thả, hoặc nhập URL.
2. Preview video và ROI.
3. Runtime OCR và language packs có vùng cuộn riêng.
4. Settings/action.
5. Reviewed transcript/export.

Trang dùng `ScrollView` + `ColumnLayout` + `GridLayout`, hai cột khi rộng và một cột khi hẹp; không trộn anchor với `Layout` trên cùng item. Card có `implicitHeight`/geometry rõ ràng, System Logs không phủ nội dung. ROI giữ mapping normalized theo vùng ảnh (không theo letterbox), tám handle, kéo di chuyển, preset lower region, reset và crop preview đều nằm trong preview.

QML route smoke kiểm tra geometry/no-overlap, content scroll, controls source vẫn usable khi runtime `Missing`, tám ROI handles và layout tại 1024×720, 1280×800, 1600×900. Smoke cũng kiểm tra Home có chính xác 10 card, card 09/10, route và vùng card không giao nhau.

## Source media và shared public-media backend

- **Choose video**, local drag/drop và **Import link** dùng controller/file access hiện có.
- URL dùng `DubbingController`/`RemoteMediaImportService` dùng chung; không tạo downloader thứ hai, không persist URL/query, không cookie/login/DRM.
- Direct media, YouTube, TikTok và Douyin đi qua adapter public-media hiện có. Progress là byte thực hoặc indeterminate; có cancel, error và retry.
- Kết quả staged được `SubtitleOcrController` nhận trực tiếp, không tải lần hai. Probe/import fail giữ source OCR cũ.
- Download tab có **Use in Subtitle OCR**, handoff staged file không redownload và chuyển route sang Subtitle OCR.
- Runtime `Missing` chỉ khóa run/language install phụ thuộc runtime; không khóa source picker, drag/drop, link input, preview, timeline hoặc ROI.

Regression mới bao phủ shared-link success, active-transfer cancel/retry, probe failure giữ source cũ và handoff không redownload. Fixture public-video vẫn là adapter/loopback contract; không kết luận live download từ các nền tảng công khai.

## Home

Home lấy card từ `StudioRouteRegistry.homeFeatureCards`, cùng nguồn với route/sidebar:

- **09 Download** → `media-download`
- **10 Subtitle OCR** → `subtitle-ocr`

Hai card dùng cùng component/style/repeater, số thứ tự, accent, category, title, description, Open và click toàn card như 01–08. Heading/count đã cập nhật 10 feature cards.

## Kiểm chứng tự động

Đã chạy QML lint, targeted OCR/media/QML smoke và full CTest. Media integration được chạy với FFmpeg/FFprobe staged thật tại `out/ffmpeg-runtime-validation/bin/media-tools`; ba ca trước đây SKIP đều PASS:

- direct-link probe/normalize chỉ commit sau thành công;
- standalone staged download handoff không redownload;
- probe failure giữ project/source cũ.

Kết quả full: **38/38 CTest PASS, 0 failed** (47.41 giây). Test runner Windows cũng được sửa để CTest hiển thị PASS/FAIL/SKIP chi tiết thay vì chỉ trả một kết quả xanh không có bằng chứng.

Lệnh chính:

```powershell
$runtime = (Resolve-Path out\ffmpeg-runtime-validation\bin\media-tools).Path
$env:LASTUDIO_FFMPEG = Join-Path $runtime 'ffmpeg.exe'
$env:LASTUDIO_FFPROBE = Join-Path $runtime 'ffprobe.exe'
ctest --test-dir out\build\windows-msvc-tests --output-on-failure
```

Graphify đã được cập nhật sau source edit; graph là generated local và không commit.

## Portable package nội bộ

Lệnh build/stage CLI, không mở EXE/browser/GUI:

```powershell
.\scripts\package.ps1 -Preset windows-msvc-release -QtRoot .tools\Qt\6.9.3 -VcpkgRoot .deps\vcpkg -LlamaCppSourceDir .deps\llama.cpp -Version 0.0.2.7 -SkipInstaller -PortableInternalLayout -AllowUnsignedEspeakForInternalBuild
```

| Hạng mục | Kết quả |
| --- | --- |
| Artifact | `out/LA-Studio-0.0.2.7/LA-Studio-0.0.2.7.exe` |
| File/Product/Original version | `0.0.2.7` |
| SHA-256 | `A400D25F68ECBD7FDCD84AF5DA7CF9A79369DCE94471A022528779773FEC6EA5` |
| Package manifest | script xác minh 19 artifact bắt buộc |
| License manifest | script xác minh 18 artifact bắt buộc |
| Media runtime | managed FFmpeg/FFprobe + yt-dlp đã stage |
| OCR manifest | `subtitle-ocr/runtime-manifest.json`, explicit user download, runtime/language hash pinned |

Đây là internal-only package: script cảnh báo eSpeak NG MSI hash hợp lệ nhưng unsigned; không coi là artifact public distribution.

## Checklist manual cần người dùng xác nhận

Không có GUI PASS trong báo cáo này vì agent không mở EXE hay điều khiển máy. Hãy mở đúng:

`out/LA-Studio-0.0.2.7/LA-Studio-0.0.2.7.exe`

1. Resize cửa sổ khoảng 1024×720, 1280×800, 1600×900; mở Subtitle OCR và xác nhận không chồng/cắt/đơ.
2. Khi runtime Missing, thử Choose video, kéo-thả file, nhập URL, xem preview và chỉnh ROI; chỉ Run/language install phụ thuộc runtime bị khóa.
3. Thử link hợp lệ, cancel khi đang tải, Retry, và link/media lỗi; source cũ phải còn nguyên.
4. Ở Download, sau khi staged xong bấm **Use in Subtitle OCR**; phải chuyển sang OCR mà không tải lại.
5. Kiểm tra Home card 09 Download và 10 Subtitle OCR mở đúng route.

Các live service YouTube/TikTok/Douyin, cài runtime/language thật và OCR trên video của người dùng vẫn cần nghiệm thu thủ công; chúng không được tuyên bố PASS chỉ từ fixture/loopback.
