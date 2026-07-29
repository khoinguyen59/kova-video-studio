# Build from Source (Windows)

This project is standardized around a single primary build flow on Windows:

1. MSVC 2022 toolchain
2. Qt 6.9.x (`msvc2022_64` kit)
3. CMake + Ninja
4. vcpkg manifest mode

## Quick Start

```powershell
git clone https://github.com/dduongtrandai/LA-Studio.git
cd LA-Studio
.\scripts\bootstrap.bat
```

After a successful build, executable output is:

`out/build/windows-msvc-release/LA-Studio-<version>.exe`

## Prerequisites

Install these before running bootstrap:

1. Visual Studio 2022 (or Build Tools) with MSVC x64 toolchain
2. Qt 6.9.x with the `msvc2022_64` kit and the Qt Multimedia, Qt Image Formats, and Qt Tools modules
3. CMake 3.21+
4. Ninja
5. Git (used to provision the pinned vcpkg baseline and llama.cpp b10036 headers automatically)
6. FFmpeg and FFprobe on `PATH` for development builds (release packages bundle a pinned runtime; Video Dubbing, source separation, and audio decoding fallback use it automatically)
7. Internet access for the first non-`-SkipDeploy` build, unless the eSpeak NG MSI is already cached in `.deps/espeak-ng`

Packaging only additionally requires 7-Zip and Inno Setup 6. eSpeak NG is downloaded and staged next
to the application by a normal deploy build; it is not a separate manual prerequisite.

## Bootstrap Behavior

`scripts/bootstrap.ps1` performs:

1. Tool checks (`git`, `cmake`, `ninja`)
2. Qt detection from `-QtRoot`, `LA_QT`, or common `C:\Qt\...` paths
3. vcpkg resolution from `-VcpkgRoot`, `VCPKG_ROOT`, common paths, or a managed clone pinned to the
   `vcpkg.json` baseline at `.deps/vcpkg`
4. Automatic provisioning of llama.cpp b10036 public headers at `.deps/llama.cpp`
5. Build execution via `scripts/build.ps1` with the resolved llama.cpp path
6. Normal deploy builds cache eSpeak NG 1.52.0 in `.deps/espeak-ng` and stage it under
   `out/build/<preset>/espeak-ng` with `libespeak-ng.dll` and `espeak-ng-data`.

## Common Commands

Default release build:

```powershell
.\scripts\bootstrap.bat
```

Faster development build (skip deployment step):

```powershell
.\scripts\bootstrap.bat -SkipDeploy
```

The `-SkipDeploy` form intentionally does not stage eSpeak NG. Run the default command
before launching the packaged application or validating dubbing phoneme counts.

Clean rebuild:

```powershell
.\scripts\bootstrap.bat -Clean
```

Explicit Qt path:

```powershell
.\scripts\bootstrap.bat -QtRoot C:\Qt\6.9.3
```

MinGW is experimental only: it is not covered by CI or release validation. Use it only for local
investigation; official binaries are built with MSVC.

```powershell
.\scripts\bootstrap.bat -Preset windows-mingw-release -QtRoot C:\Qt\6.9.3
```

Internal staged package only:

```powershell
.\scripts\package.ps1 -Preset windows-msvc-release -QtRoot .tools\Qt\6.9.3 -VcpkgRoot .deps\vcpkg -LlamaCppSourceDir .deps\llama.cpp -SkipInstaller -AllowUnsignedEspeakForInternalBuild
```

This explicit opt-in permits the currently pinned eSpeak NG MSI for internal testing even though
the upstream file is not Authenticode-signed. Its SHA-256 is still verified. Do not distribute or
promote this output to a release. Without the switch, packaging remains fail-closed and requires a
valid Authenticode signature as well as the pinned SHA-256.

For a deployed developer build (without creating `out/stage`), pass the same explicit flag through
bootstrap:

```powershell
.\scripts\bootstrap.bat -QtRoot .tools\Qt\6.9.3 -AllowUnsignedEspeakForInternalBuild
```

To keep an already-running staged build open while validating a fresh **internal**
payload, stage into another directory below `out/`. This option is deliberately
rejected when an installer is requested:

```powershell
.\scripts\package.ps1 -QtRoot .tools\Qt\6.9.3 -SkipInstaller `
  -AllowUnsignedEspeakForInternalBuild -StageDir out\stage-internal
```

This flag is intentionally opt-in and must never be used by a release build or CI release job.

Actionable QML lint gate (run after a preset has been configured):

```powershell
.\scripts\lint_qml.ps1 -QtRoot C:\Qt\6.9.3
```

## CMake Presets

Primary presets:

1. `windows-msvc-release`
2. `windows-msvc-debug`
3. `windows-mingw-release`

Legacy aliases (`x64-release`, `x64-debug`, `mingw-release`) are retained for compatibility only.

`Debug` builds keep the console attached for developer logging. All non-Debug Windows builds are packaged as GUI apps, so the terminal is hidden when `LA-Studio-<version>.exe` opens.

## Incremental Build and Build Speed

By default, CMake + Ninja already performs incremental builds. If you only change a few source files, only affected targets should rebuild.

If your build feels slow during daily development, the most common reason is deployment work after compile (for example, `windeployqt`), not full recompilation.

Recommended workflow:

1. Use `.\scripts\bootstrap.bat -SkipDeploy` while coding to reduce build time.
2. Run `.\scripts\bootstrap.bat` (without `-SkipDeploy`) before packaging, sharing binaries, or validating runtime dependencies.
3. Use `-Clean` only when you really need a full rebuild (toolchain change, cache corruption, major dependency switch).

## Notes

1. `CMakeUserPresets.json` is optional and user-local; it is not required for the official build path.
2. CI and local development should use `scripts/bootstrap.ps1` or `scripts/build.ps1` with explicit arguments.
