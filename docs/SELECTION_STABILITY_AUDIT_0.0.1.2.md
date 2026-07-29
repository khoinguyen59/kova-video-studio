# Selection and remote-route stability audit — 0.0.1.2

**Recorded:** 2026-07-29
**Scope:** Stability defects identified from the desktop application's own log
while selecting remote model routes. This is separate from the direct-Colab
long-transcription timeout corrected in `0.0.1.1`.

## Findings and fixes

| Log symptom | Root cause | Released correction |
| --- | --- | --- |
| `FOREIGN KEY constraint failed` while saving a Colab selection | A direct Colab route has no local runtime, but the repository bound an empty string to the foreign-key `runtime_id` instead of SQL `NULL`. The selection was therefore not persisted. | Empty runtime ID/version are bound as SQL `NULL`. A test now enables real SQLite foreign-key enforcement and verifies that a remote-only selection survives a repository round trip. |
| `familyId` / `readmeContent` null warnings during a model-gallery refresh | The selected ID can outlive replacement of its catalog item for one QML binding cycle. Hidden child bindings still evaluated the stale, null detail object. | Detail bindings now require an actual family map before exposing it to descendants. |
| `modelData` / `entry` undefined warnings in Remote Inference settings | `Repeater` delegates can be evaluated during model replacement before `modelData` has a map value. | The catalog row and delegates use a safe empty-map boundary and render a neutral row until a real entry arrives. |
| Deprecated injected `stepId` parameter in Dubbing workflow | The signal handler relied on deprecated implicit parameter injection. | The handler now declares `function(stepId)` explicitly. |

## Verification performed before packaging

| Gate | Result |
| --- | --- |
| Full CTest release gate | 35/35 pass, including offscreen QML route smoke and Qt runtime deployment fixture |
| Staged portable package smoke | pass; `LA-Studio-0.0.1.2.exe` loaded all registered routes in offscreen mode from the final staged layout |
| QML parse/lint | pass (`scripts/lint_qml.ps1`) |
| Generated exact-model notebooks | 31/31 match their generator |
| Direct Colab UI/worker surface | 8/8 capability routes verified |
| Remote preflight contract | pass; rejects wrong capability/model and keeps Gateway credentials isolated |

The source test suite also asserts the exact new QML safety guards, so a later
edit cannot silently restore the two stale-data bindings.

## Relation to the reported Transcribe failure

The screenshot's workflow was executing **Transcribe** (not Translation) on a
14:59 source. Its message was a Cloudflare 120-second upstream timeout. That
is a worker-response-duration problem, not a translation-model failure.
`0.0.1.1` changed direct Colab STT from one long request to an asynchronous
job submission plus status polling; `0.0.1.2` retains that correction and adds
the model-selection stability corrections above.

## Remaining acceptance boundary

Automated tests establish desktop, UI and worker contracts. A live GPU Colab
run remains required to accept a real selected model end-to-end, including
copying its temporary URL/token and executing a representative request. The
desktop package is not treated as fully feature-complete until those live runs
are recorded feature by feature.
