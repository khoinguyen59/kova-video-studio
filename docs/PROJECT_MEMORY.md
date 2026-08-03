# Tri nho du an LA Studio

Muc dich: luu quyet dinh san pham, loi da khoanh vung va trang thai nhat quan de agent khong lam lai viec cu. Khong chep patch hay log dai.

## Hien trang

- Desktop local-first: STT, TTS, Voice Cloning, Voice Design, Alignment, Translation, Dubbing, LLM Chat, Download va Subtitle OCR.
- API Gateway va Direct Colab la hai route doc lap. Token/URL Colab chi o memory session, khong persist vao project/report.
- Package noi bo moi nhat: `0.0.2.19` tai `out/LA-Studio-0.0.2.19/LA-Studio-0.0.2.19.exe`, SHA-256 `4960CC603BB67586E3BA506B7933830F802734CCBA9420081CBD3E0430D1F41D`. Source `main` tai `3debeab`; full CTest 39/39 va package audit PASS. Manual GUI/live Colab luon la gate rieng.

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
- Direct Colab cache profile saved voice trong session. API gateway khong thay the saved voice im lang.
- OCR ROI production duoc dua vao Dubbing preview cho OCR/STT+OCR, normalized/persisted/handoff, drag + 8 resize handles, crop preview va controls duoi video.
- Candidate 0.0.2.17 da audit version/hash, Qt platform, FFmpeg, PaddleOCR worker/model/manifest va health check; 39/39 CTest va packaged QML smoke PASS.

### 2026-08-02 - C4 saved voice local correction (source `1e05fb2`)

- Local Dubbing saved voice khong duoc clone lai theo segment. Chi Qwen3-TTS duoc phep vi native runtime co `crispasr_session_set_voice()` de dat va reuse profile theo signature trong session; backend clone-per-request bi chan truoc Run TTS voi huong dan chon Qwen3-TTS hoac Direct Colab.
- Internal saved-profile fields chi di qua Dubbing-to-TTS boundary, khong persist vao project/UI va duoc mask trong log. API Gateway va Direct Colab van tach biet; Direct Colab cache profile cu khong thay doi.
- Test targeted 6/6 va full CTest 39/39 PASS. Artifact `0.0.2.17` tao truoc correction khong bi ghi de va khong dai dien current source; can ten candidate moi duoc phep de package.

### 2026-08-03 - 0.0.2.18 transcript reconciliation

- `transcriptSource` la single source of truth cho STT only/OCR only/STT+OCR o Transcribe va Direct Colab setup. STT provider/model thuc te duoc persist/reload; cards, readiness va Colab validation chi danh gia nguon active va selected route.
- STT/OCR fusion luu day du evidence. Policy `ask` default giu conflict pending; prefer STT/OCR, batch/manual resolution, AI suggestion accept/reject deu preserve original evidence. Conflict unresolved chan Translate.
- AI reconciliation yeu cau capability structured explicit; model dich thuong khong duoc dung lam LLM va khong co fallback route im lang.
- OCR E2E production tren `1.mp4` PASS: 1125 Paddle frames, 430 matched Standalone/Dubbing cues, cache reuse, khong Tesseract fallback hay child-process leak; artifact o `out/ocr-e2e-new`.
- Package portable `0.0.2.18` audit FileVersion/ProductVersion, SHA, Qt/FFmpeg/PaddleOCR manifest-health va license/runtime inventory PASS. Package script tu resolve `out/paddle-ocr-runtime-ready` neu khong duoc truyen override, tranh loi empty binding.

### 2026-08-03 - 0.0.2.19 Direct Colab and workflow readiness

- Direct Colab session now verifies and retains exact capability, model and
  variant. All current exact-model notebooks advertise the fixed variant;
  Local CPU file variants do not leak into the Colab route. Replacing any
  connection/model/variant input invalidates the old verification.
- One shared Colab status component exposes Check connection, Disconnect,
  sanitized worker identity and verified timestamp across all Direct Colab
  studios and Dubbing nodes. Token/URL stay in session memory and are absent
  from project persistence, logs and handoff reports.
- Automatic Dubbing requires a configuration/review/check preflight and a
  fresh one-use approval. Step-by-step remains supported with the same route
  readiness rule.
- Voice Clone consent is a real focusable checkbox. Required controls show a
  local reason; settings are scrollable at narrow sizes. Shared toggles and
  metadata controls now make descriptions, ranges/defaults and Advanced state
  explicit.
- Activity uses real controller lifecycle and only measured progress. It no
  longer borrows unrelated download progress for Dubbing or displays invented
  5%/8% values. QML smoke caught and the source fixed an invalid status string
  formatter and missing Repeater index before packaging.
- Release validation: QML lint, targeted Colab/Dubbing/QML smoke, notebook
  contract 32/32, binding audit 31/31 and full CTest 39/39 PASS. Portable
  package `0.0.2.19` passed staged runtime/license audit. Pinned bsdtar
  packaging suppresses CMake developer warnings only; non-zero configure/build
  exit codes still fail packaging.

## Quy tac duy tri

- Doc `AI_AGENT_REQUEST.md` truoc task; sau task cap nhat response latest, summary cumulative va memory nay khi da co bang chung.
- Package mot candidate sau khi batch va full gate PASS; khong ghi de candidate cu.
- Bao cao ro automated/package PASS khac voi GUI/live service manual acceptance.
- Khi user dua evidence FAIL, dua dung loi do vao request va sua theo regression truoc package tiep theo.
