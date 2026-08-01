# Phản hồi mới nhất của AI agent

Ngày: 2026-08-01

## Kết quả batch A–D

- Commit `cbe252c` đã được push trực tiếp lên `origin/main`.
- Root cause false Ready: UI trước đó có thể đánh dấu language theo file/hash,
  trong khi worker Tesseract sử dụng tessdata khác. Runtime, preflight và worker
  nay dùng cùng resolved app-data tessdata; Ready chỉ sau SHA-256 và `--list-langs`
  xác nhận đúng language. Diagnostics nêu binary, tessdata và language an toàn.
- Đã thêm Local CPU / Direct Colab GPU cho Subtitle OCR và Dubbing OCR. Colab dùng
  PP-OCRv5 multilingual 3.1, notebook/contract exact model, chỉ upload PNG ROI
  đã crop, có connect/check/progress/cancel/retry và không fallback im lặng.
  Route này độc lập API Gateway; URL/token tạm không được ghi vào project.
- Đã đổi ROI sang local QML state với 8 handles; controller chỉ nhận normalized
  ROI khi commit. Play/pause/seek được đặt dưới preview, overlay fullscreen
  auto-hide sau 2 giây khi không còn pointer/drag/menu/focus.

## Bằng chứng automated

- Targeted OCR runtime/controller/worker, Colab loopback (success, failure,
  cancel, retry, zero profile), Dubbing STT+OCR và QML route smoke đã PASS.
- Full CTest sau cùng: **38/38 PASS, 0 FAIL**. QML offscreen có regression ở
  1024×720, 1280×800 và 1600×900, kể cả mapping HiDPI, ROI sát đáy, drag dài,
  pause/seek/focus.

## Package

- Candidate nội bộ: `out/LA-Studio-0.0.2.14/LA-Studio-0.0.2.14.exe`.
- SHA-256: `77990CB996D5BDA0A8F908AE98A86D13A2C318818F963A09AB82503B06FC6508`.
- File/Product version đều là 0.0.2.14. Qt Windows platform plugin, Tesseract
  5.5.1 bundled runtime, manifest hash và package health check đều đã audit PASS.
- Language data không bundle cạnh EXE theo thiết kế: nó được cài nguyên tử vào
  app-data rồi worker xác nhận. Package internal-only vì eSpeak MSI chưa ký.

## Manual pending

- Chạy desktop thật để xác nhận pointer/ROI, OCR video và language thật.
- Chạy notebook Colab PP-OCRv5, tunnel và output live của exact worker.
- Cụm platform download, subtitle burn-in/timing ripple và CapCut import với
  media thật vẫn chờ manual acceptance.
