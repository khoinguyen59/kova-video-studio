# CURRENT AUTHORITATIVE RESPONSE -- Direct Dubbing Colab voice-clone closure (2026-07-31)

**Request executed:** the current `AI_AGENT_REQUEST.md` Direct-Colab Dubbing voice-clone regression, source fix, verification, commit/push to `main`, and portable internal packaging. Work stayed in LA-STUDIO. No GUI/EXE/browser control, no live Colab GPU worker, no model download, and no external credential was used.

## Root cause and product fix

The prior controller-level loopback coverage did not exercise `DubbingSynthesisJob` itself. Its direct-Colab clone request omitted the existing explicit **loopback-only test-session flag**. Consequently, the real job path rejected the test HTTP loopback URL before it could prove profile creation/reuse/cancellation. This was a testability gap, not permission to weaken production transport security.

- `src/controllers/dubbing/DubbingSynthesisJob.cpp` now carries `ColabSession::allowsInsecureLocalhostForTests()` into `ColabVoiceCloneRequest`.
- The flag can only be set by the existing non-production `ColabSession::setSession(..., true)` test seam. Normal user sessions still require a verified HTTPS worker endpoint; no production HTTP fallback was added.
- `TestDubbingProject::dubbingDirectColabVoiceCloneReusesProfileAcrossSegments` drives the actual `DubbingSynthesisJob` against a local HTTP worker protocol loopback, including profile creation/polling, generation/polling/audio, an exact-model change, an in-flight session change, cancellation, and recovery.
- Version source is `0.0.2.4`.

## Direct loopback evidence

| Scenario | Result | Evidence from the job's actual worker requests |
| --- | --- | --- |
| Two segments with the same selected reference voice | PASS | 1 profile creation and 2 generation requests. Both generations send `profile-1`, `model=omnivoice`, `language=vi`; the profile request carries the exact owned transcript. |
| Segment snapshot integrity | PASS | Both completed segments retain selected preset ID, the app-owned absolute reference path, and exact reference transcript. |
| Exact clone-model change | PASS | Switching `omnivoice` to `voxcpm2` creates profile 2 instead of reusing profile 1. |
| Worker session changes while a generation is pending | PASS | The active job emits the explicit session-changed failure, cancels its remote work, and does not continue on the old profile. |
| Recovery after reconnect | PASS | A subsequent job creates fresh profile 3; final counts are 3 profile creations, with no stale-profile reuse. |
| API Gateway interaction | Independent | This test and the Dubbing clone path use verified Direct Colab only. Gateway is neither called nor used as fallback. |
| Live Colab GPU result and subjective voice quality | BLOCKED | No temporary live worker/token/GPU acceptance was supplied or controlled. Loopback protocol success is not represented as live-GPU acceptance. |

## Verification completed

```powershell
ctest --test-dir out\build\windows-msvc-tests -R '^(TestDubbingProject|TestColabVoiceCloneRunner)$' --output-on-failure
ctest --test-dir out\build\windows-msvc-tests --output-on-failure
& .\graphify\.venv\Scripts\graphify.exe update .
.\scripts\package.ps1 -SkipInstaller -PortableInternalLayout -Version 0.0.2.4 -MaxParallelJobs 1 -QtRoot .tools\Qt\6.9.3 -VcpkgRoot .deps\vcpkg -LlamaCppSourceDir .deps\llama.cpp -AllowUnsignedEspeakForInternalBuild
```

- Direct Dubbing + clone-runner suites: **PASS (2/2)**.
- Full CTest: **PASS (35/35)**.
- Graphify incremental update completed after source changes (its generated files intentionally remain uncommitted).
- Portable package static verification: `FileVersion=0.0.2.4`, `ProductVersion=0.0.2.4`, `ProductName=LA Studio`, runtime manifest **16/16**, license manifest **16/16**.

## Source, push, and package

- Product/test/version commit on `main`: `0a40fbe` — `fix: cover direct dubbing voice clone profile reuse`.
- Push: **PASS** to `origin/main` (`ac3eaaa..0a40fbe`).
- Portable internal executable: `out\LA-Studio-0.0.2.4\LA-Studio-0.0.2.4.exe`.
- SHA-256: `237C2534D5B5540A4F4A4C218DE480B0378D4BA958403249C76ACA02A12A8666`.
- Package remains internal-only because the verified eSpeak MSI used for this internal build is unsigned. The EXE was not launched or controlled.

---

# Previous authoritative response -- Saved voice-clone reuse closure (2026-07-31)

**Request executed:** current `AI_AGENT_REQUEST.md` saved voice-clone reuse validation. Work stayed in LA-STUDIO. No GUI/EXE/browser control, no live Colab GPU worker, no model download, and no external credential was used.

## Root cause and product fix

`DubbingSynthesisJob` kept a temporary Direct-Colab profile cache signature containing reference path/size/mtime, transcript and language, but **not the exact voice-clone model**. A same-session model change could therefore reuse an incompatible remote profile.

- `src/controllers/dubbing/DubbingSynthesisJob.cpp` now includes `effectiveVoiceCloneModel` in the temporary profile signature and clears the cached profile when any signature input changes.
- `src/controllers/tts/ColabVoiceCloneController.cpp` now propagates the existing loopback-only session flag to its runner. Production sessions still require HTTPS: the flag comes solely from the non-production `ColabSession::setSession` test seam and cannot weaken production endpoint validation.
- `qml/components/voicecloning/ReferenceInputBox.qml` now calls the durable item a **reference voice** / **saved reference voices** / **save reference**. Saving it copies reference audio plus transcript and exact model family; it does not claim to persist a Colab profile. The existing **Save Audio File** action exports generated WAV only.
- `CMakeLists.txt` source version is now `0.0.2.3`.

No `profile_id`, worker URL, bearer token, or gateway credential is persisted in `VoiceClonePresetService`, `DubbingProject`, settings, source fixtures, or this report. `profile_id` is process-memory state belonging only to the active direct Colab worker session.

## Reuse conclusion and evidence

