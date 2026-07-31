# Báo cáo nghiệm thu kỹ thuật — LA Studio 0.0.2.8

Ngày: 2026-08-01
Phạm vi: đối chiếu lại toàn bộ A–M trong `docs/AI_AGENT_REQUEST.md`, sửa các
khoảng trống phát hiện được, chạy regression không tương tác GUI và stage bản
portable nội bộ 0.0.2.8.

## Kết luận ngắn

A–G không bị bỏ qua. Chúng đã có baseline source 0.0.2.7 (`7bb6b0b`) nhưng
được mở lại để đối chiếu theo yêu cầu mới. Audit này phát hiện hai thao tác ROI
khác nhãn nhưng cùng hành vi; đã sửa và thêm regression trong `e56cc77`.

H–M đã được tích hợp trực tiếp trên `main`; regression timing được bổ sung thêm
trong `6c0603c`. Sau khi đối chiếu lại yêu cầu, `b78cd7f` bổ sung regression
thực thi đủ ba route transcript ở `DubbingJobRunner`, thay vì chỉ kiểm thuật
toán fusion riêng lẻ. Full CTest ở source version 0.0.2.8: **38/38 PASS, 0
FAIL, 0 SKIP**. Package là **internal candidate**, không phải
GUI/live-service/CapCut acceptance hoàn tất: các mục manual ở cuối báo cáo vẫn
cần người dùng xác nhận.

## Commit và package

| Hạng mục | Bằng chứng |
| --- | --- |
| Baseline A–G | `7bb6b0b` — responsive Subtitle OCR workflow |
| H — transcript source/fusion | `dad56ec` |
| J — shared delayed media controls | `b837720` |
| K — Dubbing subtitle system | `8fd45ed` |
| L — timing conflict/ripple | `e02f9ea` |
| M — editable CapCut draft | `e26130e` |
| A/E follow-up ROI fix | `e56cc77` |
| L regression follow-up | `6c0603c` |
| H/I runner regression follow-up | `b78cd7f` — all three transcript source modes, shared OCR, no-fallback error |
| Source version commit | `03e7f79` (`LASTUDIO_VERSION=0.0.2.8`) |
| Stage path | `out/LA-Studio-0.0.2.8/LA-Studio-0.0.2.8.exe` |
| File/Product/Original version | `0.0.2.8` / `0.0.2.8` / `LA-Studio-0.0.2.8.exe` |
| SHA-256 | `81409C7A5A42515BCF71FC996B29DD769F7173DF4EE95EE09B9CDE02CDE48240` |
| Stage inventory | 57 top-level files, 18 runtime directories, 17 license files; package script also verified 19 required artifacts and 18 license artifacts |

Source, test và version commits đã được push trực tiếp `origin/main`; không
stage request/handoff, `VoiceLibraryDialog.qml`, Graphify, `.agents/`, `out/`
hay temporary logs.

## Bổ sung bằng chứng baseline A–G 0.0.2.7

Đây là các thông tin còn thiếu từ report 0.0.2.7, không phải lý do để coi A–G
là bị bỏ qua.

| Bằng chứng | Giá trị xác minh |
| --- | --- |
| Source commit A–G | `7bb6b0b` |
| Commit report A–G riêng | `6ed1e19badbfd4095e86b5559b2bde6fa7c992c1` (`docs/AI_AGENT_RESPONSE_REPORT.md`) |
| Regression đối chiếu lại ở 0.0.2.8 | Full CTest 38/38 PASS, 0 FAIL, 0 SKIP, 42.01 s với FFmpeg/FFprobe staged |

Danh sách đầy đủ file thuộc source commit `7bb6b0b`:

```text
CMakeLists.txt
qml/Main.qml
qml/Theme.qml
qml/components/shared/StudioRouteRegistry.qml
qml/pages/MediaDownloadPage.qml
qml/pages/SubtitleOcrPage.qml
qml/pages/WelcomePage.qml
src/controllers/subtitles/SubtitleOcrController.cpp
src/controllers/subtitles/SubtitleOcrController.h
tests/main.cpp
tests/test_SubtitleOcrController.cpp
tests/test_SubtitleOcrController.h
tests/test_SubtitleOcrRuntimeService.cpp
tests/test_SubtitleOcrRuntimeService.h
```

## Ma trận yêu cầu → source → kiểm chứng

