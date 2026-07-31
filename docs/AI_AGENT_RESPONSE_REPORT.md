# Báo cáo hoàn tất và kiểm chứng lại — LA Studio 0.0.2.5

Ngày kiểm chứng: 2026-07-31

Phạm vi đã đối chiếu: toàn bộ mục B–K trong `docs/AI_AGENT_REQUEST.md`. File yêu cầu hiện tại không có yêu cầu sản phẩm mới sau baseline 0.0.2.5; vì vậy vòng này không sửa source chỉ để tạo commit mới. Thay vào đó, report này kiểm chứng lại source, test, package và `origin/main` trước khi xác nhận hoàn tất.

## Trạng thái Git và phiên bản

- Làm trực tiếp trên `main`; `main` và `origin/main` cùng trạng thái tại thời điểm kiểm chứng (`0 ahead`, `0 behind`).
- Source version tại `CMakeLists.txt` là `0.0.2.5`; basename EXE được lấy từ chính version này là `LA-Studio-0.0.2.5.exe`.
- Các thay đổi source chính đã có trên `main`: `85fb172`, `0a40fbe`, `c5e3ca5`, `9fc530b`, `a2bea3d`, `5ba9dcb`, `5d8fdc1`, `3ab1fec`, `b0c87a4`.
- Các file local do người dùng/agent khác đang sửa hoặc sinh ra vẫn không được stage/commit: request/handoff, `VoiceLibraryDialog.qml`, `.agents/`, `graphify-out/`, `out/` và các file untracked đã có.

## Ma trận tái sử dụng voice

| Đường dùng lại | Selector và dữ liệu bền vững | Request synthesis thực tế | Invalidation/chặn lỗi | Bằng chứng kiểm thử | Kết luận |
| --- | --- | --- | --- | --- | --- |
| Saved reference/clone voice → TTS | `VoiceClonePresetService`; lưu ID, `familyId`, audio reference được app quản lý, transcript, hash/size. Sau khi tạo lại service, selector cùng family nạp lại ID và file tham chiếu. Không lưu worker URL, token hoặc `profile_id`. | `ColabVoiceCloneController` tạo profile tạm từ reference rồi gửi generation với `profile_id`, exact model và language. Hai câu có cùng reference/model dùng cùng profile. | Đổi transcript, language, file reference, preset, exact model hoặc session làm profile tạm bị bỏ và tạo lại. Thiếu/không hợp lệ không fallback. | `TestColabVoiceCloneRunner::controllerReusesProfileOnlyForMatchingDurableReference`; `savedPresetSurvivesRestartAndInvalidatesTemporaryProfile`. | PASS ở service/controller/loopback protocol. Live Colab GPU: BLOCKED. |
| Saved reference/clone voice → Dubbing | `DubbingController::cloneVoicePresets` và `selectCloneVoicePreset`; project chỉ giữ durable preset selection. Mở lại project nạp lại ID qua cùng `VoiceClonePresetService`. | `DubbingSynthesisJob` gửi profile request với exact clone model + transcript, rồi dùng cùng profile cho hai segment. Kết quả segment giữ preset ID, owned reference path và transcript. | Đổi model tạo profile mới; đổi session giữa job fail/cancel rõ ràng rồi reconnect tạo profile mới. Family khác, preset xóa/mất hoặc preset rỗng bị chặn; không dùng TTS local/ngẫu nhiên thay thế. | `dubbingDirectColabVoiceCloneReusesProfileAcrossSegments`; `audioGenerationUsesSavedCloneVoiceForEverySegment`; `cloneVoicePresetSelectionPersistsAndMissingPresetBlocks`; `zeroCloneVoicePresetBlocksSynthesisWithoutFallback`. | PASS ở controller/job/loopback protocol. Live Colab GPU: BLOCKED. |
| Saved voice-design preset → TTS | `VoiceDesignPresetService`; lưu ID, exact family và design description. Tạo lại service vẫn nạp lại cùng ID/mô tả. WAV xuất chỉ là audio export, không bị gọi là preset/model/profile. | `ColabVoiceDesignRunner` gửi POST `/v1/audio/voice_designs` với `model`, `input`, `voice_description`, `style`, `language`, `temperature`, `seed`; không gửi `profile_id` hoặc `ref_audio`. | Đổi model chọn đúng notebook/worker của family và bỏ session cũ; remote-first không lén chuyển sang gateway/local. | `TestColabVoiceDesignRunner::testPostsIndependentVoiceDesignContract`; `exactModelMappingMatchesCatalogAndNotebooks`; `TestRemoteExecution::remoteFirstVoiceDesignStaysDirectWhenAColabSessionIsAvailable`. | PASS ở service/runner/loopback. Live Colab GPU và chất lượng voice: BLOCKED. |
| Saved voice-design preset → Dubbing | Không có Dubbing voice-design synthesis worker tương thích trong source. | Không có request được gửi và không có giả lập fallback. | `DubbingNodeInspector.qml` vô hiệu hóa lựa chọn với giải thích rằng node chỉ nhận exact TTS voice hoặc saved reference voice; không coi WAV export là design preset. | `TestDubbingProject::dubbingUiUsesExactModelWorkers` kiểm tra thông báo và cấm silent substitution. | SUPPORTED = NO, được hiển thị rõ/chặn đúng theo yêu cầu thay vì báo PASS giả. |

