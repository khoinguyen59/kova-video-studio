# Báo cáo thực hiện — LA Studio 0.0.2.6

Ngày kiểm chứng: 2026-08-01
Phạm vi: các mục B–F trong `docs/AI_AGENT_REQUEST.md`.

## Git và phạm vi thay đổi

- Làm trực tiếp trên `main`; source, regression và version đã được commit/push lên `origin/main` tại `e439b14` (`feat: add managed subtitle OCR runtime`).
- Đã giữ nguyên và không stage/commit các file ngoài phạm vi: `docs/AI_AGENT_REQUEST.md`, `docs/AI_AGENT_HANDOFF_2026-07-30.md`, `qml/components/shared/VoiceLibraryDialog.qml`, `.agents/`, Graphify, `out/` và log test tạm.
- Version nguồn, EXE và package mới đều là `0.0.2.6`; không ghi đè package `0.0.2.5`.
- Graphify đã cập nhật sau source edit. Dữ liệu graph là generated/local, không được commit.

## B–C. Runtime và language pack cho Subtitle OCR

`SubtitleOcrRuntimeService` được thêm vào luồng controller/QML. Tesseract không còn phụ thuộc PATH mặc định, không tự tải nền và không yêu cầu quyền admin.

- Người dùng phải bấm **Install runtime** trong Subtitle OCR. UI hiển thị `Missing`, `Downloading`, `Installing`, `Installed`, `Invalid` hoặc `Failed`, nguồn runtime, path đang dùng, byte progress khi server báo content length, lỗi, retry và cancel.
- Runtime installer được pin tại Tesseract `5.5.3.20260724`, URL/version/size/SHA-256 được đóng trong manifest; checksum phải đúng trước khi cài. Cài đặt dùng staging app-owned và promote atomic, nên download/cài thất bại không phá runtime đang có.
- Vị trí managed sau khi cài là app data `subtitle-ocr/runtime/tesseract.exe`; lần mở sau tự phát hiện bằng manifest. `LASTUDIO_TESSERACT` vẫn là advanced override rõ ràng trên UI và không bị ghi đè.
- Sáu gói `tessdata_fast` có thể cài độc lập: `eng`, `vie`, `chi_sim`, `chi_tra`, `jpn`, `kor`. Mỗi gói pin commit/URL/size/SHA-256, kiểm tra trước khi atomic replace. Gói lỗi hoặc bị hủy giữ nguyên file hợp lệ cũ.
- Trước khi OCR, controller kiểm tra runtime và language được chọn; thiếu dependency bị chặn với thông báo/luồng cài rõ ràng, không fallback sang PATH hoặc tải ngầm.
- Portable package chỉ mang `subtitle-ocr/runtime-manifest.json` và `licenses/tesseract/RUNTIME-NOTICE.md`; không bundle Tesseract binary/traineddata. Manifest quy định Apache-2.0 và explicit user download.

Cách dùng sau khi người dùng tự mở app:

1. Mở **Subtitle OCR**.
2. Bấm **Install runtime**, chờ checksum và cài hoàn tất.
3. Bấm **Install** cho ngôn ngữ cần dùng, ví dụ `vie` hoặc `chi_sim`.
4. Chọn language, xác định ROI và chạy OCR. Nếu dùng `LASTUDIO_TESSERACT`, UI sẽ báo đây là override; language pack managed không được cài lẫn vào runtime bên ngoài.

## D. UI, route và ROI

- Route sidebar `subtitle-ocr` thực sự nạp `SubtitleOcrPage` qua registry/main loader.
- ROI giữ mapping normalized tới source frame, gồm letterbox/resize/HiDPI; có drag move, tám handle resize cạnh/góc, preset lower region, reset và preview crop.
- ROI rỗng hoặc ngoài biên bị chặn trước khi pipeline chạy.
- Regression mới kiểm tra QML route/source, wiring runtime UI, dynamic language pack, mapping crop/preview/reset và đúng tám ROI handles; không chỉ kiểm README.

## E. Ma trận public-media dùng chung

`RemoteMediaImportService` là backend chung của tab Download và Dubbing. Handoff dùng staged result đã sở hữu, không tải lần hai.

