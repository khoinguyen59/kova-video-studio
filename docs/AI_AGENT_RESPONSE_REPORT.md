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
