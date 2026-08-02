#Requires -Version 5.1

<#
.SYNOPSIS
    Build and run unit tests for LA Studio.

.DESCRIPTION
    Resolves dependencies (Qt, vcpkg), compiles the unit tests target, and runs the tests.
#>

param(
    [string] $Preset = "windows-msvc-release",
    [string] $QtRoot,
    [string] $VcpkgRoot,
    [ValidateRange(1, 64)]
    [int] $MaxParallelJobs = 4,
    [switch] $NoBuild,
    [switch] $Verbose
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

. (Join-Path $PSScriptRoot "cmake_helpers.ps1")

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
    param([string] $Candidate, [string] $BuildPreset)

    $kit = if ($BuildPreset -like "*mingw*") { "mingw_64" } else { "msvc2022_64" }
    $options = @()
    if (-not [string]::IsNullOrWhiteSpace($Candidate)) { $options += $Candidate.Trim('"') }
    if (-not [string]::IsNullOrWhiteSpace($env:LA_QT)) { $options += $env:LA_QT }
    if (Test-Path "C:\Qt") {
        $options += Get-ChildItem "C:\Qt" -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending |
            ForEach-Object { $_.FullName }
    }
    $options += @("C:\Qt\6.9.3", "C:\Qt\6.9.1", "C:\Qt\6.8.3", "C:\Qt\6.8.2", "C:\Qt\6.8.1", "C:\Qt\6.8.0")

    foreach ($root in $options | Select-Object -Unique) {
        if (Test-Path (Join-Path $root "$kit\lib\cmake\Qt6\Qt6Config.cmake")) {
            return $root
        }
    }
    return $null
}

function Resolve-VcpkgRoot {
    param([string] $Candidate)

    $options = @()
    if (-not [string]::IsNullOrWhiteSpace($Candidate)) { $options += $Candidate.Trim('"') }
    $options += @(
        (Join-Path $RepoRoot ".deps\vcpkg")
    )
    if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) { $options += $env:VCPKG_ROOT }
    $options += @(
        "C:\vcpkg",
        "D:\vcpkg",
        "E:\dev\vcpkg",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\vcpkg",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\vcpkg",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg"
    )

    foreach ($root in $options | Select-Object -Unique) {
        if (Test-Path (Join-Path $root "scripts\buildsystems\vcpkg.cmake")) {
            return $root
        }
    }
    return $null
}

function Test-UnitTestsConfigured {
    param([Parameter(Mandatory)][string] $BuildDirectory)

    $cachePath = Join-Path $BuildDirectory "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDirectory "build.ninja")) -or
        -not (Test-Path -LiteralPath $cachePath)) {
        return $false
    }
    return [bool] (Select-String -LiteralPath $cachePath -Pattern '^BUILD_TESTING:BOOL=ON$' -Quiet)
}

