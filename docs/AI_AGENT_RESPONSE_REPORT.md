# AI agent response — Voice Clone / Isolator completion

Date: 2026-08-09

## Result

Completed the requested Voice Clone and Voice Isolator repair batch.

- **Voice Clone reference cleanup is now independent.** The embedded
  `Clean reference audio with Isolator` card owns a dedicated Direct Colab
  separation session and controller. Its setup stays inside Voice Clone;
  Configure no longer redirects to the standalone Isolator page. A verified
  Spleeter worker can be connected there, run there, and its cached vocals WAV
  is passed directly into the clone request without another file picker or
  upload.
- **Standalone Isolator export is repaired.** Export now reads Qt's
  `selectedFile`, enforces a WAV extension, checks the controller result, and
  displays the exact saved path or a real failure. The prior code read a
  nonexistent dialog property and could silently send an empty destination.
- **Reference transcript is optional for Direct Colab cloning.** The UI and
  generated worker notebooks accept an empty `ref_text`; model-specific
  reference-audio-only/profile paths are used where supported. Local Qwen3
  keeps its exact-transcript requirement because that local runtime genuinely
  needs it. The target speech prompt remains required because it is the text
  to synthesize, not a tone/style option.
- **Packaging no longer stalls on Qt's redundant import scanner.** LA Studio
  uses shared Qt and `windeployqt --qmldir qml` for dynamic module deployment,
  so the CMake-only static-plugin scan is disabled with
  `QT_QML_MODULE_NO_IMPORT_SCAN`. A clean reconfigure completed in 3.8s and
  the release package configured/built successfully.

## Validation

- Rebuilt source/QML resources and `LAStudioUnitTests` with MSVC 2022.
- Focused suites: Voice Clone **8/8**, Remote Execution **37/37**, Direct
  Colab Separation **8/8**, Source Separation **8/8**, Studio Capabilities
  **10/10**.
- Full headless CTest: **39/39 passed** in 67.74 seconds, including the
  offscreen QML route smoke and packaging fixtures.
- Generated exact-model Colab notebooks: **32/32 verified**.
- The portable staged EXE completed its own offscreen QML smoke from the
  staged runtime layout. No visible GUI, browser, or live Colab worker was
  opened.

## Package result

New internal portable candidate:

`C:\Users\Nguyen Trong Khoi\Downloads\LA-STUDIO\out\LA-Studio-0.0.2.30\LA-Studio-0.0.2.30.exe`

- FileVersion/ProductVersion: `0.0.2.30`.
- SHA-256: `5C70D8194621DF613DC64EF6777C324D68D67C6D73121FB0ED2C0860F8C8F3EB`.
- Staging verification passed for 19 required runtime/license artifacts,
  including Qt Windows/offscreen plugins, FFmpeg, RuntimeHost, Subtitle OCR,
  PaddleOCR, Colab notebooks, and licenses.
- This is still an internal-only package: the eSpeak NG MSI is hash-verified
  but unsigned and must not be promoted as a public distributable release.

## Manual acceptance still required

Run the selected Spleeter notebook in Colab, paste its temporary URL/token
into the inline Voice Clone cleanup card, press **Run Isolator**, then clone.
Confirm the generated reference uses the displayed cached vocals path and
listen to the produced audio. This live Colab/service acceptance was not
claimed by the automated tests.
