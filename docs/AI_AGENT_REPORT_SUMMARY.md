# Báo cáo tổng hợp LA Studio

Cập nhật: 2026-08-01

## Baseline hiện tại

| Mục | Trạng thái |
| --- | --- |
| Latest packaged candidate | `0.0.2.14` từ commit `cbe252c` |
| Artifact | `out/LA-Studio-0.0.2.14/LA-Studio-0.0.2.14.exe` |
| SHA-256 | `77990CB996D5BDA0A8F908AE98A86D13A2C318818F963A09AB82503B06FC6508` |
| Automated suite | 38/38 CTest PASS; QML offscreen PASS ở ba viewport |
| Distribution | Internal only; eSpeak MSI chưa ký |

## Đã có implementation và regression

| Nhóm | Trạng thái có thể chứng minh |
| --- | --- |
| Home/Download/OCR | Có card Download và Subtitle OCR; local file, shared staged media, URL adapter và handoff không redownload có regression. |
| Public media | Direct URL, YouTube, TikTok và Douyin dùng shared ingest contract; cancel/retry/probe-failure được kiểm bằng fixture/loopback. |
| Voice reuse | Saved voice clone và voice design có đường dùng lại trong TTS/Dubbing; thay preset làm invalid cache/profile cũ. |
| Dubbing transcript | Có STT, OCR và STT+OCR; combined mode dùng fusion có provenance/conflict và không fallback im lặng. |
| Dubbing subtitle | Import SRT/VTT/ASS/SSA/TXT/MD, style/persistence, sidecar và burn-in route; Unicode và timing có regression. |
| Timing | Có conflict detection, Keep timing, Ripple forward, Manual, preview/apply/undo và cache invalidation. |
| CapCut | Có Editable CapCut Draft tách media/audio/voice/text, copy asset/font và validate trước publish. |
| OCR runtime | Tesseract 5.5.1 được bundle, có manifest/hash/health check; language chỉ Ready sau khi SHA-256 và Tesseract worker cùng app-data tessdata xác nhận. |
| Subtitle OCR route | Có Local CPU và Direct Colab GPU PP-OCRv5 multilingual 3.1; Colab chỉ nhận frame ROI đã crop, không dùng API Gateway và không lưu URL/token. |
| Responsive UI | Home, Dubbing và Subtitle OCR có QML route/offscreen regression tại 1024×720, 1280×800, 1600×900; ROI local-state/8 handles và media controls dưới preview. |

## Batch hiện hành đã hoàn tất

- Đã tạo project skill, sửa false Ready OCR, thêm route Direct Colab GPU cho
  Subtitle OCR/Dubbing OCR, và sửa tương tác ROI/media controls theo request A–D.
- Full CTest sau cùng: 38/38 PASS; candidate tổng hợp 0.0.2.14 đã audit file,
  version metadata, Qt Windows platform plugin và Tesseract manifest/health.
- Traineddata không nằm cạnh EXE theo thiết kế: language pack được cài nguyên tử
  vào app-data, sau đó worker preflight mới cho phép OCR local.

## Manual acceptance còn mở

- OCR video/language thật và chất lượng ROI/pointer trên desktop.
- YouTube/TikTok/Douyin live với media được phép truy cập.
- Colab exact PP-OCRv5 worker/model, notebook/tunnel và output live.
- Visual/HiDPI, pointer, drag/drop và file picker thật.
- Subtitle burn-in, timing ripple với audio thật.
- Import và chỉnh sửa Editable Draft trong CapCut thật.

Các mục manual không phải yêu cầu viết lại implementation nếu chưa có lỗi cụ
thể. Khi người dùng đưa bằng chứng FAIL, chuyển đúng lỗi đó vào
`AI_AGENT_REQUEST.md`.

## Quy ước tài liệu

- File này là trạng thái tích lũy duy nhất và phải giữ ngắn gọn.
- `AI_AGENT_REQUEST.md` chỉ chứa việc chưa xong.
- `AI_AGENT_RESPONSE_REPORT.md` chỉ chứa phản hồi mới nhất.
- `PROJECT_MEMORY.md` lưu lịch sử sản phẩm ở mức tính năng, không chép code.
- Bằng chứng chi tiết cũ vẫn truy được trong Git history; không tạo thêm report
  theo version.
