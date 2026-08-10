#Requires -Version 5.1

<#
.SYNOPSIS
    Configure and build LA Studio using CMake presets.

.DESCRIPTION
    Portable build entrypoint for local development and CI.
    Does not depend on CMakeUserPresets.json.
#>

[CmdletBinding()]
param(
    [string] $Preset = "windows-msvc-release",
    [string] $QtRoot,
    [string] $VcpkgRoot,
    [string] $LlamaCppSourceDir,
    [string] $Version,
    [ValidateRange(1, 64)]
    [int] $MaxParallelJobs = 4,
    [string] $ReleaseSuffix,
    [switch] $Clean,
    [switch] $SkipDeploy,
    [switch] $AllowUnsignedEspeakForInternalBuild
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

. (Join-Path $PSScriptRoot "cmake_helpers.ps1")
. (Join-Path $PSScriptRoot "runtime_helpers.ps1")

function Test-Command {
    param([string] $Name)
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Add-PathIfExists {
    param([string] $PathEntry)
    if (-not [string]::IsNullOrWhiteSpace($PathEntry) -and (Test-Path $PathEntry)) {
        $parts = $env:PATH -split ";"
        if ($parts -notcontains $PathEntry) {
            $env:PATH = "$PathEntry;$env:PATH"
        }
    }
}

function Ensure-Command {
    param(
        [string] $Name,
        [string[]] $FallbackPaths
    )

    if (Test-Command $Name) {
        return
    }

    foreach ($candidate in $FallbackPaths) {
        Add-PathIfExists -PathEntry $candidate
        if (Test-Command $Name) {
            return
        }
    }

    throw "$Name is required but was not found in PATH."
}

function Resolve-QtRoot {
    param([string] $Candidate)

    if (-not [string]::IsNullOrWhiteSpace($Candidate) -and (Test-Path -LiteralPath $Candidate)) {
        return $Candidate.Trim('"')
    }
    if (-not [string]::IsNullOrWhiteSpace($env:LA_QT) -and (Test-Path -LiteralPath $env:LA_QT)) {
        return $env:LA_QT
    }
    if (Test-Path "C:\Qt") {
        $latestQtRoot = Get-ChildItem "C:\Qt" -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending |
            Select-Object -First 1
        if ($latestQtRoot) {
            return $latestQtRoot.FullName
        }
    }
    return $null
}

function Resolve-VcpkgRoot {
    param([string] $Candidate)

    if (-not [string]::IsNullOrWhiteSpace($Candidate)) {
        return $Candidate.Trim('"')
    }
    if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
        return $env:VCPKG_ROOT
    }

    $knownRoots = @(
        (Join-Path $RepoRoot ".deps\vcpkg"),
        "C:\vcpkg",
        "D:\vcpkg",
        "E:\dev\vcpkg",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\vcpkg",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\vcpkg",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg"
    )

    foreach ($root in $knownRoots) {
        if (Test-Path (Join-Path $root "scripts\buildsystems\vcpkg.cmake")) {
            return $root
        }
    }
    return $null
}

function Get-QtKitName {
    param([string] $BuildPreset)
    if ($BuildPreset -like "*mingw*") {
        return "mingw_64"
    }
    return "msvc2022_64"
}

function Resolve-LlamaCppSourceDir {
    param([string] $Candidate)

    $candidates = @(
        $Candidate,
        $env:LLAMA_CPP_SOURCE_DIR,
        (Join-Path $RepoRoot ".deps\llama.cpp"),
        (Join-Path (Split-Path -Parent $RepoRoot) "llama.cpp")
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique

    foreach ($root in $candidates) {
        $normalizedRoot = $root.Trim('"')
        if ((Test-Path -LiteralPath (Join-Path $normalizedRoot "include\llama.h")) -and
            (Test-Path -LiteralPath (Join-Path $normalizedRoot "ggml\include\ggml.h"))) {
            return $normalizedRoot
        }
    }
    return $null
}

function Get-SourceAppVersion {
    $cmakePath = Join-Path $RepoRoot "CMakeLists.txt"
    $match = Select-String -LiteralPath $cmakePath -Pattern 'set\(LASTUDIO_VERSION\s+"([^"]+)"' | Select-Object -First 1
    if ($null -eq $match) {
        throw "Could not find LASTUDIO_VERSION in $cmakePath."
    }
    return $match.Matches[0].Groups[1].Value
}

function Normalize-AppVersion {
    param([string] $Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        $Value = Get-SourceAppVersion
    } else {
        $Value = $Value.Trim()
        if ($Value.StartsWith("v")) {
            $Value = $Value.Substring(1)
        }
    }
    if ($Value -notmatch '^[0-9]\.[0-9]\.[0-9]\.[0-9]$') {
        throw "Version must use four single digits (0-9), carrying at 9 (for example 0.0.1.0); got '$Value'."
    }
    return $Value
}

function Normalize-ReleaseSuffix {
    param([string] $Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return ""
    }
    $Value = $Value.Trim()
    if ($Value -notmatch '^-(alpha|beta|rc)\.[1-9][0-9]*$') {
        throw "Release suffix must be empty or -alpha.N, -beta.N, or -rc.N; got '$Value'."
    }
    return $Value
}

function Ensure-MsvcEnvironment {
    # A bare cl.exe can leak in through PATH (for example after another build
    # tool modified it) without INCLUDE/LIB.  In that state MSVC fails on its
    # first standard header, so do not mistake the executable for a usable
    # toolchain environment.
    $hasCppHeaders = -not [string]::IsNullOrWhiteSpace($env:INCLUDE) -and
        ($env:INCLUDE -split ";" | Where-Object { Test-Path (Join-Path $_ "type_traits") } | Select-Object -First 1)
    if ((Test-Command "cl.exe") -and $hasCppHeaders) {
        return
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "cl.exe not found in PATH and vswhere.exe was not found. Install Visual Studio 2022 C++ workload."
    }

    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installPath)) {
        throw "cl.exe not found in PATH and Visual Studio with C++ tools was not detected."
    }

    $vcvars = Join-Path $installPath "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path -LiteralPath $vcvars)) {
        throw "cl.exe not found in PATH and vcvars64.bat was not found at '$vcvars'."
    }

    Write-Host ">> Initializing MSVC toolchain environment" -ForegroundColor Cyan
    $envDump = & cmd.exe /d /c "`"$vcvars`" >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to initialize MSVC environment from '$vcvars'."
    }

    $seenEnvNames = @{}
    foreach ($line in $envDump) {
        $idx = $line.IndexOf("=")
        if ($idx -gt 0) {
            $name = $line.Substring(0, $idx)
            $value = $line.Substring($idx + 1)
            $normalizedName = $name.ToUpperInvariant()
            if ($seenEnvNames.ContainsKey($normalizedName)) {
                continue
            }
            $seenEnvNames[$normalizedName] = $true
            Set-Item -Path "Env:$name" -Value $value
        }
    }

    if (-not (Test-Command "cl.exe")) {
        throw "MSVC environment was initialized but cl.exe is still unavailable."
    }
}

function Test-CatalogRequiresWebp {
    $catalogPath = Join-Path $RepoRoot "data\catalog.json"
    if (-not (Test-Path -LiteralPath $catalogPath)) {
        return $false
    }
    return [bool](Select-String -LiteralPath $catalogPath -Pattern '"mimeType"\s*:\s*"image/webp"' -Quiet)
}

function Ensure-WebpImageFormatPlugin {
    param(
        [string] $QtPrefixPath,
        [string] $DeployRoot
    )

    if (-not (Test-CatalogRequiresWebp)) {
        return
    }

    $pluginName = "qwebp.dll"
    $sourcePlugin = Join-Path $QtPrefixPath "plugins\imageformats\$pluginName"
    $targetDir = Join-Path $DeployRoot "imageformats"
    $targetPlugin = Join-Path $targetDir $pluginName

    if (-not (Test-Path -LiteralPath $targetPlugin)) {
        if (-not (Test-Path -LiteralPath $sourcePlugin)) {
            throw "Catalog contains WebP thumbnails, but Qt WebP image plugin was not found at '$sourcePlugin'. Install the Qt Image Formats module (qtimageformats) for this Qt kit."
        }
        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
        Copy-Item -LiteralPath $sourcePlugin -Destination $targetPlugin -Force
        Write-Host ">> Deployed Qt WebP image plugin: $targetPlugin" -ForegroundColor Cyan
    }

    if (-not (Test-Path -LiteralPath $targetPlugin)) {
        throw "Catalog contains WebP thumbnails, but '$targetPlugin' was not deployed."
    }
}

function Ensure-ArchiveExtractor {
    param(
        [string] $DeployRoot,
        [string] $VcpkgRoot
    )
    $target = Join-Path $DeployRoot "7z.exe"
    if (Test-Path -LiteralPath $target) { return }

    $source = $null
    if (Test-Command "7z.exe") {
        $source = (Get-Command "7z.exe").Source
    }

    if ([string]::IsNullOrWhiteSpace($source)) {
        $installedCandidates = @(
            (Join-Path $env:ProgramFiles "7-Zip\7z.exe"),
            (Join-Path ${env:ProgramFiles(x86)} "7-Zip\7z.exe")
        )
        foreach ($candidate in $installedCandidates) {
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $source = $candidate
                break
            }
        }
    }

    if ([string]::IsNullOrWhiteSpace($source)) {
        $toolRoots = @(
            (Join-Path $VcpkgRoot "downloads\tools"),
            (Join-Path $RepoRoot ".deps\vcpkg\downloads\tools")
        ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique

        foreach ($toolRoot in $toolRoots) {
            if (-not (Test-Path -LiteralPath $toolRoot)) { continue }
            $candidate = Get-ChildItem -Path $toolRoot -Filter "7z.exe" -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($null -ne $candidate) {
                $source = $candidate.FullName
                break
            }
        }
    }

    if ($null -eq $source) {
        Write-Warning "7z.exe was not found; .tar.bz2 runtime extraction will require bsdtar in PATH."
        return
    }

    Copy-Item -LiteralPath $source -Destination $target -Force
    $sevenZipDll = Join-Path (Split-Path -Parent $source) "7z.dll"
    if (Test-Path -LiteralPath $sevenZipDll) {
        Copy-Item -LiteralPath $sevenZipDll -Destination (Join-Path $DeployRoot "7z.dll") -Force
    }
}

Ensure-Command -Name "cmake" -FallbackPaths @(
    "C:\Qt\Tools\CMake_64\bin",
    "C:\Program Files\CMake\bin"
)
Ensure-Command -Name "ninja" -FallbackPaths @(
    "C:\Qt\Tools\Ninja",
    "C:\Program Files\Ninja"
)
if ($Preset -like "*mingw*") {
    Add-PathIfExists -PathEntry "C:\Qt\Tools\mingw1310_64\bin"
} else {
    Ensure-MsvcEnvironment
}

$QtRoot = Resolve-QtRoot -Candidate $QtRoot
$VcpkgRoot = Resolve-VcpkgRoot -Candidate $VcpkgRoot
$LlamaCppSourceDir = Resolve-LlamaCppSourceDir -Candidate $LlamaCppSourceDir
$Version = Normalize-AppVersion -Value $Version
$ReleaseSuffix = Normalize-ReleaseSuffix -Value $ReleaseSuffix

$cmakeArgs = @()
$kitName = Get-QtKitName -BuildPreset $Preset
$ninjaCommand = Get-Command ninja -ErrorAction Stop
$cmakeArgs += "-DCMAKE_MAKE_PROGRAM=$($ninjaCommand.Source.Replace('\', '/'))"

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    throw "Qt root not found. Pass -QtRoot or set LA_QT environment variable."
}

$qtPrefixPath = Join-Path $QtRoot $kitName
if (-not (Test-Path (Join-Path $qtPrefixPath "lib\cmake\Qt6\Qt6Config.cmake"))) {
    throw "Qt6Config.cmake not found in '$qtPrefixPath'."
}
$cmakeArgs += "-DCMAKE_PREFIX_PATH=$($qtPrefixPath.Replace('\', '/'))"

if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    throw "vcpkg root not found. Pass -VcpkgRoot or set VCPKG_ROOT environment variable."
}
if ([string]::IsNullOrWhiteSpace($LlamaCppSourceDir)) {
    throw "llama.cpp b10036 headers not found. Run scripts\\bootstrap.ps1 or pass -LlamaCppSourceDir."
}

& (Join-Path $PSScriptRoot "verify_runtime_abi.ps1")

$toolchainFile = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $toolchainFile)) {
    throw "vcpkg toolchain file not found: $toolchainFile"
}

# Isolate this build from machine-level vcpkg overlays/triplets.
$env:VCPKG_ROOT = $VcpkgRoot
if ($env:VCPKG_OVERLAY_TRIPLETS) {
    Write-Warning "Ignoring VCPKG_OVERLAY_TRIPLETS from environment for reproducible build."
}
if ($env:VCPKG_DEFAULT_TRIPLET) {
    Write-Warning "Ignoring VCPKG_DEFAULT_TRIPLET from environment for reproducible build."
}
$env:VCPKG_OVERLAY_TRIPLETS = ""
$env:VCPKG_DEFAULT_TRIPLET = ""

$cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$($toolchainFile.Replace('\', '/'))"
$cmakeArgs += "-DVCPKG_ROOT=$($VcpkgRoot.Replace('\', '/'))"
$cmakeArgs += "-DLLAMA_CPP_SOURCE_DIR=$($LlamaCppSourceDir.Replace('\', '/'))"
$cmakeArgs += "-DLASTUDIO_VERSION=$Version"
$cmakeArgs += "-DLASTUDIO_RELEASE_SUFFIX=$ReleaseSuffix"
$cmakeArgs += "-DLASTUDIO_PORTABLE_INTERNAL_LAYOUT=OFF"
$cmakeArgs += "-DBUILD_TESTING=ON"

if ($Preset -like "*mingw*") {
    $cmakeArgs += "-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic"
} else {
    $cmakeArgs += "-DVCPKG_TARGET_TRIPLET=x64-windows"
    # CMake can otherwise select a stray ld.exe from a developer's PATH even
    # after vcvars has initialized MSVC. Pin both the linker and static
    # librarian from the MSVC environment: a stale MinGW ar.exe cannot create
    # an MSVC .lib archive and used to break an otherwise valid configure.
    $linkerCommand = Get-Command "link.exe" -ErrorAction Stop
    $archiverCommand = Get-Command "lib.exe" -ErrorAction Stop
    $cmakeArgs += "-DCMAKE_LINKER=$($linkerCommand.Source.Replace('\', '/'))"
    $cmakeArgs += "-DCMAKE_AR=$($archiverCommand.Source.Replace('\', '/'))"
}

if ($Clean) {
    $buildDirToClean = Join-Path $RepoRoot "out\build\$Preset"
    if (Test-Path -LiteralPath $buildDirToClean) {
        Remove-Item -LiteralPath $buildDirToClean -Recurse -Force
    }
}

$buildDir = Join-Path $RepoRoot "out\build\$Preset"
Remove-StaleCMakeBuildDirectory -BuildDirectory $buildDir -ExpectedSourceDirectory $RepoRoot
if ($Preset -notlike "*mingw*" -and (Test-Path -LiteralPath $buildDir)) {
    # The compiler probe persists CMAKE_AR in CMakeCXXCompiler.cmake.  If an
    # older MSVC build inherited MinGW's ar.exe from PATH, merely passing
    # -DCMAKE_AR on the next configure is too late: Ninja will still issue
    # GNU ar with MSVC flags. Reset only this repository-owned build cache.
    $compilerInfo = Get-ChildItem -LiteralPath (Join-Path $buildDir "CMakeFiles") `
        -Recurse -Filter "CMakeCXXCompiler.cmake" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $compilerInfo) {
        $archiverMatch = Select-String -LiteralPath $compilerInfo.FullName `
            -Pattern '^set\(CMAKE_AR "([^"]+)"\)' | Select-Object -First 1
        if ($null -ne $archiverMatch) {
            $configuredArchiver = $archiverMatch.Matches[0].Groups[1].Value
            $expectedArchiver = (Get-Command "lib.exe" -ErrorAction Stop).Source
            $sameArchiver = [string]::Equals(
                [IO.Path]::GetFullPath($configuredArchiver),
                [IO.Path]::GetFullPath($expectedArchiver),
                [System.StringComparison]::OrdinalIgnoreCase)
            if (-not $sameArchiver) {
                $resolvedRepo = (Resolve-Path -LiteralPath $RepoRoot).Path.TrimEnd('\', '/')
                $resolvedBuild = (Resolve-Path -LiteralPath $buildDir).Path
                if (-not $resolvedBuild.StartsWith($resolvedRepo + '\',
                                                    [System.StringComparison]::OrdinalIgnoreCase)) {
                    throw "Refusing to reset a CMake toolchain outside this repository: $resolvedBuild"
                }
                Write-Host ">> Resetting stale CMake toolchain cache with archiver '$configuredArchiver'..." -ForegroundColor Yellow
                Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
            }
        }
    }
}

Write-Host ">> Configuring CMake preset: $Preset" -ForegroundColor Cyan
cmake --preset $Preset $cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ">> Building CMake preset: $Preset" -ForegroundColor Cyan
cmake --build --preset $Preset --parallel $MaxParallelJobs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exePath = Join-Path $buildDir "LA-Studio-$Version.exe"
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Build completed but executable was not found: $exePath"
}

if (-not $SkipDeploy) {
    $windeployqt = Join-Path $qtPrefixPath "bin\windeployqt.exe"
    if (Test-Path -LiteralPath $windeployqt) {
        Write-Host ">> Running windeployqt" -ForegroundColor Cyan
        & $windeployqt --verbose 0 --qmldir qml --no-translations --compiler-runtime $exePath
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        Ensure-WebpImageFormatPlugin -QtPrefixPath $qtPrefixPath -DeployRoot $buildDir
        Ensure-ArchiveExtractor -DeployRoot $buildDir -VcpkgRoot $VcpkgRoot
        if ($AllowUnsignedEspeakForInternalBuild) {
            Write-Warning "INTERNAL BUILD ONLY: permitting the SHA-256-verified but unsigned eSpeak NG MSI. Do not distribute this deployed build or promote it to a release."
        }
        Ensure-EspeakNgRuntime -RepositoryRoot $RepoRoot -DeployRoot $buildDir -AllowUnsignedEspeakForInternalBuild:$AllowUnsignedEspeakForInternalBuild
        Assert-StagedEspeakNgRuntime -DeployRoot $buildDir
    } else {
        Write-Warning "windeployqt not found at $windeployqt. Skipping deployment."
    }

    if ($Preset -like "*mingw*") {
        $mingwRuntimeBin = "C:\Qt\Tools\mingw1310_64\bin"
        $runtimeDlls = @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll", "libgomp-1.dll")
        foreach ($dll in $runtimeDlls) {
            $src = Join-Path $mingwRuntimeBin $dll
            if (Test-Path -LiteralPath $src) {
                Copy-Item -LiteralPath $src -Destination $buildDir -Force
            }
        }
    }
}

Write-Host "[SUCCESS] Build output: $exePath" -ForegroundColor Green