| Scope | Result | Evidence |
| --- | --- | --- |
| Edit generated text in an unchanged worker session | PASS | New loopback controller regression performs two generation requests and proves only one profile creation; second request carries the temporary profile ID. |
| Multiple dubbing speakers/segments in one run | PASS | Existing `audioGenerationUsesSavedCloneVoiceForEverySegment` proves one selected preset snapshot (ID, owned reference path, transcript) is used by two speakers/segments. |
| Close/reopen Dubbing project | PASS | Existing `cloneVoicePresetSelectionPersistsAndMissingPresetBlocks` persists only the selected durable preset ID and reloads it through `VoiceClonePresetService`. |
| Close/reopen application | PASS | Existing `voiceClonePresetLibraryPersistsAtomicallyAndProtectsSource` recreates the service and verifies its atomic metadata plus app-owned reference copy. A fresh worker profile is intentionally recreated later; it is not persisted. |
| Worker reconnect/restart | PASS at loopback/source level | New regression clears the session, verifies the controller profile is empty, reconnects, then proves the next request creates a profile again from the durable reference. Active Dubbing synthesis already cancels/fails on session change instead of continuing with a stale profile. |
| Change reference file, transcript, language, or exact model | PASS | New regression proves each reference/transcript/language change creates a new profile. Model change clears the session/profile; Dubbing's model-inclusive signature is source-regressed in `dubbingUiUsesExactModelWorkers`. |
| Missing/corrupt/wrong-family preset | PASS | Existing Dubbing and preset-library regressions block selection/synthesis with no random/source/local fallback. |
| Live Colab GPU result and subjective voice quality | BLOCKED | No live temporary worker/token/GPU acceptance was supplied or controlled in this run. This is intentionally not represented by mock/loopback success. |

The direct Colab and API Gateway paths remain independent. Voice cloning uses the verified direct Colab route; a Gateway route cannot supply or reuse its temporary profile.

## Tests and checks

New regression: `TestColabVoiceCloneRunner::controllerReusesProfileOnlyForMatchingDurableReference` covers text reuse, changed transcript, changed language, changed reference file, worker-session invalidation/rebuild, and model-session clearing. `TestDubbingProject::dubbingUiUsesExactModelWorkers` also guards the Dubbing model-inclusive profile signature and absence of a persisted clone profile field.

```powershell
cmake --build out\build\windows-msvc-tests --target LAStudioUnitTests -j 1
ctest --test-dir out\build\windows-msvc-tests -R '^(TestDubbingProject|TestColabVoiceCloneRunner)$' --output-on-failure
ctest --test-dir out\build\windows-msvc-tests --output-on-failure
& .\graphify\.venv\Scripts\graphify.exe update .
git diff --check
```

- Targeted Dubbing + voice-clone suites: PASS (2/2).
- Full CTest: PASS (35/35).
- Graphify incremental update completed after source changes.

## Source and package evidence

Source/test/version commit on `main`: `07a215848cdbe69b8213ee3f877b798607502daf` (`fix: make saved voice clone reuse model-safe`). No push was made.

- Portable internal EXE: `out\LA-Studio-0.0.2.3\LA-Studio-0.0.2.3.exe`
- File/Product version: `0.0.2.3`; product: `LA Studio`
- Size: `21,429,248` bytes
- SHA-256: `B81FCF04029EB1681434F44C413114B8BE456ED5646D7FB29D912E7D796E15F7`
- Package script result: runtime manifest **16/16** and license manifest **16/16**. It remains internal-only because the SHA-256-verified eSpeak MSI is unsigned.
- The EXE was not launched or controlled.

---

# Previous authoritative response -- Download media vertical slice (2026-07-31)

**Request executed:** the newest `AI_AGENT_REQUEST.md`, with Download media completed before any unrelated work. Work remained inside LA-STUDIO. No GUI/browser/desktop control, real profile access, model/runtime download, live Colab worker, or reference repository modification was performed.

## Download media result

| Requirement | Source boundary | Regression/evidence | Status |
| --- | --- | --- | --- |
| Separate visible Download feature while preserving model/runtime Downloads popup | `qml/components/shared/StudioRouteRegistry.qml`, `qml/Main.qml`, `qml/components/Sidebar.qml`, `qml/components/DownloadsPopup.qml`, `qml/pages/MediaDownloadPage.qml` | New `media-download` route, loader and page; source regression proves old popup still reads `AppController.downloads.allDownloads`; QML route smoke loads the new page | PASS |
| Dubbing link import is visible at the source entry area | `qml/components/dubbing/DubbingSourceMediaPanel.qml`, `qml/pages/DubbingPage.qml` | Direct HTTPS control moved before the fill-height preview and retains existing controller wiring; source regression verifies one control and its placement marker | PASS at QML-source/route-smoke level |
| One shared downloader and safe Download -> Dubbing handoff | `src/controllers/dubbing/DubbingController.cpp`, `src/dubbing/media/RemoteMediaImportService.cpp`, `src/dubbing/media/MediaIngestService.cpp` | `standaloneDownloadHandsOffOwnedMediaWithoutSecondDownload` verifies one loopback request, app-owned staging, then probe/normalize after handoff; no second network download | PASS |
| Preserve existing Dubbing project unless probe/normalization succeeds | same controller/ingest boundary | `standaloneDownloadKeepsExistingProjectWhenProbeFails` proves failed media probe retains the current project and keeps the staged file retryable | PASS |
| Direct HTTPS media validation, byte progress, cancel/error/retry, size/scheme protections | `RemoteMediaImportService.cpp`, `DubbingController.cpp`, `MediaDownloadPage.qml` | Existing loopback success/progress/cancel/oversize/unsafe-URL/controller regressions plus new standalone handoff tests; page displays bytes only when total is known (no invented percentage) | PASS at service/controller/QML-source level |
| Public video-page URLs (YouTube/TikTok) | no public-source adapter in this build | The page explicitly distinguishes direct files from public video pages. A compatible no-cookie/no-login/no-DRM adapter was not present and was not invented or claimed. | BLOCKED (external adapter/product decision) |

## Root cause and fix

The pre-existing sidebar **Downloads** action only opened the model/runtime queue (`DownloadsPopup`); it did not expose a media-from-link workflow. Dubbing already had direct-link controller logic, but its control was below a fill-height media preview and could be hidden at short window heights.

`MediaDownloadPage.qml` now provides a distinct Download route with URL input, direct-HTTPS scope notice, app-owned staged result, real byte counters, cancel/retry/error state, and a **Use in Dubbing** action. It calls new narrowly-scoped `DubbingController` download/handoff methods which reuse its existing `RemoteMediaImportService`; the handoff intentionally uses the existing `MediaIngestService` runner to probe and normalize before it replaces a Dubbing project. No second downloader, model queue, credential storage, or silent fallback was introduced.

## Changed source and tests

- `CMakeLists.txt` - version `0.0.2.2` and new QML page packaging.
- `src/controllers/dubbing/DubbingController.{h,cpp}` - download-only staging state and guarded handoff into the existing ingest pipeline.
- `qml/components/shared/StudioRouteRegistry.qml`, `qml/Main.qml` - Download route/loader and smoke coverage.
- `qml/pages/MediaDownloadPage.qml` - standalone user surface.
- `qml/components/dubbing/DubbingSourceMediaPanel.qml` - above-fold direct-link import placement.
- `tests/test_MediaIngestService.{h,cpp}` - no-redownload handoff, failed-probe project retention, and static route/wiring checks.

