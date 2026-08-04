# Bao cao tong hop LA Studio

Cap nhat: 2026-08-04

## Baseline hien tai

| Muc | Trang thai |
| --- | --- |
| Latest packaged candidate | `0.0.2.24` |
| Artifact | `out/LA-Studio-0.0.2.24/LA-Studio-0.0.2.24.exe` |
| SHA-256 | `4254932A08D3FD2D44E2D924328FD9F67C0CCAF8EE48B3E1D1C2170A9FB32319` |
| Current source | `main` source commit `6ef65b8`; QML lint PASS, full CTest 39/39 PASS and package audit PASS |
| Distribution | Internal only; eSpeak MSI SHA-verified but unsigned |

## Batch 0.0.2.24: Automatic Dubbing eight-stage contract

- `0.0.2.22` is superseded and `0.0.2.23` is deliberately not accepted: its
  first package was produced before the interaction trace contained route/model/
  worker state transitions. It is preserved but is not release evidence.
- Automatic Dubbing now has one controller-owned presentation contract with
  exactly eight unique stages: Import/Download, Normalize, Isolator,
  Transcribe/STT, Alignment/Subtitle, Translate, TTS and Export/Output. QML
  consumes that aggregate contract; it does not hide or deduplicate internal
  nodes by title.

| Presentation stage | Production nodes retained internally |
| --- | --- |
| Import/Download | `media-input` |
| Normalize | `ingest` |
| Isolator | `source-separate` |
| Transcribe/STT | `transcribe` |
| Alignment/Subtitle | `review-transcript`, `fit-timing`, `review-conflicts` |
| Translate | `translate`, `review-translation` |
| TTS | `assign-voices`, `synthesize` |
| Export/Output | `mix`, `export` |

- Timing/Mix are no longer user cards or headers. Their production behavior
  remains in Alignment/Subtitle and Export/Output respectively. Normalize
  explicitly reports local preprocessing, effective source/working format and
  `No model required`; it is Ready only with valid source media. Model stages
  cannot report Ready from a template/default: Local uses the configuration
  resolver, API Gateway needs its own configured URL/key, and Direct Colab
  needs a verified exact capability/model worker.
- Configure stays inside the preflight wizard as an owned dialog. Route is
  selected before its picker, Save/Apply/Cancel preserve the wizard context,
  and Direct Colab stages alone appear on its worker page. Changing route or
  exact model clears the prior worker state; URL/token remain session-memory
  only.
- Regression evidence: QML lint PASS; targeted QML route/offscreen smoke 2/2
  PASS; full CTest **39/39 PASS** in 25.57 seconds. The controller readiness
  matrix covers missing/valid media, Local without/with resolved runtime-model,
  Direct Colab without/with an asynchronously verified exact worker. The
  production-shell trace contains 18 ordered events, including the exact
  `Local -> Direct Colab (2 workers) -> Local (0 workers)` route/model states.
- Portable internal package audit PASS: source/FileVersion/ProductVersion
  `0.0.2.24`, staging and license manifests (19 required artifacts), Qt
  platform/QML/Multimedia DLLs, FFmpeg/FFprobe, Tesseract/Paddle runtime and
  eSpeak runtime verified. No GUI or live Colab worker was opened; those remain
  manual acceptance gates.

## Batch 0.0.2.22: Dubbing Automatic repair and clean package

- `0.0.2.21` remains rejected for the user-reported Automatic dead-end. The
  current source repairs the gate-level root cause: preflight now offers real
  source Browse/URL import through the existing ingest controller, distinguishes
  source input from downstream blocking, removes no-op Configure controls and
  routes actionable Configure/Fix controls to production dialogs/pages.
- The production-shell offscreen interaction trace records 15 actions from
  Dubbing entry through Review, including the real Browse request, file-picker
  boundary injection, all displayed Configure buttons, no-Direct-Colab skip and
  Review/Fix. It is generated at
  `out/build/windows-msvc-tests/dubbing-qml-interaction-trace.json`.
- The `Subtitle OCR` smoke failure was a portable-test defect, not a failed URL
  route: the valid URL enabled Import, but the offscreen platform cannot own a
  native active window and therefore reports `activeFocus=false`. The test now
  verifies QML local `focus` (and still accepts desktop `activeFocus`), so it
  tests the real keyboard-focus contract without inventing OS focus.
- Targeted Dubbing regression, QML lint and full CTest are **39/39 PASS**.
  Portable internal package audit verified the staging and license manifests,
  FileVersion/ProductVersion `0.0.2.22`, Qt runtime, FFmpeg/FFprobe,
  Tesseract/Paddle OCR and eSpeak internal runtime. This is not a claim of
  manual desktop or live Colab acceptance.

## Batch 0.0.2.21: Dubbing entry gate and automatic setup

- Opening Dubbing now requires an explicit Automatic or Step-by-step choice.
  The modal cannot be escaped, clicked outside, or closed into the workspace;
  the only non-selection path leaves Dubbing. Existing project mode is saved
  without deleting transcripts, artifacts, node configuration, or resume state.
- Automatic opens a five-page setup wizard before the workspace: source and
  target language, stages/routes/models, Direct Colab workers, review, start.
  Language fields update persisted project SSoT and missing fields keep focus
  on the required control. Each stage card and review item show the actual
  route, model, variant, language, readiness, and block reason.
- Direct Colab setup is shown only for active Direct Colab nodes. Approval is
  single-use and is invalidated by source/target language, route, model,
  variant, media, or worker changes.

### Evidence 0.0.2.21

- Targeted Dubbing controller/QML regression: 5 passed, 0 failed. QML lint
  PASS. Full CTest: 39/39 PASS in 31.97 seconds.
