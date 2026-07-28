# Model Selection Freeze — Root Cause and Fix (0.0.0.7)

## Scope

This report covers the freeze reproduced in `0.0.0.6` when a user opened a
feature such as Speech-to-Text and clicked a different model card without
accepting the configuration.

## Evidence from 0.0.0.6

- The application log identifies the affected run as version `0.0.0.6`,
  started at `2026-07-29 02:24:31`.
- Startup, QML loading, hardware detection and the asynchronous local-model
  scan all started normally.
- No STT model-load, runtime-host or GPU-inference operation was logged after
  the card click.
- Windows recorded no native crash for this `0.0.0.6` session. The observed
  white window and "Not Responding" state were therefore a blocked GUI event
  loop, not a missing DLL or a GPU model being launched locally.
- The local models directory contained only the four-byte empty
  `registry.json`; there was no local model payload for the picker to load.
- The active-selection table was empty, so the freeze happened during pending
  selection, before a configuration was accepted.

## Root cause

The picker did not isolate its pending selection from the active studio state.
One card click followed this path:

1. `CapabilityGallery` changed `selectedFamilyId`.
2. The gallery emitted `familySelected`.
3. Every modal host immediately called
   `StudioSessionViewModel::selectFamily()`.
4. The controller emitted `selectionChanged` before the user accepted.
5. `StudioContext`, the feature view, settings panels, parameter schemas and
   session bindings behind the modal all reevaluated during the pointer event.
6. Controller property getters also queried SQLite synchronously during those
   repeated QML reads.

The card click was therefore treated as a global studio reconfiguration rather
than a lightweight highlight change. The earlier fix that only skipped an
empty file-selection refresh removed one expensive sub-path but did not stop
this global binding/rebuild cascade.

## Fix in 0.0.0.7

- Model, runtime and file choices remain local to `CapabilityGallery` while the
  dialog is open.
- Modal hosts no longer call `selectFamily()` from `onFamilySelected`.
- The active controller and feature page change only from
  `configurationAccepted`.
- Closing the dialog without accepting leaves the active studio unchanged.
- `StudioSessionViewModel` render-facing getters now return the in-memory state
  loaded by `syncSelectionFromSettings()` instead of querying SQLite on every
  QML property read.
- Workflow, alignment and Developer model dialogs use the same pending-state
  boundary.
- Empty initial file maps remain no-ops and cannot trigger a full synchronous
  catalogue refresh.

## Regression coverage

`QmlRouteSmoke` now performs the previously failing sequence:

1. Load the Speech-to-Text route.
2. Open the model dialog.
3. Change to a different family without accepting.
4. Verify the controller family/commit state did not change.
5. Continue processing QML events and close the dialog.

Results:

- Focused `QmlRouteSmoke`: passed in 4.38 seconds.
- Full test suite: 34/34 passed.
- Remote feature-surface contract: 8/8 direct Colab routes passed.
- Portable-package smoke test: exit code 0.

## Artifact

- File: `out/LA-Studio-0.0.0.7/LA-Studio-0.0.0.7.exe`
- File version: `0.0.0.7`
- Product version: `0.0.0.7`
- SHA-256:
  `9D66A10B389E489D2EA68D84BDDA91380B17717C074B8D160AB68D8E60243E96`