Source/test/version commit on `main`: `3e357558aab430461adb6690a4a5814673c36967` (`feat: add staged media download workflow`). No push was made.

## Verification completed before packaging

```powershell
cmake --build out\build\windows-msvc-tests --target LAStudioUnitTests -j 1
cmake --build out\build\windows-msvc-tests --target LAStudio -j 1
ctest --test-dir out\build\windows-msvc-tests -R '^TestMediaIngestService$' --output-on-failure
ctest --test-dir out\build\windows-msvc-tests -R '^(TestMediaIngestService|TestDubbingProject|TestRemoteExecution|QmlRouteSmoke)$' --output-on-failure
ctest --test-dir out\build\windows-msvc-tests --output-on-failure
```

- New media-ingest suite: PASS.
- Targeted Media/Dubbing/Remote/QML set: PASS after rebuilding the application QML cache. The initial smoke run correctly caught a stale app executable built before the route change; rebuilding target `LAStudio` made the new resource bundle active and the smoke passed.
- Full CTest: PASS, **35/35**.
- GUI click-through, public web-page download, and live Colab GPU work remain unclaimed/blocklisted rather than reported as tested.

## Package evidence

`scripts/build.ps1` and `scripts/package.ps1` completed for a new, non-overwriting portable internal package. The package scripts verified 16 required staging artifacts and 16 required license artifacts. The build retains the existing internal-only restriction because the verified eSpeak NG MSI is unsigned.

- EXE: `out\\LA-Studio-0.0.2.2\\LA-Studio-0.0.2.2.exe`
- File/Product version: `0.0.2.2`; product: `LA Studio`
- Size: `21,429,248` bytes
- SHA-256: `6C2FA314EDFD0C3CCB1DF083BDAC5A4EBA441AB2D3B950CF296746A51BEEFC4A`
- Portable dependency checks: `media-tools\\ffmpeg.exe`, `media-tools\\ffprobe.exe`, and `licenses\\THIRD-PARTY-NOTICES.md` present.
- No EXE was launched or controlled. The earlier `0.0.2.1` package remains untouched.

---

# Previous authoritative response -- Coverage closure regressions (2026-07-31)

**Request executed:** the current `AI_AGENT_REQUEST.md` A–F coverage instruction. Work stayed inside LA-STUDIO: no GUI/browser/CapCut automation, no real account/profile access, no model/runtime download, and no reference repository was modified.

## A. Verification of the preceding evidence

Commit `5a4c84a` was inspected against its implementation and tests. `VoiceClonePresetService` has schema version 1, atomic `QSaveFile` envelope writes, and backward reads for the former top-level array. The earlier selector regression has no local clone/TTS model load and rejects a preset from the wrong exact family. The portable `0.0.2.1` evidence remains valid from the preceding report; this continuation did not rebuild or overwrite it.

## B. Coverage matrix

| Requirement | Source boundary | Concrete regression/evidence | Status |
| --- | --- | --- | --- |
| Reference audit: OpenCut/Kova/Voice Studio/pyCapCut reuse decision | This report’s preceding audit; `src/dubbing/CapCutDraftExporter.cpp`, `src/dubbing/media/RemoteMediaImportService.cpp` | Direct read-only audit recorded below; OpenCut rejected as placeholder shell, Kova patterns adapted only, pyCapCut structural-only | PASS |
| One Dubbing Direct-Colab setup, exact notebook/model, Check all, snapshots, route isolation | `DubbingController.cpp`, `DubbingColabModelRoutes.h`, `DubbingColabSetupDialog.qml` | `TestDubbingProject::dubbingColabModelsMapToExactNotebooks`, `dubbingUiUsesExactModelWorkers`; `TestRemoteExecution` exact CUDA/model/stale/session-isolation cases; `verify_colab_model_bindings.py` 31/31 | PASS at source/loopback level |
| App-owned voice library: schema, checksum, atomic write, CRUD/reload, no local inference dependency | `VoiceClonePresetService.cpp`, `DubbingController.cpp` | `voiceClonePresetLibraryPersistsAtomicallyAndProtectsSource`, `cloneVoicePresetSelectionPersistsAndMissingPresetBlocks` | PASS |
| Legacy-array migration to current metadata envelope | `VoiceClonePresetService::loadAllPresets/saveAllPresets` | **New:** `voiceClonePresetLibraryMigratesLegacyArrayOnEdit` writes legacy array, reads valid owned reference, then proves next edit produces schema envelope atomically | PASS |
| Zero/missing/corrupt/incompatible preset blocks; project keeps ID; run uses a snapshot for every speaker | `DubbingController.cpp`, `DubbingSynthesisJob.cpp`, `DubbingProject.cpp` | `zeroCloneVoicePresetBlocksSynthesisWithoutFallback`, `cloneVoicePresetSelectionPersistsAndMissingPresetBlocks`, `changingCloneVoicePresetAppliesToEntireNextRun`, `audioGenerationUsesSavedCloneVoiceForEverySegment` | PASS |
| Local/direct-link import, owned staging, Unicode-safe normalization, actual bytes, cancel/size/scheme/error and retain old project | `RemoteMediaImportService.cpp`, `MediaIngestService.cpp`, `DubbingController.cpp` | `TestMediaIngestService` loopback download/progress/cancel/oversize/unsafe URL/controller commit-after-probe cases | PASS |
| Separation requires vocals **and** accompaniment; no original-audio fallback | `DubbingJobRunner.cpp`, source-separation controller routes | `colabSourceSeparationDoesNotFallbackToLocal`, `unavailableLocalSourceSeparationDoesNotUseOriginalAudio`, `failedSeparationBackendDoesNotUseOriginalAudio`, `incompleteSeparationStemsDoNotCompleteTheNode` | PASS at failure-contract level |
| Clean vocal can become durable shared preset and reappear after service recreation | `DubbingController.cpp`, `VoiceClonePresetService.cpp`, `DubbingNodeInspector.qml` | `voiceClonePresetLibraryPersistsAtomicallyAndProtectsSource` proves owned reference + recreation; `dubbingUiUsesExactModelWorkers` verifies the shared library surface | PASS at controller/QML-source level |
| CapCut structural draft: assets, timeline/timebase, video/original/background/mix/voice clips/subtitles, atomic/collision/Unicode/missing/no-secret | `CapCutDraftExporter.cpp` | **Expanded:** `exportsSelfContainedCapCutDraftWithUnverifiedImportStatus` now parses a two-speaker/two-clip video draft, exact 850–1800 ms timing, original/background/mix tracks, Unicode SRT, collision publication, missing-clip rejection, and excludes injected Colab/Gateway secret fields | PASS structurally |
| Progress/cancel/retry, migration/save/reopen, cache isolation, Gateway/Colab credential isolation | `WorkflowGraphRunner.cpp`, `DubbingController.cpp`, `ColabSession.cpp`, `PathUtils.cpp` | `TestWorkflowGraph::exposesOnlyActiveNodeMeasuredProgress`, media cancellation tests, Dubbing project migrations/save-reopen, `TestFileAccessService`, `TestRemoteExecution` memory-only/stale/wrong-model/independent-route tests | PASS at regression level |
| Real desktop click-through, real Colab GPU results, real CapCut import and subjective voice quality | External desktop/Colab/CapCut only | Explicitly prohibited for this run; no temporary worker credentials or live CapCut acceptance supplied | BLOCKED (external/manual) |

