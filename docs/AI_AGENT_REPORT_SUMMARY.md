# Báo cáo tổng hợp LA Studio

Cập nhật: 2026-08-02

## Baseline hiện tại

| Mục | Trạng thái |
| --- | --- |
| Latest packaged candidate | 0.0.2.16 |
| Artifact | out/LA-Studio-0.0.2.16/LA-Studio-0.0.2.16.exe |
| SHA-256 | DF16ADFE912EA5D2A55A266E4D215C93C944C7E12EBBB925B47AD743D10E3727 |
| Automated suite | 39/39 CTest PASS; QML offscreen PASS ở ba viewport |
| Distribution | Internal only; eSpeak MSI chưa ký |

## Batch PaddleOCR production / package 0.0.2.16

- Local CPU PaddleOCR có runtime cô lập được package cùng executable: CPython,
  worker, 52 model files PP-OCRv6 tiny, manifest UTF-8 không BOM và metadata
  license. Chỉ chi_sim được bundle và hiển thị Ready; ngôn ngữ khác bị chặn rõ
  ràng trước khi chạy, thay vì báo Ready giả. Direct Colab vẫn là route độc lập
  và dùng contract segment/timestamp/model có sẵn.
- Dubbing dùng cùng readiness/cache của Subtitle OCR. Cache/handoff giữ source
  hash, ROI, language, model và route; các regression STT/OCR/STT+OCR, export,
  reload và QML route đều nằm trong full suite.
- Root cause package đã sửa: Paddle staging trước đây chưa được gọi; sau khi
  được nối vào package, manifest có nguy cơ BOM và license copy dùng hai ký tự
  backslash cho TrimStart làm stage dừng muộn. Staging nay ghi UTF-8 không BOM,
  health-check lại sau lần ghi cuối và dùng đúng một ký tự path separator; có
  regression cho cả hai điểm.
- Automated evidence: targeted runtime/controller/pipeline/Dubbing/QML PASS và
  **39/39 CTest PASS** cuối cùng. Package audit PASS: FileVersion/ProductVersion
  0.0.2.16, Qt qwindows.dll, FFmpeg/FFprobe, Paddle worker/Python/model,
  manifest/license và health check trong chính thư mục package.
- E2E được tái sử dụng hợp lệ vì batch chỉ đổi readiness/package, không đổi
  engine/input/config OCR: 1.mp4 SHA
  84f6bed3bb9d6ad42eccb0b830a873aae1998c016e361f075265d6d0eacca214,
  PaddleOCR 3.7.0 PP-OCRv6 tiny từ upstream
  2661c7c0ef5c613e8f93c6e93b2e052399f0f854, zh-Hans, ROI
  0.009,0.883,0.976,0.096, interval 800 ms, confidence 50%, 1,125 samples
  và 430 segments. Dubbing đã reuse 430 segments, không OCR lần hai.
- Bốn output E2E còn đọc được: out/ocr-e2e-new/standalone-zh-Hans.srt,
  dubbing-zh-Hans.srt, transcript-zh-Hans.txt và OCR_TEST_RESULT.md.

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
- Full CTest sau cùng: **39/39 PASS**. Candidate 0.0.2.16 đã audit File/Product
  version, hash, Qt Windows platform plugin, FFmpeg/FFprobe, Tesseract 5.5.1,
  PaddleOCR CPU runtime/model/manifest/license và health check.

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
