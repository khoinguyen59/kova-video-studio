# Current response — Dubbing, shared Colab setup, voice library, and CapCut export

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