There are no remaining **MISSING** rows that can be fixed without the prohibited GUI/GPU/external acceptance work.

## C. Coverage gaps corrected

The implementation was not changed: the remaining gaps were missing regression proof, not a product defect.

- `tests/test_DubbingProject.{h,cpp}` adds `voiceClonePresetLibraryMigratesLegacyArrayOnEdit`. It proves compatibility data does not become unreadable at upgrade and that normal editing migrates it to the versioned envelope.
- The CapCut structural regression now covers the previously untested two-segment timing, original/background/mix/video asset layout, Unicode subtitle timing, missing asset failure, collision-safe output, and export whitelist behavior for injected transient credential fields.

No silent fallback, progress behavior, route, model mapping, package dependency, or external-reference source was altered.

## D. Commands and results

```powershell
cmake --build out\build\windows-msvc-tests --target LAStudioUnitTests -j 1
ctest --test-dir out\build\windows-msvc-tests -R '^TestDubbingProject$' --output-on-failure
ctest --test-dir out\build\windows-msvc-tests -R '^(TestMediaIngestService|TestDubbingProject|TestRemoteExecution|QmlRouteSmoke)$' --output-on-failure
ctest --test-dir out\build\windows-msvc-tests --output-on-failure
python scripts\verify_generated_colab_notebooks.py
python scripts\verify_colab_model_bindings.py
& .\graphify\.venv\Scripts\graphify.exe update .
git diff --check
```

- Targeted Dubbing: PASS.
- Media/Dubbing/Remote/QML route set: PASS (5/5).
- Full CTest: PASS (35/35, 24.32 seconds).
- Generated exact-model notebooks: PASS (31/31).
- Controller/UI/notebook exact bindings: PASS (31/31).
- Graphify update completed: 11,120 nodes, 21,352 edges, 534 communities. Generated Graphify files remain untracked/excluded.
- `git diff --check`: PASS.

## E. Packaging decision

`0.0.2.2` was **not** built. The active MD permits it only for a source fix after `5a4c84a`; this continuation adds regression coverage only and therefore preserves the verified `0.0.2.1` portable package. No binary or `out/` artifact is staged.

## F. Remaining external acceptance blockers

- A user-provided temporary Direct-Colab URL/token plus a real GPU runtime is required to establish live inference, tunnel stability, exact-model output and quality.
- A manual CapCut installation/import is required to move the structural label beyond **“CapCut structure generated — import not yet verified.”**
- Desktop click-through remains intentionally unclaimed because no GUI or machine control was used.

---

# Previous authoritative response -- Direct Colab Dubbing audit and voice-library hardening (2026-07-31)

**Request executed:** the current A → H instruction in `AI_AGENT_REQUEST.md`, without GUI automation, desktop-app control, user-profile access, model/runtime download, or changes to any reference repository.

## Audit result and decisions

| Reference | Evidence read | LA Studio decision |
| --- | --- | --- |
| OpenCut (`origin = https://github.com/OpenCut-app/OpenCut.git`) | `apps/desktop/src/shell.rs` and its Browser/Preview/Inspector/Timeline panels | **Reject as implementation source.** It is currently a persistent-panel shell with placeholder text only; it has no project, asset, timeline, serialization, undo, autosave, or exporter model to reuse. |
| Kova | `internal/service/link2file.go`, `source_audio_separation.go`, project/workdir/manifest, CapCut builder and desktop entrypoint | **Adapt safety patterns only.** LA Studio retains its own direct-HTTPS staging, probe/normalize, two-stem fail-closed separation, workflow journal, and portable staging. It does not adopt yt-dlp, browser/cookie handling, or a second project architecture. |
| Kova Voice Studio | Wails app, Go worker bridge, Python `store.py`, `api.py`, `jobs.py`, and tests | **Adapt owned-copy/validated persistence.** The useful pattern is durable reference media with checksum, explicit consent and no credential persistence; no Go/Python code was copied. |
| pyCapCut | `draft_folder.py`, `script_file.py`, material/track/segment serializers | **Structural reference only.** LA Studio's `CapCutDraftExporter` remains labelled `CapCut structure generated — import not yet verified`; no CapCut application was opened or controlled. |

## Root cause and correction

The shared voice library already copied reference audio atomically and stored its SHA-256, but its metadata file was an unversioned top-level JSON array. That left no explicit on-disk schema contract for a clean package or future migration. The Dubbing selector test also unnecessarily loaded a mock local TTS model, masking the required Direct-Colab-only persistence invariant.

`VoiceClonePresetService` now writes an atomic metadata envelope:

```json
{ "schemaVersion": 1, "presets": [ ... ] }
```

It still reads the legacy array safely and rewrites it to the current schema on the next successful edit. Each preset keeps its app-owned audio path, byte count, SHA-256, and storage version. No URL, token, gateway key, worker profile, or other credential is serialized.

The Dubbing regression now proves all of the following without a loaded local TTS/clone model:

- create/import → app-owned WAV copy → atomic metadata → recreated service → Dubbing selector;
- a selected preset survives project save/reopen;
- changing from the exact `omnivoice` clone family to `voxcpm2` leaves the old selector empty and rejects selection instead of falling back;
- zero preset and missing/corrupt preset remain blocking conditions; existing two-speaker snapshot regression remains intact.

The QML/catalog/Direct-Colab contract was also re-traced: `VoiceCloningPage.qml` selects an exact catalog family through `ColabVoiceCloneController::selectColabModel`, then saves that family without local runtime/files; `ReferenceInputBox` and `VoiceLibraryDialog` use that same family key; Dubbing filters `VoiceClonePresetService` by the configured `voiceCloneModelId`. The six voice-cloning catalog IDs match the six exact notebook mappings.

## Source and verification

Changed and committed files for this continuation:

- `CMakeLists.txt` — default application version `0.0.2.1`.
- `src/controllers/shared/VoiceClonePresetService.cpp` — schema envelope plus legacy-read migration.
- `tests/test_DubbingProject.cpp` — no-local-model persistence/selector check, exact-family mismatch check, schema assertions.

Source/test/version commit on `main`: `5a4c84a` — `fix: harden direct colab voice preset library`.

Commands completed:

```powershell
ctest --test-dir out\build\windows-msvc-tests --output-on-failure
# 35/35 passed

& .\graphify\.venv\Scripts\graphify.exe update .

.\scripts\package.ps1 -SkipInstaller -PortableInternalLayout -Version 0.0.2.1 `
  -MaxParallelJobs 1 -QtRoot .tools\Qt\6.9.3 -VcpkgRoot .deps\vcpkg `
  -LlamaCppSourceDir .deps\llama.cpp -AllowUnsignedEspeakForInternalBuild
```

## Package evidence

- EXE: `out\LA-Studio-0.0.2.1\LA-Studio-0.0.2.1.exe`
- EXE metadata: `FileVersion = 0.0.2.1`, `ProductVersion = 0.0.2.1`, `ProductName = LA Studio`
- Size: `21,314,048` bytes
- SHA-256: `B650AD4DB6B5610FCDE7490CB6CD46486B0561032E1AADD5A767F4439FA02199`
- Package staging and license manifests: passed (16 required artifacts each).
- Distribution status: **internal-only** because the package uses the SHA-256-verified but unsigned eSpeak NG MSI.

| Gate | Result | Evidence / limit |
| --- | --- | --- |
| Source/mock/loopback/QML regression | PASS | Full CTest: 35/35. |
| Direct Colab / Gateway isolation | PASS at regression level | Exact-model, session-memory, wrong-worker and route-isolation suites passed. |
| Clean-package voice library | PASS at persistence/controller level | No local TTS model is loaded by the new selector/persistence regression. |
| Portable package | PASS | Build, staging and EXE metadata/version verification completed. |
| GUI click-through acceptance | BLOCKED | The active instruction explicitly forbids GUI automation or opening/controlling the EXE. No UI result is claimed. |
| Live Colab GPU synthesis/separation | BLOCKED | No user-provided temporary worker URL/token; no GPU job was started. |
| Live CapCut import | BLOCKED | No CapCut UI was opened or controlled. |

---

# Previous authoritative response -- package acceptance continuation (2026-07-30)

**Request executed:** the newest `AI_AGENT_REQUEST.md` instruction: finish package/user acceptance only; do not add features or rewrite architecture; repair only a reproducible concrete defect; record results here; then wait for a new MD instruction.

**Source result:** committed directly to `main` as `c1033dbd3fbbbd4e1fc52ca82bfcd867c5560e37` (`fix: isolate cache when data directory is overridden`). No push was made.

## Defect found and fixed

The portable application honours `LASTUDIO_DATA_DIR` for its data/models/project folders, but `PathUtils::cacheDir()` previously ignored that override and returned the normal Windows per-user cache. A real package acceptance run proved the defect: importing and normalizing a loopback WAV wrote `master.wav` below `%LOCALAPPDATA%\\LA Studio\\cache\\dubbing` even though a disposable profile was supplied.

`PathUtils::cacheDir()` now returns `<LASTUDIO_DATA_DIR>/cache` when the override is explicitly set, while preserving the existing `QStandardPaths::CacheLocation` location for ordinary user launches. `TestFileAccessService::dataDirectoryOverrideAlsoIsolatesCache` locks this contract down and restores its environment variable after the assertion.

The pre-fix acceptance run did create one small 768,092-byte loopback fixture cache item at `%LOCALAPPDATA%\\LA Studio\\cache\\dubbing\\imports\\efaef678...\\master.wav` at `2026-07-30 23:12:43`. It has deliberately **not** been removed: it is existing user-profile data. The post-fix repeat at `2026-07-30 23:34:38` wrote the corresponding 768,092-byte `master.wav` only below the new disposable profile:

`out\\acceptance-profile-0a813344b62e4baf84fe079461a4e9e0\\cache\\dubbing\\imports\\efaef678...\\master.wav`

## Evidence from this acceptance run

| Check | Result | Actual evidence / limit |
| --- | --- | --- |
| Regression for isolated cache | PASS | New `TestFileAccessService` test passed in the compiled test binary. |
| Full automated regression | PASS | `ctest --test-dir out\\build\\windows-msvc-tests --output-on-failure`: **35/35** passed, including `QmlRouteSmoke`. |
| Code graph | PASS | `graphify update .` completed after the source edit. Generated graph files remain untracked. |
| Portable package | PASS | `package.ps1 -SkipInstaller -PortableInternalLayout -Version 0.0.2.0 ... -AllowUnsignedEspeakForInternalBuild` succeeded; staging and license manifests each verified 16 required artifacts. |
| Clean-profile launch | PASS | The EXE launched as `LA Studio - 0.0.2.0` with a newly created `LASTUDIO_DATA_DIR` profile; onboarding was completed without enabling update checks. |
| Direct media link UI | PASS | In the packaged Dubbing UI, `http://127.0.0.1:8765/loopback-fixture.wav` was accepted as permitted loopback HTTP, downloaded, normalized, displayed as a two-second original audio source, and advanced to Separate. The visible normalized output path was inside the disposable profile cache. |
| Invalid remote URL / cancellation UI | Previously observed, not repeated after this cache-only change | This run tested the successful loopback path. No external remote URL or cancellation was invoked. |
| Direct Colab and Gateway live work | BLOCKED | No user worker URL/token or Gateway credential was provided; no secrets were written, and no GPU job was started. Their independent configuration surfaces were not changed by this cache-only fix. |
| Voice library live persistence | BLOCKED | The clean package has no installed voice-clone model/runtime, so an actual user-side voice-library save/reload could not be reached without installing a model. Existing regressions passed, but that is not claimed as live UI proof. |
| CapCut live import/export | BLOCKED | No local live CapCut acceptance fixture/install was used in this continuation. |

No LA Studio package process or runtime-host process remained after the test. The temporary loopback HTTP server was also stopped. A search of the latest disposable profile found no persisted `127.0.0.1:8765`, `example.invalid`, or test-token text in JSON/INI/TXT/LOG files.

## Package delivered for internal use

- EXE: `out\\LA-Studio-0.0.2.0\\LA-Studio-0.0.2.0.exe`
- Size: `21,312,512` bytes
- SHA-256: `EBE4C2E1D658BDA127E008181D5C073D3EB1B25B199D5AA506E4E75DCC140E03`
- Status: **internal-only**. The packaging command intentionally allowed the SHA-256-verified but unsigned eSpeak NG MSI. It must not be promoted as a distributable signed release.

## Handoff status

This report section is authoritative for the current continuation and supersedes any contradictory historical wording below. No new product feature was added. The repository is now waiting for a new instruction in `docs/AI_AGENT_REQUEST.md`; do not package or change source again merely because this report exists.

---

# Historical response -- Dubbing, shared Colab setup, voice library, and CapCut export

**Status:** source, regression and portable-package gates complete; source commit is on `main` (2026-07-30).

