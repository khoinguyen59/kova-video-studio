# Model Selection Freeze — Live Root Cause and Fix (0.0.0.8)

## Scope

This report covers the freeze reproduced by the user in the packaged
`0.0.0.7` build while the affected process was still running. The trigger was
opening Speech-to-Text and selecting another model card without accepting the
configuration.

## Live evidence from 0.0.0.7

- Exact process: `LA-Studio-0.0.0.7.exe`, PID `35564`, started at
  `2026-07-29 02:56:04`.
- Windows reported the process as not responding.
- In a 0.9-second measurement, GUI thread `16500` consumed about 0.94 seconds
  of user-mode CPU. The worker threads were waiting. This was a tight loop on
  the GUI thread, not a blocked model download, inference job, GPU operation or
  runtime host.
- The application log stopped after normal startup, QML load, catalog scan and
  hardware detection. No model load, CUDA call or STT inference was started.
- Ten live call-stack samples all captured the same path:
  `CapabilityGallery.qml` → `QJSEngine::fromVariant<bool>` →
  `QV4::ExecutionEngine::fromVariant` →
  `VariantAssociationPrototype::fromQVariantMap` → QV4 allocation and garbage
  collection.
- The application PDB mapped the active generated function to
  `CapabilityGallery_qml.cpp:27237`, whose source expression was
  `visible: detailPanel.f` at the former QML line 1233.

## Root cause

`detailPanel.f` is a large `QVariantMap` containing the selected model and its
nested raw metadata. A QML `visible` binding expects a boolean, but the binding
passed the entire map:

```qml
visible: detailPanel.f
```

Qt 6.9.3 generated `QJSEngine::fromVariant<bool>` for that expression. It
recursively converted the nested catalog entry into JavaScript associations,
allocated strings and repeatedly ran QV4 garbage collection on the GUI thread.
The event loop could no longer repaint or process input, producing the white
window and Windows "Not Responding" state.

The pending-selection isolation implemented in `0.0.0.7` was still a valid
correctness fix, but it did not address this independent QML conversion loop.
The previous smoke test also produced a false sense of coverage: it changed a
selection and closed the dialog in the same JavaScript turn, often before the
catalog contained multiple rows and before the detail bindings were exercised
through later event-loop ticks.

## Fix in 0.0.0.8

- Added the scalar `detailPanel.hasFamily` boolean, based on the model pointer
  and selected family ID.
- Replaced the failing `visible: detailPanel.f` binding with
  `visible: detailPanel.hasFamily`.
- Replaced all other direct boolean coercions of `detailPanel.f` in the same
  gallery with `detailPanel.hasFamily`, including header, badges, license,
  required-file, runtime and README bindings.
- Kept model metadata as data only; it is no longer used as a truth-value.
- Preserved the `0.0.0.7` pending-selection boundary: clicking a card still
  does not commit or rebuild the active feature until the user accepts.

The compiled QML for the corrected `visible` binding now stores and returns a
native `bool`; it contains no `fromVariant<bool>` call.

## Regression coverage

The QML route smoke test now yields to the event loop between model changes:

1. Load the Speech-to-Text route.
2. Open and keep the configuration dialog visible.
3. Wait until at least two STT model rows are available.
4. Select every STT family one at a time on successive timer ticks.
5. After every change, verify that the detail panel matches the selected ID.
6. Verify that no selection was committed before confirmation.

Results for the release source:

- `qmllint`: passed with zero warnings.
- Focused `QmlRouteSmoke`: passed in 5.27 seconds.
- Full suite: 34/34 passed; final `QmlRouteSmoke` run passed in 4.66 seconds.
- File and product versions: `0.0.0.8`.

The packaged EXE was not opened interactively by automation after packaging.
The earlier automated portable launch incorrectly requested the `offscreen`
platform plugin, which is intentionally absent from the portable Windows-only
deployment; that test configuration was discarded and no environment variable
was persisted.

## Artifact

- File: `out/LA-Studio-0.0.0.8/LA-Studio-0.0.0.8.exe`
- File version: `0.0.0.8`
- Product version: `0.0.0.8`
- SHA-256:
  `9B9B644C8869E6CD749C8D47F6A5365D112A2A518060F04C58514CA72A714D15`