Root cause đã được sửa ở clone reuse là cache profile tạm không mang đủ dấu hiệu của exact model/reference và đường Dubbing thực chưa phủ bằng job thật. `DubbingSynthesisJob` nay bao gồm exact clone model trong cache signature và test chạy job thật. `profile_id` vẫn chỉ sống trong memory của worker session, không ghi vào preset/project/settings.

## Voice Studio UI và Colab model binding

- Voice Cloning Studio dùng thuật ngữ `reference voice` / `saved reference voices` / `save reference`; audio WAV đã sinh chỉ là export.
- Voice Design Studio dùng `voice-design preset`; chọn model map tới notebook exact-model tương ứng, không dùng notebook chung sai model.
- Test static + parsed notebook kiểm tra ba Voice Design family `omnivoice`, `qwen3-tts-1.7b-voicedesign`, `voxcpm2`, metadata `family_id`, `MODEL_ID`, CUDA guard, endpoint capability và URL/token session riêng. Gateway không xuất hiện trong contract notebook Voice Design.
- Đây là bằng chứng source/loopback, không phải click-through GUI hoặc live notebook acceptance.

## Public media adapter dùng chung

Một `RemoteMediaImportService` phục vụ cả tab Download và Dubbing; `Use in Dubbing` handoff staged file cho `MediaIngestService`, không tải lần hai.

- Hỗ trợ direct HTTPS media và trang public YouTube, TikTok, Douyin, bao gồm `v.douyin.com` short link qua resolver. Chỉ lấy một video (`--no-playlist`).
- Resolver gọi `yt-dlp` bằng `QProcess` argument list, với `--` trước URL không tin cậy; không ghép shell. Test chứng minh URL có `--output=...` chỉ là positional argument.
- Cấm cookie, user-info, login, DRM/paywall bypass; direct HTTP chỉ chấp nhận loopback phục vụ test.
- Kiểm tra cả URL gốc và redirect: HTTPS, DNS/literal private address, scheme, size 2 GiB, timeout, cancel, malformed/multiple resolver output và non-zero exit. File dở được dọn, progress chỉ dùng byte thực; URL/query không bị persist vào project.
- Trước khi thay project, media được ffprobe/normalize; probe fail giữ project cũ và staged file retryable.
- UI tách `Download` khỏi popup model/runtime Downloads và hiện rõ phạm vi direct file/YouTube/TikTok/Douyin cùng các điều cấm.

`TestMediaIngestService` chạy adapter fixture cho YouTube, TikTok, Douyin và short link Douyin; có các regression unsafe redirect, private address, multiple output, timeout/retry, injection, handoff không redownload, Unicode/staging và failed probe giữ project cũ. Kết quả suite: **18 passed, 0 failed**.