## Mandatory reference check before code

### Located reference sources

- Voice library reference: `C:\Users\Nguyen Trong Khoi\Downloads\Kova-1.0.1\Kova\voice-clone-desktop` (first-party Wails/Go desktop app, React UI, and Python worker/store/tests).
- Official OpenCut was cloned read-only as required to `C:\Users\Nguyen Trong Khoi\Downloads\OpenCut-reference`; `origin` is `https://github.com/OpenCut-app/OpenCut.git`. No reference-source file was modified.
- CapCut-format evidence available locally: `C:\Users\Nguyen Trong Khoi\Downloads\Kova-1.0.1\pyCapCut`, including its draft schema writer, `draft_meta_info.json` asset template, media/audio/text segment serializers, and documentation. This is usable for structural study only; it is not proof that a generated draft has been imported by a live CapCut installation.

### Patterns selected for LA Studio

- Keep Direct Colab URL/token transient in a session object; validate health, capability, CUDA, exact model, and contract before a stage can run.
- Persist a voice as structured metadata plus an app-owned copy of reference audio, validate it on reload, and delete only the owned copy. Use an atomic metadata write so a crash cannot leave a half-written library.
- Keep the selected voice preset as a snapshot at Dubbing-run start; do not select a reference per segment.
- Build editable export from explicit assets, tracks, microsecond timing, and a versioned manifest. Do not call it CapCut-compatible until live import evidence exists.

### LA Studio boundaries to change

- Global Dubbing setup: `AppController`, `DubbingController`, `ColabSession`, Dubbing QML setup/inspector components, and their tests.
- Voice library durability: `VoiceClonePresetService`, its consumers in `DubbingController`, QML library/selector surfaces, and Dubbing/project regressions.
- Export: `DubbingExportJob`, Dubbing media/project serialization, QML export choice, and export tests. This part will remain explicitly unverified for real CapCut import unless a local fixture or real import evidence is available.

No source code has been changed for this new task at the time of this entry. Existing STT/Dubbing worktree changes and `version-1` remain untouched.

## Continuation â€” OpenCut/Kova reference audit and media vertical slice

### Reference decisions recorded before this continuation

| Reference pattern | Concrete source | LA Studio boundary | Decision |
| --- | --- | --- | --- |
| Persistent panel entities, rather than recreating editor panels every render | `OpenCut-reference/apps/desktop/src/shell.rs` | Dubbing QML/controller ownership | **Reject as a direct implementation.** The official OpenCut checkout currently contains placeholder Browser/Preview/Inspector/Timeline panels only (`apps/desktop/src/panels/*.rs`) and its web editor is explicitly “Coming soon”; it has no project, asset, track, clip, serialization, undo, autosave, or exporter model to transplant. |
| Direct source staged before later processing | `Kova/internal/service/link2file.go` | `RemoteMediaImportService` â†’ `MediaIngestService` | **Adapt with a stricter boundary.** LA Studio downloads only an ordinary direct HTTPS URL into private staging, then validates it through FFprobe/normalization before it becomes project state. It does not reuse Kova yt-dlp, cookies, temporary browser sessions, or protected-source handling. |
| Two explicit stems and a hard failure when separation cannot produce them | `Kova/internal/service/source_audio_separation.go` | `DubbingJobRunner::startSourceSeparation` | **Adapt.** Dubbing no longer advances with original/normalized audio substituted for missing vocals or background. Both stems must exist, otherwise the stage errors. |
| Slot fitting, silence padding and non-overlap validation | `Kova/internal/service/dubbing/audio.go`, `tts.go` | existing `AudioTimelineMixer` / Dubbing timing services | **Reuse existing LA implementation.** Its controller/job boundary already owns fitting/mix; no parallel Kova-style pipeline was added. |
| App-owned reference copy plus store validation | `Kova/voice-clone-desktop/worker/src/kova_voice_studio/store.py`, `api.py`, `jobs.py`; desktop `app.go`, `worker_jobs.go` | `VoiceClonePresetService` | **Adapted earlier in this task.** Metadata is atomically committed; a preset owns a copied reference, validates checksum/missing state, and never deletes the imported external source. |
| CapCut draft material/track serialization | `Kova/pyCapCut/script_file.py`, `draft_folder.py`, `track.py`, `local_materials.py`, `segment.py`, `video_segment.py`, `audio_segment.py` | `CapCutDraftExporter` | **Adapt structural evidence only.** The generated draft remains labelled “import not yet verified” until a real local CapCut import is witnessed. |
| Immutable stage/artifact records and downstream invalidation | `Kova/internal/project/types.go`, `store.go`, `http.go` | existing `DubbingProject` + workflow graph/journal | **Reject as a direct transplant.** Kova’s SQLite state model is useful evidence for explicit revision/stage states, but replacing LA Studio’s versioned JSON project/workflow system would be an untested architecture rewrite. LA Studio keeps the existing workflow journal and invalidation rules. |
| Workdir + named output manifest | `Kova/internal/pipeline/workdir.go`, `manifest.go`, `render.go`, `tts.go` | `MediaIngestService`, `DubbingProject`, `DubbingExportJob` | **Adapt selectively.** LA Studio already has an app-owned cache workspace and atomic project/media manifests. The new link import uses a private staging boundary, and no Kova task URL is copied into LA project metadata. |
| Explicit review before CapCut compile, tool path validation, and output specification | `Kova/internal/capcutstudio/types.go`, `builder.go`, `internal/desktop/creator_studio.go` | `CapCutDraftExporter` + Dubbing export UI | **Adapt the safety posture only.** LA generates a self-contained structural draft and flags it unverified. It does not claim a live CapCut import or invoke non-existent CapCut tooling. |
| Portable desktop startup rooted beside the executable | `Kova/cmd/desktop/main.go` | LA Studio package script/runtime layout | **Reuse existing LA packaging design.** `scripts/package.ps1` creates a root-level portable EXE plus Qt, media and eSpeak runtimes; no Kova desktop code is copied. |

### Code corrected in this continuation

1. **No silent separation fallback.** `src/controllers/dubbing/DubbingJobRunner.cpp` previously emitted a completed `source-separate` node while substituting normalized input for either failed or missing stems. It now fails the node if the local runtime/model is absent, separation reports failure, or either required stem is absent/non-file. It therefore cannot send music-contaminated audio silently to STT or use it as a clone reference.
2. **Direct link import path added.** `src/dubbing/media/RemoteMediaImportService.{h,cpp}` downloads a user-entered direct HTTPS URL to app-owned staging with atomic save, byte-count progress, cancel, a 2 GiB limit, no cookies/credentials/browser profile, and no URL persistence. HTTP is restricted to loopback so the test can be offline and deterministic.
3. **Project gate added.** `DubbingController` starts `MediaIngestService` after download and does not replace source-media project state until probe/normalization succeeds. A failed link retains the previous project rather than leaving half-imported media.
4. **UI added.** `DubbingSourceMediaPanel.qml` now has an explicit direct-link field, Import link/Cancel actions, and status based only on actual bytes when a total is known. It never displays an invented percentage. `DubbingPage.qml` wires the actions to the controller.
5. **Voice source is explicit.** After source separation, the Dubbing inspector offers **Use clean vocals**. It opens the shared voice-library dialog prefilled with the generated vocal stem, but persistence still requires an explicit Save; temporary audio is copied into app-owned voice storage by `VoiceClonePresetService`.