| Mục | Source thực thi | Regression đã chạy | Kết quả và giới hạn |
| --- | --- | --- | --- |
| A–B Layout Subtitle OCR | `qml/pages/SubtitleOcrPage.qml`, `qml/Main.qml` | `TestSubtitleOcrRuntimeService`, `QmlRouteSmoke` | PASS offscreen: các card/source/runtime/settings/transcript được kiểm geometry, overlap, scroll/reachability tại 1024×720, 1280×800, 1600×900. Visual QA người dùng còn manual. |
| C Shared source media | `SubtitleOcrController`, `DubbingController`, `RemoteMediaImportService`, `MediaDownloadPage.qml` | `importsSharedStagedMediaWithoutRedownloadAndPreservesSourceOnProbeFailure`, `TestMediaIngestService` | PASS loopback: import, probe-fail giữ source cũ, cancel/retry và handoff không redownload. YouTube/TikTok/Douyin thật còn manual; không lưu URL vào OCR project. |
| D Runtime gating | `SubtitleOcrRuntimeService`, `SubtitleOcrController`, `SubtitleOcrPage.qml` | `blocksMissingManagedRuntimeWithoutSilentDownload`, `blocksMissingSelectedLanguageBeforeFrameExtraction`, `TestSubtitleOcrRuntimeService` | PASS: source/ROI vẫn dùng được khi Missing; Run và language install phụ thuộc runtime bị khóa. Cài runtime thật còn manual. |
| E ROI/preview | `SubtitleOcrPage.qml`, `SubtitleOcrController`, `SubtitleOcrPipeline` | `qmlRouteRoiAndManagedRuntimeControlsAreWired`, `keepsLowerRegionPresetSeparateFromFullFrameReset`, `QmlRouteSmoke` | PASS regression. Fix `e56cc77`: lower preset = vùng phụ đề mặc định; reset = full frame, không còn hai nút cùng hành vi. Mapping actual video/HiDPI cần manual. |
| F Controller/media test + package A–G | `tests/test_SubtitleOcrController.cpp`, `tests/test_MediaIngestService.cpp`, `scripts/package.ps1` | Full CTest 38/38 với staged FFmpeg/FFprobe | PASS source/loopback. Ba media cases được chạy, không còn SKIP. |
| G Home cards 09/10 | `StudioRouteRegistry.qml`, `WelcomePage.qml`, `Main.qml` | `qmlSmokeHomeCardsCheck`, `responsiveLayoutSharedMediaAndHomeCardsAreWired`, `QmlRouteSmoke` | PASS: 10 cards cùng registry/repeater, card 09 → `media-download`, 10 → `subtitle-ocr`. Visual/click thực tế manual. |
| H Transcript source STT/OCR/STT+OCR | `DubbingJobRunner.cpp`, `DubbingTranscriptionJob.cpp`, `DubbingController.cpp`, `DubbingPage.qml` | `sttOnlyTranscriptDoesNotRequireOcrRuntime`, `ocrOnlyTranscriptUsesTheSharedSubtitleOcrController`, `combinedTranscriptRunsSttAndSharedOcrWithoutFallback`, `combinedTranscriptReportsOcrFailureWithoutSttFallback`, `transcriptionRequiresReadyModel` | PASS runner regression: STT-only hoàn tất dù không inject OCR; OCR-only tái dùng shared controller; combined chạy cả remote-STT contract và OCR controller, fusion thành một segment có 2 provenance; OCR thiếu báo rõ lỗi và dừng, không fallback STT. Live STT/OCR/Colab còn manual. |
| I Deterministic transcript fusion | `DubbingTranscriptFusionService.cpp`, `DubbingProject.cpp`, `DubbingController.cpp`, `DubbingPage.qml` | `normalizesOcrOnlyTranscriptWithProvenance`, `fusesMatchingAndShiftedTranscriptWithoutDuplicates`, `combinedTranscriptRunsSttAndSharedOcrWithoutFallback`, `exposesConflictEvidenceWithoutSilentChoice`, `preservesFusionAndTranscriptSettingsAcrossProjectReload`, `reviewerMustResolveFusionConflictExplicitly` | PASS fixture + runner: STT giữ trục timing, OCR có thể đóng góp text, không duplicate ở case match/shift; conflict hiện 2 evidence/confidence và bắt reviewer chọn; persistence được kiểm. Không có LLM silent choice. |
| J Shared video controls | `MediaControlsAutoHide.qml`, `DubbingSourceMediaPanel.qml`, `SubtitleOcrPage.qml` | `qmlSmokeMediaControlsCheck`, `TestSubtitleOcrRuntimeService`, `QmlRouteSmoke` | PASS offscreen: shared 2000 ms behavior, pause/focus/drag guard. Cảm nhận thao tác chuột/keyboard thật manual. |
| K Dubbing subtitles | `DubbingSubtitleService`, `DubbingProject`, `DubbingSubtitleEditor.qml`, `DubbingSourceMediaPanel.qml` | `importsDubbingSubtitleFormatsWithoutInventingTiming`, `persistsDubbingSubtitleStyleAndExportsUnicodeAss`, `dubbingSubtitleUiWiresImportPreviewAndBurnIn`, `QmlRouteSmoke` | PASS: SRT/VTT/ASS/SSA preserve timing; TXT/MD không bịa timing; Unicode/style/project persistence. Burn-in MP4 thực tế manual. |
| L Global timing/ripple/undo | `DubbingTimingService`, `DubbingController`, `DubbingVoiceClipReview.qml` | `resolvesGlobalTimingConflictsWithRippleAndUndo`, `dubbingTimingUiWiresPreviewApplyAndUndo`, `QmlRouteSmoke` | PASS: multi-speaker chain, exact boundary + gap, subtitle/word shift, final no blocking conflict, undo, preview/export cache invalidation. Actual generated audio durations still require live workflow manual. |
| M Separate MP4/CapCut export | `CapCutDraftExporter`, `DubbingExportDialog.qml`, `DubbingController` | `exportsSelfContainedCapCutDraftWithUnverifiedImportStatus`, `dubbingExportUiSeparatesMp4AndEditableCapCutDraft`, `QmlRouteSmoke` | PASS structural: separated media/voice/text tracks, Unicode/style/ripple manifest, no publish if asset missing/no secret. **Actual CapCut import is explicitly unverified**; effects/blur/mask only appear as separate data when the project supplies them. |

