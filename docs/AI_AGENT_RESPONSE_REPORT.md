# Phản hồi mới nhất của AI agent

Ngày: 2026-08-01

## Đã hoàn tất theo yêu cầu mới nhất

- Đã đọc `AI_AGENT_REQUEST.md`, điều tra sâu đường Subtitle OCR từ UI/controller,
  probe metadata, FFmpeg crop, giải mã ảnh đến Tesseract; không mở ứng dụng hay điều
  khiển giao diện máy tính.
- Đã xác định lỗi thực: tại timestamp quá sát duration, FFmpeg có thể kết thúc với mã
  `0` nhưng không tạo crop image. Vì vậy lỗi trước đây không phải là Tesseract hay
  việc chọn `chi_sim`/`chi_tra`.
- Đã sửa pipeline để chọn frame cuối an toàn (cách cuối video một giây), giữ crop ROI
  full-width và pixel lẻ chính xác, xử lý ROI 0-pixel thành lỗi rõ ràng, và xác thực
  file tồn tại/kích thước/chữ ký PNG/giải mã trước khi OCR. Không còn fallback im lặng
  sang toàn khung hình.
- Đã thêm diagnostics có giới hạn kích thước cho probe/frame extraction/output/cleanup,
  cùng nút Retry frame extraction và Open diagnostics. Retry giữ source video, ROI và
  ngôn ngữ đã chọn.

## Bằng chứng kiểm thử

- Regression mới dùng chính FFmpeg được stage theo đường runtime/package, không dùng
  mock che lỗi boundary: video có đường dẫn Unicode/khoảng trắng, landscape,
  portrait/rotation, full-width ROI sát đáy, ROI nhỏ hợp lệ, ROI 0-pixel, safe-end,
  process failure, timeout, cancel/retry và cleanup sau khi consumer dùng xong.
- Không có test mới bị `SKIP`. Full suite cuối cùng: **39/39 CTest PASS, 0 FAIL**.
- Đây là bằng chứng tự động cho boundary đã được sửa. Chưa thể khẳng định video người
  dùng từng chọn đã manual-pass trên desktop vì ứng dụng không được mở trong lượt này.

## Commit và package

- Source đã commit và push trực tiếp lên `origin/main`: `3a9d5c3`
  (`fix: harden subtitle OCR frame extraction`).
- Candidate nội bộ: `out/LA-Studio-0.0.2.15/LA-Studio-0.0.2.15.exe`.
- File/Product version: `0.0.2.15`.
- SHA-256: `86280ADD832B1DD6CAA7A53ED6D5AC43246C63587CEFC22FCBC3ED2C498A0B3C`.
- Kiểm tra package bằng CLI PASS: `platforms/qwindows.dll`, FFmpeg, FFprobe,
  Tesseract 5.5.1 và `subtitle-ocr/runtime-manifest.json` với
  `healthCheckPassed: true` đều có mặt. Không mở EXE khi kiểm tra.

## Manual acceptance còn lại

- Chạy candidate 0.0.2.15 với đúng video trước đây, chọn `chi_sim` hoặc `chi_tra` và
  ROI sát đáy. Nếu frame vẫn không tạo được, bấm **Open diagnostics** và cung cấp nội
  dung diagnostics để xử lý đúng boundary còn lại; có thể bấm **Retry frame extraction**
  mà không phải chọn lại video/ROI/language.
- Notebook Colab PP-OCRv5 và các luồng live khác vẫn là phần manual acceptance riêng,
  không được suy diễn là đã pass từ regression local này.