| Mục | Backend/kiểm tra source-loopback | Trạng thái live |
| --- | --- | --- |
| Direct HTTPS media | staging app-owned, byte progress, cancel dọn partial, giới hạn 2 GiB | BLOCKED — không gọi site thực |
| YouTube URL | managed `yt-dlp`, một video `--no-playlist`, URL là positional arg sau `--` | BLOCKED — fixture/loopback chỉ kiểm contract |
| TikTok URL | cùng managed adapter và contract an toàn | BLOCKED — fixture/loopback chỉ kiểm contract |
| Douyin URL chuẩn | cùng managed adapter và contract an toàn | BLOCKED — fixture/loopback chỉ kiểm contract |
| Douyin short link (`v.douyin.com`) | resolver contract có regression riêng | BLOCKED — fixture/loopback chỉ kiểm contract |
| Redirect/private address | scheme, user-info, DNS/literal private address và redirect unsafe bị từ chối trước staging | PASS source/loopback |
| Playlist/injection/cookie | `--no-playlist`, không cookie/login, không shell concatenation, URL độc hại vẫn là positional arg | PASS source/loopback |
| Probe/handoff | ffprobe/normalize trước commit project; fail giữ project cũ; shared staged media không redownload | Một phần BLOCKED vì FFmpeg/FFprobe managed runtime không có trong CLI test environment |

Không có cookie, profile, login hay website công khai thực nào được dùng. Do đó không đánh đồng fixture với việc tải thật từ YouTube/TikTok/Douyin.

## Kiểm thử đã chạy

Targeted suite:

```powershell
ctest --test-dir out\build\windows-msvc-tests -R '^(TestSubtitleOcrRuntimeService|TestSubtitleOcrController|TestSubtitleOcrPipeline|TestMediaIngestService)$' --output-on-failure
```

Kết quả: **4/4 test executables passed**.

Full suite:

```powershell
ctest --test-dir out\build\windows-msvc-tests --parallel 4 --output-on-failure
```

Kết quả: **38/38 test executables passed, 0 failed** (13.22 giây).

`TestSubtitleOcrRuntimeService` có 7 regression: pin manifest/checksum/sáu gói language, replace atomic, checksum-failure preservation, runtime promotion/reopen, cancel/error/retry state, QML/route/ROI wiring. `TestSubtitleOcrController` và `TestSubtitleOcrPipeline` đều pass 8 ca.

Trong `TestMediaIngestService`, 15 ca pass và 3 ca bị **SKIP có điều kiện**: controller FFprobe/normalize, handoff no-redownload và probe-failure preservation cần managed FFmpeg/FFprobe mà môi trường CLI không cài qua `LASTUDIO_FFMPEG`/`LASTUDIO_FFPROBE`. CTest vẫn pass; ba ca đó không được báo là live/integration PASS.

## Package portable nội bộ

Lệnh đóng gói CLI (không mở EXE/browser/GUI):

```powershell
.\scripts\package.ps1 -Preset windows-msvc-release -QtRoot .tools\Qt\6.9.3 -VcpkgRoot .deps\vcpkg -LlamaCppSourceDir .deps\llama.cpp -Version 0.0.2.6 -SkipInstaller -PortableInternalLayout -AllowUnsignedEspeakForInternalBuild
```

| Hạng mục | Kết quả |
| --- | --- |
| Artifact | `out/LA-Studio-0.0.2.6/LA-Studio-0.0.2.6.exe` |
| File/Product version | `0.0.2.6` |
| SHA-256 | `AC9D59BFD83BD70A8AD49B2E866E8F623AB8D33B8F81B75DB51C60C1ED65125C` |
| Staging manifest | script xác minh 19 artifact bắt buộc |
| License manifest | script xác minh 18 artifact bắt buộc |
| OCR manifest/notice | có `subtitle-ocr/runtime-manifest.json`, `subtitle-ocr/README.txt`, `licenses/tesseract/RUNTIME-NOTICE.md` |
| Runtime media | staged FFmpeg/FFprobe và yt-dlp managed |

Package này là **internal-only**: script đã cảnh báo eSpeak NG MSI SHA-256 hợp lệ nhưng không ký số. Không dùng artifact này làm bản public distribution.

## Giới hạn còn BLOCKED

- Không chạy live OCR trên video của người dùng, không tải/cài Tesseract/traineddata thật và không mở EXE; hành vi đã kiểm qua source, mock/loopback và package inspection.
- Không chạy live Colab GPU/notebook/token; không kết luận inference hoặc chất lượng model thật.
- Không truy cập public YouTube/TikTok/Douyin; contract adapter được kiểm fixture/loopback, live-site là BLOCKED.
- Ba media integration test cần managed FFmpeg/FFprobe đang SKIP trong môi trường CLI như mô tả ở trên.

Automation 30 phút trùng trước đó đã được gỡ theo request; automation theo giờ đã có vẫn được giữ nguyên. Không tạo automation mới trong vòng này.