### Final regression/package evidence

Commands run after the final source edits:

```powershell
$env:LASTUDIO_FFMPEG = (Resolve-Path 'out\LA-Studio-0.0.2.0\media-tools\ffmpeg.exe').Path
$env:LASTUDIO_FFPROBE = (Resolve-Path 'out\LA-Studio-0.0.2.0\media-tools\ffprobe.exe').Path
ctest --test-dir out\build\windows-msvc-tests -R '^TestMediaIngestService$' --output-on-failure
ctest --test-dir out\build\windows-msvc-tests -R '^TestDubbingProject$' --output-on-failure
ctest --test-dir out\build\windows-msvc-tests --output-on-failure
& '.\graphify\.venv\Scripts\graphify.exe' update .
```

- `TestMediaIngestService`: **passed**. It covers a loopback direct download, Unicode-safe owned staging, atomic result/no persisted query, byte-count progress, cancellation cleanup, 2 GiB `Content-Length` rejection before staging, scheme/user-info rejection, and controller-only project commit after an actual FFprobe/FFmpeg normalization run.
- `TestDubbingProject`: **passed**. It covers unavailable runtime/model, a genuine failed runtime-load backend path, and a completed result with a missing background stem; each case emits no completed source-separation node and never substitutes the original audio. Existing voice-library/project/two-speaker snapshot regressions also pass.
- Full CTest: **35/35 passed** in 24.91 seconds, including `TestRemoteExecution`, `PrepareQmlRouteSmokeRuntime`, and `QmlRouteSmoke`.
- Graphify update completed after source edits: 11,130 nodes, 21,352 edges, 531 communities. Its generated `graphify/` and `graphify-out/` are deliberately excluded from commits.

Portable internal package command:

```powershell
.\scripts\package.ps1 -SkipInstaller -PortableInternalLayout -Version '0.0.2.0' -MaxParallelJobs 1 `
  -QtRoot '.tools\Qt\6.9.3' -VcpkgRoot '.deps\vcpkg' -LlamaCppSourceDir '.deps\llama.cpp' `
  -AllowUnsignedEspeakForInternalBuild
```

- Result: **success**. The package script rebuilt the release target, deployed Qt/vcpkg/media/eSpeak dependencies, and verified its staging and license manifests.
- Executable: `C:\Users\Nguyen Trong Khoi\Downloads\LA-STUDIO\out\LA-Studio-0.0.2.0\LA-Studio-0.0.2.0.exe`
- Size: `21,312,000` bytes; SHA-256: `00A19BF41DD6CBCD3A7C1CC5D50E48831F8492B1B924BECD4AE20452C7DC2095`.
- Smoke: launched from the staged root with no special Qt environment; after 8 seconds it was alive and Windows reported `Responding=True`, then the exact smoke PID was stopped cleanly.
- The package is explicitly **internal only** because the verified eSpeak MSI was unsigned and `-AllowUnsignedEspeakForInternalBuild` was used. It must not be promoted as a distributable release without resolving that signing condition.

### Honest limits

- The controller regression uses a loopback HTTP fixture and real managed FFprobe/FFmpeg. It does not validate arbitrary internet links, DRM/paywall sources, or any bypass; those are intentionally unsupported.
- The separation success/live quality boundary still requires an actual model and local or Direct Colab GPU. The test injects deterministic failure/incomplete results at the existing service-result boundary and confirms fail-closed behavior; it does **not** claim vocal-quality validation.
- Voice library/reload/selection and two-speaker snapshot behavior are regression tested. This is not a subjective fidelity test of a live cloned voice.
- The CapCut draft is structurally tested and still named **“CapCut structure generated — import not yet verified.”** No live CapCut import is claimed.
- No operation targeted or modified `version-1`.

### Git record

- Source/QML/test commit on `main`: `484f563a6aa96334ee16ced72c9ade24dbc2b025` — `feat: harden dubbing remote media workflow`.
- This response report is committed separately after the source commit so it can record that exact immutable hash.
- Intentionally excluded: `docs/AI_AGENT_REQUEST.md`, `docs/AI_AGENT_HANDOFF_2026-07-30.md`, `.agents/`, `AGENTS.md`, `.graphifyignore`, `graphify/`, `graphify-out/`, all reference repositories, and all `out/` build/package artifacts. No push was performed.

---

# Historical response — Dubbing clone voice selection complete

**Completed:** 2026-07-30
**Task source:** `docs/AI_AGENT_REQUEST.md` — select one clone voice and keep it for a full Dubbing run.

## Outcome

The active Dubbing task is complete at source/build/test level. A user now selects one saved clone-voice preset for the project; that preset is captured when synthesis starts and is used for every segment, including segments assigned to different speakers. Dubbing no longer selects a source-audio window, a random voice, or a per-segment fallback.

## Root cause

`VoiceClonePresetService` already contained the reusable voice library, but Dubbing never consumed it. The Dubbing QML only exposed an **auto-select clean source reference** toggle. `DubbingSynthesisJob` then called `DubbingVoiceReferenceSelector::select(...)` on source media; the result was temporary and had no durable project preset ID. The workflow adapter also forwarded source reference audio into synthesis settings.

## Fixed data flow

1. `AppController` injects `VoiceClonePresetService` into `DubbingController`.
2. `DubbingController` exposes valid presets for the current clone-model family, refreshes on `presetsChanged`, and exposes selected/valid/error state to QML.
3. `DubbingProject` schema **8** persists only `cloneVoicePresetId`; no worker URL, token, or temporary Colab profile ID is persisted.
4. Dubbing's node inspector now has a saved-preset selector, an empty state, and **Create or import clone voice**. Synthesis is blocked with a clear diagnostic when no valid preset exists.
5. At run start, the controller passes a snapshot of the selected preset to `DubbingSynthesisJob`. The job validates it once, includes its ID in the cache signature, and reuses the same reference for every segment.
6. Each generated segment records `cloneVoicePresetId`, preset name, and reference metadata. A changed preset causes the following run to use the new voice globally rather than cache the old voice.
7. Legacy automatic-reference settings now fail with a saved-voice diagnostic rather than silently falling back. Voice cloning remains Direct Colab only; Gateway receives no clone reference or Colab credential.

