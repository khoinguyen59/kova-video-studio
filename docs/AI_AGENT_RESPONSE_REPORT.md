# Báo cáo hoàn tất — LA Studio 0.0.2.5

Ngày: 2026-07-31
Phạm vi: các yêu cầu B–K trong `AI_AGENT_REQUEST.md`, gồm tái sử dụng voice, import media công khai, luồng Subtitle OCR, kiểm thử, đóng gói và đẩy trực tiếp lên `main`.

## Kết quả đã hoàn tất

### Voice clone và voice design

- Dữ liệu preset đã lưu được nạp lại qua service, giữ đúng ID và mô tả sau khi service được tạo lại.
- Luồng TTS và Dubbing dùng clone profile đã lưu không còn giữ URL/token Colab cũ hoặc dùng nhầm profile sau khi đổi preset.
- Luồng TTS dùng voice design đã lưu có regression test cho việc nạp lại preset và gửi mô tả vào loopback worker thật.
- Dubbing hiện không có worker voice-design tương ứng trong source. UI không giả vờ hỗ trợ: route này được chặn rõ ràng thay vì gửi một request sai. Đây là giới hạn sản phẩm hiện tại, không phải một ca đã chạy thành công.

Các commit liên quan: `85fb172`, `0a40fbe`, `c5e3ca5`, `9fc530b`.

### Import media công khai

- Adapter public-media xử lý redirect, retry và lỗi mạng rõ ràng hơn; không dùng cookie, đăng nhập hoặc bypass DRM.
- Kiểm thử media dùng HTTP fixture cục bộ và FFmpeg/FFprobe runtime đã stage, không mạo nhận là kiểm thử trực tiếp YouTube, TikTok hoặc Douyin.
- Kết quả `TestMediaIngestService`: **18 passed, 0 failed**.

Các commit liên quan: `a2bea3d`, `5ba9dcb`.

### Subtitle OCR

Đã chọn kiến trúc OCR local CPU, tách hẳn khỏi Colab/API, vì đây là công cụ xử lý subtitle theo khung hình và cần hoạt động dự đoán được trong ứng dụng desktop.

- Thêm pipeline `SubtitleOcrPipeline` cho ROI chuẩn hóa, lệnh crop FFmpeg, lịch lấy mẫu frame, parse TSV Tesseract (kể cả Unicode/multiline), gộp quan sát và xuất SRT.
- Thêm `SubtitleOcrRuntimeLocator`: tìm Tesseract theo thứ tự thư mục package, biến môi trường `LASTUDIO_TESSERACT`, rồi `PATH`; không tự tải runtime/model.
- Thêm `SubtitleOcrController` bất đồng bộ bằng `QProcess`: probe media, preview crop, kiểm tra ngôn ngữ Tesseract trước khi xử lý, tiến độ theo frame thật, hủy/retry, dọn workspace, tránh crop trùng bằng SHA-256, lưu/mở dự án OCR và xuất SRT/text.
- Hoàn thiện trang `SubtitleOcrPage.qml`: chọn video, ROI kéo-thả, cấu hình language/sample/confidence, kiểm tra runtime, tiến độ thực, review/chỉnh segment, lưu/mở/xuất và gửi sang Subtitle Voice hoặc Dubbing.
- Gửi sang Subtitle Voice dùng import SRT thật; gửi sang Dubbing thay transcript segment thật và ghi nguồn `subtitle-ocr` cùng confidence. Các segment đều được kiểm tra trước khi mutation để tránh trạng thái nửa chừng.
- Đã thêm route sidebar và QML route smoke test.

Đánh giá thư viện đã ghi nhận: VideoSubFinder bị loại do GPL-2.0; Tesseract được dùng theo Apache-2.0 và được cung cấp như runtime do người dùng cài, không kèm tải ngầm. PaddleOCR/RapidOCR/EasyOCR không được đưa vào package hiện tại vì kích thước/runtime không phù hợp với gói desktop này.

Các commit liên quan: `5d8fdc1`, `3ab1fec`.

## Kiểm thử đã chạy

