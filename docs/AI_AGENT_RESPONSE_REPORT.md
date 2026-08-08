# AI agent response — OmniVoice Clone to TTS

Date: 2026-08-09

## Completed scope

This delivery fixes only the requested OmniVoice Voice Clone → TTS handoff and
builds a new internal portable package.

- **A visible name field is now beside the clone reference.** Enter a value in
  `Voice name for TTS reuse`, then run a successful Direct Colab clone. The
  name, reference audio and optional reference transcript are stored as a
  reusable OmniVoice voice. Selecting an existing saved reference also keeps
  its saved name. A failed or cancelled clone does not create a saved voice.
- **TTS recognises an active OmniVoice clone connection.** When the exact
  Direct Colab Voice Clone worker is verified as `omnivoice`, TTS selects the
  OmniVoice family configuration and no longer leaves the user blocked at the
  model gallery just because no Local model was installed.
- **The two Colab protocols remain deliberately separate.** The clone
  URL/token is not injected into the generic TTS worker and is never replaced
  with a Local model. This is required because the OmniVoice clone notebook
  exposes the profile/reference protocol, while generic TTS has a different
  route. No second notebook or GPU worker is started for reuse.
- **TTS now explicitly exposes reuse.** In `TTS Settings` → `Reuse cloned
  OmniVoice`, choose the saved voice, confirm permission, and press `Use
  cloned OmniVoice in TTS`. Generate then sends the saved reference audio and
  transcript to the verified existing Voice Clone worker, producing a real
  clone/profile request rather than a standard TTS request.

## Validation performed

- Rebuilt all changed QML files to Qt AOT C++ with MSVC 2022 in the test
  target. This catches QML syntax and type-generation faults without opening
  the GUI.
- Added and ran the focused regression
  `voiceCloneOmniVoiceIsReusableInTtsWithoutLocalFallback`: **3 passed,
  0 failed** (init, new regression, cleanup).
- Ran `graphify update .` after the source change.
- The internal package script rebuilt/staged version `0.0.2.31` and verified
  **19 required runtime/license artifacts**.

Stable-feature full CTest, visible desktop testing and live Colab testing were
not repeated for this delivery because the requested scope explicitly excluded
parts already stable. None are claimed as evidence here.

## Package

`C:\Users\Nguyen Trong Khoi\Downloads\LA-STUDIO\out\LA-Studio-0.0.2.31\LA-Studio-0.0.2.31.exe`

- FileVersion/ProductVersion: `0.0.2.31`
- SHA-256: `2146DA71119C3330914287A4C17D79C0149BFAB3CCAD5EA15C75EE73C631A995`
- Internal-only package. The eSpeak NG MSI is hash-verified but unsigned; it
  must not be promoted as a public distributable release.

## Source delivery

- Source commit on `main`: `4c00000 fix: reuse cloned omnivoice in tts`
- Pushed to `origin/main`.
