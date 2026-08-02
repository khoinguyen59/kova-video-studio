# Phan hoi AI agent — Dubbing/TTS/ROI va package 0.0.2.17

Ngay: 2026-08-02

## Ket qua

**PASS cho automated gates va package noi bo.** Candidate duy nhat cua batch nay:

`C:\Users\Nguyen Trong Khoi\Downloads\LA-STUDIO\out\LA-Studio-0.0.2.17\LA-Studio-0.0.2.17.exe`

- FileVersion/ProductVersion: `0.0.2.17`.
- SHA-256 EXE: `665B4FD4AD9C76774639466907AE46B553BCC713BCB3A309D97341E1EE2862C3`.
- Full CTest cuoi: **39/39 PASS**. `TestRemoteExecution` da duoc sua theo contract UI TTS moi; khong con bat Dubbing phai hien thi `voice-cloning`.
- Package audit PASS: `platforms/qwindows.dll`, FFmpeg/FFprobe, Qt DLL, eSpeak runtime, VC redist, Subtitle OCR va PaddleOCR runtime/model deu co trong candidate. Paddle worker `--health` tra `manifestVerified: true`.
- QML route smoke cua **EXE da dong goi** chay headless `QT_QPA_PLATFORM=offscreen` va exit `0`; khong mo hay dieu khien GUI nguoi dung.

## Ma tran Dubbing: vai tro, duong chay va tai nguyen

Metadata cua `DubbingWorkflowDefinition` la nguon chung cho workflow/header/inspector/controller; QML khong tu suy dien GPU tu ten model. Badge `Nen dung Colab` chi hien khi metadata cua route/model dang chon bao GPU-heavy/GPU-required.

| Buoc | Input -> output | Runner/thuc thi | Route duoc phep | Tai nguyen UI |
| --- | --- | --- | --- | --- |
| Import & Normalize | media -> working audio/media | `DubbingJobRunner`/FFmpeg | Local CPU | `CPU phu hop` |
| Separate speech | working media -> stems | `VoiceIsolatorController` | Local, API Gateway, Direct Colab | Colab khi model/route GPU-heavy |
| Transcribe | audio/ROI -> transcript | `DubbingTranscriptionJob`, STT va `SubtitleOcrController` | STT: Local/API/Colab; OCR: Local CPU/Direct Colab | Colab chi o STT/OCR model can GPU |
| Reconcile/alignment | STT+OCR -> transcript co provenance/conflict | fusion + optional alignment | Local/Direct Colab | Colab chi khi bat alignment GPU |
| Translate | source transcript -> target transcript | `DubbingTranslationJob` | Local, API Gateway, Direct Colab | badge theo model/route |
| TTS | target text + `ttsVoiceId` -> clips | `DubbingSynthesisJob` | Local, API Gateway, Direct Colab | Colab cho TTS GPU-heavy; API khong tu thay saved voice |
| Timing/Mix/Output | clips -> mix/video/draft | Dubbing runner + FFmpeg | Local CPU | `CPU phu hop` |

## Thay doi da thuc hien

- Ten node va panel sinh am thanh trong Dubbing da thanh **TTS / Text to Speech**. Dubbing khong con co dialog, setup card hay control tao Voice Clone; Voice Cloning Studio van doc lap.
- Selector **Giong noi** lay built-in voice cua TTS va saved preset tu `VoiceClonePresetService`. Muc saved hien ten, family, compatibility va tinh trang asset/checksum; preset thieu, hong hay khong tuong thich chan Run voi loi ro rang. Lua chon luu bang `ttsVoiceId`; project schema 12 migrate `cloneVoicePresetId` cu sang ID nay.
- Direct Colab giu profile cua saved voice trong bo nho theo preset/model/session va reuse giua cac segment. API Gateway tu choi saved voice khi route khong ho tro thay vi fallback im lang. Dubbing khong tu chon vocals/source media/reference audio.
- `DubbingNodeInspector.qml` va `DubbingNodeSettingsPanel.qml` hien vai tro node, resource badge va chuoi chinh xac **`Nen dung Colab`** theo metadata. Notebook link dung model hien chon.
- Dubbing OCR/STT+OCR dung ROI normalized production cua `SubtitleOcrController`: overlay bam `VideoOutput.contentRect`, co drag inside + 8 resize handles, clamp, persist/reload qua `transcriptConfiguration`, preset/reset/preview crop nam duoi video. STT an ROI va khong dung ROI.
- Transcribe selector va Colab setup dung cung `transcriptConfiguration.transcriptSource`: Chi STT, Chi OCR, STT + OCR. Worker/check selected chi ap dung cac source dang chon.

## Kiem thu da chay

Targeted PASS:

- `TestDubbingProject`, `TestWorkflowGraph`, `TestSubtitleOcrPipeline`, `PrepareSubtitleOcrFrameRuntime`, `TestSubtitleOcrController`, `TestSubtitleOcrRuntimeService`, `TestColabTtsRunner`, `TestColabVoiceCloneRunner`, `PrepareQmlRouteSmokeRuntime`, `QmlRouteSmoke`.
- `TestRemoteExecution` PASS sau khi cap nhat assertion cu bat buoc chuoi `voice-cloning` trong Dubbing inspector.

Full gate PASS mot lan cuoi sau targeted: **39/39 CTest**. Graphify da duoc cap nhat sau sua source.

## Files chinh da thay doi

- `CMakeLists.txt`
- `src/dubbing/DubbingProject.*`, `src/dubbing/workflow/DubbingWorkflowDefinition.cpp`, `DubbingWorkflowNodes.cpp`
- `src/controllers/dubbing/DubbingController.*`, `DubbingSynthesisJob.*`, `DubbingColabModelRoutes.h`
- `src/controllers/shared/VoiceClonePresetService.*`, `src/controllers/app/WorkflowActivityManager.*`
- `qml/components/dubbing/DubbingNodeInspector.qml`, `DubbingNodeSettingsPanel.qml`, `DubbingColabSetupDialog.qml`, `DubbingSourceMediaPanel.qml`, `qml/pages/DubbingPage.qml`
- `tests/test_DubbingProject.cpp`, `tests/test_WorkflowGraph.cpp`, `tests/test_RemoteExecution.cpp`

## Gioi han nghiem thu con lai

Automated/package PASS khong thay the nghiem thu thu cong. Chua co bang chung thao tac chuot ROI/letterbox tren desktop that, ket noi Colab Internet that, hay chat luong audio/video dau ra that. Khong co GUI nguoi dung nao da duoc mo hay dieu khien trong batch nay.

Local engine hien dung saved managed reference trong ham synthesis cua engine; khong co local persistent zero-shot voice-profile API doc lap de chung minh strict “profile khong tai tao” nhu Direct Colab. Vi vay behavior local saved voice nay can duoc nghiem thu thu cong/engine-specific truoc khi cong bo nhu mot dam bao profile-runtime. Dubbing van khong hien thi hay yeu cau user tao/cau hinh clone.