Không có cookie/profile/login thật và không có kiểm thử website live. Vì vậy khả năng tải thật tại YouTube/TikTok/Douyin được ghi là **BLOCKED**, không được suy từ fixture thành PASS live-site.

## Subtitle OCR (khác STT và import SRT)

Audit Graphify/source xác nhận trước thay đổi không có luồng trích phụ đề cháy/hardcoded từ ảnh video. STT từ audio và import subtitle track không được coi là OCR. Source hiện có route riêng `Subtitle OCR` và không tạo pipeline trùng với Subtitle Voice.

### Lựa chọn công nghệ

| Công nghệ | Repo chính thức / license | Nhận định cho package Qt/C++ offline | Quyết định |
| --- | --- | --- | --- |
| VideoSubFinder | [SWHL/VideoSubFinder](https://github.com/SWHL/VideoSubFinder) — GPL-2.0 | Có video text detection/extraction nhưng GPL-2.0 không phù hợp để nhúng vào package hiện tại. | Loại. |
| PaddleOCR | [PaddlePaddle/PaddleOCR](https://github.com/PaddlePaddle/PaddleOCR) — Apache-2.0 | Nhận dạng đa ngôn ngữ mạnh nhưng runtime/model/Python lớn, không hợp portable package hiện tại. | Không bundle. |
| RapidOCR | [RapidAI/RapidOCR](https://github.com/rapidai/rapidocr) — Apache-2.0 | Có hướng ONNX/cross-platform, nhưng vẫn cần packaging model/runtime riêng và chưa có integration C++ đã kiểm thử trong app. | Không bundle. |
| Tesseract | [tesseract-ocr/tesseract](https://github.com/tesseract-ocr/tesseract) — Apache-2.0 | CLI ổn định, offline, có traineddata đa ngôn ngữ; hợp adapter QProcess/FFmpeg CPU và kiểm soát dependency rõ ràng. | Chọn runtime ngoài package. |
| EasyOCR | [JaidedAI/EasyOCR](https://github.com/JaidedAI/EasyOCR) — Apache-2.0 | Python/PyTorch/model nặng so với mục tiêu package CPU desktop hiện tại. | Không bundle. |

License và trạng thái maintained của các repo được kiểm tra lại từ GitHub. Tesseract mô tả engine Apache-2.0, dòng v5 và traineddata ngôn ngữ; RapidOCR/PaddleOCR là lựa chọn Apache-2.0 có thể xem lại nếu sau này chọn một runtime model-managed lớn hơn. VideoSubFinder bị loại vì GPL-2.0, không chỉ vì số sao.

### Luồng đã triển khai

- `SubtitleOcrPipeline`: ROI normalized → source frame, FFmpeg crop arguments, sample timestamps, parse TSV Tesseract Unicode/multiline, filter confidence, dedup/merge observation và SRT timing.
- `SubtitleOcrRuntimeLocator`: tìm `subtitle-ocr/tesseract.exe` cạnh EXE, `LASTUDIO_TESSERACT`, rồi `PATH`. Không có auto-download runtime/model.
- `SubtitleOcrController`: `QProcess` cho ffprobe/FFmpeg/Tesseract với argument list; probe media, preview crop, kiểm tra language bằng `--list-langs` trước run, progress từ frame sample đã xử lý, cancel/retry, cleanup workspace app-owned, hash crop để tránh OCR trùng, save/open `.laocr.json`, export SRT/text.
- `SubtitleOcrPage.qml`: drag/drop/chọn video, MediaPlayer seek/timeline, ROI overlay được chuẩn hóa theo source video để không lệch letterbox/HiDPI, 8 handle resize/move, reset, preview crop, language/sample/confidence, review/edit/delete segment, save/open/export và gửi tiếp.
- `sendToSubtitleVoice` tạo SRT tạm rồi gọi import SRT native; `sendToDubbing` thay transcript segments native chỉ sau khi validate toàn bộ segment, ghi `timingSource=subtitle-ocr` và confidence.

### Regression OCR

`TestSubtitleOcrPipeline` có ROI 1920×1080, crop argument, sample time không duplicate cuối, merge/dedup confidence, TSV multiline Việt/Trung/Nhật/Hàn và SRT. `TestSubtitleOcrController` có runtime thiếu không download, language thiếu, source probe fail không làm mất source cũ, fake tool async/crop/review/export/open project, integration Subtitle Voice/Dubbing, cancel/retry/cleanup và QML route smoke. Kết quả: **8 + 8 passed, 0 failed**.

Chất lượng OCR trên video thật và các ngôn ngữ thật vẫn **BLOCKED** vì Tesseract/traineddata không được tự tải hoặc bundle và phiên này không chạy GUI/video người dùng. Không biến fixture/mock thành đánh giá độ chính xác OCR thật.

## Kiểm chứng và package

Lệnh kiểm chứng lại sau cùng:

```powershell
ctest --test-dir out\build\windows-msvc-tests --parallel 4 --output-on-failure
```

Kết quả hiện tại: **37/37 passed, 0 failed**, 9.70 giây. Bao gồm `TestSubtitleOcrController`, `TestSubtitleOcrPipeline`, `TestMediaIngestService`, `TestDubbingProject`, `TestColabVoiceCloneRunner`, `TestColabVoiceDesignRunner`, `TestRemoteExecution` và `QmlRouteSmoke`.

Graphify incremental update đã chạy sau source change OCR: 11,445 nodes, 22,176 edges, 551 communities. Graph data là generated/local và không được commit.

Portable internal package đã được kiểm tra tĩnh, không mở EXE hay điều khiển máy:

| Hạng mục | Kết quả |
| --- | --- |
| Artifact | `out/LA-Studio-0.0.2.5/LA-Studio-0.0.2.5.exe` |
| File/Product version | `0.0.2.5` |
| SHA-256 | `513090A3DEB30A84E6D1DA6715E786A5303F5991AC0FC6B811AE8E184C59429E` |
| Staging manifest | 19/19 artifact bắt buộc |
| License manifest | 18/18 artifact bắt buộc |
| Subtitle OCR package | `subtitle-ocr/README.txt`, `runtime-manifest.json`, `licenses/tesseract/RUNTIME-NOTICE.md` có mặt |
| OCR manifest | `bundled=false`, `automaticDownload=false`, `license=Apache-2.0` |
| Media runtime | `media-tools/ffmpeg.exe` và `media-tools/ffprobe.exe` có mặt |

`b0c87a4` sửa pin SHA-256 yt-dlp bằng checksum release upstream đã xác minh; không hạ/tắt checksum để đóng gói qua lỗi. Package là **internal-only** vì eSpeak NG MSI dù SHA-256 hợp lệ vẫn không ký số; không dùng artifact này làm bản public distribution.

## Giới hạn còn được nêu trung thực

- Không live Colab GPU/notebook/token; không kết luận inference GPU hay chất lượng voice thật.
- Không live public website; không kết luận YouTube/TikTok/Douyin tải thật ngoài fixture.
- Không có đánh giá OCR thật trên media của người dùng do runtime/traineddata không bundle.
- Không mở EXE/browser/GUI trong audit, nên UI được kiểm chứng bằng route smoke/source wiring chứ không ghi là click-through acceptance.

## Theo dõi yêu cầu mới

Automation `C:\Users\Nguyen Trong Khoi\.codex\automations\check-la-studio-request-after-completion\automation.toml` đang active. Sau khi report này được commit/push và không còn task đang chạy, nó kiểm tra `docs/AI_AGENT_REQUEST.md` mỗi 30 phút. Chỉ khi yêu cầu thay đổi nó mới bắt đầu một vòng công việc mới; không mở GUI, browser hoặc EXE trong lần kiểm tra tự động.
