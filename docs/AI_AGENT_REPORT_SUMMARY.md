# Bao cao tong hop LA Studio

Cap nhat: 2026-08-02

## Baseline hien tai

| Muc | Trang thai |
| --- | --- |
| Latest packaged candidate | `0.0.2.17` |
| Artifact | `out/LA-Studio-0.0.2.17/LA-Studio-0.0.2.17.exe` |
| SHA-256 | `665B4FD4AD9C76774639466907AE46B553BCC713BCB3A309D97341E1EE2862C3` |
| Automated suite | 39/39 CTest PASS; packaged QML offscreen smoke PASS |
| Distribution | Internal only; eSpeak MSI SHA-verified but unsigned |

## Batch 0.0.2.17: Dubbing ro rang resource, TTS va OCR ROI

- Workflow Dubbing dung metadata runtime-backed cho vai tro node, route va resource badge. GPU-heavy/GPU-required hien dung `Nen dung Colab`; node CPU khong bi gan canh bao chung chung.
- Dubbing Voice duoc doi thanh TTS: chon built-in hoac saved Voice Cloning preset, persist `ttsVoiceId`, migrate project cu, chan preset missing/invalid/incompatible va khong co UI tao clone trong Dubbing.
- Direct Colab saved voice reuse profile theo session; API Gateway va Colab van doc lap. Khong co fallback im lang va khong luu URL/token vao project/report.
- Dubbing OCR/STT+OCR dung ROI normalized that cua Subtitle OCR, 8 handles, letterbox-aware content rect, preview crop, persistence/handoff; STT an ROI.
- PaddleOCR 0.0.2.16 baseline duoc giu nguyen. Candidate 0.0.2.17 stage Qt, FFmpeg, eSpeak va bundled PaddleOCR 3.7.0 PP-OCRv6 tiny; health check manifest PASS.

## Bang chung

- Targeted Dubbing/workflow/OCR/Colab/QML tests PASS.
- Full CTest sau cung: **39/39 PASS**.
- EXE audit: FileVersion/ProductVersion `0.0.2.17`, SHA-256 o tren, `platforms/qwindows.dll`, FFmpeg/FFprobe, Paddle worker/runtime/model/manifest deu co; worker health `manifestVerified=true`.
- Packaged EXE QML route smoke headless PASS. Khong mo GUI nguoi dung.

## Manual acceptance con mo

- Desktop ROI pointer/resize voi video letterbox/rotation that, file picker va responsive UI.
- Live Direct Colab worker/notebook/tunnel va audio/video output that.
- Local saved-voice engine semantics: source co durable asset, nhung chua co persistent local voice-profile API de khang dinh strict no-reclone nhu Direct Colab.

## Quy uoc tai lieu

- `AI_AGENT_REQUEST.md`: chi yeu cau chua xong do nguoi dung/agent cap nhat.
- `AI_AGENT_RESPONSE_REPORT.md`: chi report moi nhat.
- `PROJECT_MEMORY.md`: lich su cap san pham ngan gon.
- Bang chung chi tiet cu tra trong Git history; khong tao report theo version rieng.
