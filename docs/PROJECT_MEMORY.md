# Tri nho du an LA Studio

Muc dich: luu quyet dinh san pham, loi da khoanh vung va trang thai nhat quan de agent khong lam lai viec cu. Khong chep patch hay log dai.

## Hien trang

- Desktop local-first: STT, TTS, Voice Cloning, Voice Design, Alignment, Translation, Dubbing, LLM Chat, Download va Subtitle OCR.
- API Gateway va Direct Colab la hai route doc lap. Token/URL Colab chi o memory session, khong persist vao project/report.
- Package noi bo moi nhat: `0.0.2.17`; 39/39 CTest PASS va packaged QML offscreen smoke PASS. Manual GUI/live Colab luon la gate rieng.

## Lich su cap san pham

### Truoc 2026-07-30

- Xay dung cac studio cot loi va pipeline Dubbing, local/remote runtime host, subtitle/timing/export va CapCut Draft.

### 2026-07-30 den 2026-08-01

- Them exact-model Colab notebooks/routes, harden retry/tunnel/capability/model/progress, public media ingest va saved voice reuse.
- Hoan thien Subtitle OCR, Dubbing STT/OCR/STT+OCR fusion, subtitle editor/export/timing/CapCut Draft.
- Sua false-ready OCR language, frame extraction safe-end, ROI interaction va media controls; bundle Tesseract 5.5.1 va Direct Colab OCR.

### 2026-08-02 — 0.0.2.16 PaddleOCR baseline

- Bundle PaddleOCR 3.7.0 PP-OCRv6 tiny Local CPU: isolated Python, worker, model cache, manifest/license va package health check.
- Sua staging runtime, UTF-8 manifest BOM va license path; 39/39 CTest PASS.

### 2026-08-02 — 0.0.2.17 Dubbing batch

- Workflow Dubbing co metadata chung cho node role/resource va badge dynamic `Nen dung Colab`; UI/QML doc metadata cung controller/runtime thay vi hard-code theo ten model.
- Voice trong Dubbing thanh TTS. `ttsVoiceId` luu built-in/saved voice, migrate `cloneVoicePresetId` cu; saved preset duoc validate/loc compatibility. Voice Cloning authoring chi nam o studio doc lap.
- Direct Colab cache profile saved voice trong session. API gateway khong thay the saved voice im lang. Local engine dung managed saved reference; chua co persistent local profile primitive tach biet.
- OCR ROI production duoc dua vao Dubbing preview cho OCR/STT+OCR, normalized/persisted/handoff, drag + 8 resize handles, crop preview va controls duoi video.
- Candidate 0.0.2.17 da audit version/hash, Qt platform, FFmpeg, PaddleOCR worker/model/manifest va health check; 39/39 CTest va packaged QML smoke PASS.

## Quy tac duy tri

- Doc `AI_AGENT_REQUEST.md` truoc task; sau task cap nhat response latest, summary cumulative va memory nay khi da co bang chung.
- Package mot candidate sau khi batch va full gate PASS; khong ghi de candidate cu.
- Bao cao ro automated/package PASS khac voi GUI/live service manual acceptance.
- Khi user dua evidence FAIL, dua dung loi do vao request va sua theo regression truoc package tiep theo.
