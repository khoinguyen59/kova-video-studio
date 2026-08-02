# Trí nhớ dự án LA Studio

Mục đích: lưu lịch sử sản phẩm và quyết định quan trọng để agent không làm lại
việc đã hoàn tất. Chỉ ghi tính năng, lỗi, quyết định và trạng thái; không chép
chi tiết code, log dài hoặc toàn bộ test output.

## Hiện trạng

- Ứng dụng desktop local-first gồm STT, TTS, Voice Cloning, Voice Design,
  Alignment, Translation, Dubbing, LLM Chat, Download và Subtitle OCR.
- Dubbing hỗ trợ remote Colab workers, local/remote model routing, transcript,
  subtitle, timing conflict và export MP4/Editable CapCut Draft.
- Package mới nhất là `0.0.2.15`, internal-only. Automated suite gần nhất
  39/39 PASS, nhưng manual acceptance vẫn tách riêng.
- Batch OCR language path, Direct Colab OCR, ROI interaction và media controls
  đã hoàn tất; yêu cầu mới chỉ được lấy từ `AI_AGENT_REQUEST.md`.

## Lịch sử thay đổi cấp sản phẩm

### Trước 2026-07-30

- Xây dựng các studio lõi và pipeline Dubbing.
- Bổ sung voice cloning tự động, adaptive translation/dubbing, subtitle muxing,
  Vietnamese normalization, audio stems và LLM Chat.
- Hình thành RuntimeHost và các controller/service cho local/remote workflow.

### 2026-07-30

- Tích hợp Colab exact-model workflows và notebook setup cho các route remote.
- Sửa retry, tunnel readiness, stale worker, capability/model guard và progress
  giả trong Dubbing/voice/translation.
- Chuẩn hóa runtime packaging, eSpeak/FFmpeg checks và tạo handoff ban đầu.

### 2026-07-31

- Thêm Download workflow và harden public media import, redirect, retry.
- Hoàn thiện dùng lại saved voice clone trong TTS và Dubbing; harden saved Voice
  Design reuse và invalidation khi đổi preset.
- Thêm Subtitle OCR primitives, managed workflow, runtime/language management và
  route chính trên UI.

### 2026-08-01

- Hoàn thiện responsive Subtitle OCR, Home card Download/OCR và shared media
  handoff.
- Thêm Dubbing STT/OCR/STT+OCR fusion, subtitle editor/export, timing ripple và
  Editable CapCut Draft.
- Sửa subtitle source, vị trí, font, max width, line spacing; làm draft tự chứa
  asset/font và giữ job đang chạy an toàn.
- Thay installer OCR ngoài bằng Tesseract 5.5.1 bundled Local CPU; thêm
  manifest/hash/health check, Ready/Refresh/Retry và route smoke chặt hơn.
- Tạo các internal candidate 0.0.2.7 đến 0.0.2.13. Việc build quá thường xuyên
  được xác định là không phù hợp; từ nay chỉ package một candidate tổng hợp sau
  khi batch yêu cầu hiện hành hoàn tất.
- Người dùng tái hiện trên 0.0.2.12: `chi_sim` false Ready, ROI kéo khựng và
  media controls che ROI; đồng thời yêu cầu thêm OCR Colab tùy chọn.
- Hoàn tất batch OCR và package 0.0.2.14: language Local CPU chỉ Ready sau
  SHA-256 + worker preflight trong app-data tessdata; Subtitle OCR/Dubbing OCR
  có route Direct Colab GPU PP-OCRv5 multilingual 3.1 không fallback hay lưu secret.
- ROI/8 handles dùng QML local state và media controls nằm dưới preview;
  automated regression 38/38 PASS. Live desktop/Colab vẫn là manual acceptance.
- Khắc phục lỗi trích xuất frame Subtitle OCR sát cuối video: FFmpeg có thể trả mã 0
  nhưng không tạo ảnh ở timestamp đó. Pipeline dùng safe-end, xác thực ảnh trước OCR,
  hiển thị diagnostics/retry và không fallback toàn khung hình. Regression runtime
  FFmpeg đã stage kiểm tra Unicode, rotation, ROI sát đáy/0-pixel và retry; 39/39 PASS.

### 2026-08-02

- Hoàn tất package nội bộ 0.0.2.16 với PaddleOCR 3.7.0 PP-OCRv6 tiny Local CPU
  cô lập trong package. Chỉ chi_sim được xác nhận bundle/Ready; ngôn ngữ chưa
  bundle không còn có thể báo Ready giả.
- Sửa pipeline staging PaddleOCR để package thực sự chứa runtime/model/worker,
  manifest UTF-8 không BOM và metadata license; health check được chạy từ thư
  mục package. Full automated suite 39/39 PASS; desktop/live Colab vẫn cần
  nghiệm thu người dùng riêng.

## Quy tắc duy trì trí nhớ

- Cập nhật sau mỗi task đã merge/push hoặc sau bằng chứng manual mới.
- Mỗi entry tối đa vài dòng: đã thêm gì, sửa gì, trạng thái automated/manual.
- Không ghi class/function/line, patch, stack trace, command dài hoặc checksum
  của mọi build; checksum mới nhất thuộc report summary.
- Không biến kế hoạch thành lịch sử hoàn tất. Việc chưa xong luôn nằm trong
  request; giới hạn/manual nằm trong report summary.
- Các quy tắc vận hành cố định thuộc project skill, không lặp lại tại đây.