## Lệnh và kết quả kiểm thử

```powershell
$env:LASTUDIO_FFMPEG = (Resolve-Path out\LA-Studio-0.0.2.7\media-tools\ffmpeg.exe).Path
$env:LASTUDIO_FFPROBE = (Resolve-Path out\LA-Studio-0.0.2.7\media-tools\ffprobe.exe).Path
ctest --test-dir out\build\windows-msvc-tests --output-on-failure
# 38/38 passed, 0 failed, 42.01 s
```

Test tree được reconfigure với `-DLASTUDIO_VERSION=0.0.2.8` trước full CTest.
`QmlRouteSmoke` chạy ứng dụng ở `QT_QPA_PLATFORM=offscreen`, thực sự load route
và resize các trang Dubbing/Subtitle OCR; nó không được dùng để tuyên bố visual
hoặc packaged-desktop PASS.

`b78cd7f` chỉ thay đổi test harness/regression; không thay đổi executable
source. Vì vậy package 0.0.2.8 giữ nguyên SHA ở trên và không bị build/ghi đè
lại chỉ để lấy một EXE khác tên nhưng cùng code runtime.

Package được tạo bằng:

```powershell
.\scripts\package.ps1 -Preset windows-msvc-release -QtRoot .tools\Qt\6.9.3 `
  -VcpkgRoot .deps\vcpkg -LlamaCppSourceDir .deps\llama.cpp -Version 0.0.2.8 `
  -SkipInstaller -PortableInternalLayout -AllowUnsignedEspeakForInternalBuild
```

Stage có `platforms/qwindows.dll`, `Qt6Multimedia.dll`, FFmpeg/FFprobe, eSpeak,
MSVC redistributable và OCR runtime manifest. eSpeak MSI là SHA-verified nhưng
unsigned; vì vậy package chỉ dùng nội bộ.

## Checklist manual còn bắt buộc

Mở đúng `out/LA-Studio-0.0.2.8/LA-Studio-0.0.2.8.exe` và xác nhận:

1. Resize Subtitle OCR và Home tại 1024×720, 1280×800, 1600×900; kiểm text,
   scroll, màu disabled, click card 09/10 và không có overlap.
2. Khi OCR runtime Missing, thử choose, drag/drop, Import link, preview/ROI;
   sau đó cài runtime/language thật và OCR một video thật.
3. Thử direct URL, YouTube, TikTok, Douyin bằng media được phép truy cập;
   kiểm cancel/retry và source cũ giữ nguyên khi lỗi.
4. Trong Dubbing, chạy audio thật để kiểm 2000 ms controls, STT/OCR/STT+OCR,
   subtitle burn-in, Ripple với duration worker đo được và export MP4.
5. Mở draft bằng CapCut thật, xác nhận text/track/audio có thể edit. Không coi
   CapCut import là PASS trước bước này.
6. Với các route GPU, chạy worker Colab exact-model thật và kiểm health,
   capabilities, CUDA, URL/token, wrong-model rejection và output thật.
