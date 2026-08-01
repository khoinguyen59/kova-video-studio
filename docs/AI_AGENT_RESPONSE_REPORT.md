# Điều phối báo cáo hiện hành — bắt buộc đọc trước khi làm tiếp

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

Ba commit sau đã được push thẳng lên `origin/main`, chưa tạo package mới vì full audit/chạy full regression chưa kết thúc:

- `04bc435` — import Subtitle OCR không có `Content-Length` có BusyIndicator indeterminate; smoke Home chạy 1024×720, 1280×800, 1600×900 và kiểm tra card 09/10 + scroll.
- `0641650` — CapCut draft chỉ gán stem “source vocals” sau khi source separation tạo cả vocals và background; không gán nhãn sai analysis mono.
- `da67f9a` — ASS burn-in giữ custom normalized x/y, chuyển thư mục font tuỳ chỉnh cho FFmpeg `fontsdir`, và fail sớm nếu font file đã cấu hình bị mất.
- `149a689` — OCR runtime preflight lại file/size/SHA trước launch, giữ cache installer đã verify cho Retry, ghi QProcess/working-directory/signature diagnostics, health-check `tesseract --version` trước `Ready`, và thêm UI Open diagnostics/Clean failed download.
- `67063f3` — launch installer/health-check qua Windows short path nếu NTFS cung cấp alias, vẫn giữ long Unicode canonical path để hash/cleanup/diagnostic; khắc phục path cache có khoảng trắng/Unicode mà không giả định 8.3 luôn bật.
- `7c1544d` — Rendered MP4 tôn trọng lựa chọn source reviewed (STT/OCR/import) hoặc translated target text, và `maxWidth` được map thành ASS horizontal margins. CapCut vẫn giữ cả original/dubbed sidecar và target→source fallback cho editable text.
- `865898f` — Rendered burn-in thực thi `lineSpacing`: text được Qt bọc theo `maxWidth`, mỗi visual line thành dialogue ASS có `pos`/bước dọc riêng; MP4 burn-in dùng encoder `mpeg4` có trong FFmpeg staged thay vì `libx264` không có trong runtime internal.
- `b6ed4ad` — Dubbing workspace không còn cố nhét History/source/review/inspector vào chiều rộng ngắn: viewport ngang có scrollbar khi cần, và smoke buộc chứng minh panel ngoài viewport còn reachable.

Regression đã chạy cho các thay đổi trên: `TestSubtitleOcrController`, `TestSubtitleOcrRuntimeService`, `TestDubbingProject`, `TestMediaToolService`, `PrepareQmlRouteSmokeRuntime`, `QmlRouteSmoke` đều PASS ở lượt chăm sóc tương ứng. Đây **không** thay cho full CTest/package, không được gọi là release hoàn tất, và không được gọi live GUI/Colab/CapCut PASS.

Sau `865898f`, full CTest với FFmpeg/FFprobe staged chạy **38/38 CTest target PASS, 0 CTest FAIL** (44.48 giây). Cần đọc đúng mức evidence: một subcase eSpeak trong `TestDubbingProject` tự `QSKIP` khi sandbox test không có eSpeak runtime; nó không liên quan line-spacing/burn-in và không được dùng để gọi runtime eSpeak hoặc toàn feature đã accepted. Chưa có package mới từ kết quả này.

Sau `b6ed4ad`, `PrepareQmlRouteSmokeRuntime` và `QmlRouteSmoke` PASS offscreen ở 1024×720, 1280×800, 1600×900. Đây chứng minh route load/resize/binding không warning và scrollbar reachability contract, nhưng visual/pointer acceptance vẫn manual.

Riêng `149a689`/`67063f3` đã chạy lại `TestSubtitleOcrRuntimeService`, `TestSubtitleOcrController`, `TestMediaIngestService`, `PrepareQmlRouteSmokeRuntime` và `QmlRouteSmoke` PASS. `7c1544d` đã chạy `TestDubbingProject`, `TestMediaToolService`, `PrepareQmlRouteSmokeRuntime` và `QmlRouteSmoke` PASS. `865898f` chạy lại bốn target trên; `TestDubbingProject::preservesConfiguredLineSpacingInBurnInAss` xác nhận khoảng cách dòng tăng theo cấu hình, còn `TestMediaToolService::rendersLineSpacedAssWithStagedFfmpeg` tạo video/audio ngắn, render ASS hai dòng qua FFmpeg staged/libass và xác minh MP4 kết quả. Lỗi Windows gốc trước đó không có QProcess diagnostic trong log và đã xóa installer đã hash-verify, nên không thể kết luận hồi tố là lỗi mạng hay quyền. Cần user chạy bản package kế tiếp để xác nhận process-launch thật; không gọi gate đó PASS trước bằng chứng mới.

Audit K đã đóng khoảng trống kỹ thuật về line spacing trong `865898f`: không gán nhầm vào ASS `Spacing` (character spacing), mà mỗi line ASS được đặt theo bước dọc proportional sau khi app Qt bọc theo `maxWidth`; custom font được đăng ký để phép đo/wrap dùng đúng family khi app GUI hoạt động. Test headless chỉ dùng newline tác giả nhập để không truy cập font database khi không có `QGuiApplication`; kiểm thử FFmpeg staged xác minh libass render được file ASS phát sinh. Đây là **regression/CLI render PASS**, không phải visual acceptance: người dùng vẫn cần xác nhận trên package mới về typography, wrap HiDPI, màu/position và CapCut import thật.

Sau khi đọc, AI tiếp theo phải tiếp tục audit H–M (K đã có regression/render follow-up nhưng chưa có manual visual acceptance) và báo cáo requirement → source location → test cụ thể → PASS/FAIL/SKIP/manual. Đồng thời giữ N ở trạng thái manual pending cho đến khi có launch/install thật ở package mới. Nếu phát hiện A–G có regression mới thì sửa kèm test; không được chỉ nói “baseline đã xong” để bỏ qua.

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
