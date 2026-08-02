# Phản hồi AI agent — PaddleOCR production và package 0.0.2.16

Ngày: 2026-08-02

## Kết quả

**PASS (automated/package).** Source đã là 0.0.2.16 và package portable nội bộ:

C:\Users\Nguyen Trong Khoi\Downloads\LA-STUDIO\out\LA-Studio-0.0.2.16\LA-Studio-0.0.2.16.exe

- FileVersion và ProductVersion: 0.0.2.16.
- SHA-256 EXE:
  DF16ADFE912EA5D2A55A266E4D215C93C944C7E12EBBB925B47AD743D10E3727.
- CTest cuối: **39/39 PASS**, gồm Subtitle OCR pipeline/controller/runtime,
  Dubbing STT/OCR/STT+OCR, export/cache/reload và QML route offscreen.
- Package audit trực tiếp từ thư mục trên PASS: Qt platforms/qwindows.dll,
  FFmpeg/FFprobe, Python cô lập, Paddle worker, 52 model files, manifest không
  BOM, license metadata và --health của worker (manifestVerified=true).

## Phần đã tích hợp/sửa

- Local CPU dùng PaddleOCR 3.7.0 PP-OCRv6 tiny thật; package chỉ hiển thị
  chi_sim là bundle/Ready. Với language chưa bundle, UI/controller dừng trước
  khi tạo worker và hướng dẫn chọn chi_sim, Tesseract matching language hoặc
  Direct Colab. Không có fallback im lặng.
- Subtitle OCR và Dubbing tiếp tục dùng chung configuration/cache/handoff:
  source hash, ROI, language, model và route nằm trong cache key; Dubbing reuse
  transcript artifact thay vì OCR lại.
- Direct Colab giữ độc lập với API Gateway và contract segment/timestamp/model
  hiện có; secret không được ghi vào source/report.
- Root cause package: Stage-PaddleOcrRuntime chưa được gọi. Sau khi nối staging
  vào flow, phát hiện thêm hai lỗi release-only: manifest PowerShell UTF-8 BOM
  làm worker Python không đọc JSON, và TrimStart truyền hai backslash khiến copy
  license dừng sau khi stage. Cả hai đã được sửa, có regression, và package
  health-check lại manifest cuối.

## Evidence OCR đã tái sử dụng

Không rerun full video vì thay đổi batch này chỉ ở readiness/UI/package, không
đổi engine, input hay cấu hình OCR đã PASS.

- Input: C:\Users\Nguyen Trong Khoi\Downloads\1.mp4
- SHA-256:
  84f6bed3bb9d6ad42eccb0b830a873aae1998c016e361f075265d6d0eacca214
- Engine: PaddleOCR 3.7.0, PP-OCRv6 tiny det/rec, zh-Hans; upstream
  PaddlePaddle/PaddleOCR commit
  2661c7c0ef5c613e8f93c6e93b2e052399f0f854.
- Config: ROI x=.009, y=.883, w=.976, h=.096; interval 800 ms; confidence
  50%; 1,125 samples, 430 segments. Dubbing reuse 430 segments; Tesseract
  fallback 0 and no child OCR process còn lại.

Output đọc được:

- out/ocr-e2e-new/standalone-zh-Hans.srt
- out/ocr-e2e-new/dubbing-zh-Hans.srt
- out/ocr-e2e-new/transcript-zh-Hans.txt
- out/ocr-e2e-new/OCR_TEST_RESULT.md

## Còn chờ nghiệm thu thủ công

Chưa mở EXE/điều khiển GUI và chưa gọi Colab live. Desktop drag/drop, ROI pointer,
file picker, chất lượng OCR trực quan và kết nối Direct Colab cần người dùng nghiệm
thu riêng; chúng không được tính là PASS chỉ từ test headless/package.
