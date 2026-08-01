# Báo cáo tổng hợp LA Studio

Cập nhật: 2026-08-01

## Baseline hiện tại

| Mục | Trạng thái |
| --- | --- |
| Latest packaged candidate | `0.0.2.15` từ commit `3a9d5c3` |
| Artifact | `out/LA-Studio-0.0.2.15/LA-Studio-0.0.2.15.exe` |
| SHA-256 | `86280ADD832B1DD6CAA7A53ED6D5AC43246C63587CEFC22FCBC3ED2C498A0B3C` |
| Automated suite | 39/39 CTest PASS; QML offscreen PASS ở ba viewport |
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

## Batch Subtitle OCR frame extraction đã hoàn tất

- Root cause của lỗi `Subtitle OCR frame extraction did not produce a readable crop`
  đã được khoanh vùng: sampling sát cuối duration có thể làm FFmpeg thoát `0` nhưng
  không tạo ảnh. Pipeline nay dùng timestamp an toàn cách cuối video một giây;
  crop giữ đúng full-width/odd pixel thay vì âm thầm thu hẹp.
- Controller hiện thu diagnostics giới hạn dung lượng cho probe, command, crop,
  stderr, output file/signature/decode và cleanup; lỗi giữ nguyên source/ROI/language
  để người dùng Retry frame extraction hoặc mở diagnostics. Không có full-frame
  fallback im lặng.
- Regression dùng FFmpeg đã stage theo runtime/package thật: đường dẫn Unicode và
  khoảng trắng, landscape, portrait/rotation, ROI full-width sát đáy, ROI nhỏ hợp lệ,
  ROI 0-pixel, safe-end, process failure, timeout, cancel/retry và cleanup sau OCR.
  Không có test mới bị `SKIP`.
- Full CTest sau cùng: **39/39 PASS**. Candidate 0.0.2.15 đã audit File/Product
  version, hash, Qt Windows platform plugin, FFmpeg/FFprobe, Tesseract 5.5.1 và
  `runtime-manifest.json` (`healthCheckPassed: true`).

## Manual acceptance còn mở

- OCR video/language thật và chất lượng ROI/pointer trên desktop, đặc biệt nguồn
  video đã báo lỗi trước đó với `chi_sim`/`chi_tra`.
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
