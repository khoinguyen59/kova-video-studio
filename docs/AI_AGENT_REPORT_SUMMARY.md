# Bao cao tong hop LA Studio

Cap nhat: 2026-08-03

## Baseline hien tai

| Muc | Trang thai |
| --- | --- |
| Latest packaged candidate | `0.0.2.18` |
| Artifact | `out/LA-Studio-0.0.2.18/LA-Studio-0.0.2.18.exe` |
| SHA-256 | `2FC962F21721E285927ADD7F6201A11FA9DA46D10242BA4787A4015F02CD8381` |
| Current source | `main` commit `3db38db`; full CTest 39/39 PASS and package audit PASS |
| Distribution | Internal only; eSpeak MSI SHA-verified but unsigned |

## Batch 0.0.2.18: Transcript source, STT/OCR reconciliation and package completion

- Dubbing Transcribe va Dubbing Direct Colab setup dung cung `transcriptConfiguration.transcriptSource`: STT only, OCR only, va STT+OCR. Route/model STT duoc persist rieng trong project va readiness sau reload dung route thuc te, khong doc template cu.
- Colab cards/check-all chi yeu cau worker cho nguon dang active va route Direct Colab. Nguon inactive hien `Not used`; Local khong block Colab setup. Cau hinh STT/OCR da luu duoc giu khi doi mode.
- Fusion giu STT/OCR text, confidence va provenance. Default `ask` de conflict pending; co prefer STT/OCR, batch resolution va manual final text. Conflict chua resolve chan Translate va workflow readiness.
- AI reconciliation chi duoc bat khi route co khai bao `supportsStructuredReconciliation`; M2M100/NLLB dich thuong khong duoc coi la LLM. Suggestion dung source language, timestamp/context/confidence, luon pending user accept/reject va khong ghi de evidence.
- `scripts/run_tests.ps1` dung CTest fixture thay vi goi aggregate EXE truc tiep. `scripts/package.ps1` tu dung runtime PaddleOCR da duoc controlled-prepared neu khong truyen override, roi van xac minh manifest/hash/health.

## Bang chung 0.0.2.18

- Targeted `TestDubbingProject`: **82 passed, 0 failed, 5 skipped**.
- Full CTest tren source 0.0.2.18: **39/39 PASS**, 64.21 giay, bao gom Subtitle OCR fixture, Remote Live Preflight contract va packaged QML smoke.
- OCR production code-only E2E tren `C:/Users/Nguyen Trong Khoi/Downloads/1.mp4`: **PASS** trong 599867 ms; PaddleOCR 3.7.0 PP-OCRv6 tiny, 1125 sampled/recognized frames, 430 cues o ca Standalone va Dubbing, Dubbing reuse cache, Tesseract fallback = false, child processes alive = 0. Artifact: `out/ocr-e2e-new/standalone-zh-Hans.srt`, `dubbing-zh-Hans.srt`, `transcript-zh-Hans.txt`, `OCR_TEST_RESULT.md`.
- Package audit: FileVersion/ProductVersion `0.0.2.18`; 26 required runtime/license artifacts co mat, bao gom `platforms/qwindows.dll`, FFmpeg/FFprobe va isolated PaddleOCR runtime. Worker health `ok=true`, `manifestVerified=true`.
- Khong mo GUI. Live Direct Colab worker/notebook/tunnel va manual desktop acceptance van **chua xac minh**.

## Batch 0.0.2.17: Dubbing ro rang resource, TTS va OCR ROI

- Workflow Dubbing dung metadata runtime-backed cho vai tro node, route va resource badge. GPU-heavy/GPU-required hien dung `Nen dung Colab`; node CPU khong bi gan canh bao chung chung.
- Dubbing Voice duoc doi thanh TTS: chon built-in hoac saved Voice Cloning preset, persist `ttsVoiceId`, migrate project cu, chan preset missing/invalid/incompatible va khong co UI tao clone trong Dubbing.
- Direct Colab saved voice reuse profile theo session; API Gateway va Colab van doc lap. Khong co fallback im lang va khong luu URL/token vao project/report.
- C4 da duoc sua trong source `1e05fb2`: Local Dubbing saved voice chi chay tren Qwen3-TTS persistent profile, goi native `crispasr_session_set_voice()` mot lan cho moi profile signature va sau do goi TTS cho moi segment. Runtime Local clone-per-request bi chan ro rang; khong clone lai theo segment.
- Dubbing OCR/STT+OCR dung ROI normalized that cua Subtitle OCR, 8 handles, letterbox-aware content rect, preview crop, persistence/handoff; STT an ROI.
- PaddleOCR 0.0.2.16 baseline duoc giu nguyen. Candidate 0.0.2.17 stage Qt, FFmpeg, eSpeak va bundled PaddleOCR 3.7.0 PP-OCRv6 tiny; health check manifest PASS.

## Bang chung

- Targeted Dubbing/workflow/OCR/Colab/QML tests PASS.
- Full CTest sau sua C4: **39/39 PASS**; targeted Dubbing/validator/Colab/QML: **6/6 PASS**.
- EXE audit cua artifact lich su `0.0.2.17`: FileVersion/ProductVersion `0.0.2.17`, SHA-256 o tren, `platforms/qwindows.dll`, FFmpeg/FFprobe, Paddle worker/runtime/model/manifest deu co; worker health `manifestVerified=true`.
- Khong ghi de artifact `0.0.2.17`; do ten candidate nay da ton tai truoc C4, current source chua co package moi. Khong mo GUI nguoi dung.

## Manual acceptance con mo

- Desktop ROI pointer/resize voi video letterbox/rotation that, file picker va responsive UI.
- Live Direct Colab worker/notebook/tunnel va audio/video output that.
- Live Qwen3 saved-voice audio/profile reuse va Direct Colab worker that van can manual acceptance; automated source regression PASS nhung khong thay the live audio acceptance.

## Quy uoc tai lieu

- `AI_AGENT_REQUEST.md`: chi yeu cau chua xong do nguoi dung/agent cap nhat.
- `AI_AGENT_RESPONSE_REPORT.md`: chi report moi nhat.
- `PROJECT_MEMORY.md`: lich su cap san pham ngan gon.
- Bang chung chi tiet cu tra trong Git history; khong tao report theo version rieng.
