# Dubbing independent audit

Date: 2026-08-05

## Scope and rules

This is an independent verification pass for **Dubbing only**. It does not
reuse a previous requirement or acceptance conclusion. No application source
file was changed, no executable was built or packaged, and no desktop GUI or
live Colab worker was opened. The only source changes in this audit are two
new regression tests in `tests/test_DubbingProject.{h,cpp}`.

The purpose was to test four claims:

1. A selected Direct Colab route cannot silently run a Local model/runtime.
2. The Dubbing route/configuration/worker flow is internally executable at its
   controller and direct-worker protocol boundaries.
3. Expected mid-flow failures stop at the selected route, preserve safety, and
   provide a recovery/error path rather than falling back.
4. Coverage extends beyond a fixed happy-path list: persistence, stale state,
   exact capability/model binding, protocol success/failure/cancel, progress,
   activity UI and offscreen QML were exercised.

## New independent regressions

### 1. `independentAuditDirectColabPurgesLocalStateAcrossDubbingStages`

The test starts with a saved Dubbing project containing stale Local metadata at
both root and nested-parameter level for all four executable model stages:

| Dubbing stage | Direct Colab capability | Exact model |
| --- | --- | --- |
| Isolator | `voice-isolation` | `sherpa-onnx-spleeter-2stems-fp16` |
| Transcribe/STT | `stt` | `whisper.cpp` |
| Translate | `translation` | `m2m100-418m` |
| TTS | `tts` | `kokoro` |

For each stage, it changes the persisted route to `colab-direct`, verifies an
independent loopback worker with the exact capability/model pair, then checks:

- Root and nested Local fields (`familyId`, runtime ID/version, selected files
  and configuration signature) are removed.
- The durable route/model are Direct Colab and exact.
- Automatic preflight requires and obtains four verified Direct Colab workers.
- The first Automatic activity row displays `Direct Colab GPU` while the
  global remote-first preference is deliberately disabled.
- URL and bearer token do not appear in the saved `.ladub.json` project.

The test stops before external media execution. Its workers intentionally only
implement health/capability verification; transfer behavior is tested through
the dedicated protocol suites below.

### 2. `independentAuditDirectColabFailureNeverFallsBackToLocal`

This tests three adversarial Isolator configurations against a real
`DubbingJobRunner` instance:

- Direct Colab selected with no worker: fails with the Direct-Colab connection
  instruction and does not start processing.
- Direct Colab selected with a model that has no exact notebook mapping: fails
  at model validation and does not start processing.
- API Gateway selected for source separation: fails explicitly because that
  route is unsupported; it does not switch to Local.

Both new cases passed: **4 passed, 0 failed, 0 skipped** in the focused QtTest
run (the count includes test-suite setup and teardown).

## Broadened validation matrix

| Perspective | Evidence exercised | Result |
| --- | --- | --- |
| Route isolation | Four-stage stale Local-to-Colab migration, `remoteFirstMode=false`, exact session checks, activity-route label, secret persistence | PASS |
| Isolator protocol success | Direct multipart request, job polling, vocals/background artifacts and measured progress | PASS (`TestColabSeparationRunner`) |
| Isolator failure/recovery | CUDA failure presentation, missing/incomplete stems rejected, cancellation discards partial artifacts, absent/wrong worker and unsupported route rejection | PASS |
| STT | Explicit provider routing, Direct Colab async job contract, worker/model mismatch and no Gateway/Colab cross-fallback | PASS (`TestSttSession`) |
| Translation | Direct patch contract, completed-segment progress, invalid patch, empty/needs-review behavior and cancellation | PASS (`TestColabTranslationRunner`) |
| TTS | Direct worker request/result contract and route separation from Gateway | PASS (`TestColabTtsRunner`) |
| Controller/workflow | Dubbing preflight, persistence, route readiness, interrupted/cancelled state protections and exact Colab mappings | PASS (`TestDubbingProject`, `TestRemoteExecution`, `TestSourceSeparation`) |
| UI/static validation | QML lint plus production QML route smoke with `QT_QPA_PLATFORM=offscreen` | PASS |
| Cross-feature regression | Full CTest suite after test-only change | PASS |

## Commands and results

- Focused independent QtTest cases: PASS, 4 passed / 0 failed / 0 skipped.
- QML lint gate: PASS, zero permitted warnings.
- Targeted Dubbing/Colab/route/offscreen CTest: **9/9 PASS** in **29.38 s**.
- Full CTest: **39/39 PASS** in **71.35 s**.
- Graphify was updated after the test-code change.

## What this proves

For the audited code path, selecting Direct Colab is a durable per-stage
contract. It removes old Local metadata, demands an exact verified worker, and
the source-separation runner dispatches to the Colab runner before returning;
the Local resolver is only reachable from an explicit Local selection. Tested
failure cases stop with actionable route-specific errors rather than running a
different backend or substituting original audio for separated stems.

The separate runner tests prove the desktop's direct protocols can exchange
requests, progress, output artifacts, cancellation and failure data against
loopback workers. The offscreen checks ensure the QML route surfaces remain
loadable after the controller/test changes.

## What this does not prove

This audit does **not** claim that a live Colab notebook, Cloudflare tunnel,
GPU driver, third-party package image, a particular source video, or a desktop
interaction is error-free. No real Colab URL/token was supplied or used, and
the user-facing desktop was not opened by the audit. A live acceptance run is
still needed to prove that the selected external GPU worker can process the
user's actual media end-to-end; failure there must be reported as a worker or
service failure, never counted as an automated pass.

## Files changed by this audit

- `tests/test_DubbingProject.h`
- `tests/test_DubbingProject.cpp`
- `docs/DUBBING_INDEPENDENT_AUDIT_2026-08-05.md`

No file under `src/`, `qml/`, packaging scripts, notebook templates, or an EXE
was modified.