| Kiểm tra | Kết quả |
| --- | --- |
| `TestSubtitleOcrPipeline` | 8 passed, 0 failed |
| `TestSubtitleOcrController` | 8 passed, 0 failed |
| QML route smoke | passed |
| `TestMediaIngestService` với HTTP fixture + runtime media stage | 18 passed, 0 failed |
| Toàn bộ CTest (`ctest --parallel 4 --output-on-failure`) | **37 passed, 0 failed**, 9.47 giây |
| Cập nhật đồ thị mã nguồn | `graphify update .`: 11,445 nodes, 22,176 edges, 551 communities |

Các test controller OCR bao gồm runtime thiếu không tự download, ngôn ngữ thiếu, source probe lỗi không làm mất source hợp lệ trước đó, chạy bất đồng bộ với fake tools, crop/review/export/open lại, tích hợp thực với Subtitle Voice và Dubbing, cancel/retry/dọn workspace.

Không có test nào được ghi là pass bằng mock UI đơn thuần khi backend không hề được gọi. Các giới hạn không thể kiểm chứng tại máy hiện tại được ghi rõ bên dưới.

## Đóng gói nội bộ 0.0.2.5

Portable package đã được build và xác minh mà **không mở EXE/không điều khiển giao diện máy**.

| Hạng mục | Giá trị |
| --- | --- |
| EXE | `out/LA-Studio-0.0.2.5/LA-Studio-0.0.2.5.exe` |
| File/Product version | `0.0.2.5` |
| SHA-256 EXE | `513090A3DEB30A84E6D1DA6715E786A5303F5991AC0FC6B811AE8E184C59429E` |
| Staging manifest | 19/19 artifact bắt buộc |
| License manifest | 18/18 license artifact bắt buộc |
| OCR runtime package | có `subtitle-ocr/README.txt`, `runtime-manifest.json`, `licenses/tesseract/RUNTIME-NOTICE.md` |
| OCR manifest | `bundled=false`, `automaticDownload=false`, `Apache-2.0` |
| Media runtime | đã có `media-tools/ffmpeg.exe` và `media-tools/ffprobe.exe` |

Lỗi checksum `yt-dlp` khi đóng gói đã được điều tra đến release upstream, sau đó pin SHA-256 được sửa bằng checksum của chính asset `yt-dlp 2026.07.04`; không tắt kiểm tra checksum để “cho qua”. Commit: `b0c87a4`.

Gói này được đánh dấu **internal-only** vì eSpeak NG MSI được xác minh SHA-256 nhưng không có chữ ký số. Không được phát hành phân phối công khai từ artifact này.

## Trạng thái Git

Các sửa đổi source đã commit và push trực tiếp lên `origin/main`; không tạo branch công việc mới. Commit source mới nhất trước báo cáo này là `b0c87a4`.

## Giới hạn được nêu trung thực

- Không có GPU Colab đang hoạt động trong phiên này, nên không tuyên bố đã kiểm thử inference GPU, độ chính xác voice thật hoặc notebook live.
- Không có kiểm thử live đối với YouTube, TikTok, Douyin hay các website cần điều kiện mạng/giới hạn bên ngoài; chỉ có kiểm thử adapter bằng fixture cục bộ.
- Tesseract không được bundle và không tự download. Do đó chưa có đánh giá chất lượng OCR trên video người dùng thật tại máy này; phần logic, preflight, UI, export và tích hợp native đã được kiểm thử tự động.
- EXE chỉ được kiểm tra artifact/version/dependency manifest, chưa được mở trong bước xác minh này theo yêu cầu không điều khiển máy.

## Theo dõi yêu cầu mới

Đã có automation cục bộ tại:

`C:\Users\Nguyen Trong Khoi\.codex\automations\check-la-studio-request-after-completion\automation.toml`

Sau khi báo cáo này được commit/push và không còn task đang chạy, automation sẽ đọc `docs/AI_AGENT_REQUEST.md` mỗi 30 phút. Nếu file có yêu cầu mới, agent sẽ thực hiện theo yêu cầu đó; không mở GUI, browser hay EXE trong lần kiểm tra tự động.