function Configure-UnitTests {
    param(
        [Parameter(Mandatory)][string] $BuildPreset,
        [Parameter(Mandatory)][string] $ResolvedQtRoot,
        [Parameter(Mandatory)][string] $ResolvedVcpkgRoot
    )

    $qtKit = if ($BuildPreset -like "*mingw*") { "mingw_64" } else { "msvc2022_64" }
    $qtPrefix = Join-Path $ResolvedQtRoot $qtKit
    $toolchain = Join-Path $ResolvedVcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    $llamaHeaders = Join-Path $RepoRoot ".deps\llama.cpp"
    if (-not (Test-Path -LiteralPath (Join-Path $qtPrefix "lib\cmake\Qt6\Qt6Config.cmake"))) {
        throw "Qt6Config.cmake was not found under '$qtPrefix'."
    }
    if (-not (Test-Path -LiteralPath $toolchain)) {
        throw "vcpkg toolchain file was not found: $toolchain"
    }
    if (-not (Test-Path -LiteralPath $llamaHeaders)) {
        throw "llama.cpp headers were not found: $llamaHeaders. Run scripts\bootstrap.ps1 first."
    }

    # A portable/package configure intentionally turns BUILD_TESTING off.
    # Reconfigure only the test target with the same pinned dependencies,
    # rather than invoking the application-packaging build as a side effect.
    $ninjaPath = (Get-Command "ninja" -ErrorAction Stop).Source.Replace('\', '/')
    $cmakeArgs = @(
        "-DBUILD_TESTING=ON",
        "-DCMAKE_MAKE_PROGRAM=$ninjaPath",
        "-DCMAKE_PREFIX_PATH=$($qtPrefix.Replace('\', '/'))",
        "-DCMAKE_TOOLCHAIN_FILE=$($toolchain.Replace('\', '/'))",
        "-DVCPKG_ROOT=$($ResolvedVcpkgRoot.Replace('\', '/'))",
        "-DLLAMA_CPP_SOURCE_DIR=$($llamaHeaders.Replace('\', '/'))"
    )
    if ($BuildPreset -like "*mingw*") {
        $cmakeArgs += "-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic"
    } else {
        $cmakeArgs += "-DVCPKG_TARGET_TRIPLET=x64-windows"
        # Pin MSVC's linker: a stale MinGW ld.exe in PATH must never be used
        # for an MSVC test configuration.
        $linkerPath = (Get-Command "link.exe" -ErrorAction Stop).Source.Replace('\', '/')
        $archiverPath = (Get-Command "lib.exe" -ErrorAction Stop).Source.Replace('\', '/')
        $cmakeArgs += "-DCMAKE_LINKER=$linkerPath"
        $cmakeArgs += "-DCMAKE_AR=$archiverPath"
    }

    $env:VCPKG_ROOT = $ResolvedVcpkgRoot
    $env:VCPKG_OVERLAY_TRIPLETS = ""
    $env:VCPKG_DEFAULT_TRIPLET = ""
    Write-Host ">> Configuring unit test target..." -ForegroundColor Cyan
    cmake --preset $BuildPreset @cmakeArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Ensure-MsvcEnvironment {
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

    foreach ($line in $envDump) {
        $idx = $line.IndexOf("=")
        if ($idx -gt 0) {
            $name = $line.Substring(0, $idx)
            $value = $line.Substring($idx + 1)
            Set-Item -Path "Env:$name" -Value $value
        }
    }

    if (-not (Test-Command "cl.exe")) {
        throw "MSVC environment was initialized but cl.exe is still unavailable."
    }
}

# Resolve and ensure commands
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

$resolvedQtRoot = Resolve-QtRoot -Candidate $QtRoot -BuildPreset $Preset
if ([string]::IsNullOrWhiteSpace($resolvedQtRoot)) {
    throw "Qt root not detected for preset '$Preset'. Pass -QtRoot or set LA_QT."
}

$kit = if ($Preset -like "*mingw*") { "mingw_64" } else { "msvc2022_64" }
$qtBin = Join-Path $resolvedQtRoot "$kit\bin"
Add-PathIfExists -PathEntry $qtBin

$buildDir = Join-Path $RepoRoot "out\build\$Preset"
Remove-StaleCMakeBuildDirectory -BuildDirectory $buildDir -ExpectedSourceDirectory $RepoRoot

# 1. Build unit tests if requested
if (-not $NoBuild) {
    $resolvedVcpkgRoot = Resolve-VcpkgRoot -Candidate $VcpkgRoot
    if ([string]::IsNullOrWhiteSpace($resolvedVcpkgRoot)) {
        throw "vcpkg root was not detected. Pass -VcpkgRoot <path-to-vcpkg>."
    }
    if (-not (Test-UnitTestsConfigured -BuildDirectory $buildDir)) {
        Configure-UnitTests -BuildPreset $Preset -ResolvedQtRoot $resolvedQtRoot -ResolvedVcpkgRoot $resolvedVcpkgRoot
    }
    Write-Host ">> Building unit tests target..." -ForegroundColor Cyan
    cmake --build $buildDir --target LAStudioUnitTests --parallel $MaxParallelJobs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# 2. Run through CTest, not the aggregate test executable directly.  Several
# tests require CTest fixtures (for example the staged FFmpeg/FFprobe runtime
# used by Subtitle OCR); bypassing CTest makes a correct fixture-dependent test
# fail merely because its setup never ran.
Write-Host ">> Running CTest suite..." -ForegroundColor Cyan
$ctestArgs = @("--test-dir", $buildDir, "--output-on-failure")
if ($Verbose) {
    $ctestArgs += "--verbose"
}
if ($args) {
    $ctestArgs += $args
}

& ctest @ctestArgs
exit $LASTEXITCODE