## Changed files for this task

- `src/controllers/app/AppController.cpp`
- `src/controllers/dubbing/DubbingController.{h,cpp}`
- `src/controllers/dubbing/DubbingSynthesisJob.{h,cpp}`
- `src/dubbing/DubbingProject.{h,cpp}`
- `src/dubbing/workflow/DubbingWorkflowAdapter.cpp`
- `src/dubbing/workflow/DubbingWorkflowDefinition.cpp`
- `qml/components/dubbing/DubbingNodeInspector.qml`
- `qml/pages/DubbingPage.qml`
- `tests/test_DubbingProject.{h,cpp}`

Pre-existing STT changes and user-authored worktree files were preserved. `version-1` was not touched.

## Regression evidence

New/updated Dubbing tests verify:

- a saved preset is exposed/selected and survives project save/reopen;
- removing the selected preset preserves its ID and produces a missing-voice diagnostic instead of selecting another voice;
- zero preset/settings blocks before synthesis with no fallback;
- two segments with two speakers use the same selected preset ID/reference;
- changing A to B before the next run applies B to every segment of the next run;
- Direct Colab/Gateway boundaries and clone consent checks remain intact;
- QML source checks require the selector/create-import dialog and reject the old automatic-reference label.

Successful commands:

```powershell
cmake --build out\build\windows-msvc-tests --target LAStudioUnitTests -j 1
ctest --test-dir out\build\windows-msvc-tests -R '^TestDubbingProject$' --output-on-failure
ctest --test-dir out\build\windows-msvc-tests -R '^(TestRemoteExecution|QmlRouteSmoke)$' --output-on-failure
ctest --test-dir out\build\windows-msvc-tests --output-on-failure
& ".\graphify\.venv\Scripts\graphify.exe" update .
```

Results: **TestDubbingProject passed; remote/QML smoke passed; full suite 35/35 passed.**

## Limits and next state

- No live Colab GPU worker was available, so this is not proof of live voice fidelity or long-running remote reliability.
- No release package/EXE was created or manually tested for this task.
- No commit or push was made, as the request forbids it unless separately requested.

The active task is complete. Read the next instruction from `docs/AI_AGENT_REQUEST.md`, complete it, then add the next result to this report. The STT-only report below is historical, not the current task status.

---

# Historical response — STT progress reporting

**Thời điểm:** 2026-07-30
**Phạm vi đã hoàn thành:** khắc phục một lỗi tái lập được của luồng Speech-to-Text (STT) qua Direct Colab/Gateway: phần trăm tiến độ không trung thực.

## Kết quả

Đã xác nhận và sửa lỗi desktop tự tạo tiến độ cho STT từ các mốc nội bộ thay vì số đo do worker trả về. Đây là lỗi UI/UX có thể tái lập bằng mock loopback, không cần GPU Colab thật.

### Root cause

`ColabSttRunner` trước đây phát các mốc `4`, `5`, `20` ở desktop, rồi co giãn phần trăm từ worker vào khoảng `20–95`. Các số này không phải tiến độ inference mà worker đo được. Vì `SttInputSection.qml` luôn hiển thị `Processing... N%`, người dùng có thể thấy phần trăm không phản ánh công việc thực tế.

### Luồng trước và sau

| Trước sửa | Sau sửa |
|---|---|
| Desktop tự báo 4%/5%/20% khi mã hóa WAV và upload. | Không phát % tổng quát cho các bước đó. |
| `%` do worker báo bị biến đổi thành 20–95%. | Chỉ chuyển tiếp giá trị 1–99% do worker báo. |
| UI luôn hiển thị `Processing... N%`. | UI chỉ hiện `%` khi `progressAvailable`; nếu worker chưa đo được thì hiện `Processing...`. |
| Gateway STT không có cách báo UI rằng % chưa khả dụng. | `SttSessionController` dùng chung `progressAvailable` cho Colab và Gateway. |

## Các file đã sửa

- `src/stt/ColabSttRunner.cpp`
- `src/controllers/stt/SttSessionController.h`
- `src/controllers/stt/SttSessionController.cpp`
- `qml/components/stt/SttInputSection.qml`
- `tests/test_SttSession.cpp`

## Regression và bằng chứng

Regression mới trong `TestSttSession::testColabSttRunnerUsesAsynchronousJobContract` dùng worker loopback:

- Worker mock chỉ báo 50% khi đang chạy, rồi kết thúc thành công.
- Trước sửa, test fail vì runner phát **7** sự kiện progress thay vì **2**.
- Sau sửa, test xác nhận chính xác hai sự kiện: `50` (worker đo được) và `100` (hoàn tất).

Các lệnh đã chạy thành công:

```powershell
cmake --build out\build\windows-msvc-tests --target LAStudioUnitTests -j 1
ctest --test-dir out\build\windows-msvc-tests -R '^TestSttSession$' --output-on-failure
ctest --test-dir out\build\windows-msvc-tests -R '^(TestRemoteExecution|QmlRouteSmoke)$' --output-on-failure
ctest --test-dir out\build\windows-msvc-tests --output-on-failure
```

Kết quả suite đầy đủ: **35/35 tests passed**. Đã chạy `graphify update .` sau source edit.

## Chưa được xác minh / không được tuyên bố

- Chưa kiểm thử live một GPU Colab thật vì không có URL worker, token, và runtime GPU đang chạy để dùng trong phiên này.
- Chưa tạo hay kiểm thử package/EXE sạch từ đầu.
- Không có bằng chứng nào ở trên chứng minh chất lượng transcript/âm thanh hay toàn bộ tính năng hoạt động ngoài môi trường mock/loopback.
- Không commit, push hoặc đóng gói vì yêu cầu tác vụ trước đó cấm các hành động này.

## Trạng thái cho agent tiếp theo

`docs/AI_AGENT_REQUEST.md` hiện đã chuyển sang nhiệm vụ mới: sửa danh sách chọn clone voice và bảo đảm một voice cố định cho toàn bộ lần Dubbing. Nhiệm vụ đó **chưa được thực hiện trong báo cáo này**.

Agent tiếp theo cần:

1. Đọc đầy đủ `docs/AI_AGENT_HANDOFF_2026-07-30.md`, `docs/AI_AGENT_REQUEST.md` và báo cáo này.
2. Kiểm tra worktree trước khi sửa; không ghi đè các thay đổi STT ở trên hay tài liệu đang sửa cục bộ.
3. Dùng Graphify theo lệnh trong `AI_AGENT_REQUEST.md` trước khi đọc source Dubbing diện rộng.
4. Không coi 35/35 test ở trên là bằng chứng cho yêu cầu Dubbing mới; phải thêm regression riêng cho voice selector, persistence và một voice duy nhất xuyên suốt synthesis.
