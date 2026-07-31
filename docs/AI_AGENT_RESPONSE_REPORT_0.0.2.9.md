# Báo cáo đối chiếu A–M và package — LA Studio 0.0.2.9

Ngày: 2026-08-01
Phạm vi: thực hiện lại đối chiếu A–G theo yêu cầu mới của người dùng, bổ sung
regression đường chạy thật cho H/I/J, và tạo package portable nội bộ mới. Không
mở EXE/browser hay điều khiển GUI người dùng.

## Kết quả có thể chứng minh bằng CLI

- Commit source/test/version: `dd99cef` trên `origin/main`.
- Các follow-up đã push trực tiếp `main`:
  - `b78cd7f`: thực thi cả STT, OCR và STT+OCR qua `DubbingJobRunner`.
  - `9559245`: state-machine regression của media controls dùng component thật.
  - `dd99cef`: runtime Missing không còn khóa selector ngôn ngữ OCR.
- Test tree được reconfigure với `-DLASTUDIO_VERSION=0.0.2.9`.
- Full CTest với FFmpeg/FFprobe staged: **38/38 PASS, 0 FAIL, 0 SKIP**
  (40.07 giây).
- Graphify đã được cập nhật sau các sửa source.

## Package mới, không ghi đè 0.0.2.8

| Hạng mục | Giá trị xác minh |
| --- | --- |
| EXE | `out/LA-Studio-0.0.2.9/LA-Studio-0.0.2.9.exe` |
| FileVersion / ProductVersion | `0.0.2.9` / `0.0.2.9` |
| Original filename | `LA-Studio-0.0.2.9.exe` |
| SHA-256 | `A6A2999482CCA61256ABDE3AB94A5D9D419ED12B91EB26360CD7D326A3F3621E` |
| Kích thước EXE | 22,782,976 bytes |
| Inventory | 57 top-level files, 18 directories, 17 license files |
| Package verification | script xác minh 19 required artifacts và 18 license artifacts |
| Independent checks | `qwindows.dll`, `Qt6Multimedia.dll`, FFmpeg/FFprobe, eSpeak, VC redist và `subtitle-ocr/runtime-manifest.json` đều tồn tại |

eSpeak MSI được SHA-256 verify nhưng unsigned, vì vậy đây vẫn là **internal
build only**. Package không được chạy trong quá trình xác minh này.

## A–M: yêu cầu → bằng chứng → trạng thái

| Mục | Bằng chứng source/regression hiện tại | Trạng thái trung thực |
| --- | --- | --- |
| A–B | `SubtitleOcrPage.qml` dùng `ScrollView`/responsive grid; `QmlRouteSmoke` resize page thật ở 1024×720, 1280×800, 1600×900 và gọi `qmlSmokeLayoutCheck` kiểm bounds, overlap, reachability và scroll. | PASS offscreen; visual/thao tác chuột thật là manual. |
| C | `SubtitleOcrController` dùng shared Dubbing media staging/ingest; `TestMediaIngestService` và `importsSharedStagedMediaWithoutRedownloadAndPreservesSourceOnProbeFailure` kiểm direct/public adapter, cancel/retry/probe-fail/handoff. | PASS loopback; YouTube/TikTok/Douyin thật là manual. |
| D | Fix `dd99cef`: `languageSelector.enabled: !ocr.processing`; `Run Subtitle OCR` vẫn cần source+ROI+runtime+pack, language install vẫn chỉ enabled khi managed runtime available. QML smoke kiểm selector enabled và Run disabled lúc runtime Missing. | PASS offscreen/controller; cài runtime thật là manual. |
| E | ROI mapping dựa trên `displayedX/Y/Width/Height`; 8 handle; preset lower-region và full-frame reset được tách ở `e56cc77`; controller/pipeline/QML smoke regression. | PASS offscreen/unit; mapping video/HiDPI thật là manual. |
| F | OCR controller/media integration chạy với staged FFmpeg/FFprobe; package manifest/runtime inventory được xác minh. | PASS CLI/loopback; file picker/drag/drop/URL thực là manual. |
| G | `StudioRouteRegistry.homeFeatureCards` là nguồn chung; Home có 10 card, route 09 `media-download`, 10 `subtitle-ocr`; smoke kiểm card registry/route. | PASS offscreen; click visual thật là manual. |
| H | `DubbingJobRunner`/`DubbingTranscriptionJob`/shared `SubtitleOcrController`; `sttOnlyTranscriptDoesNotRequireOcrRuntime`, `ocrOnlyTranscriptUsesTheSharedSubtitleOcrController`, `combinedTranscriptRunsSttAndSharedOcrWithoutFallback`, `combinedTranscriptReportsOcrFailureWithoutSttFallback`. | PASS runner regression; Colab/OCR thật là manual. |
| I | `DubbingTranscriptFusionService` plus match/shift/conflict/persistence/reviewer tests and combined runner test. | PASS deterministic/unit+runner; review UI thao tác thật là manual. |
| J | Shared `MediaControlsAutoHide.qml`; `9559245` chạy schedule/reveal/expiry trên component thật cho leave, re-enter, drag, menu, focus, paused; delay vẫn 2000 ms. | PASS offscreen state machine; cảm nhận pointer/keyboard thật là manual. |
| K | `DubbingSubtitleService` và editor: SRT/VTT/ASS/SSA timing, TXT/MD không bịa timestamp, Unicode/style/persistence/burn-in route regressions. | PASS source/regression; burn-in MP4 thật là manual. |
| L | `DubbingTimingService`/controller: chain, exact boundary, gap, multi-speaker, subtitle shift, undo, cache invalidation; test `resolvesGlobalTimingConflictsWithRippleAndUndo`. | PASS deterministic regression; duration audio worker thật là manual. |
| M | `CapCutDraftExporter`/export dialog: structural draft có asset/track/text segment tách; missing asset fail trước publish; Unicode/style/ripple parser regression. | PASS structural only; **CapCut import thật chưa được tuyên bố PASS**. |

## Baseline A–G 0.0.2.7 đã được truy vết lại

- Source baseline: `7bb6b0b`.
- Commit report riêng: `6ed1e19badbfd4095e86b5559b2bde6fa7c992c1`
  (`docs/AI_AGENT_RESPONSE_REPORT.md`).
- Danh sách 14 file source baseline đã được ghi đầy đủ trong
  `docs/AI_AGENT_RESPONSE_REPORT_0.0.2.8.md`.

## Manual gates còn lại (không thể giả PASS)

1. Mở đúng EXE 0.0.2.9, resize/click/drag Subtitle OCR và Home ở ba kích
   thước nêu trên.
2. Khi runtime Missing: chọn language, choose/drag/import video và đặt ROI;
   xác nhận chỉ Run/install pack bị khóa. Sau đó cài runtime/pack và OCR video
   thật.
3. Chạy URL được phép, YouTube/TikTok/Douyin; kiểm cancel/retry và source cũ
   khi probe fail.
4. Chạy Colab workers exact-model thật cho STT/OCR, subtitle burn-in và Ripple
   với audio duration đo được.
5. Mở Editable CapCut Draft bằng CapCut thật và xác minh clip/text/track có
   thể chỉnh sửa.