- Portable package audit PASS: FileVersion/ProductVersion `0.0.2.21`, Qt
  Windows/offscreen plugins, RuntimeHost, FFmpeg/FFprobe, Tesseract, and a
  staged PaddleOCR health check (`ok=true`, `manifestVerified=true`) verified.

## Batch 0.0.2.20: Dubbing stage clarity and Voice Clone reference Isolator

- Dubbing now presents exactly nine production-backed UI stages: Import/Download,
  Normalize, Isolator, Transcribe/STT, Alignment/Subtitle, Translate, TTS,
  Timing/Mix and Export/Output. The presentation mapping retains durable
  workflow node IDs, project data and resume/rerun behavior.
- Alignment/Subtitle opens the existing subtitle editor and routes to Alignment
  Studio; it is not a placeholder. Voice Cloning has `Clean reference audio
  with Isolator`, reusing the existing Local or Direct Colab route/model.
- Enabled cleanup only exposes decoded Vocals to clone/train, preserves
  Original/Background for preview, caches by source/configuration fingerprint,
  and reports real progress/cancel/retry/error through Activity.

### Evidence 0.0.2.20

- QML lint PASS; targeted Dubbing, Colab separation and offscreen QML smoke:
  4/4 PASS. Full CTest: 39/39 PASS in 69.47 seconds.
- Package audit PASS: FileVersion/ProductVersion `0.0.2.20`, SHA above,
  staged Qt Windows/offscreen plugins, runtime host, FFmpeg/FFprobe,
  OCR/Tesseract, Paddle health (`ok=true`, `manifestVerified=true`) and
  third-party notices verified.

## Batch 0.0.2.19: Direct Colab / Dubbing preflight / Activity

- Direct Colab now carries an exact `capability + model + variant` contract.
  Every current exact-model notebook declares `variant: fixed`; Local CPU
  files/quantization are not presented as Colab configuration. Replacing URL,
  token, model or variant clears verification immediately.
- `ColabSessionStatus` is the shared Check/Disconnect/status surface for STT,
  TTS, cloning, design, alignment, separation, translation, OCR, LLM and
  Dubbing nodes. It shows the sanitized worker URL, capability/model/variant
  and verification time; token remains session memory only.
- Automatic Dubbing opens a preflight wizard (automatic or step-by-step,
  routes/models, only required workers, Check, Review, one-use approval).
  Any configuration change invalidates the approval.
- Voice Clone now uses a real keyboard-focusable consent checkbox, visible
  required-field reasons and responsive settings scroll. Shared settings
  toggles now have focus, descriptions, model metadata range/default hints and
  explicit Advanced affordance.
- Activity covers real long-running feature controllers. It shows route/model/
  variant/errors and renders `Working` when no measured counter exists;
  Dubbing counts only downloads started by that run, eliminating unrelated
  fixed 5%/8% reports.

### Root causes fixed

- The prior remote contract checked family/model but did not retain variant.
- QML offscreen smoke found an invalid chained `String.arg()` call and an
  unscoped `Repeater` index in the new preflight dialog; both were corrected.
- The package's pinned bsdtar dependency emitted a CMake developer warning
  that PowerShell treated as an error despite a successful exit code; the
  scoped third-party configure now suppresses only developer warnings and
  retains the exit-code gate.

### Evidence 0.0.2.19

- QML lint PASS.
- Targeted `TestRemoteExecution`, `TestDubbingProject`, and offscreen
  `QmlRouteSmoke`: 4/4 PASS.
- Generated notebooks: 32/32 PASS; exact controller/UI/notebook bindings:
  31/31 PASS.
- Full CTest: 39/39 PASS in 68.36 seconds.
- Portable package audit PASS: FileVersion/ProductVersion `0.0.2.19`, staged
  runtime manifest (19 required artifacts), Qt Windows/offscreen plugins,
  FFmpeg, eSpeak, Subtitle OCR/Paddle manifests and licenses present.

## Batch 0.0.2.18: Transcript source, STT/OCR reconciliation and package completion

- Dubbing Transcribe va Dubbing Direct Colab setup dung cung `transcriptConfiguration.transcriptSource`: STT only, OCR only, va STT+OCR. Route/model STT duoc persist rieng trong project va readiness sau reload dung route thuc te, khong doc template cu.
- Colab cards/check-all chi yeu cau worker cho nguon dang active va route Direct Colab. Nguon inactive hien `Not used`; Local khong block Colab setup. Cau hinh STT/OCR da luu duoc giu khi doi mode.
- Fusion giu STT/OCR text, confidence va provenance. Default `ask` de conflict pending; co prefer STT/OCR, batch resolution va manual final text. Conflict chua resolve chan Translate va workflow readiness.
- AI reconciliation chi duoc bat khi route co khai bao `supportsStructuredReconciliation`; M2M100/NLLB dich thuong khong duoc coi la LLM. Suggestion dung source language, timestamp/context/confidence, luon pending user accept/reject va khong ghi de evidence.
- `scripts/run_tests.ps1` dung CTest fixture thay vi goi aggregate EXE truc tiep. `scripts/package.ps1` tu dung runtime PaddleOCR da duoc controlled-prepared neu khong truyen override, roi van xac minh manifest/hash/health.

## Bang chung 0.0.2.18

- Targeted `TestDubbingProject`: **82 passed, 0 failed, 5 skipped**.
- Full CTest tren current transcript-reconciliation source: **39/39 PASS**, 64.21 giay, bao gom Subtitle OCR fixture, Remote Live Preflight contract va packaged QML smoke. Package configure sau do da truyen version `0.0.2.18` va artifact duoc audit rieng.
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
