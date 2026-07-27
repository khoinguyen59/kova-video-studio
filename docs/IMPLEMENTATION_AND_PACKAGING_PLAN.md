# LA Studio — Implementation, Testing, Build and Packaging Plan

**Status:** Draft for review — findings independently re-verified against the working tree at `0aaf446` on 2026-07-26 (all primary claims confirmed; catalog-hash and i18n counts corrected to measured values)
**Scope:** LA Studio v0.2.0 → first production-quality Windows release
**Audience:** Technical Lead, Release Engineer, maintainers
**Baseline commit:** `0aaf446` (branch `main`, tree clean)
**Date:** 2026-07-26

> This document is written in English to match the policy stated in [docs/README.md](README.md)
> ("Public documentation in this directory should be written in English, avoid machine-specific
> paths, and describe stable project behavior").
>
> Every claim below was derived by reading the repository. Findings carry `file:line` evidence so
> they can be re-verified independently. Items that could **not** be verified from the repository
> alone are collected in [§7 Open Questions and Assumptions](#7-open-questions-and-assumptions)
> rather than guessed at.

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Current State Assessment](#2-current-state-assessment)
3. [Proposed Technical Decisions and Rationale](#3-proposed-technical-decisions-and-rationale)
4. [Phased Plan](#4-phased-plan)
5. [Build, Test, Packaging and Release Strategy](#5-build-test-packaging-and-release-strategy)
6. [Risk Register](#6-risk-register)
7. [Open Questions and Assumptions](#7-open-questions-and-assumptions)
8. [Pre-Release Acceptance Checklist](#8-pre-release-acceptance-checklist)

---

## 1. Executive Summary

LA Studio is a substantial and architecturally coherent product: ~50,800 lines of C++17 and
~31,300 lines of QML across 114 QML files, organised into clean controller / service / backend
boundaries, with a genuinely good out-of-process runtime host, a catalog-driven model system, and
an exemplary versioned project format in `DubbingProject`. The application layer is in far better
shape than most projects at this stage — there are **zero** `TODO`/`FIXME`/`HACK` markers in the
tree, and both the QML file list and the C++ source list are exactly consistent with `CMakeLists.txt`
(114 = 114, 154 = 154, no orphans in either direction).

The gap is not in the application. **It is in the delivery chain around it.** The repository builds,
tests and ships through a pipeline that has never been closed:

- A **clean clone cannot configure**. The documented quick start (`.\scripts\bootstrap.bat`) fails at
  `CMakeLists.txt:32` because `scripts/build.ps1` never supplies `-DLLAMA_CPP_SOURCE_DIR` and nothing
  provisions the llama.cpp b10036 headers. Only CI and `package.ps1` pass that flag.
- **No CI runs on push or pull request.** The single workflow triggers only on `v*.*.*` tags. An
  18-file, 123-test Qt Test suite exists, is registered with CTest, is compiled by every build — and
  is executed by nothing in automation.
- **The shipped installer is missing components the developer build has.** `scripts/package.ps1` —
  the script CI actually calls — never stages eSpeak NG; only `scripts/build.ps1` does, and into a
  different directory. Dubbing phoneme budgeting therefore degrades silently in every released build
  while working perfectly on every developer machine.
- **FFmpeg is an undocumented, unbundled hard dependency** of Video Dubbing, source-separation
  decode and audio fallback. On a stock Windows machine the flagship v0.2.0 feature does not work.
- **Nothing is signed, hashed, or verified.** No Authenticode signature, no published SHA-256, and
  only 10 of the catalog's 75 `releases/download` URL references carry a checksum (8 runtime
  archives + the 2 CUDA `cudart` dependency archives) — yet those archives contain native
  DLLs that are `LoadLibrary`'d into the application process. The in-app updater downloads an `.exe`
  chosen by filename substring and launches it elevated (`ShellExecuteExW` verb `runas`) with the
  only guard being `QFileInfo::exists()`.
- **The installer ships no license text at all.** `LicenseFile` is commented out
  (`scripts/installer.iss.in:14`) and no `install()` rule places `LICENSE` in the staging tree, while
  the installer redistributes Qt 6 (LGPLv3), libcurl, zlib, 7-Zip and the MSVC runtime. This is a
  plain AGPLv3 §4/§6 and LGPLv3 §4 defect on the current artifact.

None of these are deep architectural problems. They are a finite, well-bounded set of pipeline,
compliance and hardening tasks. The plan below sequences them into **eight phases** on a critical
path that starts with making the build reproducible from a clean clone, because every other gate —
CI, tests, packaging assertions, smoke tests, signing — is unverifiable until that is true.

**Recommended release posture:** do not ship a public "1.0"-grade release until Phases 0–3 and 6 are
complete. Phases 4–5 (product-readiness and deeper hardening) can be sequenced into follow-up
releases provided the gaps are documented. A realistic estimate for Phases 0–3 + 6 is
**6–9 engineer-weeks** for a small team; the full plan is **14–20 engineer-weeks**. See
[§4.10](#410-effort-summary) for the breakdown and the assumptions behind it.

**The three decisions that must be made before engineering starts** (they change the plan's shape,
not just its content):

| # | Decision | Why it blocks |
|---|---|---|
| D1 | Is a code-signing certificate procured/budgeted? | EV/OV issuance takes days–weeks; it gates the entire trust story (installer, updater, SmartScreen). Start procurement on day 1 regardless. **A zero-cash route exists — SignPath Foundation free OSS signing (D-17, Appendix B); apply on day 1 instead of purchasing.** |
| D2 | How does FFmpeg reach the user — bundled LGPL build, managed catalog runtime, or documented prerequisite? | Determines packaging work, installer size, and the license notices required. |
| D3 | Does the CC BY-NC 4.0 model stay in the default catalog? | The README grants users commercial use; the catalog silently offers a non-commercial model. These contradict. |

---

## 2. Current State Assessment

### 2.1 What the product is

| Attribute | Value | Evidence |
|---|---|---|
| Type | Offline AI audio workstation, native Windows desktop | [README.md](../README.md) |
| Language / UI | C++17, Qt 6.9.3 (Quick, QuickControls2, Multimedia, Network, Concurrent, Sql) | [cmake/Dependencies.cmake:1](../cmake/Dependencies.cmake), [cmake/CompilerOptions.cmake:1](../cmake/CompilerOptions.cmake) |
| Build | CMake ≥ 3.21 + Ninja + MSVC 2022 x64, CMake presets | [CMakePresets.json](../CMakePresets.json) |
| C++ deps | vcpkg manifest mode: `curl`, `zlib` (baseline pinned) | [vcpkg.json](../vcpkg.json) |
| Binaries | `LA Studio.exe` (GUI) + `LAStudioRuntimeHost.exe` (out-of-process native model host) | [CMakeLists.txt:399](../CMakeLists.txt), [CMakeLists.txt:607](../CMakeLists.txt) |
| Extra target | `vietnorm::vietnorm` — Vietnamese text normalisation library | [src/textnorm/CMakeLists.txt](../src/textnorm/CMakeLists.txt) |
| Installer | Inno Setup 6, single-file EXE, per-machine `{autopf}` | [scripts/installer.iss.in](../scripts/installer.iss.in) |
| License | AGPL-3.0-only | [LICENSE:1-8](../LICENSE) |
| Size | ~50,800 LOC C++/headers, ~31,300 LOC QML (114 files) | `git ls-files … \| xargs wc -l` |
| Version | `LASTUDIO_VERSION "0.2.0"`; 5 tags exist (`v0.1.7` … `v0.2.0`) | [CMakeLists.txt:5](../CMakeLists.txt), `git tag --list` |

**Feature surface** (all verified as genuinely implemented and routed, not stubs): Speech-to-Text,
Text-to-Speech, Voice Cloning, Voice Design, Voice Isolator (source separation), Alignment Studio,
Translation Studio, Video Dubbing, LLM Chat, Models Gallery + Hugging Face downloads, Runtime
management, and a local OpenAI-compatible HTTP API server.

### 2.2 What is genuinely strong

These are load-bearing strengths the plan preserves and builds on — they should not be refactored:

1. **Source-of-truth consistency.** 114 QML files ↔ 114 `QML_FILES` entries; 154 `src/**/*.cpp` ↔
   144 in `LASTUDIO_SOURCES` + 15 in the runtime-host target + 4 in `src/textnorm`. Both diffs are
   empty in both directions. No dead-unbuilt code, no CMake entry pointing at a missing file.
2. **No conventional debt markers.** Repo-wide search for `TODO|FIXME|HACK|XXX|not implemented|
   Q_UNIMPLEMENTED|qFatal` across `src/`, `qml/`, `include/`, `tests/`, `scripts/` returns nothing.
3. **Out-of-process runtime isolation.** `LAStudioRuntimeHost.exe` runs OmniVoice, whisper.cpp and
   llama.cpp in a separate process over an authenticated Windows named pipe
   (`QLocalServer::UserAccessOption`, 128-bit token, non-Hello frames rejected until authenticated),
   with audio passed through capped shared memory. A host process is permanently bound to one
   adapter id so incompatible `ggml.dll` variants never share a module table.
   ([src/runtimehost/RuntimeHostServer.cpp:36-45,101-138](../src/runtimehost/RuntimeHostServer.cpp))
4. **`DubbingProject` persistence is exemplary** — `CurrentSchemaVersion = 7`, explicit version-range
   guard, per-version migration branches, `QSaveFile` atomic commit. This is the pattern the rest of
   the app's persistence should be migrated to.
   ([src/dubbing/DubbingProject.cpp:63-131](../src/dubbing/DubbingProject.cpp))
5. **`ModelLifecycleController`** implements a clean load/unload state machine with pending-operation
   queueing across Loading/Unloading/Processing.
6. **Download resilience.** 4 attempts, `CURLOPT_RESUME_FROM_LARGE` resume from the partial file, a
   retryable-error whitelist, atomic rename on success.
   ([src/core/HFHubClient.cpp:249-352](../src/core/HFHubClient.cpp))
7. **TLS is correct where it exists.** `CURLOPT_SSL_VERIFYPEER=1` on all three curl call sites; no
   `ignoreSslErrors`, no `QSslConfiguration` weakening, no `CAINFO` override anywhere. The only
   `http://` URLs in the tree are loopback.
8. **No telemetry, no analytics, no crash upload.** The offline claim is substantially true for the
   inference path; the catalog is a bundled file, not a fetched one.
9. **Log rotation is implemented and bounded** — 5 MB × 5 files + active, checked at startup and
   after each write. ([src/core/Logger.h:38-39](../src/core/Logger.h))
10. **Catalog generation is deterministic and currently in sync.** Regenerating `data/catalog.json`
    from `catalog-src/` produces a byte-identical file. Nothing *enforces* this, but the discipline
    has held.

### 2.3 Release-blocking defects

Ordered by the sequence in which they must be fixed. "Blocker" means: shipping without fixing this
produces a defective, non-compliant, or unverifiable artifact.

#### B1 — A clean clone cannot configure (build blocker)

`CMakeLists.txt:30-35` raises `FATAL_ERROR` unless `LLAMA_CPP_SOURCE_DIR` resolves to a llama.cpp
b10036 checkout containing `include/llama.h` and `ggml/include/ggml.h`. Resolution order is
`$ENV{LLAMA_CPP_SOURCE_DIR}` → `<src>/.deps/llama.cpp` → `<src>/../llama.cpp`.

- `scripts/build.ps1` (what `bootstrap.ps1` invokes) never passes `-DLLAMA_CPP_SOURCE_DIR` and never
  clones llama.cpp. Only `scripts/package.ps1:153-171,354` resolves it.
- `docs/BUILD.md:22-29` lists prerequisites as VS 2022, Qt 6.5+, CMake 3.21+, Ninja, Git — llama.cpp
  is not mentioned.
- `.deps/` does not exist in a fresh clone (verified: `ls -d .deps` → no such directory).
- The requirement is real, not vestigial: `src/translation/LlamaTranslationInterface.cpp:18-19`
  genuinely includes `<ggml-backend.h>` and `<llama.h>`.

This is the **first CMake failure**, reached only after `bootstrap.ps1` has already resolved Qt and
cloned/bootstrapped vcpkg (`bootstrap.ps1:140-171`) — those throw first if Qt or vcpkg are missing.
The entire documented onboarding path in README.md is therefore broken.

**It is not only the app.** `scripts/run_tests.ps1:183-188` delegates to `bootstrap.ps1` when the
build tree is absent, which delegates to `build.ps1` — so `bootstrap.bat`, `build.bat`,
`run_tests.bat` and `setup.bat` **all fail identically** on a fresh machine. `package.ps1` is the
only script in the repo that resolves and passes the flag, and it is invoked only by CI.
**The tagged-release CI job is the sole configure path that has ever worked from a clean state.**

**Related — `CMakePresets.json` is not self-sufficient.** The presets carry only `CMAKE_BUILD_TYPE`,
generator, `binaryDir`, `CMAKE_EXPORT_COMPILE_COMMANDS` and `cl.exe`. There is no `toolchainFile`, no
`CMAKE_PREFIX_PATH`, no `VCPKG_TARGET_TRIPLET`, no `LLAMA_CPP_SOURCE_DIR`. Every dependency arrives
as a command-line argument assembled by a PowerShell script. Consequences: `cmake --preset
windows-msvc-release` **alone can never configure**, so IDE / CMake Tools / clangd workflows and any
non-PowerShell contributor have no supported entry point — and the presets and scripts can silently
drift apart. **They already have:** `package.ps1` passes `-DLLAMA_CPP_SOURCE_DIR` and `build.ps1`
does not.

#### B2 — eSpeak NG never reaches the installer

`Ensure-EspeakNgRuntime` exists **only** in `scripts/build.ps1:272-343` and stages into
`out/build/<preset>/espeak-ng`. `scripts/package.ps1` contains zero `espeak` references
(verified by grep), and `.github/workflows/windows-release.yml:65` calls `package.ps1` directly —
never `build.ps1`. CMake `install()` rules cover only the two targets plus `resources/`.

Runtime consequence, traced through the code:
`EspeakNgPhonemizer::api()` fails to load → `count()` returns `-1`
([src/dubbing/EspeakNgPhonemizer.cpp:80-88,180](../src/dubbing/EspeakNgPhonemizer.cpp)) →
`DubbingDurationPlanner::countPhonemes` clamps to `0`
([src/dubbing/DubbingDuration.cpp:141-147](../src/dubbing/DubbingDuration.cpp)) →
`DubbingTranslationFixService::isOverBudget` returns `false` for every segment
([src/controllers/dubbing/DubbingTranslationFixService.cpp:51-67](../src/controllers/dubbing/DubbingTranslationFixService.cpp)).

There is no user-facing error — only a line in `app.log`. This is a dev/CI divergence that is
structurally invisible to local testing, because every developer's machine ran `build.ps1` at some
point.

**The precise behaviour is worse than "missing", and worth stating carefully** — an early reading of
this defect as "eSpeak is undiscoverable in packaged installs" is too strong, and the true shape
changes the fix:

A second delivery path exists. eSpeak NG is also a catalog `dependencyDownload` (`espeak-ng.msi`
1.52.0) attached to the CrispASR runtimes, extracted via `msiexec /a` into the runtime tree under
`~/.lastudio` (`DownloadInstallService.cpp:771-808`). `EspeakNgPhonemizer` itself searches only
`applicationDirPath()`, `applicationDirPath()/espeak-ng`, the directory of any `espeak-ng` on
`PATH`, and finally a bare-name `QLibrary("libespeak-ng")`. **But** `CrispCommon.h:62-65` explicitly
prepends the runtime's `espeak-ng`, `espeak-ng/bin` and `espeak-ng/eSpeak NG[/bin]` directories to
the process `PATH`, and `crispPreloadRuntimeDlls` `LoadLibraryW`s `libespeak-ng.dll` with a dedicated
priority slot.

So the accurate statement is:

> **Phonemization works only if a CrispASR runtime carrying the eSpeak dependency was installed
> *and* its interface initialised earlier in the same process — and it is disabled for the rest of
> the process lifetime if the first phoneme call loses that race.**

`EspeakNgPhonemizer::api()` **permanently negative-caches the failure**
(`EspeakNgPhonemizer.cpp:74-79`: `if (value.initialized || !value.error.isEmpty()) return value;`) —
a function-local static with no retry and no invalidation hook. A user who opens the Dubbing studio
before loading any Crisp runtime disables phoneme budgeting until they restart the application.

**This makes the defect order-dependent, silent and non-reproducible in bug reports** — the worst
combination for field diagnosis. The fix therefore has four parts, not one:
1. Stage eSpeak NG in `package.ps1` (deterministic location next to the executable).
2. Extend the phonemizer's search roots to the catalog-installed runtime location.
3. **Remove the permanent negative cache** so a later successful probe can recover.
4. Remove the bare-name `QLibrary("libespeak-ng")` fallback — it resolves through the OS search
   order over a `PATH` the app itself mutated, which is a DLL-planting hole (see B7).

#### B3 — FFmpeg is an unbundled, undocumented hard dependency

`ffmpeg`/`ffprobe` are resolved from the `LASTUDIO_FFMPEG` environment variable or `PATH`
([src/audio/AudioFileDecoder.cpp:138-144](../src/audio/AudioFileDecoder.cpp)) and are required by:

- Video Dubbing media ingest and export/mux (`src/dubbing/media/MediaIngestService.cpp`, `MediaToolService.cpp`)
- Source-separation decode (`src/separation/SeparationWorker.cpp:93`)
- Audio timeline rendering (`src/audio/AudioTimelineRenderer.cpp:41`)
- The `AudioFileDecoder` fallback when Qt Multimedia cannot decode a file

It is **not** bundled, **not** in the catalog (`grep ffmpeg data/catalog.json` → no matches), **not**
installed by Inno Setup, and **not listed as a prerequisite** in README.md or docs/BUILD.md — it
appears only in `docs/architecture/source_separation.md`. On a clean Windows machine the headline
v0.2.0 feature fails with a generic message.

#### B4 — No CI, no test execution, no artifact verification

`.github/workflows/windows-release.yml:3-6` triggers **only** on `v*.*.*` tag pushes. Its steps are
checkout → llama.cpp headers → MSVC → Qt → vcpkg → Inno Setup → 7-Zip → `package.ps1` → rename →
GitHub Release. There is no build check on PR, no `ctest`, no `qmllint`, no smoke test of the
produced installer, and no artifact retention beyond the Release itself.

`include(CTest)` at `CMakeLists.txt:54` means `BUILD_TESTING` defaults ON, so the ~120-translation-unit
test target is *compiled* by every release build and then discarded.

**Aggravating factor:** `.gitignore:113-118` ignores `.github/*` and `.github/workflows/*` with a
single re-inclusion for `windows-release.yml`. Verified: `git check-ignore -v .github/ci.yml
.github/CODEOWNERS .github/dependabot.yml` reports all three ignored. **Any new CI file added by a
teammate will silently not be tracked.** This must be fixed before any other CI work.

#### B5 — The installer ships no license text

`scripts/installer.iss.in:14` is `; LicenseFile=../LICENSE` (commented out). `CMakeLists.txt:704-716`
installs only the two targets and `resources/` (which contains only `app_icon.rc`, a build-time file
with a dangling relative path that should never ship). `LICENSE` is never installed.

Meanwhile the installer provably redistributes: Qt 6 (7 modules, LGPLv3), libcurl, zlib,
7-Zip `7z.exe` + `7z.dll`, and the MSVC runtime. There is no `THIRD-PARTY-NOTICES`, no `NOTICE`, no
`COPYING`, and zero SPDX headers across 494 tracked source files
(`git grep -l 'SPDX-License-Identifier' -- '*.cpp' '*.h' '*.qml' | wc -l` → 0).

**AGPLv3 §4 requires a copy of the license to accompany each conveyed copy, and LGPLv3 §4 requires
the license texts and a relinking notice for the bundled Qt DLLs. Neither is satisfied.** That is a
defect on the artifact as currently produced, independent of any other consideration.

**§6 (Corresponding Source) is a weaker claim and should not be treated as a blocker.** §6(d)
permits conveying object code by offering access from a designated place, provided equivalent access
to the Corresponding Source is offered *in the same way through the same place*. The installer is
served from the GitHub Releases page of a public repository whose source sits at the same place, at
no charge — a defensible §6(d) posture **for the copyright holder's own unmodified release**. The
residual real risks are that (a) no §6 route is documented, so nothing pins the source to the exact
tag, and (b) anyone who mirrors the `.exe` elsewhere loses §6(d) and inherits a §6(b) written-offer
duty with no template provided. **Action: document the route and attach a tagged source archive —
not a release blocker.**

Similarly, **AGPL §13** conditions the remote-user source offer on *"if you modify the Program"*.
The upstream holder shipping an unmodified build incurs no §13 duty. The gap is that any downstream
party who modifies LA Studio and enables LAN mode is immediately non-compliant with no affordance
provided — cheap insurance for redistributors (task 4.7), **not** a v0.2.0 compliance defect.

#### B6 — Unsigned, unverified update and download chain

| Control | State | Evidence |
|---|---|---|
| Authenticode signing of installer/exes | **Absent** | repo-wide grep for `signtool`/`certificate` → no packaging hits |
| Published SHA-256 of the release artifact | **Absent** | workflow uploads only the `.exe` |
| Hash check before the updater elevates | **Absent** | only guard is `QFileInfo::exists()`, [AppUpdateService.cpp:177-197](../src/controllers/shared/AppUpdateService.cpp) |
| Signature check before the updater elevates | **Absent** | no `WinVerifyTrust` anywhere |
| SHA-256 on runtime archives | **8 entries** (plus 2 `cudart` dependency archives — 10 non-empty `sha256` fields against 75 `releases/download` URL references in the catalog) | `data/catalog.json`; empty hash short-circuits to *pass* at [DownloadInstallService.cpp:384-388](../src/controllers/models/DownloadInstallService.cpp) |
| SHA-256 on model weight files | **None** | non-archive install path performs no hash check |
| Verification of `espeak-ng.msi` before `msiexec` | **None** | [DownloadInstallService.cpp:771-808](../src/controllers/models/DownloadInstallService.cpp) |

The updater selects an asset by filename predicate (must end `.exe` **and** contain `setup`,
`windows` and `x64` — `AppUpdateService.cpp:48-55`), takes `browser_download_url` verbatim from a
single hardcoded releases API URL, downloads it, and runs `ShellExecuteExW` with `lpVerb = "runas"`
and `/VERYSILENT`. **TLS is the only integrity control on an admin-level code path.**

Two mitigating details, stated so the risk is not overdrawn: the install is **not** silent from the
user's perspective — `qml/Main.qml:121-128` gates `installDownloadedUpdate()` behind a confirmation
dialog, and Windows then shows a UAC prompt. And `runas` elevates to the invoking user's
administrator token, not SYSTEM. Neither changes the conclusion: the user is being asked to approve
elevation of a binary that **neither they nor the application can verify.**

#### B7 — DLL loading is unhardened against a user-writable runtime tree

Runtime packages install to `~/.lastudio/extensions/backends` — a user-writable path. Three
inconsistent load strategies coexist:

- `SetDllDirectoryW` + `QLibrary::load` — **eight** interfaces (CrispKokoro, CrispNemotronStt,
  CrispQwen3Stt, CrispQwen3Tts, CrispVibeVoice, CrispVoxCpm2, Omnivoice, Whisper)
- `qputenv("PATH", …)` + **blanket `LoadLibraryW` of every `*.dll` found recursively**, with no
  flags at all ([include/runtimes/CrispCommon.h:67-137](../include/runtimes/CrispCommon.h))
- Hardened `LoadLibraryExW(… LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)` —
  **two** interfaces (`KokoroVietnameseInterface.h:101,247`, `VieneuTtsInterface.h:187,410`)

`SetDefaultDllDirectories` / `AddDllDirectory` are called **nowhere**. The `PATH` mutation is
permanent for the process lifetime and also changes executable resolution for the later unqualified
`msiexec` launch. The bare-name `QLibrary("libespeak-ng")` fallback in `EspeakNgPhonemizer.cpp:80`
resolves through that same mutated `PATH` — a further planting hole.

The good news: two interfaces already use the hardened pattern successfully with these DLLs, so
consolidating onto it (D-07) is a known-good migration rather than a bet.

#### B8 — `WavIO::loadAsFloat` is a heap-corruption primitive reachable from any opened audio file

[src/audio/WavIO.cpp:60-95](../src/audio/WavIO.cpp) parses attacker-controlled 32-bit header fields
with no bounds arithmetic. Three distinct defects, in the order they are reached:

1. **Unbounded `fmt` pointer.** The chunk walk does `ptr += 8 + chunkSize` (`:72`) with no overflow
   or remaining-length check, so `fmt = reinterpret_cast<const FmtChunk*>(ptr)` (`:66`) can point
   past `end`. Lines `:81-84` then dereference `sampleRate` / `numChannels` / `bitsPerSample`
   **out of bounds — before any other validation is reached.**
2. **Out-of-bounds write, not merely a read.** `result.samples.resize(static_cast<int>(numTotalSamples))`
   (`:86`) narrows `size_t → int`, while the copy loops (`:90-95`) iterate the full `size_t` count.
   A `data` chunk size above 2³¹ bytes yields an undersized (or negative) `QVector` and then writes
   past it. **This is a heap-corruption primitive from a plain `.wav` file.**
3. Divide-by-zero on `bitsPerSample == 0`.

It is reached from `SttAudioDecoder.cpp:30-37`, `AudioFileDecoder.cpp:196` and
`AudioTimelineRenderer.cpp:55` — i.e. **every audio file the user opens**, including the output of
the FFmpeg decode fallback.

> Severity note: this was initially characterised across the audit as an "unbounded read". The
> `size_t → int` narrowing at `:86` makes it a write primitive. Treat it as the highest-priority
> code-level defect in the tree.

#### B9 — Local API server authorization does not fail closed

[src/api/ApiServerService.cpp:631-652](../src/api/ApiServerService.cpp): `checkAuthorization()`
returns `true` when the API key is empty, **and** returns `true` for any loopback peer even when a
key is set. It also accepts the key as an `?api_key=` query parameter. There is no `Origin`
validation, no CORS policy, no CSRF token, and `/health` is served *before* the auth check and leaks
configuration (`allow_lan`, `api_key_required`, engine readiness).

The server is off by default and loopback-only by default, which bounds the exposure — but once
enabled, any web page the user visits can issue fire-and-forget POSTs to `/v1/audio/speech` and
`/v1/audio/transcriptions`. With `allowLan` on it binds `0.0.0.0:3900` in cleartext.

#### B10 — No on-disk state versioning

| Store | Versioned? | Corruption detection? | Evidence |
|---|---|---|---|
| `DubbingProject` (`.json`) | **Yes — v7 + migrations** | Yes | `DubbingProject.cpp:63-131` |
| SQLite registry | No `user_version`, no `PRAGMA integrity_check`; `schema_migrations` table created but never read or written | No rebuild path | `RegistryManager.cpp:196-240`, `data/registry_schema.sql:4-8` |
| `settings.ini` | No schema key, `QSettings::status()` never checked | No | `Settings.cpp:126-140` |
| `HistoryRepository` (`.json`) | No | Non-atomic truncate-then-write, no size cap | `HistoryRepository.cpp:95-105` |

The registry schema is applied as pure `CREATE TABLE IF NOT EXISTS`. **The first release that adds a
column will silently not apply it to existing installs** — and will not reproduce on clean installs
or in CI, so it cannot be caught before shipping.

### 2.4 Significant non-blocking gaps

| # | Gap | Evidence |
|---|---|---|
| G1 | **No rollback path.** Updater refuses any version ≤ current (`AppUpdateService.cpp:315`); installer has no `AppId` GUID and no `VersionInfoVersion`; `[Files]` lacks `ignoreversion` so a downgrade silently skips versioned DLLs, leaving a mixed-version `bin/`. | `scripts/installer.iss.in:4-32` |
| G2 | **No release debug symbols.** Release preset sets only `CMAKE_BUILD_TYPE=Release`; no `/Zi`, no `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`, no PDB archival. Shipped crashes cannot be symbolicated. | `CMakePresets.json:28-35`, `cmake/CompilerOptions.cmake:11-17` |
| G3 | **No `VERSIONINFO`.** `resources/app_icon.rc` is 2 lines (icon only); `LAStudioRuntimeHost` gets no `.rc` at all. Explorer properties, inventory tooling and Inno's file-replacement comparison all have nothing to work from. | `resources/app_icon.rc:1-2` |
| G4 | **No crash reporting.** No `SetUnhandledExceptionFilter`, `MiniDumpWriteDump`, `std::set_terminate`, or signal handlers anywhere in `src/`. | repo-wide grep |
| G5 | **No disk-space preflight.** `QStorageInfo`/`bytesAvailable`/`bytesFree` appear nowhere in `src/`, against a catalog totalling ~19 GB of default files (~50 GB if fully downloaded). Models-path migration copies before deleting, needing 2× the tree. | repo-wide grep; `data/catalog.json` size strings |
| G6 | **Errors do not reach the user.** `AppController::onError` is a single last-write-wins `QString`. `RegistryManager`, `CatalogManager`, `RuntimeManager`, `ModelManager`, `DubbingController`, `VoiceIsolatorController`, `ApiServerService`, `LlmChatController` and others emit `errorOccurred` with **no receiver connected**. A corrupt `registry.sqlite` produces a silently empty Models gallery. | `AppController.cpp:79-96,128-138` |
| G7 | **No resume-after-crash.** `WorkflowRunJournal::read()` has no production caller; `WorkflowRunState::Cancelled`/`Interrupted` are never referenced outside their own definition; `WorkflowGraphRunner::cancel()` routes through `fail()`, so a user cancel is journalled and reported as a failure. The scaffolding exists; the wiring does not. | `WorkflowRunJournal.cpp:62`, `WorkflowRun.cpp:22-34`, `WorkflowGraphRunner.cpp:86-92` |
| G8 | **Unbounded cache growth.** Dubbing imports write 48 kHz stereo float32 masters (~230 MB per 10 min of source) into a SHA-256-keyed cache tree that nothing ever collects, plus uncollected source-separation and alignment scratch. | `MediaIngestService.cpp:88-119`; no `removeRecursively` targeting those roots |
| G9 | **Translations badly stale.** QML files carrying hundreds of `qsTr()` calls have **no context in either `.ts` file** — including all 11 `components/dubbing/*.qml` (only `DubbingPage` itself has a context), LLM Chat, Translation Studio, SRT Voice. `lastudio_vi.ts` has 35 `vanished` entries and a dead `SystemLogsTab` context. Vietnamese users see the flagship v0.2.0 feature entirely in English. | `i18n/lastudio_vi.ts`; context diff vs `qml/` |
| G10 | **~1,650 lines of unreachable QML.** Eight files compile into the module but are instantiated by nothing — including `RuntimeSettingsTab.qml` (456 lines; the Runtime Management UI the README advertises and screenshots) and `HFExplorerPage.qml` (167 lines, no route). | `SettingsPage.qml:144-151`; `git grep HFExplorerPage` → `CMakeLists.txt:416` only |
| G11 | **Startup work blocks the UI.** `AppController`'s singleton constructor runs inside `engine.loadFromModule()` before `app.exec()`, and synchronously performs a recursive model-tree scan plus a `QDir::drives()` probe of every drive letter. A disconnected network drive delays or prevents the window appearing. | `AppController.cpp:42-43`, `Settings.cpp:102-107`, `main.cpp:57` |
| G12 | **Frameless window without native handling.** `Qt.FramelessWindowHint` + 8 hand-rolled resize `MouseArea`s and no `WM_NCCALCSIZE`/`WM_NCHITTEST`/`WS_THICKFRAME` handling. Aero Snap, Windows 11 snap layouts and the drop shadow are affected. Window geometry is never persisted (hard-coded `Maximized`, 1280×800). | `qml/Main.qml:9-14,522-610` |
| G13 | **Zero accessibility annotations** across 114 QML files, compounded by non-native window controls. | `grep -rn "Accessible\." qml` → 0 |
| G14 | **Runtime ABI is never checked.** `protocolVersion` (e.g. `llama-c-api-b10036`) flows catalog → manifest → `RuntimeInfo` → QML but is **never compared to anything**. Only symbol *names* are resolved, which cannot detect struct-layout changes. The build compiles against b10036 headers while the DLL is downloaded at runtime. | `RuntimeManager.cpp:392,636,747`; `LlamaTranslationInterface.cpp:112-144` |
| G15 | **Runtime host has no watchdog.** `RuntimeHostClient` connects neither `QProcess::finished` nor `errorOccurred`. A crash is detected only by a fixed `waitForReadyRead(10000)`. Whisper and Llama-translation adapters emit **no progress frames**, and STT submits the whole file in one `transcribe()` call — so any inference exceeding 10 s of silence is reported as a timeout on a healthy host. | `RuntimeHostClient.cpp:302-312`; `RuntimeHostAdapter.cpp:158,217`; `SttWorker.cpp:74` |
| G16 | **Hardware gating is a substring match.** CUDA/Vulkan compatibility is decided by matching the runtime id against `cuda`/`vulkan`/… and checking a DXGI adapter description for `NVIDIA`/`AMD`/`Intel`. No driver version, compute capability, or VRAM check. AVX/AVX2/AVX512 flags are detected and then never consulted. | `HardwareManager.cpp:41-190` |
| G17 | **Test suite is not hermetic.** It writes to the real `~/.lastudio` (catalog cache, `registry.sqlite`, history, `settings.json`), and eight tests bake the build machine's source path in via `__FILE__` + `QDir::setCurrent()`, making the binary non-relocatable. `ctest` and `run_tests.ps1` use different working directories, and `run_tests.ps1` looks for the binary at a path CMake likely does not produce. | `tests/test_ModelsAndRuntimes.cpp:172-179 et al`; `tests/CMakeLists.txt:282-286` vs `scripts/run_tests.ps1:21-22,197` |
| G18 | **Model licensing is dead data.** `data/catalog.json` carries 25 license strings; `git grep -i license -- src/ include/ qml/` returns **nothing**. One catalogued model is **CC BY-NC 4.0** (non-commercial) with only a prose warning, while README.md:343 explicitly grants users commercial use. `canary-ctc-aligner` is CC BY-4.0 (attribution required); `nemotron` is OpenMDW-1.1. 5 of 25 families have no license at all. | `catalog-src/hub/models/**/manifest.json`; grep |
| G19 | **`src/textnorm` provenance is unresolved.** `src/textnorm/UPSTREAM.md:8` says: *"Keep upstream attribution and review the Apache-2.0/MIT notices before moving this module to a standalone repository."* That review has not happened; there is no `LICENSE`, no `NOTICE`, no header attribution — and the code is compiled into the shipped binary. | `src/textnorm/UPSTREAM.md:3-9` |
| G20 | **Undisclosed startup network call.** `AppController` fires `checkForUpdates("stable")` to `api.github.com` 2 s after every launch, with no settings gate and no first-run consent — contradicting the "Offline AI" badge and README.md:289-291. | `AppController.cpp:103-107` |
| G21 | **License provenance is wrong by construction** for most models: the recorded license describes the upstream publisher (Qwen, NVIDIA, Microsoft) while the bytes actually come from third-party re-quantizer accounts (`cstr/*`, `Serveurperso/*`) whose own redistribution rights were never recorded. | `data/catalog.json` `requiredFiles[].sources` vs family `modelId` |
| G22 | **Dead build options and include paths.** `ENABLE_SHERPA_ONNX` / `ENABLE_OMNIVOICE` define macros referenced by zero source files; `third_party/whisper/include` and `omnivoice.cpp/src` are added as include directories but exist in neither git nor on disk. See the note below — they are misleading, not build-breaking. | `CMakeLists.txt:59,68-75,568,579,645` |
| G23 | **The installer ships a third-party binary of unpinned, unrecorded provenance.** `Ensure-ArchiveExtractor` copies whatever `7z.exe` `Get-Command` resolves on the build agent's `PATH` — in CI, whatever version `choco install 7zip` produced that day — plus the adjacent `7z.dll`, straight into `{app}\bin`. No version pin, no hash, no record of what shipped. The installer is not reproducible build-to-build, and the shipped 7-Zip version **cannot be audited against CVEs after the fact**. | `scripts/package.ps1:226-264`; `.github/workflows/windows-release.yml:53-55` |
| G24 | **Installer contents are unenumerated and unbounded.** `Copy-VcpkgRuntimeLibraries` copies **every** `*.dll` from the vcpkg bin tree, asserting only that `libcurl.dll` and `zlib1.dll` are among them; `installer.iss.in:32` then sweeps the whole stage tree with `recursesubdirs createallsubdirs`. Nothing produces a manifest of what the installer actually contains — so transitive vcpkg dependencies **and their licenses** enter the shipped product invisibly, and no reviewer or license scanner can enumerate the payload without building. | `scripts/package.ps1:266-295`; `scripts/installer.iss.in:32` |
| G25 | **`CMakePresets.json` is not self-sufficient** — no `toolchainFile`, no `CMAKE_PREFIX_PATH`, no `VCPKG_TARGET_TRIPLET`, no `LLAMA_CPP_SOURCE_DIR`. `cmake --preset` alone can never configure, so IDE / CMake Tools / clangd workflows have no supported entry point, and presets and scripts have already drifted (`package.ps1` passes the llama flag; `build.ps1` does not). | `CMakePresets.json`; `scripts/build.ps1:363-406` vs `scripts/package.ps1:348-362` |

> **Clarification on `third_party/` and `omnivoice.cpp/` (a claim worth getting right).**
> These directories are referenced by `target_include_directories` but are absent from both git and
> the filesystem. This is **not** a build blocker. Verified independently: no source in `src/`,
> `include/` or `tests/` includes `whisper.h` or any header from `omnivoice.cpp/src`.
> `include/runtimes/WhisperInterface.h` deliberately re-declares the whisper types to avoid the
> header dependency, and `OmnivoiceBackend.cpp` includes only the in-repo
> `<runtimes/OmnivoiceInterface.h>`. CMake tolerates non-existent include directories. They are
> vestigial references that should be deleted (see Phase 0), not provisioned.

### 2.5 Test coverage map

Framework is QTest exclusively. Two CTest-registered binaries: `LAStudioUnitTests` (17 suites,
123 test functions, hand-aggregated in `tests/main.cpp`) and `VietNormUnitTests` (10 tests).

| Subsystem | Coverage |
|---|---|
| Dubbing project | **Strong** — `test_DubbingProject` (43 tests) |
| Catalog / registry / runtimes | **Good** — `test_ModelsAndRuntimes` (20) |
| Workflow graph | **Good** — `test_WorkflowGraph` (12), `test_AlignmentWorkflow` (5) |
| Runtime host protocol | **Partial** — 6 tests, all happy-path framing/shm/ping/admission. No crash, timeout, cancel, or ABI-mismatch test |
| STT session, TTS text prep, translation project, separation, alignment matcher, subtitles, capabilities, path migration, download/install | **Thin** — 2–6 tests each; engines and network fully mocked |
| **API server, LLM Chat, localization, TTS/STT engines and all 8 backends, voice cloning/design services, app update, hardware detection, media tooling, phonemizer, audio timeline, logging, thread policy, studio actions, `HFHubClient`, `DownloadManager`** | **Zero** — compiled into the test target, never exercised |
| **QML (114 files / 31,299 lines)** | **Zero** — no QtQuickTest, no `tst_*` files, `qmllint` never invoked anywhere |

The real inference, download and install paths are never tested: `TtsEngine`, `SttEngine`,
`HFHubClient` and `DownloadManager` are replaced by mocks (the latter two are empty stubs), and all
TTS/STT backends are excluded from the test target. **123 green tests give false confidence about
the features users actually run.**

---

## 3. Proposed Technical Decisions and Rationale

Each decision states what is chosen, why, and what was rejected. Decisions marked **⚠ needs
confirmation** depend on an answer in §7.

### D-01 Keep the toolchain exactly as-is: MSVC 2022 + Qt 6.9.3 + CMake/Ninja + vcpkg

**Decision.** No toolchain migration. Pin Qt to 6.9.3 (already pinned in CI), keep MSVC 2022 x64 as
the only supported production toolchain, keep vcpkg manifest mode.

**Rationale.** The toolchain works and the problems are elsewhere. Every hour spent on a toolchain
change is an hour not spent on the delivery chain. Qt 6.9.3 is already what CI installs; `CMakeLists`
declaring `6.5` minimum while CI uses `6.9.3` is a latent inconsistency, so raise the CMake minimum
to `6.9` to make the supported version explicit.

**Rejected:** Conan (no benefit for two dependencies); vendoring curl/zlib (loses vcpkg's CVE
tracking); Qt 6.8 LTS (would be defensible for long-term support but forces a revalidation cycle now
— revisit for 1.0, record as a follow-up).

### D-02 Demote the MinGW preset to unsupported

**Decision.** Mark `windows-mingw-release` explicitly unsupported in `docs/BUILD.md`, or delete it.

**Rationale.** It exists in `CMakePresets.json`, every script branches on it (different kit,
different vcpkg triplet `x64-mingw-dynamic`, manual libgcc/libstdc++/libwinpthread/libgomp copying),
and **CI never builds it**. It is almost certainly rotting: `tests/test_SubtitleVoice.cpp:7-9`
defines namespaced members at global scope after a `using namespace`, which MSVC accepts and
GCC/Clang reject. Maintaining an untested second toolchain is a cost with no demonstrated consumer.

**Alternative if it must stay:** add it to CI. Do not leave it in the tree untested and documented
as available.

### D-03 Provision llama.cpp headers automatically; delete the phantom include paths

**Decision.** `scripts/bootstrap.ps1` shallow-clones `ggml-org/llama.cpp` at ref `b10036` into
`.deps/llama.cpp` when `include/llama.h` is absent, and `scripts/build.ps1` passes
`-DLLAMA_CPP_SOURCE_DIR` exactly as `package.ps1` already does. Simultaneously delete the
`third_party/whisper/include` and `omnivoice.cpp/src` include directories and the dead
`ENABLE_SHERPA_ONNX`/`ENABLE_OMNIVOICE` options.

**Rationale.** Mirrors what CI already does (`windows-release.yml:24-29`), so there is one
provisioning story instead of two. The phantom paths are deleted rather than provisioned because no
source consumes them (§2.4 clarification) — provisioning them would add a dependency the code does
not have.

**Rejected:** git submodule (the workflow already requests `submodules: recursive` and there is no
`.gitmodules`; adding one now means every contributor must remember `--recursive`);
`FetchContent` (would download at configure time on every clean build tree, and we need headers
only).

### D-04 Single source of truth for version, enforced at three points

**Decision.** `LASTUDIO_VERSION` in `CMakeLists.txt` remains authoritative. Add:
1. A CI gate as the **first** step of the release workflow that fails when
   `${GITHUB_REF_NAME#v} != LASTUDIO_VERSION`.
2. A `configure_file`-generated `.rc` carrying a real `VERSIONINFO` block driven by
   `@PROJECT_VERSION@`, applied to **both** executables.
3. `VersionInfoVersion=@PROJECT_VERSION@` in the Inno Setup template.

**Rationale.** Today `package.ps1 -Version` overrides the source value with no comparison
(`package.ps1:98-113`), so tagging `v0.3.0` against a `0.2.0` tree produces a perfectly successful
release reporting `0.3.0` while every developer build, test run and bug report says `0.2.0`. Version
metadata is also what code signing, crash triage, inventory tooling and Inno's file-replacement logic
all key off — and neither executable has any today.

### D-05 Ship an LGPL FFmpeg as a managed catalog runtime **⚠ needs confirmation (D2 / Q-03)**

**Decision (recommended).** Add `ffmpeg` + `ffprobe` (LGPL-configured build) as a first-class
catalog runtime with a pinned SHA-256, following the pattern the eSpeak NG MSI dependency already
uses. Teach `MediaToolService`, `MediaIngestService`, `AudioFileDecoder` and `AudioTimelineRenderer`
to prefer the managed location over `PATH`. Gate the Dubbing studio entry point on availability with
a one-click install action instead of an error string.

**Rationale.** Three options were considered:

| Option | Installer size | License posture | UX |
|---|---|---|---|
| Bundle in installer | +60–100 MB | LGPL notice required in installer; a GPL build would be a genuine problem against AGPL-3.0 | Best — works offline immediately |
| **Managed catalog runtime (recommended)** | No change | LGPL notice required at download; consistent with how every other runtime is delivered | Good — one click, matches existing mental model |
| Documented prerequisite | No change | None | Worst — the flagship feature fails on a stock machine with a generic error |

The catalog-runtime option is chosen because it keeps the installer small (already a concern with Qt
Quick + Multimedia + QML), reuses machinery that already exists and is already tested, and keeps the
license notice attached to the thing being delivered. **LGPL over GPL is not optional** — a GPL
FFmpeg redistributed alongside an AGPL-3.0 application is a question the project should not have to
answer.

**Note:** today FFmpeg is resolved purely from `PATH`, so the GPL/LGPL question belongs entirely to
the user's own installation and creates **no** current redistribution obligation. That changes the
moment anything is bundled or fetched on the user's behalf.

### D-06 Integrity verification becomes mandatory and fails closed

**Decision.**
- Invert the short-circuit at `DownloadInstallService.cpp:384-388`: an **empty expected hash is a
  refusal**, not a pass, for any catalog entry that yields loadable native code.
- Populate SHA-256 for every runtime download entry (75 `releases/download` URL references today, only 10 hashed) and every `dependencyDownload` (including
  `espeak-ng.msi` and the CUDA redistributables) in `catalog-src/`, propagated by
  `scripts/generate_catalog.py`.
- Add a SHA-256 (or the Hugging Face blob etag the code already fetches) to every `requiredFiles`
  candidate and verify it on the non-archive install path before `ModelManager::addModel`.
- Move the checksum check **above** the `.msi` branch so `espeak-ng.msi` is verified before
  `msiexec` runs, and additionally verify its Authenticode signature.

**Rationale.** These archives contain DLLs that are subsequently `LoadLibrary`'d into the LA Studio
and runtime-host processes. Most weights come from third-party re-quantizer accounts (`cstr/*`,
`Serveurperso/*`) that are **outside the project's control**. Compromise of any one of them is
currently native code execution on every user who installs that model. Fail-closed is the only
posture that makes the control meaningful; an opt-in hash that silently passes when absent is
security theatre.

**Sequencing note:** flip the fail-closed switch **after** the catalog is fully populated, otherwise
every install breaks. Populate → verify with a test that asserts 100% coverage → then invert.

### D-07 One DLL-loading strategy, hardened

**Decision.** Call `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)` as the first
statement of `main()` in **both** executables. Convert every runtime load site to
`LoadLibraryExW(absolutePath, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)`.
Replace the recursive `*.dll` discovery + blanket `LoadLibraryW` in `CrispCommon.h` with an explicit
dependency list read from the runtime's `backend-manifest.json`, resolved only within that runtime's
package directory. Stop mutating process `PATH` entirely; launch all child processes by absolute
path.

**Rationale.** `KokoroVietnameseInterface` already proves the hardened pattern works with these
DLLs, so this is a consolidation onto a known-good approach rather than a bet. The current blanket
preload executes `DllMain` of **any** file placed anywhere under a user-writable directory tree —
which is also reachable by a corrupted extraction, not only by an attacker.

### D-08 Enforce the runtime ABI contract that already exists in the data

**Decision.** Compare the installed manifest's `protocolVersion` against a compiled-in constant
before any DLL load, and refuse with a specific user-facing message on mismatch. Pin the catalog
entry, the CI header checkout ref, and the compiled-in constant to **one** value in a single source
of truth.

**Rationale.** `protocolVersion` already flows end to end and is never compared. Symbol-name
resolution cannot detect struct-layout changes in `llama_model_params` / `llama_context_params`, so a
mismatched `llama.dll` passes `resolveAll()` and then corrupts memory on first call. The data is
already there; only the check is missing. This is a small change with a large failure mode retired.

### D-09 CI in two workflows, with the release gated on the CI workflow

**Decision.**
- **First, fix `.gitignore:113-118`** so `.github/` files can be tracked at all.
- `ci.yml` on `pull_request` + `push: main`: configure → build all targets → `ctest --output-on-failure`
  → `qmllint` → catalog regeneration diff check.
- `windows-release.yml` on tags: version-consistency gate → build → **staging manifest assertion** →
  sign → installer → sign → SHA-256 → **install-and-launch smoke test in a clean runner** → publish
  as **draft**.
- Promotion from draft to public is a deliberate human action after the RC checklist passes.

**Rationale.** The `.gitignore` fix genuinely must come first — verified with `git check-ignore`,
any CI file added today is silently untracked, and a teammate would see "nothing to commit" and
assume success. Draft-first publishing matters because the in-app updater polls the public releases
feed: today a tag push propagates a broken build to every existing user within one update-check
cycle, with no way to recall it.

### D-10 Make the test suite hermetic before making it a gate

**Decision.** Before wiring `ctest` into CI:
1. Add a `LASTUDIO_DATA_DIR` environment override honoured by `PathUtils::dataDir()`, set to a
   `QTemporaryDir` in `tests/main.cpp` before any suite constructs `Settings`/`CatalogManager`/`RegistryManager`.
2. Replace the eight `__FILE__` + `QDir::setCurrent()` blocks with the already-defined
   `LASTUDIO_SOURCE_DIR` macro, or better, a `POST_BUILD` copy of `data/` next to the test binary.
3. Add `LAStudioRuntimeHost` as a build dependency of the test target, and stage eSpeak NG for tests.
4. Fix the `run_tests.ps1` binary path (or set `CMAKE_RUNTIME_OUTPUT_DIRECTORY`).
5. Raise the 500 ms `QTRY` window in `test_SttSession.cpp:116`; replace the busy-sleep in
   `test_SourceSeparation.cpp:50-56` with an explicit handshake.
6. Pass `-DBUILD_TESTING=OFF` in `package.ps1`.

**Rationale.** If `ctest` is enabled first, it will fail on catalog/schema resolution and
`__FILE__`-derived paths, and the team will reasonably conclude "the tests are broken" and disable
the gate. The order matters more than the content here. Point 1 is also a correctness issue in its
own right: the suite currently mutates the developer's real installed application state, and an
abort before `cleanupTestCase` leaves the user's models path pointing at a deleted temp directory.

### D-11 Compliance is a build artifact, not a document

**Decision.** Generate a `licenses/` directory into the installer stage containing `AGPL-3.0.txt`,
`LGPL-3.0.txt`, `GPL-3.0.txt`, the curl/zlib/7-Zip texts, and a `THIRD-PARTY-NOTICES.md` that
enumerates every component reaching the user tagged **bundled** / **runtime-downloaded** /
**build-only**. Re-enable `LicenseFile` in the Inno template. Add an in-app **About → Licenses**
page. Publish an SBOM alongside each release.

**Rationale.** A notices file that lives only in the repository does not satisfy an obligation that
attaches to the *conveyed binary*. Generating it into the stage makes it verifiable by the same
manifest assertion that checks every other shipped file, which is what stops it going stale. The
in-app page is the standard way to satisfy the LGPLv3 prominent-notice obligation and gives the
AGPL §13 source link somewhere to live.

### D-12 Surface model licensing in the UI and gate restricted models

**Decision.** Promote `license` (plus `licenseUrl`, `commercialUse`, `attributionRequired`, `gated`,
and the license of the *actual download source repo*) to a required, schema-validated field in
`catalog-src/**/model.yaml`. Display it on the model card. Require explicit acknowledgement before
downloading any model whose license is non-commercial, attribution-requiring, non-standard, or
unknown.

**Rationale.** The data is 80% there already (25 license strings in `catalog.json`) and completely
unused (`grep -i license -- qml/` → nothing). The concrete conflict: `README.md:343` grants users
commercial use of LA Studio, while the default catalog silently offers a **CC BY-NC 4.0** model. A
user who relies on the README and ships paid dubbing work produced with that aligner creates
downstream infringement traceable to the app.

### D-13 Adopt the `DubbingProject` persistence pattern everywhere

**Decision.** Extend versioning + corruption detection to the other three stores:
`PRAGMA user_version` + `integrity_check` + quarantine-and-rebuild for the SQLite registry (which is
fully rebuildable from `catalog.json` + a disk scan, so recovery is cheap); a `schemaVersion` key and
a `QSettings::status()` check for `settings.ini`; `QSaveFile` + an entry cap + orphaned-WAV pruning
for `HistoryRepository`.

**Rationale.** `DubbingProject` already demonstrates the right pattern in this codebase, so this is
propagation rather than design. Without it, the first release that changes the registry schema
breaks existing installs **only** — never clean installs, never CI — which is the worst possible
failure distribution.

### D-14 Errors reach the user through a structured, queued channel

**Decision.** Connect every subsystem's `errorOccurred` to `AppController`, and replace the single
`QString` property with a queued notification model carrying code, severity, category, human message
and technical detail.

**Rationale.** This is the root cause of the app's silent-failure behaviour and it is cheap to fix.
Today two errors in the same event-loop turn means the first is never seen, and a corrupt registry
produces an empty gallery with no diagnostic — which users report as "all my models vanished" and
support cannot triage.

### D-15 Rollback is a documented, rehearsed procedure — not a feature

**Decision.** For this release cycle: set a permanent `AppId` GUID and `VersionInfoVersion`, add
`Flags: ignoreversion` so a downgrade installer fully overwrites `bin/`, retain all previous release
assets, wire `prerelease:` into the workflow so the existing (currently dead) Beta channel becomes
functional, and write `docs/RELEASE.md` with a rehearsed rollback runbook.

**Rationale.** An in-app downgrade feature is not worth building yet. What *is* required is that
rollback be **possible and tested**: today `AppUpdateService.cpp:315` refuses any version ≤ current,
and without `ignoreversion` a manual downgrade silently skips versioned DLLs and leaves a
mixed-version `bin/` that is very hard to diagnose. Marking a bad release as prerelease removes it
from the stable channel without deleting it — which is the actual recall mechanism.

### D-16 Ship `RelWithDebInfo`-equivalent binaries and archive PDBs privately

**Decision.** Set `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=ProgramDatabase` plus linker `/DEBUG` on the
release preset. Upload PDBs as a retained build artifact (not necessarily a public release asset).
Add a crash handler writing minidumps to `~/.lastudio/logs/crashes`.

**Rationale.** The architecture deliberately loads third-party native runtimes and GPU contexts —
exactly the code most likely to fault in the field. Without symbols and dumps, every field crash
report is unactionable. PDBs need not be public; they need to exist and be retrievable by version.

### D-17 Run the entire programme at zero cash cost

**Decision.** Adopt a **zero-cash-budget posture** for this release cycle. Every paid item in the
plan has a viable free substitute; none of the release-blocking controls (signing, CI, hash
verification, compliance artifacts, RC validation) has to be dropped to reach $0. The full
item-by-item mapping is in **Appendix B**; the load-bearing choices are:

1. **Code signing → SignPath Foundation** (free Authenticode signing for open-source projects).
   LA Studio qualifies on its face: OSI-approved license (AGPL-3.0-only), public GitHub repository,
   CI-built artifacts. The Foundation holds the certificate and signs via a policy-controlled
   CI integration — which *forces* the discipline Phase 6 wants anyway (releases built only from
   the public pipeline). Apply on **day 1**; approval latency replaces certificate-procurement
   latency as the long pole. Trade-offs stated in Appendix B; fallback if rejected is R-41.
2. **CI → GitHub-hosted runners**, free without minute limits for public repositories, including
   `windows-latest`. The nightly T4 integration tier runs on a developer machine as a self-hosted
   runner (or drops to weekly on-demand) instead of paid infrastructure.
3. **FFmpeg → BtbN LGPL shared builds** delivered through the catalog per D-05, pinned by SHA-256,
   downloaded from the upstream GitHub release at install time — no hosting, no installer growth.
4. **RC hardware → the community the project already has.** The Facebook group and Discord provide
   the GPU/CPU spread Q-08 asks about. Distribute the RC as a GitHub **prerelease** (task 6.10
   makes this safe — the stable updater channel never sees it), collect the §5.4 matrix through a
   GitHub issue template, and record which hardware classes were actually covered in the release
   notes. Clean-machine scenarios stay in-house and free: Windows Sandbox and Microsoft's free
   Windows evaluation VMs.
5. **Crash reporting → local-only** minidumps plus a user-initiated "Report a problem" that
   pre-fills a GitHub issue — no backend, and it matches the offline-first positioning (Q-17).
6. **Everything else is already free:** Qt (LGPLv3 path, Q-12), Visual Studio Community / Build
   Tools (free for OSS development), Inno Setup, 7-Zip, vcpkg, Ninja, Syft/CycloneDX for the SBOM,
   REUSE for SPDX linting, GitHub Releases for artifact + PDB retention, free timestamp-authority
   servers, and winget/Chocolatey as no-cost distribution channels.

**What does not go to zero.** Engineering time (the §4.10 estimates are unchanged) and calendar
time for the SignPath review. Cash cost of the programme: **$0**.

**Rationale.** The only line item the original plan treated as unavoidable spend was the
certificate (Q-01 called it "the long pole"). SignPath Foundation removes the spend without
removing the control — and unlike shipping unsigned, it preserves the SmartScreen and updater
trust story. Every other substitution is a like-for-like swap already consistent with existing
decisions (D-05 catalog delivery, D-09 draft-first releases, D-16 local dumps).

---

## 4. Phased Plan

**Sequencing principle.** Phase 0 is on the critical path for everything: no gate, assertion or smoke
test can be trusted until a clean clone builds reproducibly and the test binary runs. Phases 2, 3 and
4 can run in parallel once Phase 1 is green. Phase 6 depends on Phases 1 and 2. Phase 7 depends on
everything.

```
P0 Foundation ──► P1 CI & Test Harness ──┬──► P2 Packaging Correctness ──┐
                                          ├──► P3 Supply Chain & Security ├──► P6 Release Eng ──► P7 RC & Launch
                                          └──► P4 Legal & Compliance ─────┘
                                                                          
                          P5 Product Readiness (parallel; scope-negotiable)
```

---

### Phase 0 — Foundation: reproducible build from a clean clone

**Goal.** `git clone` → documented command → working binaries and a runnable test binary, on a
machine that has never built this project.

**Why first.** Every subsequent gate is unverifiable without it, and the documented onboarding path
is currently broken.

| # | Task | Effort |
|---|---|---|
| 0.1 | Fix `.gitignore:113-118` so `.github/` files can be tracked. **Do this before anything else.** | XS |
| 0.2 | Auto-provision llama.cpp b10036 headers in `bootstrap.ps1`; pass `-DLLAMA_CPP_SOURCE_DIR` from `build.ps1` | S |
| 0.3 | Delete phantom include dirs (`third_party/whisper/include`, `omnivoice.cpp/src`) and the dead `ENABLE_SHERPA_ONNX`/`ENABLE_OMNIVOICE` options and macros | XS |
| 0.4 | Update `docs/BUILD.md` prerequisites: llama.cpp provisioning, FFmpeg, eSpeak NG, 7-Zip, Inno Setup | S |
| 0.5 | Fix `run_tests.ps1` binary path (or set `CMAKE_RUNTIME_OUTPUT_DIRECTORY`); add `LAStudioRuntimeHost` as a test-target dependency | S |
| 0.6 | Pin the vcpkg clone in `bootstrap.ps1` to the commit matching `vcpkg.json`'s `builtin-baseline` | XS |
| 0.7 | Raise the CMake Qt minimum from 6.5 to 6.9 to match what is actually supported and tested | XS |
| 0.8 | Decide and record the MinGW preset's status (D-02) | XS |

**Deliverables.** Updated `bootstrap.ps1`/`build.ps1`, trimmed `CMakeLists.txt`, corrected
`docs/BUILD.md`, working `.gitignore`.

**Exit criteria (all must be demonstrated on a machine with no `.deps/`, no `LLAMA_CPP_SOURCE_DIR`,
and no prior build):**
- `git clone <repo> && cd <repo> && .\scripts\bootstrap.bat -SkipDeploy` exits 0
- `out/build/windows-msvc-release/LA Studio.exe` exists
- `.\scripts\run_tests.bat` runs the suite end to end without a prior `build.ps1` run
- `git check-ignore -v .github/ci.yml` exits 1 with no output
- `git grep -E 'third_party|omnivoice\.cpp/src|ENABLE_SHERPA_ONNX|ENABLE_OMNIVOICE' CMakeLists.txt`
  returns nothing, and a clean release build still succeeds

**Dependencies.** None. **Risks.** Low; mostly mechanical. The one unknown is whether
`install-qt-action` supplies `qttools` for `LinguistTools` (Q-05) — surfaces immediately in CI.

---

### Phase 1 — CI and a trustworthy test harness

**Goal.** Every PR is built, tested and linted; the suite is hermetic and location-independent.

**Why here.** Ordering matters: make the suite hermetic *before* making it a gate (D-10), or the
first red CI run will be blamed on the tests rather than the harness.

| # | Task | Effort |
|---|---|---|
| 1.1 | `LASTUDIO_DATA_DIR` override in `PathUtils::dataDir()`; set to a `QTemporaryDir` in `tests/main.cpp` | S |
| 1.2 | Remove the eight `__FILE__` + `QDir::setCurrent()` blocks in `test_ModelsAndRuntimes.cpp`; `POST_BUILD` copy of `data/` next to the test binary | M |
| 1.3 | Stabilise timing-dependent assertions; fix the always-true assertion at `test_DownloadInstallService.cpp:49` | S |
| 1.4 | Split the monolithic runner into per-suite CTest entries for failure isolation | M |
| 1.5 | Add `.github/workflows/ci.yml`: configure → build all → `ctest --output-on-failure` → `qmllint` | M |
| 1.6 | Add a catalog-sync CI job: run `generate_catalog.py`, fail on any `git diff` | S |
| 1.7 | `-DBUILD_TESTING=OFF` in `package.ps1` | XS |
| 1.8 | QML smoke test: load each of the ~9 studio routes with mocked controllers, fail on any `QQmlEngine` warning | M |
| 1.9 | Wrap `test_SubtitleVoice.cpp` member definitions in `namespace LAStudio { }` (only if MinGW stays supported) | XS |

**Deliverables.** `ci.yml`, hermetic test suite, QML smoke binary, catalog-sync gate.

**Exit criteria.**
- A PR shows a required green check whose log contains both `LAStudioUnitTests` and
  `VietNormUnitTests` passing under `ctest`
- Deliberately breaking one assertion turns the check red
- `ctest -N` lists ≥ 18 tests (not 2)
- Copying the test binary + DLLs to a scratch directory and running it from `C:\` still passes
- Deleting `~/.lastudio`, running the suite, and confirming `~/.lastudio` is **not** recreated
- A one-character edit to `data/catalog.json` fails the catalog-sync job
- A deliberate typo in a QML binding fails the smoke test

**Dependencies.** Phase 0 (0.1 for the workflow file; 0.5 for the test binary path).

**Risks.** Task 1.2 may surface further CWD assumptions once `ctest`'s working directory differs
from `run_tests.ps1`'s — budget for iteration. `qmllint` on 114 files will likely produce a
substantial first-run warning backlog; land it as non-blocking initially, then flip to blocking once
burned down.

---

### Phase 2 — Packaging correctness

**Goal.** The installer contains exactly what it should, asserted mechanically rather than by
inspection.

| # | Task | Effort |
|---|---|---|
| 2.1 | Hoist `Ensure-EspeakNgRuntime` into a shared helper; call it from **both** `build.ps1` and `package.ps1` | S |
| 2.2 | Extend `EspeakNgPhonemizer` search roots to include the catalog-installed runtime location; bound the recursive fallback scan | S |
| 2.3 | **Post-stage manifest assertion** — fail the build unless every required artifact exists in `out/stage/bin` (both exes, Qt core/quick/multimedia, `platforms/qwindows.dll`, `imageformats/qwebp.dll`, `libcurl.dll`, `zlib1.dll`, `7z.exe`, `espeak-ng/libespeak-ng.dll`, `espeak-ng-data/voices`, `licenses/`) | S |
| 2.4 | Resolve the MSVC redistributable question: determine what `windeployqt --compiler-runtime` actually emits; if it stages `vc_redist.x64.exe`, add an installer `[Run]` entry executing it (Q-04) | S |
| 2.5 | `VERSIONINFO` via `configure_file`d `.rc` for **both** executables (D-04) | S |
| 2.6 | Installer hygiene: permanent `AppId` GUID, `AppMutex`, `CloseApplications=force`, `PrivilegesRequired=admin`, `VersionInfoVersion`, `Flags: ignoreversion`, re-enabled `LicenseFile` | S |
| 2.7 | `install(FILES LICENSE)`; **remove** `install(DIRECTORY resources)` (it ships a build-time `.rc` to end users) | XS |
| 2.8 | Uninstall: offer an opt-in "also remove downloaded models, logs and history" step (default: keep) | S |
| 2.9 | FFmpeg delivery per D-05 — managed catalog runtime + preference over `PATH` + UI availability gate | M–L |
| 2.10 | Stage `bsdtar.exe` alongside `7z.exe` and invoke it explicitly instead of a bare `tar` from `PATH` | S |
| 2.11 | Release preset emits PDBs; CI archives them (D-16) | S |
| 2.12 | Add a `licenses/` staging rule (payload authored in Phase 4) | XS |

**Deliverables.** Corrected `package.ps1` with assertions, hardened `installer.iss.in`, versioned
executables, FFmpeg delivery mechanism.

**Exit criteria.**
- `.\scripts\package.ps1 -SkipInstaller` then `Test-Path 'out\stage\bin\espeak-ng\espeak-ng-data\voices'` → `True`
- Deleting any one required artifact makes the manifest assertion exit non-zero **naming the missing file**
- `(Get-Item 'out\stage\bin\LA Studio.exe').VersionInfo.FileVersion` returns the built version;
  same for `LAStudioRuntimeHost.exe`
- Installer wizard displays the AGPL license page
- `Test-Path '<installdir>\LICENSE'` → `True`; `Test-Path '<installdir>\resources\app_icon.rc'` → `False`
- **On a clean Windows Sandbox with no Visual Studio, no VC++ redist, no FFmpeg and no eSpeak NG:**
  the installer completes, `LA Studio.exe` reaches the main window, and a dubbing import of a real
  `.mp4` succeeds (or presents a specific, actionable one-click remedy)
- Running the installer over a running instance prompts to close and completes without a reboot request

**Dependencies.** Phase 0. Task 2.9 depends on decision D2. Task 2.12's payload comes from Phase 4.

**Risks.** 2.9 is the largest single item in this phase and its size depends entirely on D2 —
scope it after the decision, not before. 2.4 cannot be resolved without a Qt installation (Q-04);
treat it as a measurement task, not an engineering one.

---

### Phase 3 — Supply chain and security hardening

**Goal.** Nothing executes on a user's machine without a verified provenance chain.

| # | Task | Effort |
|---|---|---|
| 3.1 | Fix `WavIO::loadAsFloat` bounds checking; add a malformed-WAV test corpus; run under MSVC ASan | M |
| 3.2 | Populate SHA-256 for every runtime entry + every `dependencyDownload` in `catalog-src/` | M |
| 3.3 | Add SHA-256/etag to `requiredFiles` and verify on the non-archive install path | M |
| 3.4 | Move the checksum check above the `.msi` branch; add `WinVerifyTrust` on `espeak-ng.msi`; invoke `msiexec` by absolute path | S |
| 3.5 | **Then** invert `fileMatchesSha256` to fail closed (D-06 sequencing note) | XS |
| 3.6 | `SetDefaultDllDirectories` in both `main()`s; convert all load sites to hardened `LoadLibraryExW`; remove `PATH` mutation (D-07) | M |
| 3.7 | Replace the recursive blanket DLL preload with a manifest-declared dependency list (D-07) | M |
| 3.8 | Post-extraction canonical-path assertion (every produced path resolves under `extractDir`); add a crafted-`../` regression test | S |
| 3.9 | `ApiServerService`: require the key for all non-`/health` peers including loopback, drop `?api_key=`, validate `Host`, reject unexpected `Origin`, move `/health` behind auth, reject invalid `Content-Length` (D-09/B9) | M |
| 3.10 | Move `api/serverApiKey` and the dubbing LLM key to DPAPI/Credential Manager; migrate and overwrite existing plaintext | M |
| 3.11 | Runtime-host IPC: generate the token with `QRandomGenerator::system()`, deliver off the command line, constant-time compare, add an `m_memory->size() >= bytes` guard in `RuntimeHostSharedBuffer` | M |
| 3.12 | ABI gate on `protocolVersion` before every DLL load (D-08) | S |
| 3.13 | Updater: verify a published SHA-256 **and** Authenticode publisher before elevating | M |
| 3.14 | Add a "Check for updates automatically" setting that `AppController` honours + first-run network consent; correct the README privacy section (G20) | S |
| 3.15 | Redact TTS text previews and home-directory prefixes from logs; add a "copy sanitized diagnostics" action | S |

**Deliverables.** Hardened parsers, a fully hashed catalog, single hardened DLL-load path,
fail-closed API auth, verified update path.

**Exit criteria.**
- Malformed-WAV corpus (`dataSize=0xFFFFFFFF`, `bitsPerSample=0`, truncated `fmt`) returns empty
  `WavData` without crashing; zero ASan reports
- A script over `data/catalog.json` asserts **every** entry containing `releases/download` has a
  non-empty `sha256` → exit 0
- Truncating one byte of a downloaded archive produces a logged checksum refusal, not an extraction
- A benign marker DLL placed in the backends tree is **not** in the process module list after loading
  a Crisp runtime (verified with Process Explorer)
- A crafted archive containing `../evil.dll` is rejected and writes nothing outside `extractDir`
- With the server enabled and a key set: `curl http://127.0.0.1:3900/v1/models` → **401**;
  with `Authorization: Bearer <key>` → 200; `?api_key=<key>` → 401; unexpected `Origin` → 403
- `settings.ini` contains no plaintext key value after configuration, and both features still
  authenticate after restart
- `Get-CimInstance Win32_Process` for `LAStudioRuntimeHost.exe` shows no token on the command line
- Hand-editing an installed manifest to `protocolVersion: llama-c-api-b9999` marks the runtime
  incompatible with **no** DLL load attempted
- Corrupting a byte of a staged update payload makes the updater refuse to launch it
- With the update setting off, a full launch produces **zero** outbound connections (packet capture)

**Dependencies.** Phase 0. 3.5 strictly after 3.2–3.4. 3.13 depends on Phase 6 signing for the
Authenticode half (the hash half can land first).

**Risks.** 3.2 requires cooperation from upstream runtime publishers, several of which may be outside
the project's control (Q-06) — if hashes must be captured out-of-band, add a documented, repeatable
procedure and a CI check that they exist. 3.7 risks regressing runtime loading for families whose
dependency lists are incomplete; roll it out one family at a time with the marker-DLL test.

---

### Phase 4 — Legal and compliance

**Goal.** The artifact is lawfully redistributable and its obligations are visible.

| # | Task | Effort |
|---|---|---|
| 4.1 | Author `THIRD-PARTY-NOTICES.md` covering every component reaching the user, tagged bundled / runtime-downloaded / build-only | M |
| 4.2 | Author `licenses/` payload (AGPL-3.0, LGPL-3.0, GPL-3.0, curl, zlib, 7-Zip texts) | S |
| 4.3 | **Resolve `src/textnorm` provenance** — determine the upstream license of vietnormalizer 0.2.3 (commit `dd38778…`) and nghitts, add `LICENSE` + `NOTICE`, add attribution headers | M |
| 4.4 | Add SPDX headers to all 494 tracked source files (scripted; `src/textnorm` gets its upstream identifier) | S |
| 4.5 | Written source offer: attach a source archive to every release; state the offer in the installer and README | S |
| 4.6 | In-app **About → Licenses** page (D-11) | M |
| 4.7 | `/source` route on the API server returning license id, version and repository URL; advertise from `/health` (AGPL §13 hygiene for downstream forks) | S |
| 4.8 | Required, schema-validated `license`/`gated`/`commercialUse` fields in `catalog.schema.json` + all `model.yaml`; backfill the ~25 download repos with none | M |
| 4.9 | Pre-download license consent gate for non-commercial / attribution / unknown-license models (D-12) | M |
| 4.10 | Resolve the CC BY-NC 4.0 model's status: keep behind consent, or remove from the default catalog (D3) | XS + decision |
| 4.11 | SBOM generation (CycloneDX or SPDX) over `out/stage`, attached to each release | S |
| 4.12 | `CONTRIBUTING.md` with DCO sign-off, enforced in CI — required if the advertised commercial relicensing is to remain possible | S |

**Deliverables.** `THIRD-PARTY-NOTICES.md`, `licenses/`, resolved textnorm provenance, About page,
license-aware catalog, SBOM, `CONTRIBUTING.md`.

**Exit criteria.**
- Every `.dll`/`.exe` in `out/stage/bin` has a matching entry in `THIRD-PARTY-NOTICES.md`
  (cross-checked mechanically against a directory listing)
- Installed `{app}\licenses\` contains all license texts; the wizard shows the AGPL page
- `test -f src/textnorm/LICENSE` and every ported file carries an SPDX header
- The GitHub release contains the installer, a source archive, `SHA256SUMS`, and an SBOM
- Schema validation of `data/catalog.json` passes with `license` required; a script asserts every
  `modelId` has a non-null license
- Attempting to download the CC BY-NC model presents a non-commercial acknowledgement and does not
  start the transfer until accepted
- `curl http://127.0.0.1:3900/source` returns the license identifier and repository URL

**Dependencies.** Phase 2 (2.12) to stage the payload. 4.10 needs decision D3.

**Risks.** 4.3 may reveal an upstream license that requires more than attribution — it is the single
clearest infringement exposure in the tree and `UPSTREAM.md` documents the team's awareness of it,
which undercuts an innocent-infringement position. **Start 4.3 first in this phase.** 4.8 depends on
answers about repos outside the project's control (Q-06).

---

### Phase 5 — Product readiness (parallel; scope-negotiable)

**Goal.** Close the gaps that turn a working build into a product people trust. This phase is
explicitly negotiable in scope — but items 5.1–5.5 are strongly recommended before a public 1.0.

| # | Task | Effort | Priority |
|---|---|---|---|
| 5.1 | Connect every subsystem `errorOccurred`; structured queued notification model (D-14) | M | **High** |
| 5.2 | Crash handler + minidumps to `~/.lastudio/logs/crashes`; "Report a problem" bundling log tail + dump (D-16) | M | **High** |
| 5.3 | On-disk state versioning across registry / settings / history (D-13) | M | **High** |
| 5.4 | `QStorageInfo` disk preflight before download, extraction and models-path migration; store catalog sizes as integer bytes | M | **High** |
| 5.5 | Regenerate `.ts` files; translate the several hundred orphaned strings (all 11 dubbing components, LLM Chat, Translation Studio); purge the 35 `vanished` entries; add a CI check that `lupdate` produces no diff | M | **High** |
| 5.6 | Journal replay + Resume/Discard for interrupted runs; distinguish `run.cancelled` from `run.failed` (G7) | L | Medium |
| 5.7 | Cache lifecycle manager for the three uncollected trees + a Settings cache size/Clear control (G8) | M | Medium |
| 5.8 | Move startup filesystem work off the critical path; show the window first (G11) | M | Medium |
| 5.9 | Runtime-host watchdog: connect `finished`/`errorOccurred`, supervised restart, release the GPU permit; forward Whisper/Llama progress; replace the fixed 10 s timeout with a resettable inactivity budget (G15) | M | **High** |
| 5.10 | Remove the remaining GUI-thread blocking (nested `QEventLoop`, three `waitForStarted(5000)`); give `MediaIngestService::fail()` an early-exit contract | M | Medium |
| 5.11 | Hardware gating: `requiredCpuFeatures` / `minDriverVersion` in the catalog, enforced using the already-detected CPU flags; add an `XGETBV` OS-enablement check (G16) | M | Medium |
| 5.12 | Resolve the 8 unreachable QML files — re-wire `RuntimeSettingsTab` and `HFExplorerPage`, or delete them and correct the README + screenshots (G10) | S | Medium |
| 5.13 | Window geometry/visibility persistence + native frame handling (G12) | M | Medium |
| 5.14 | Accessibility annotations, starting with the title bar, sidebar and primary actions (G13) | M | Medium |
| 5.15 | First-run onboarding: external-tool check, models directory + space explanation, guided first download | M | Medium |
| 5.16 | Implement or delete `ModelManager::filteredVoiceCloneModels` / `refreshVoiceCloneCache` | XS | Low |
| 5.17 | Pass the configured thread count through to llama.cpp instead of the hardcoded `idealThreadCount()` | XS | Low |
| 5.18 | First-cut tests for the zero-coverage ship-critical subsystems: `ApiServerService`, `LlmChatEngine`, `LocalizationManager`, `AppUpdateService`, `MediaToolService`, `TtsRequestValidator` | L | Medium |

**Exit criteria (for the High-priority subset).**
- A test emits `errorOccurred` from every `AppController`-owned service and each appears in the
  notification model; two errors in one event-loop turn both survive
- Manually corrupting `~/.lastudio/registry.sqlite` produces a **user-visible** error, a quarantined
  backup, and a successful rebuild
- A deliberate access violation produces a `.dmp` that opens with a resolvable call stack against the
  release PDBs
- On a volume with insufficient free space, a download is refused with a message naming required vs
  available bytes
- `grep -c 'type="unfinished"' i18n/lastudio_vi.ts` → 0 and `type="vanished"` → 0; the Dubbing page
  is fully localized in Vietnamese
- Killing `LAStudioRuntimeHost.exe` mid-transcription surfaces a recoverable error within ~2 s, the
  GPU permit count returns to 0, and a retry succeeds without restarting the app
- A 5-minute WAV transcribes to completion with progress updates and no `RuntimeHost response timed out`

**Dependencies.** Phase 0/1 for the test infrastructure. Otherwise independent — this phase can run
alongside 2–4.

**Risks.** 5.6 is the largest item and touches the dubbing critical path; it is also the difference
between "lost three hours of work" and "resumed" for the flagship feature. 5.5 requires a Vietnamese
translator's time, which is a scheduling dependency rather than an engineering one — start it early.

---

### Phase 6 — Release engineering

**Goal.** A tagged commit produces a signed, verified, reversible release with no manual steps.

| # | Task | Effort |
|---|---|---|
| 6.1 | **Secure signing capability (start on day 1 of the whole programme — approval/issuance is the long pole).** Zero-cost default: apply to SignPath Foundation OSS signing (D-17); paid OV/EV procurement only if the project wants its own certificate subject (Q-18) | External |
| 6.2 | Sign both executables before ISCC and the installer after, in CI | S |
| 6.3 | Version-consistency gate as the first release step (D-04) | XS |
| 6.4 | Generate and publish `SHA256SUMS` as a release asset | XS |
| 6.5 | Artifact smoke-test job: silent-install → launch both exes → assert clean startup + log creation → uninstall; gate the publish step on it | M |
| 6.6 | Publish as **draft**; promotion is a deliberate human action | XS |
| 6.7 | Pin every action to a commit SHA, pin `runs-on` to a dated image, pin choco packages with `--version`, add `timeout-minutes` and a `concurrency` group | S |
| 6.8 | Check out a pinned vcpkg into `.deps/vcpkg` instead of relying on `VCPKG_INSTALLATION_ROOT` (removes the baseline-resolvability risk, Q-07) | S |
| 6.9 | Archive PDBs and a toolchain manifest (MSVC version, SDK, Qt, vcpkg commit, choco versions) per release | S |
| 6.10 | Wire `prerelease:` so the existing Beta channel becomes functional | XS |
| 6.11 | Write `docs/RELEASE.md`: cut procedure, promotion, **rehearsed rollback runbook**, downgrade procedure | M |
| 6.12 | Governance: annotated signed tags, `CODEOWNERS`, branch protection with required checks, `dependabot.yml`, PR/issue templates | S |

**Exit criteria.**
- `signtool verify /pa /v out\LA-Studio-Setup.exe` reports success with the expected publisher
- A clean Windows Sandbox download shows **no** "Unknown publisher" SmartScreen dialog
- Pushing a tag whose version disagrees with `CMakeLists.txt` fails at the gate **before any build**
- The release contains: installer, `SHA256SUMS`, source archive, SBOM — and is created as a **draft**
- The smoke-test job goes red when eSpeak staging is deliberately reverted
- `grep -E '@v[0-9]+$|windows-latest' .github/workflows/*.yml` returns nothing
- A rehearsed rollback: install vN, run the vN-1 installer, confirm Add/Remove Programs shows exactly
  one entry at vN-1 with no leftover vN files in `{app}`, and the app starts and reads existing
  `~/.lastudio` state cleanly
- `git tag -v <tag>` verifies

**Dependencies.** Phase 1 (CI exists), Phase 2 (packaging is correct and asserted). 6.2 blocked on 6.1.

**Risks.** 6.1 is an **external dependency with multi-week lead time** (SignPath Foundation review,
or certificate issuance if purchased) and it gates the most user-visible trust improvement. Start it
before any engineering work. If signing is not available at RC time, ship with a prominently
published `SHA256SUMS`, documented verification instructions, and a winget manifest (which gives
users a hash-verified install path that avoids the browser SmartScreen prompt), and treat signing as
a fast-follow — but do **not** ship the auto-updater's elevation path without at least hash
verification (3.13). See R-41 for the SignPath-specific fallback.

---

### Phase 7 — Release-candidate validation and launch

**Goal.** Prove the artifact on real hardware, then ship deliberately.

| # | Task | Effort |
|---|---|---|
| 7.1 | Author `docs/RC_TEST_MATRIX.md` (see §5.4) | S |
| 7.2 | Execute the matrix on: clean Windows 11 (no VS/redist/FFmpeg), an NVIDIA CUDA machine, an AMD/Intel iGPU machine, and a pre-AVX2 CPU if one is in scope | L |
| 7.3 | Upgrade testing: install v0.1.x → upgrade to the RC → verify `~/.lastudio` state, models, history and settings survive | M |
| 7.4 | Offline/air-gapped validation: first run with no network reaches a usable state with a clear message | S |
| 7.5 | Localization pass: every studio view in `en` and `vi` | M |
| 7.6 | Performance sanity: startup time to first paint, a representative dubbing run end to end, memory ceiling under a large model | M |
| 7.7 | Sign-off against §8; promote the draft release; publish release notes | S |

**Exit criteria.** A completed, signed-off copy of §8 attached to the release, with every
unchecked item either fixed or explicitly accepted with a named owner and a target release.

**Dependencies.** Everything.

**Risks.** 7.2 needs hardware access that may not exist in-house (Q-08). If a hardware class cannot
be tested, **say so in the release notes** rather than implying coverage.

---

### 4.10 Effort summary

**Assumptions:** 2–3 engineers familiar with the codebase; effort is engineer-weeks of focused work,
excluding review latency and external procurement; XS ≈ <½ day, S ≈ 1–3 days, M ≈ 1–2 weeks,
L ≈ 2–4 weeks.

| Phase | Scope | Estimate | Critical path? |
|---|---|---|---|
| 0 | Foundation | 0.5–1 wk | **Yes — blocks everything** |
| 1 | CI + test harness | 2–3 wk | **Yes** |
| 2 | Packaging correctness | 2–3 wk (+1–2 if FFmpeg is bundled) | **Yes** |
| 3 | Supply chain + security | 3–5 wk | Partly (3.1, 3.2–3.5 are release-blocking) |
| 4 | Legal + compliance | 2–3 wk | **Yes (4.1–4.5)** |
| 5 | Product readiness | 4–8 wk | No (negotiable; 5.1–5.5, 5.9 strongly recommended) |
| 6 | Release engineering | 1.5–2 wk + **external cert lead time** | **Yes** |
| 7 | RC + launch | 1.5–2 wk | **Yes** |
| | **Minimum shippable (0–4 blocking subset + 6 + 7)** | **6–9 wk** | |
| | **Full plan** | **14–20 wk** | |
| | **Cash budget** | **$0 — every paid item has a free substitute (D-17, Appendix B)** | |

---

## 5. Build, Test, Packaging and Release Strategy

### 5.1 Build strategy

**Target state.** One supported configuration, provisioned automatically, reproducible from a clean
clone.

| Aspect | Today | Target |
|---|---|---|
| Toolchain | MSVC 2022 + Qt 6.9.3 + Ninja | Unchanged, Qt minimum raised to 6.9 |
| External inputs | Qt, vcpkg, llama.cpp headers (manual), eSpeak NG (dev only), 7-Zip, Inno Setup | All auto-provisioned or asserted with an actionable error |
| Debug info | None in Release | `ProgramDatabase` + `/DEBUG`; PDBs archived per release |
| Warnings | `/W4`, no `/WX` | `/WX` after burning down the backlog |
| Hardening | None | `/guard:cf`, `/Qspectre` |
| Reproducibility | Unpinned vcpkg clone; absolute `LASTUDIO_SOURCE_DIR` baked in; `CONFIGURE_DEPENDS` globs | Pinned vcpkg commit; `LASTUDIO_SOURCE_DIR` restricted to Debug; explicit resource file lists |

**On reproducibility.** Three things currently make two builds of the same commit differ:
1. `bootstrap.ps1:166` clones vcpkg at HEAD with no pinned ref — the *port baseline* is pinned but
   the *tool revision* is not.
2. `LASTUDIO_SOURCE_DIR` bakes the build machine's absolute source path into the binary
   (`CMakeLists.txt:573-575`), where it is read at runtime by `ExampleManager` — so the shipped exe
   also leaks the builder's directory layout.
3. Two `CONFIGURE_DEPENDS` globs pull `examples/*` and `data/language-sets/*` into the Qt resource
   set, so adding a file silently changes the binary with no review surface.

Bit-identical reproducibility is **not** proposed as a goal for this release — it is a large
undertaking on MSVC. The goal is **explicability**: two builds of the same commit differ only in
ways that are known and documented, and the toolchain manifest attached to each release makes drift
diagnosable.

### 5.2 Test strategy

A four-tier model. The point of the tiering is that each tier answers a different question and
carries a different cost.

| Tier | What | When | Gate |
|---|---|---|---|
| **T1 Unit** | `LAStudioUnitTests` + `VietNormUnitTests` under `ctest`, hermetic, per-suite entries | Every PR | **Blocking** |
| **T2 Static** | `qmllint` over 114 QML files; catalog regeneration diff; `lupdate` diff; catalog SHA-256 completeness script | Every PR | **Blocking** (after backlog burn-down) |
| **T3 Artifact smoke** | Silent-install → launch both exes → assert startup and log creation → uninstall, in a clean runner | Every tag | **Blocking on publish** |
| **T4 Integration** | Env-gated, one real small model per capability; real download + checksum + extract; runtime-host crash/cancel | Nightly, self-hosted | Non-blocking, reported |

**T1 must become hermetic before it becomes a gate** (D-10). The pattern for T4 already exists in the
tree — `test_ModelsAndRuntimes.cpp:103-110` uses `QSKIP` gated on
`LASTUDIO_TEST_LLAMA_RUNTIME_PATH`/`..._MODEL_PATH` — so T4 is an extension of an established idea,
not a new mechanism.

**What automation cannot cover**, and therefore must be in the RC matrix (§5.4): real GPU paths,
audio hardware (microphone and loopback capture), the installer on a clean machine, locale switching,
multi-monitor and HiDPI behaviour, and end-to-end dubbing on real media.

### 5.3 Packaging strategy

**Delivery model.** Application + Qt runtime + small native tools ship in the installer; AI models
and inference runtimes are downloaded on demand from the catalog. This is correct for a product whose
default catalog totals ~19 GB, and it is not proposed to change.

| Component | Delivery | Change |
|---|---|---|
| `LA Studio.exe`, `LAStudioRuntimeHost.exe` | Installer | + `VERSIONINFO`, + signature |
| Qt 6 runtime + QML + plugins | Installer (`windeployqt`) | + LGPL notice |
| libcurl, zlib | Installer (vcpkg) | Replace the blanket `*.dll` copy with an allowlist |
| `7z.exe` / `7z.dll` | Installer | + `bsdtar.exe`; notices |
| MSVC runtime | Installer (`--compiler-runtime`) | **Verify it is actually installed** (Q-04) |
| **eSpeak NG** | **Missing (B2)** | **Stage in `package.ps1` + fix the phonemizer search path** |
| **FFmpeg / FFprobe** | **Missing (B3)** | **Managed catalog runtime (D-05)** |
| `LICENSE`, `licenses/`, `THIRD-PARTY-NOTICES.md` | **Missing (B5)** | **Add** |
| AI models, native runtimes | Catalog download | + mandatory SHA-256 (D-06) |
| CUDA redistributables | Catalog dependency | + EULA acknowledgement |

**The central packaging control is the post-stage manifest assertion (2.3).** Today only `libcurl`,
`zlib` and `qwebp` are asserted — which is exactly why eSpeak NG could go missing for an entire
release series without anyone noticing. Every future addition must be asserted, or it will
eventually go missing the same way.

**Installer characteristics.** Per-machine (`{autopf}`, admin, UAC). All user state lives in
`~/.lastudio` — verified: every `applicationDirPath()` use in `src/` is a read. This is the right
design: it survives upgrades and needs no elevation at runtime. A per-user install mode is worth
considering (Q-09) since the state layout already supports it and users without local admin
currently cannot install at all.

### 5.4 Release-candidate test matrix

To be authored as `docs/RC_TEST_MATRIX.md` and completed and signed off per release.

**Environments:** clean Windows 11 (no VS, no VC++ redist, no FFmpeg, no eSpeak NG) · Windows 10
22H2 · NVIDIA CUDA machine · AMD or Intel iGPU machine · pre-AVX2 CPU (if in scope) · machine with a
disconnected mapped network drive · offline/air-gapped machine.

**Scenarios:** fresh install · upgrade over v0.1.x · downgrade to the previous release · uninstall
(both keep-data and remove-data) · first run with no models/runtimes/network · models-path migration ·
catalog refresh · one real model download + extract + load per capability (STT, TTS, clone, design,
isolator, translation, LLM) · GPU vs CPU runtime selection including the 2-concurrent-GPU-host
admission limit · runtime-host crash and recovery · microphone and loopback capture · playback and
waveform rendering · **end-to-end video dubbing on a real MP4 including FFmpeg ingest and export** ·
dubbing cancel and resume · every studio view in `en` and `vi` · window resize, maximize, snap,
multi-monitor, HiDPI · API server reachable from an external client with auth enforced · disk-full
behaviour during download · log rotation · uninstall leaving no orphaned processes.

**Every scenario records:** environment, build version, pass/fail, evidence (screenshot or log
excerpt), and a defect link where applicable.

### 5.5 Release and rollback

**Release flow.**
```
tag vX.Y.Z
   └─► version-consistency gate  (fails before any build on mismatch)
       └─► CI checks green for the tagged SHA
           └─► build ──► stage ──► manifest assertion ──► sign exes
               └─► ISCC ──► sign installer ──► SHA256SUMS ──► SBOM
                   └─► artifact smoke test (clean runner)
                       └─► publish as DRAFT
                           └─► RC matrix sign-off (§8)
                               └─► promote to public
```

**Rollback.** Three mechanisms, in escalating order:

1. **Un-promote** — a draft release was never public. Zero user impact. This is why draft-first
   publishing is the single highest-leverage release change.
2. **Demote to prerelease** — removes the release from the stable channel that the in-app updater
   polls, without deleting the asset. Requires task 6.10 (the Beta channel is currently dead because
   no release is ever marked prerelease). This is the actual recall mechanism for a release that has
   already gone public.
3. **Publish a superseding patch** — the only mechanism that reaches users who already updated,
   because `AppUpdateService.cpp:315` refuses any version ≤ current.

**Manual downgrade** must be documented and rehearsed: it requires `AppId` + `ignoreversion`
(task 2.6) to overwrite `bin/` cleanly, and it requires that `~/.lastudio` state written by the newer
version is still readable by the older one — which requires the state versioning in 5.3/D-13.
**Until D-13 lands, treat "user has run vN and downgraded to vN-1" as unsupported and say so
explicitly in the release notes.**

**Never** delete a published release to recall it — that breaks the update path for everyone rather
than reverting it.

---

## 6. Risk Register

Likelihood × Impact, ordered by exposure. "Owner" is a role, to be assigned to a person at kickoff.

### 6.1 Critical exposure

| ID | Risk | L | I | Mitigation | Phase |
|---|---|---|---|---|---|
| R-01 | **Every shipped installer lacks eSpeak NG**, so dubbing phoneme budgeting is silently inert while working on every developer machine. The dev/CI divergence makes it structurally invisible to local testing. | High | Critical | Stage in `package.ps1`; fix the phonemizer search roots; assert in the staging manifest; cover in the smoke test | 2.1–2.3 |
| R-02 | **Video Dubbing — the flagship v0.2.0 feature — does not work on a clean machine** because FFmpeg is unbundled and undocumented. Generic error, support flood at launch. | High | Critical | D-05 managed runtime + availability gate + one-click install | 2.9 |
| R-03 | **Supply-chain compromise via unverified native code.** Only 8 runtime archives (and 0 model weights) are hash-verified; the DLLs are `LoadLibrary`'d. Most weights come from third-party accounts outside the project's control. | Medium | Critical | Mandatory fail-closed SHA-256 (D-06); Authenticode on the MSI; hardened DLL loading (D-07) | 3.2–3.7 |
| R-04 | **Unsigned installer + unverified elevating updater.** TLS is the only integrity control on an admin-level code path; SmartScreen also deters first-time users of the genuine installer. | Medium | Critical | Code signing (6.1–6.2); hash + `WinVerifyTrust` before elevation (3.13) | 3.13, 6.1–6.2 |
| R-05 | **A tagged release publishes straight to the feed the updater polls**, with no test, no smoke check and no staged rollout — a broken build reaches all users within one update cycle. | High | Critical | CI gate (1.5); artifact smoke test (6.5); draft-first (6.6); functional prerelease channel (6.10) | 1.5, 6.5–6.6 |
| R-06 | **Distribution without license text.** AGPLv3 §4/§6 and LGPLv3 §4 defects on the current artifact; AGPLv3 §8 auto-terminates the distributor's own rights on violation. Qt LGPL compliance is actively policed. | High | Critical | `licenses/` + notices in the installer; re-enable `LicenseFile`; written source offer (4.1–4.5) | 4.1–4.5 |
| R-07 | **`src/textnorm` ships third-party-derived code with no attribution**, and `UPSTREAM.md` documents the team's awareness of the unresolved review — which undercuts an innocent-infringement position. | Medium | High | Resolve provenance; add `LICENSE`/`NOTICE`/headers. **Start first in Phase 4.** | 4.3 |
| R-08 | **Registry schema drift on upgrade.** The first release adding a column will silently not apply it to existing installs — and will not reproduce on clean installs or in CI. | High | High | `PRAGMA user_version` + ordered migrations + a test that opens a previous-release DB fixture | 5.3 |

### 6.2 High exposure

| ID | Risk | L | I | Mitigation | Phase |
|---|---|---|---|---|---|
| R-09 | **New contributors cannot build.** The documented quick start fails on missing llama.cpp headers with an error naming a flag the docs never mention. | High | High | Auto-provision + document (0.2, 0.4) | 0 |
| R-10 | **A tagged release fails or ships broken** because the tag workflow is the only place the full packaging path is ever exercised. | High | High | `workflow_dispatch` + a `package.ps1 -SkipInstaller` dry run on main | 1.5, 6.5 |
| R-11 | **Hosted Whisper/translation appears broken** because any inference exceeding 10 s of silence is reported as a timeout on a healthy host (no progress frames, no chunking). | High | High | Forward progress; resettable inactivity budget; liveness check | 5.9 |
| R-12 | **Users lose hours of dubbing work** to a crash — no resume, and orphaned artifacts silently consume tens of GB. The most likely source of a scathing review. | High | Critical | Journal replay + Resume/Discard (5.6); cache GC (5.7) | 5.6–5.7 |
| R-13 | **Field crashes are unactionable** — no PDBs, no minidumps, no reliable file version, with third-party native runtimes and GPU drivers in play. | Medium | High | PDBs (2.11); crash handler (5.2); `VERSIONINFO` (2.5) | 2.5, 2.11, 5.2 |
| R-14 | **Corruption produces a silently empty UI.** Registry/catalog errors have no receiver; users report "all my models vanished" and support has nothing to work from. | Medium | Critical | Connect all error signals; structured channel; DB rebuild path (5.1, 5.3) | 5.1, 5.3 |
| R-15 | **Malformed WAV triggers an out-of-bounds read** on the primary path for every audio file the user opens. | Medium | High | Bounds-check rewrite + fuzz corpus + ASan (3.1) | 3.1 |
| R-16 | **Local API abuse.** Loopback bypasses auth entirely and there is no `Origin`/CORS/CSRF check — any web page the user visits can drive TTS/STT while the server is enabled. | Medium | High | Fail-closed auth; `Origin`/`Host` validation; drop the query-parameter key (3.9) | 3.9 |
| R-17 | **Silent ABI drift.** A `llama.dll` from a build other than b10036 resolves every symbol name and then corrupts memory because struct layouts differ. | Medium | Critical | `protocolVersion` gate before load (D-08) | 3.12 |
| R-18 | **CC BY-NC 4.0 model offered silently** while the README grants commercial use — downstream infringement traceable to the app. | Medium | High | License surfacing + consent gate; decide the model's status (D-12, D3) | 4.9–4.10 |
| R-19 | **Vietnamese users get the flagship feature entirely in English** — hundreds of `qsTr()` calls across all 11 dubbing components, LLM Chat and Translation Studio have no translation entry (verified: only `DubbingPage` has a context in `lastudio_vi.ts`). Vietnamese is the project's primary locale. | High | Medium | Regenerate + translate + CI `lupdate` diff check (5.5) | 5.5 |
| R-20 | **Disk exhaustion mid-install or mid-migration** leaves truncated files and a wedged retry path; `HistoryRepository`'s truncate-then-write destroys history on a full disk. | High | High | `QStorageInfo` preflight; `QSaveFile` for history (5.4) | 5.4 |
| R-21 | **Version skew** between tag and `CMakeLists.txt` goes unnoticed for releases; bug reports cite a version that was never shipped. | Medium | Medium | Version-consistency gate (6.3) | 6.3 |
| R-22 | **Missing VC++ redist on a clean machine** — the redistributable is likely copied but never executed. Common outside developer populations. | Medium | Critical | Determine what `--compiler-runtime` emits; add a `[Run]` entry; validate in Windows Sandbox (2.4) | 2.4 |
| R-23 | **DLL hijacking** via a planted DLL in the user-writable backends tree, executed by the unconditional recursive preload. | Medium | Critical | Manifest-declared allowlist; hardened `LoadLibraryExW`; `SetDefaultDllDirectories` (D-07) | 3.6–3.7 |
| R-24 | **"Offline AI" claim falsified in minutes** by the unconditional startup call to `api.github.com` — the easiest possible criticism of the product's central differentiator. | High | Medium | Settings gate + first-run consent + accurate README (3.14) | 3.14 |

### 6.3 Medium and lower exposure

| ID | Risk | L | I | Mitigation | Phase |
|---|---|---|---|---|---|
| R-25 | Unsupported-hardware crashes (illegal instruction on pre-AVX2; CUDA on an old driver) with no pre-flight diagnostic | Medium | High | Catalog-declared CPU/driver requirements enforced in `HardwareManager` (5.11) | 5.11 |
| R-26 | Uninstall strands multi-GB model downloads with no prompt | High | Medium | Opt-in cleanup step (2.8) | 2.8 |
| R-27 | Frameless-window regressions on Windows 11 (Aero Snap, snap layouts, shadow, restore geometry) | High | Medium | Native event filter or a native frame; geometry persistence (5.13) | 5.13 |
| R-28 | Accessibility/procurement review blocks adoption in education, government or enterprise | Medium | Medium | `Accessible.*` annotations + keyboard traversal (5.14) | 5.14 |
| R-29 | Cleartext API keys in `settings.ini` harvested by commodity infostealers | Medium | Medium | DPAPI / Credential Manager (3.10) | 3.10 |
| R-30 | Zip-slip via a crafted archive if the resolved `tar` on `PATH` is not libarchive | Low | High | Bundle and invoke `bsdtar` explicitly; post-extraction canonical-path assertion (2.10, 3.8) | 2.10, 3.8 |
| R-31 | Catalog drift between `catalog-src/` and `data/catalog.json` reaching a release | Medium | Medium | CI regeneration diff (1.6) | 1.6 |
| R-32 | Catalog updates never reach upgraded users — the catalog version is frozen at `0.1.14` while the app is `0.2.0`, and `CatalogManager` prefers the user's cache unless the bundled version is strictly newer | High | Medium | Derive the catalog version from `LASTUDIO_VERSION` at generation time | 1.6 |
| R-33 | Toolchain drift from the floating `windows-latest` image and unpinned choco packages makes old releases non-reproducible | High | Medium | Pin everything; attach a toolchain manifest (6.7, 6.9) | 6.7, 6.9 |
| R-34 | Anyone with push access can tag and ship — no branch protection, no `CODEOWNERS`, no signed tags | Medium | High | Governance (6.12) | 6.12 |
| R-35 | Advertised commercial relicensing becomes impossible as AGPL-only outside contributions accumulate without a CLA/DCO | Medium | Medium | `CONTRIBUTING.md` + DCO enforced in CI (4.12) | 4.12 |
| R-36 | Unbounded cache growth (~230 MB per 10 min of dubbing source, never collected) plus indefinite retention of decoded copies of the user's media | High | Medium | Cache lifecycle manager + purge control (5.7) | 5.7 |
| R-37 | Startup appears to hang on machines with mapped network drives (synchronous all-drives probe + recursive model scan before the window appears) | Medium | High | Defer to a worker thread; show the window first (5.8) | 5.8 |
| R-38 | Logs disclose transcript fragments, document names and full directory structure in public bug reports | Medium | Low | Redaction + sanitized-diagnostics action (3.15) | 3.15 |
| R-39 | Maintainer confusion from ~1,650 lines of unreachable QML, including a Runtime Management UI the README still advertises and screenshots | High | Low | Re-wire or delete + correct the README (5.12) | 5.12 |
| R-40 | The dubbing CLI-agent path passes `--dangerously-skip-permissions` and sends transcript text on the command line, visible to any local process | Low | Medium | Gate the flag; move the prompt off argv; add a consent notice | 5 (follow-up) |
| R-41 | **SignPath Foundation application is rejected or stalls**, leaving the zero-cost programme without Authenticode at RC time | Medium | Medium | Apply on day 1 so the answer arrives early; fallback ladder: (1) ship unsigned with `SHA256SUMS` + hash-verified updater (3.13 is independent of signing) + winget manifest for a SmartScreen-free install path; (2) budget a low-cost OSS certificate as a deliberate exception to D-17. Harden the free half regardless: repo 2FA, branch protection, signed tags (6.12) — with no Authenticode, GitHub account integrity **is** the trust root | 6.1, 6.12 |

---

## 7. Open Questions and Assumptions

### 7.1 Assumptions this plan makes

These are stated so they can be challenged. If any is wrong, the affected sections change.

| # | Assumption | If wrong |
|---|---|---|
| A-01 | Windows x64 is the only release target for this cycle; the README's "Broader cross-platform packaging" roadmap item is out of scope | Add a platform phase; several packaging decisions change |
| A-02 | The team controls `github.com/dduongtrandai/LA-Studio` and can add secrets, workflows and branch protection | 6.12 and all signing work need a different home |
| A-03 | The catalog-download delivery model for models and runtimes is correct and stays | §5.3 changes substantially; installer size becomes a primary constraint |
| A-04 | ~19 GB of default catalog content is acceptable given on-demand download | Catalog curation becomes a work item |
| A-05 | `~/.lastudio` remains the state root and survives upgrade/uninstall by default | 2.8 and the migration story change |
| A-06 | Vietnamese and English are the only shipped locales | 5.5 scales with locale count |
| A-07 | 2–3 engineers are available; effort estimates assume codebase familiarity | Scale §4.10 accordingly |
| A-08 | An in-app rollback/downgrade feature is out of scope; a documented, rehearsed procedure suffices | 6.11 becomes an engineering item |
| A-09 | Bit-identical reproducible builds are not a requirement for this release | §5.1 grows significantly |
| A-10 | The local API server is a supported product surface (it persists across restarts and can bind `0.0.0.0`), not developer-only — so it gets a full security pass | If developer-only: hide it behind a flag and reduce 3.9's scope |

### 7.2 Questions requiring a decision or external information

Ordered by how much they block. **Q-01 through Q-03 should be answered before Phase 2 planning.**

| # | Question | Blocks | Why it cannot be answered from the repo |
|---|---|---|---|
| **Q-01** | **Is a code-signing certificate procured or budgeted? EV or OV?** — **Recommended answer at $0: neither; apply to SignPath Foundation OSS signing on day 1 (D-17)** and confirm the project accepts the Foundation as the visible certificate holder (interacts with Q-18) | 6.1, 6.2, 3.13, and the entire trust story | No certificate reference, secret name, or placeholder exists anywhere. Review/issuance has multi-week lead time — **this is the long pole of the whole programme** |
| **Q-02** | **Does the CC BY-NC 4.0 MMS forced aligner stay in the default catalog?** | 4.9, 4.10 | A business decision. The README grants commercial use; the catalog offers a non-commercial model. These contradict |
| **Q-03** | **How should FFmpeg be delivered — bundled LGPL build, managed catalog runtime, or documented prerequisite?** | 2.9, and the installer's license notices | Nothing in the repo states an intent. Note that GPL vs LGPL only matters once the project delivers it; today it is purely the user's own installation |
| Q-04 | Does `windeployqt` 6.9.3 with `--compiler-runtime` in Release stage `vc_redist.x64.exe` or the CRT DLLs directly? | 2.4, R-22 | Qt is not installed in the analysis environment; this is a measurement, not a judgement |
| Q-05 | Does `jurplel/install-qt-action@v4` with `modules: qtmultimedia qtimageformats` also provide `qttools` (required for `LinguistTools`)? | Phase 1 CI | Releases have apparently shipped, so presumably yes — but it needs a CI log to confirm |
| Q-06 | Are the upstream runtime repos (`CrispStrobe/CrispASR`, `dduongtrandai/*`, `k2-fsa/sherpa-onnx`) under the project's control such that SHA-256 digests can be published as part of their release process? | 3.2 | Determines whether hashes are generated in-process or captured out-of-band |
| Q-07 | Is the vcpkg `builtin-baseline` commit resolvable in the `windows-latest` image's preinstalled vcpkg clone? | 6.8 | Only answerable by running the workflow or inspecting the runner image. Note: this fails *loudly* if unresolvable, so it is a reliability rather than a correctness risk |
| Q-08 | What hardware is available for RC validation — NVIDIA, AMD, Intel iGPU, pre-AVX2 CPU? | 7.2 | If a class cannot be tested, that must be stated in the release notes rather than implied |
| Q-09 | Should a per-user (non-admin) install mode be supported? | 2.6 | Users without local admin currently cannot install at all, and the state layout already lives in the user profile |
| Q-10 | What is the **minimum supported hardware baseline** — CPU instruction set, minimum NVIDIA driver, minimum VRAM per runtime variant? | 5.11 | Nothing in the repo declares one, so compatibility gating cannot be written without a product decision |
| Q-11 | Is `src/textnorm`'s upstream (`vietnormalizer` 0.2.3, commit `dd38778…`, and `nghitts`) Apache-2.0, MIT, or something else? | 4.3, R-07 | `UPSTREAM.md` says the review has not happened. Requires checking the upstream repositories |
| Q-12 | Is Qt used under LGPLv3 or a commercial Qt license? | 4.1, 4.2, R-06 | Inferred as LGPLv3 from CI's use of the open-source installer. A commercial license removes several obligations |
| Q-13 | Were `RuntimeSettingsTab.qml` and `HFExplorerPage.qml` intentionally retired or accidentally unwired? | 5.12 | The README still advertises and screenshots the Runtime Management UI |
| Q-14 | Are the `api` (remote HTTPS) and `cli` (external agent, Google OAuth, `--dangerously-skip-permissions`) translation-fix providers intended to ship enabled, given the offline-first positioning? | 5, R-40 | The `antigravity`/`agy` special case reads as a developer workflow rather than a user feature |
| Q-15 | Is the `windows-mingw-release` preset still supported? | D-02, 1.9 | Never built by CI; a known non-conforming construct in the tests would fail under GCC/Clang |
| Q-16 | Is a one-time reset of `~/.lastudio/registry.sqlite` and `catalog.json` acceptable for existing 0.1.x users, or must all state be preserved? | 5.3, 7.3 | Changes the migration strategy substantially |
| Q-17 | Is any crash-artifact upload acceptable for an explicitly offline product, or must crash data stay strictly local and user-initiated? | 5.2 | A product-positioning decision that changes the crash-reporting design |
| Q-18 | Is `duongtd` (LICENSE copyright holder) the same legal person as `Tran Dai Duong` (installer `AppPublisher`)? | 4.5, 6.2 | Matters for enforcement standing, the certificate subject name, and the commercial-licensing offer |
| Q-19 | Are old GitHub Releases (v0.1.7 … v0.2.0) still retained and downloadable? | 6.11 rollback runbook | Requires querying the GitHub API; rollback depends on previous assets remaining available |
| Q-20 | Is `app.setOrganizationName("")` at `main.cpp:38` deliberate? It shifts every `QStandardPaths` location including the cache tree that holds uncollected dubbing artifacts | 5.7 | No rationale found in the repo |

---

## 8. Pre-Release Acceptance Checklist

Complete and sign off before promoting a draft release to public. **Every unchecked item must be
either fixed or explicitly accepted with a named owner and a target release** — an unchecked item
with no decision is a blocker.

### 8.1 Build and reproducibility

- [ ] Clean clone → `.\scripts\bootstrap.bat` succeeds on a machine that has never built the project
- [ ] `docs/BUILD.md` prerequisites are complete and were followed verbatim by someone who did not write them
- [ ] Release build produces PDBs; they are archived and retrievable by version
- [ ] Both executables report the correct `FileVersion` (`(Get-Item …).VersionInfo.FileVersion`)
- [ ] Git tag, `LASTUDIO_VERSION`, installer `AppVersion` and both `VERSIONINFO` blocks all agree
- [ ] Toolchain manifest (MSVC, SDK, Qt, vcpkg commit, choco versions) is attached to the release
- [ ] No absolute build-machine paths in the shipped binaries (`LASTUDIO_SOURCE_DIR` is Debug-only)

### 8.2 Tests and static checks

- [ ] `ctest --output-on-failure` green on the release commit; ≥ 18 registered tests
- [ ] Test binary is location-independent (runs from a scratch directory) and hermetic (does not touch `~/.lastudio`)
- [ ] `qmllint` clean over all 114 QML files
- [ ] QML smoke test loads every studio route with zero engine warnings
- [ ] Catalog regeneration produces no diff
- [ ] `lupdate` produces no diff; zero `unfinished` and zero `vanished` entries in `lastudio_vi.ts`
- [ ] SHA-256 completeness script passes: every catalog entry yielding native code has a 64-char digest
- [ ] Nightly integration tier ran within the last 7 days; results reviewed

### 8.3 Packaging

- [ ] Post-stage manifest assertion passes; deleting any required artifact fails the build **naming the file**
- [ ] `out/stage/bin/espeak-ng/libespeak-ng.dll` and `espeak-ng-data/voices` present
- [ ] FFmpeg delivery verified per D-05 (bundled, or installable in one click from the Dubbing studio)
- [ ] `bsdtar.exe` and `7z.exe` staged; archive extraction never resolves `tar` from `PATH`
- [ ] `{app}\LICENSE`, `{app}\licenses\` and `THIRD-PARTY-NOTICES.md` present after install
- [ ] `{app}\resources\app_icon.rc` **absent**
- [ ] MSVC redistributable verified present or installed on a clean machine
- [ ] Installer has a permanent `AppId` GUID, `AppMutex`, `VersionInfoVersion` and `ignoreversion`
- [ ] Installer shows the AGPL license page
- [ ] Installer size recorded and compared against the previous release

### 8.4 Clean-machine validation (Windows Sandbox or a fresh VM)

- [ ] No Visual Studio, no VC++ redist, no FFmpeg, no eSpeak NG on the image
- [ ] Installer completes without error; `LA Studio.exe` reaches the main window
- [ ] `LAStudioRuntimeHost.exe` launches and responds
- [ ] First run with **no network** reaches a usable state with a clear message and no crash
- [ ] With the update setting off, a full session produces **zero** outbound connections (packet capture)
- [ ] One real model downloaded, verified, extracted and loaded per capability
- [ ] End-to-end video dubbing on a real MP4 completes, including ingest and muxed export
- [ ] Dubbing phoneme budgeting is **active** (verified in the log, not assumed)
- [ ] Upgrade over the previous release preserves models, history, settings and dubbing projects
- [ ] Downgrade to the previous release succeeds and the app starts cleanly (or is documented as unsupported)
- [ ] Uninstall removes the application; the data-removal option behaves as designed both ways
- [ ] No orphaned processes after uninstall

### 8.5 Security and supply chain

- [ ] Installer and both executables are Authenticode-signed; `signtool verify /pa` succeeds
- [ ] A clean-machine download shows no "Unknown publisher" SmartScreen dialog
- [ ] `SHA256SUMS` published; the documented verification procedure works
- [ ] The updater refuses a corrupted payload and surfaces an integrity error
- [ ] Every catalog entry yielding native code has a verified digest; a tampered archive is refused
- [ ] `espeak-ng.msi` is hash- and signature-verified before `msiexec`
- [ ] A marker DLL placed in the backends tree is **not** loaded into the process
- [ ] A crafted `../` archive is rejected and writes nothing outside `extractDir`
- [ ] API server: unauthenticated request → 401; `?api_key=` → 401; unexpected `Origin` → 403
- [ ] No plaintext secrets in `settings.ini`
- [ ] No IPC token visible on any process command line
- [ ] A malformed-WAV corpus is handled without crash; ASan reports zero issues
- [ ] `protocolVersion` mismatch is rejected before any DLL load

### 8.6 Legal and compliance

- [ ] `THIRD-PARTY-NOTICES.md` covers every `.dll`/`.exe` in the staged tree (cross-checked mechanically)
- [ ] `src/textnorm` provenance resolved; `LICENSE`, `NOTICE` and attribution headers present
- [ ] SPDX headers on all tracked source files
- [ ] Source archive attached to the release; the written source offer is stated in the installer and README
- [ ] In-app About → Licenses page displays the AGPL notice, the Qt LGPL notice with relinking
      statement, and third-party attributions
- [ ] Every catalog model has a validated `license`; restricted models require acknowledgement
- [ ] SBOM attached to the release
- [ ] The CC BY-NC 4.0 model's status is decided and reflected in both the catalog and the README

### 8.7 Release mechanics

- [ ] CI green for the exact tagged SHA
- [ ] Version-consistency gate passed
- [ ] Artifact smoke test passed in a clean runner
- [ ] Release created as a **draft**; promotion is a deliberate action
- [ ] Release notes list known issues, unsupported scenarios and untested hardware classes
- [ ] Rollback procedure rehearsed against this exact build and documented in `docs/RELEASE.md`
- [ ] Previous release assets still available
- [ ] Tag is annotated and signed
- [ ] The prerelease/Beta channel demotion path was verified to remove a release from the stable channel

### 8.8 Documentation and product truth

> The README must describe the product that actually ships. Several current statements do not.

- [ ] README feature table matches the shipped app (currently: **zero** mentions of LLM Chat, the
      Developer page, or the OpenAI-compatible API server)
- [ ] README Roadmap reflects shipped features (Video Dubbing and Voice Isolator are described as
      shipped in Project Updates but unchecked in the Roadmap)
- [ ] README Privacy section enumerates **every** outbound path: the startup update check, the
      optionally LAN-bound API server, and the cloud/CLI translation-fix providers
- [ ] Screenshots match the shipped UI (the Runtime Settings screenshot currently shows a view that
      is not reachable in the app)
- [ ] `docs/BUILD.md` lists every prerequisite including llama.cpp, FFmpeg and eSpeak NG
- [ ] `docs/RELEASE.md` exists with cut, promote and rollback procedures
- [ ] `docs/RC_TEST_MATRIX.md` exists and a completed, signed-off copy is attached to the release

---

## Appendix A — Evidence index for the primary findings

| Finding | Primary evidence |
|---|---|
| Clean clone cannot configure (B1) | `CMakeLists.txt:30-35`; `scripts/build.ps1:363-406`; `docs/BUILD.md:22-29`; `ls -d .deps` → absent |
| eSpeak NG never staged (B2) | `scripts/build.ps1:272-343,439` vs `grep -i espeak scripts/package.ps1` → 0 hits; `.github/workflows/windows-release.yml:65` |
| eSpeak runtime consequence | `EspeakNgPhonemizer.cpp:80-88,180` → `DubbingDuration.cpp:141-147` → `DubbingTranslationFixService.cpp:51-67` |
| FFmpeg hard dependency (B3) | `AudioFileDecoder.cpp:138-151`; `MediaIngestService.cpp:30-70`; `SeparationWorker.cpp:93`; `grep ffmpeg data/catalog.json` → 0 |
| CI scope (B4) | `.github/workflows/windows-release.yml:3-6`; `git ls-files .github` → 1 file |
| `.gitignore` blocks CI files | `.gitignore:113-118`; `git check-ignore -v .github/ci.yml` → ignored |
| No license in installer (B5) | `scripts/installer.iss.in:14`; `CMakeLists.txt:704-716` |
| No signing / hashing (B6) | repo-wide grep for `signtool`/`SHA256`/`Get-FileHash` → no packaging hits |
| Updater elevates unverified (B6) | `AppUpdateService.cpp:48-55,78-105,177-197` |
| Hash short-circuit | `DownloadInstallService.cpp:384-388` (`if (expectedSha256.isEmpty()) return true;`) |
| Blanket DLL preload (B7) | `include/runtimes/CrispCommon.h:67-137`; `PathUtils.cpp:32-41` |
| `WavIO` bounds (B8) | `src/audio/WavIO.cpp:60-95` |
| API auth (B9) | `ApiServerService.cpp:631-652,1012-1050` |
| No state versioning (B10) | `RegistryManager.cpp:196-240`; `data/registry_schema.sql:4-8`; `Settings.cpp:126-140`; `HistoryRepository.cpp:95-105` |
| Source/QML lists consistent | 114 = 114 QML; 154 = 144 + 15 + 4 C++; both diffs empty |
| Catalog in sync | Regeneration into a scratch tree produced a byte-identical `data/catalog.json` |
| Phantom include dirs are not build-blocking | No source includes `whisper.h` or any `omnivoice.cpp/src` header; `WhisperInterface.h` re-declares the types deliberately |

---

## Appendix B — Zero-cost delivery strategy (D-17)

Every potential cash cost in the programme, its paid default, the chosen free substitute, and the
honest trade-off. "Condition" is what must be true for the free route to hold.

| Cost item | Paid default | Zero-cost route | Trade-off / condition |
|---|---|---|---|
| **Authenticode signing** (6.1–6.2, 3.13) | OV/EV certificate, ~$100–600+/yr | **SignPath Foundation** free OSS code signing: the Foundation issues/holds the certificate and signs release artifacts through a policy-gated CI integration | Application review takes weeks — **apply day 1**. Signing is only available for artifacts built by the approved public pipeline (Phase 6 requires this anyway). The visible certificate holder is the Foundation, not `Tran Dai Duong` (interacts with Q-18). Instant-reputation EV is not part of the free tier — SmartScreen reputation accrues normally on the signed cert. Rejection → R-41 fallback ladder |
| Signature timestamping | — | Free public TSA servers (DigiCert/Sectigo) | None — free either way |
| **CI/CD compute** (1.5, 6.5) | Paid runners / minutes | GitHub-hosted runners are free **without minute limits for public repositories**, including Windows | Repo must stay public (it is, and AGPL distribution already assumes it). Queue/concurrency limits — acceptable at this project's scale |
| Nightly T4 integration tier (§5.2) | Paid self-hosted infra | A developer machine registered as a self-hosted runner; or reduce cadence to weekly / pre-release on-demand | Self-hosted runners on a public repo must be restricted to non-PR triggers (fork-PR code execution risk) — schedule/dispatch only |
| **FFmpeg delivery** (D-05, 2.9) | Bundling + hosting | **BtbN LGPL shared builds** (GitHub releases) as a catalog runtime, pinned by SHA-256, fetched from upstream at install time | Must pin release + hash and record the LGPL notice (Phase 4 does this). Upstream availability risk → document a re-pin/mirror procedure |
| **RC hardware matrix** (7.2, Q-08) | Buying/renting test machines | Community beta through the existing Facebook group + Discord: ship the RC as a GitHub **prerelease** (6.10), collect §5.4 results via a GitHub issue template. Clean-machine scenarios: Windows Sandbox + free Microsoft Windows evaluation VMs | Coverage is opportunistic, not guaranteed — release notes must state which hardware classes were actually tested (already required). Prerelease channel must be functional first (6.10) |
| Vietnamese translation (5.5) | Paid translator | Maintainer + community contributors (Vietnamese is the community's home locale) | Scheduling risk — start early, keep the CI `lupdate` gate so drift cannot recur |
| **Crash reporting** (5.2, Q-17) | Sentry/BugSplat/backend | Local-only minidumps + a "Report a problem" action that pre-fills a GitHub issue with the sanitized log tail; user attaches the `.dmp` manually | No passive telemetry — acceptable and *preferable* for an offline-first product. Symbolication stays possible via archived PDBs |
| PDB/symbol retention (2.11, 6.9) | Symbol server | Attach the PDB zip to each GitHub Release (public repo storage is free) | Symbols become public — fine for AGPL software; source is public anyway |
| SBOM + license tooling (4.11, 4.4) | Commercial scanners | Syft (CycloneDX/SPDX output) + REUSE lint for SPDX headers, both in CI | None material |
| Distribution / SmartScreen mitigation | Paid stores | **winget-pkgs** manifest (free, hash-pinned, avoids the browser-download SmartScreen prompt); Chocolatey community optional | Manifest PR per release — automatable in the release workflow |
| Toolchain (build) | Commercial IDE/Qt | Visual Studio Community / Build Tools (free for OSS), Qt under LGPLv3 (Q-12), CMake, Ninja, vcpkg, Inno Setup, 7-Zip — all free | Qt LGPL obligations already handled in Phase 4; confirm no commercial-Qt features are assumed (none found in the tree) |
| Hosting (source, releases, ~19 GB catalog content) | CDN/storage | GitHub (repo + Releases) for app artifacts; models/runtimes are already downloaded from Hugging Face and upstream GitHub releases at user install time — the project hosts no model bytes | Third-party availability — mitigated by mandatory hashes (D-06) and recorded provenance (G21/G23 fixes) |

**Explicitly not zero:** engineering time (§4.10 unchanged: 6–9 wk minimum shippable, 14–20 wk
full) and calendar latency on the SignPath review. No release-blocking control from Phases 0–7 is
weakened by the $0 posture; the only conditional degradation is *unsigned fallback* if R-41 fires,
and its mitigations are enumerated there.

**Order of operations for the zero-cost path:** submit the SignPath Foundation application and fix
`.gitignore`/CI (0.1, 1.5) in week 1 — both are prerequisites for everything the $0 strategy leans
on (approved public pipeline, free runners); everything else proceeds exactly as the phase plan
already sequences it.

---

*End of plan. Prepared from a direct reading of the repository at commit `0aaf446`. Items marked
"⚠ needs confirmation" or listed in §7.2 must be resolved before the affected work is scheduled.*
