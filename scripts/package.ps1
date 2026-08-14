#Requires -Version 5.1

<#
.SYNOPSIS
    Build, stage, and package LA Studio into a Windows installer.
.DESCRIPTION
    Compiles the project, installs files to out/stage (or an explicit internal
    staging directory), deploys Qt dependencies, and runs Inno Setup to
    generate a single-file EXE installer.
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
    [string] $StageDir,
    [string] $PaddleRuntimeRoot,
    [switch] $SkipInstaller,
    [switch] $PortableInternalLayout,
    [switch] $AllowUnsignedEspeakForInternalBuild
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

. (Join-Path $PSScriptRoot "cmake_helpers.ps1")
. (Join-Path $PSScriptRoot "runtime_helpers.ps1")

# Helper: Test if command exists
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
    if (Test-Command $Name) { return }
    foreach ($candidate in $FallbackPaths) {
        Add-PathIfExists -PathEntry $candidate
        if (Test-Command $Name) { return }
    }
    throw "$Name is required but was not found in PATH."
}

function Ensure-MsvcEnvironment {
    $hasCppHeaders = -not [string]::IsNullOrWhiteSpace($env:INCLUDE) -and
        ($env:INCLUDE -split ";" | Where-Object { Test-Path (Join-Path $_ "type_traits") } | Select-Object -First 1)
    if ((Test-Command "cl.exe") -and $hasCppHeaders) { return }
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
    Write-Host ">> Initializing MSVC toolchain environment..." -ForegroundColor Cyan
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

function Get-VersionedApplicationExecutableName {
    param([Parameter(Mandatory = $true)][string] $AppVersion)
    return "LA-Studio-$AppVersion.exe"
}

function Assert-StagingDirectoryCanBeRebuilt {
    param(
        [Parameter(Mandatory = $true)]
        [string] $StageRoot,
        [Parameter(Mandatory = $true)]
        [string] $ApplicationExecutableName
    )

    if (-not (Test-Path -LiteralPath $StageRoot)) { return }

    $pathSeparator = [IO.Path]::DirectorySeparatorChar
    $normalizedStageRoot = [IO.Path]::GetFullPath($StageRoot).TrimEnd($pathSeparator) + $pathSeparator
    $blockers = @()
    $applicationProcessName = [IO.Path]::GetFileNameWithoutExtension($ApplicationExecutableName)
    $candidates = Get-Process -Name $applicationProcessName, 'LAStudioRuntimeHost' -ErrorAction SilentlyContinue
    foreach ($candidate in $candidates) {
        try {
            $executablePath = $candidate.Path
        } catch {
            continue
        }
        if ([string]::IsNullOrWhiteSpace($executablePath)) { continue }
        try {
            $normalizedExecutablePath = [IO.Path]::GetFullPath($executablePath)
        } catch {
            continue
        }
        if ($normalizedExecutablePath.StartsWith($normalizedStageRoot, [StringComparison]::OrdinalIgnoreCase)) {
            $blockers += "{0}.exe (PID {1})" -f $candidate.ProcessName, $candidate.Id
        }
    }

    if ($blockers.Count -gt 0) {
        throw "The existing staging payload is in use by $($blockers -join ', '). Close the staged application before running package.ps1 so its files are not partially replaced."
    }
}

function Resolve-StageDirectory {
    param(
        [string] $Candidate,
        [Parameter(Mandatory = $true)]
        [string] $RepositoryRoot,
        [Parameter(Mandatory = $true)]
        [bool] $InstallerRequested,
        [Parameter(Mandatory = $true)]
        [string] $AppVersion,
        [Parameter(Mandatory = $true)]
        [bool] $PortableLayout
    )

    $defaultStage = if ($PortableLayout) {
        Join-Path $RepositoryRoot ("out\LA-Studio-" + $AppVersion)
    } elseif ($InstallerRequested) {
        Join-Path $RepositoryRoot 'out\stage'
    } else {
        Join-Path $RepositoryRoot 'out\stage'
    }
    if ([string]::IsNullOrWhiteSpace($Candidate)) { return $defaultStage }

    if ($InstallerRequested) {
        throw '-StageDir is for internal staging only; omit it when building an installer.'
    }

    $candidatePath = $Candidate.Trim().Trim('"')
    $resolved = if ([IO.Path]::IsPathRooted($candidatePath)) {
        [IO.Path]::GetFullPath($candidatePath)
    } else {
        [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $candidatePath))
    }
    $outRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'out')).TrimEnd([IO.Path]::DirectorySeparatorChar)
    $outPrefix = $outRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        $resolved.TrimEnd([IO.Path]::DirectorySeparatorChar).Equals($outRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "-StageDir must be a directory below '$outRoot'."
    }
    return $resolved
}

function Resolve-SevenZipExecutable {
    param([string] $VcpkgRoot)

    $fromPath = Get-Command "7z.exe" -ErrorAction SilentlyContinue
    if ($null -ne $fromPath) {
        return $fromPath.Source
    }

    $candidates = @(
        (Join-Path $env:ProgramFiles "7-Zip\7z.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "7-Zip\7z.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    $toolRoots = @(
        (Join-Path $VcpkgRoot "downloads\tools"),
        (Join-Path $RepoRoot ".deps\vcpkg\downloads\tools")
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique
    foreach ($toolRoot in $toolRoots) {
        if (-not (Test-Path -LiteralPath $toolRoot)) { continue }
        $candidate = Get-ChildItem -Path $toolRoot -Filter "7z.exe" -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $candidate) {
            return $candidate.FullName
        }
    }

    return $null
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



# Helper: Find Qt path
function Resolve-QtRoot {
    param([string] $Candidate)
    if (-not [string]::IsNullOrWhiteSpace($Candidate) -and (Test-Path -LiteralPath $Candidate)) { return $Candidate.Trim('"') }
    if (-not [string]::IsNullOrWhiteSpace($env:LA_QT) -and (Test-Path -LiteralPath $env:LA_QT)) { return $env:LA_QT }
    if (Test-Path "C:\Qt") {
        $latestQtRoot = Get-ChildItem "C:\Qt" -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending |
            Select-Object -First 1
        if ($latestQtRoot) { return $latestQtRoot.FullName }
    }
    return $null
}

# Helper: Find vcpkg path
function Resolve-VcpkgRoot {
    param([string] $Candidate)
    if (-not [string]::IsNullOrWhiteSpace($Candidate)) { return $Candidate.Trim('"') }
    if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) { return $env:VCPKG_ROOT }
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
        if (Test-Path (Join-Path $root "scripts\buildsystems\vcpkg.cmake")) { return $root }
    }
    return $null
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

# Helper: Find Inno Setup ISCC compiler
function Find-Iscc {
    if (Test-Command "iscc") { return "iscc" }
    $paths = @(
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe",
        "C:\Program Files (x86)\Inno Setup 5\ISCC.exe",
        "C:\Program Files\Inno Setup 5\ISCC.exe"
    )
    foreach ($path in $paths) {
        if (Test-Path $path) { return $path }
    }
    return $null
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
    $source = Resolve-SevenZipExecutable -VcpkgRoot $VcpkgRoot

    if ([string]::IsNullOrWhiteSpace($source)) {
        throw "7z.exe is required to extract .tar.bz2 runtime packages on Windows. Install 7-Zip or provide the vcpkg tools cache."
    }

    if (-not (Test-Path -LiteralPath $target)) {
        Copy-Item -LiteralPath $source -Destination $target -Force
        $sevenZipDll = Join-Path (Split-Path -Parent $source) "7z.dll"
        if (Test-Path -LiteralPath $sevenZipDll) {
            Copy-Item -LiteralPath $sevenZipDll -Destination (Join-Path $DeployRoot "7z.dll") -Force
        }
    }

    return $source
}

function Ensure-Bsdtar {
    param(
        [Parameter(Mandatory = $true)]
        [string] $RepositoryRoot,
        [Parameter(Mandatory = $true)]
        [string] $DeployRoot,
        [Parameter(Mandatory = $true)]
        [string] $StageRoot,
        [Parameter(Mandatory = $true)]
        [string] $BuildDirectory,
        [Parameter(Mandatory = $true)]
        [string] $Triplet
    )

    $target = Join-Path $DeployRoot "bsdtar.exe"
    if (Test-Path -LiteralPath $target -PathType Leaf) {
        return
    }

    # Build from pinned source. Never redistribute the Windows inbox tar.exe
    # or a binary resolved from PATH.
    $version = "3.8.1"
    $sourceArchive = Join-Path $RepositoryRoot ".deps\libarchive-$version.tar.xz"
    $sourceSha256 = "19f917d42d530f98815ac824d90c7eaf648e9d9a50e4f309c812457ffa5496b5"
    $sourceUrl = "https://github.com/libarchive/libarchive/releases/download/v$version/libarchive-$version.tar.xz"
    $sourceRoot = Join-Path $RepositoryRoot ".deps\libarchive-$version"
    $sevenZip = Join-Path $DeployRoot "7z.exe"
    if (-not (Test-Path -LiteralPath $sevenZip -PathType Leaf)) {
        throw "A staged 7z.exe is required to unpack pinned libarchive source."
    }

    New-Item -ItemType Directory -Path (Split-Path -Parent $sourceArchive) -Force | Out-Null
    if (-not (Test-Path -LiteralPath $sourceArchive -PathType Leaf)) {
        Write-Host ">> Downloading libarchive $version source" -ForegroundColor Cyan
        Invoke-WebRequest -Uri $sourceUrl -OutFile $sourceArchive -UseBasicParsing
    }
    $actualSha256 = (Get-FileHash -LiteralPath $sourceArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualSha256 -ne $sourceSha256) {
        throw "libarchive source SHA-256 mismatch. Expected $sourceSha256 but got $actualSha256."
    }

    if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot "CMakeLists.txt") -PathType Leaf)) {
        $extractRoot = Join-Path $RepositoryRoot ".deps\libarchive-extract-$version"
        if (Test-Path -LiteralPath $extractRoot) {
            Remove-Item -LiteralPath $extractRoot -Recurse -Force
        }
        New-Item -ItemType Directory -Path $extractRoot -Force | Out-Null
        & $sevenZip x -y "-o$extractRoot" $sourceArchive | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Failed to decompress libarchive source archive." }
        $tarArchive = Join-Path $extractRoot "libarchive-$version.tar"
        if (-not (Test-Path -LiteralPath $tarArchive -PathType Leaf)) {
            throw "libarchive source decompression did not produce $tarArchive."
        }
        & $sevenZip x -y "-o$extractRoot" $tarArchive | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Failed to extract libarchive source tarball." }
        $extractedSource = Join-Path $extractRoot "libarchive-$version"
        if (-not (Test-Path -LiteralPath (Join-Path $extractedSource "CMakeLists.txt") -PathType Leaf) -or
            -not (Test-Path -LiteralPath (Join-Path $extractedSource "COPYING") -PathType Leaf)) {
            throw "Pinned libarchive source tree is incomplete after extraction."
        }
        if (Test-Path -LiteralPath $sourceRoot) {
            Remove-Item -LiteralPath $sourceRoot -Recurse -Force
        }
        Move-Item -LiteralPath $extractedSource -Destination $sourceRoot -Force
        Remove-Item -LiteralPath $extractRoot -Recurse -Force
    }

    $vcpkgPrefix = Join-Path $BuildDirectory "vcpkg_installed\$Triplet"
    if (-not (Test-Path -LiteralPath (Join-Path $vcpkgPrefix "include\bzlib.h") -PathType Leaf)) {
        throw "Pinned bzip2 dependency was not installed at $vcpkgPrefix."
    }
    $bsdtarBuildDir = Join-Path $RepositoryRoot "out\build\bsdtar-$Triplet"
    $prefixPath = $vcpkgPrefix.Replace('\', '/')
    Write-Host ">> Building pinned bsdtar $version" -ForegroundColor Cyan
    # libarchive's optional-package probes can emit developer warnings on
    # newer CMake releases.  This is a pinned third-party runtime build, and
    # those diagnostics are not build failures; suppress only developer
    # warnings while preserving the explicit exit-code gate below.
    & cmake -Wno-dev -S $sourceRoot -B $bsdtarBuildDir -G Ninja `
        "-DCMAKE_BUILD_TYPE=Release" `
        "-DCMAKE_PREFIX_PATH=$prefixPath" `
        "-DENABLE_TAR=ON" `
        "-DENABLE_CPIO=OFF" `
        "-DENABLE_CAT=OFF" `
        "-DENABLE_TEST=OFF" `
        "-DENABLE_OPENSSL=OFF" `
        "-DENABLE_BZip2=ON" `
        "-DENABLE_ZLIB=ON" `
        "-DENABLE_LZMA=OFF" `
        "-DENABLE_ZSTD=OFF"
    if ($LASTEXITCODE -ne 0) { throw "Failed to configure pinned bsdtar source." }
    & cmake --build $bsdtarBuildDir --target bsdtar --parallel $MaxParallelJobs
    if ($LASTEXITCODE -ne 0) { throw "Failed to build pinned bsdtar source." }

    $builtBsdtar = Get-ChildItem -Path $bsdtarBuildDir -Filter "bsdtar.exe" -Recurse -File |
        Select-Object -First 1
    if ($null -eq $builtBsdtar) {
        throw "Pinned libarchive build did not produce bsdtar.exe."
    }
    Copy-Item -LiteralPath $builtBsdtar.FullName -Destination $target -Force
    $archiveDll = Get-ChildItem -Path $bsdtarBuildDir -Filter "archive.dll" -Recurse -File |
        Select-Object -First 1
    if ($null -ne $archiveDll) {
        Copy-Item -LiteralPath $archiveDll.FullName -Destination (Join-Path $DeployRoot "archive.dll") -Force
    }
    $versionOutput = (& $target --version 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $versionOutput -notmatch "^bsdtar $([regex]::Escape($version))") {
        throw "Staged bsdtar failed version verification: $versionOutput"
    }

    $licenseDir = Join-Path $StageRoot "licenses\libarchive"
    New-Item -ItemType Directory -Path $licenseDir -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $sourceRoot "COPYING") -Destination (Join-Path $licenseDir "LICENSE") -Force
    Write-Host ">> Staged pinned bsdtar $version" -ForegroundColor Green
}

function Stage-ThirdPartyLicenseTexts {
    param(
        [Parameter(Mandatory = $true)]
        [string] $RepositoryRoot,
        [Parameter(Mandatory = $true)]
        [string] $StageRoot,
        [Parameter(Mandatory = $true)]
        [string] $BuildDirectory,
        [Parameter(Mandatory = $true)]
        [string] $Triplet,
        [Parameter(Mandatory = $true)]
        [string] $QtRoot,
        [Parameter(Mandatory = $true)]
        [string] $SevenZipSource
    )

    $licensesRoot = Join-Path $StageRoot "licenses"
    New-Item -ItemType Directory -Path $licensesRoot -Force | Out-Null

    $vcpkgPrefix = Join-Path $BuildDirectory "vcpkg_installed\$Triplet"
    # Every dynamic vcpkg library is copied into the portable layout. Stage
    # the matching port copyright text rather than keeping a hand-maintained,
    # incomplete list when the bundled OCR runtime brings dependencies.
    $vcpkgLicenses = @(
        Get-ChildItem -LiteralPath (Join-Path $vcpkgPrefix "share") -Directory | ForEach-Object {
            $copyright = Join-Path $_.FullName "copyright"
            if (Test-Path -LiteralPath $copyright -PathType Leaf) {
                @{ Name = $_.Name; Source = $copyright }
            }
        }
    )
    foreach ($entry in $vcpkgLicenses) {
        if (-not (Test-Path -LiteralPath $entry.Source -PathType Leaf)) {
            throw "Pinned vcpkg license text was not found: $($entry.Source)"
        }
        $destination = Join-Path (Join-Path $licensesRoot $entry.Name) "LICENSE"
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $entry.Source -Destination $destination -Force
    }

    $qtLicenseRoots = @(
        (Join-Path $QtRoot "LICENSES"),
        (Join-Path $QtRoot "msvc2022_64\LICENSES")
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Container } | Select-Object -Unique
    $qtLicenseTarget = Join-Path $licensesRoot "qt"
    New-Item -ItemType Directory -Path $qtLicenseTarget -Force | Out-Null
    foreach ($qtLicenseRoot in $qtLicenseRoots) {
        Get-ChildItem -LiteralPath $qtLicenseRoot -Force | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $qtLicenseTarget -Recurse -Force
        }
    }

    $sevenZipLicense = Join-Path (Split-Path -Parent $SevenZipSource) "License.txt"
    if ([string]::IsNullOrWhiteSpace($sevenZipLicense) -or -not (Test-Path -LiteralPath $sevenZipLicense -PathType Leaf)) {
        throw "7-Zip License.txt was not found beside the configured 7z.exe."
    }
    $sevenZipTarget = Join-Path (Join-Path $licensesRoot "7-Zip") "License.txt"
    New-Item -ItemType Directory -Path (Split-Path -Parent $sevenZipTarget) -Force | Out-Null
    Copy-Item -LiteralPath $sevenZipLicense -Destination $sevenZipTarget -Force

    $gnuLicenses = @(
        @{ File = "GPL-3.0.txt"; Url = "https://www.gnu.org/licenses/gpl-3.0.txt"; Sha256 = "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986" },
        @{ File = "LGPL-3.0.txt"; Url = "https://www.gnu.org/licenses/lgpl-3.0.txt"; Sha256 = "e3a994d82e644b03a792a930f574002658412f62407f5fee083f2555c5f23118" }
    )
    $licenseCache = Join-Path $RepositoryRoot ".deps\licenses"
    New-Item -ItemType Directory -Path $licenseCache -Force | Out-Null
    foreach ($entry in $gnuLicenses) {
        $cachedPath = Join-Path $licenseCache $entry.File
        if (-not (Test-Path -LiteralPath $cachedPath -PathType Leaf)) {
            Invoke-WebRequest -Uri $entry.Url -OutFile $cachedPath -UseBasicParsing
        }
        $actualHash = (Get-FileHash -LiteralPath $cachedPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne $entry.Sha256) {
            throw "Canonical license hash mismatch for $($entry.File). Expected $($entry.Sha256) but got $actualHash."
        }
        Copy-Item -LiteralPath $cachedPath -Destination (Join-Path $licensesRoot $entry.File) -Force
    }

    if ($qtLicenseRoots.Count -eq 0) {
        Copy-Item -LiteralPath (Join-Path $licensesRoot "LGPL-3.0.txt") -Destination (Join-Path $qtLicenseTarget "LGPL-3.0.txt") -Force
        Write-Warning "Qt installation did not include a LICENSES directory; staged the canonical LGPL-3.0 text for the deployed Qt runtime."
    }
}

function Stage-SubtitleOcrRuntimeManifest {
    param(
        [Parameter(Mandatory = $true)]
        [string] $RepositoryRoot,
        [Parameter(Mandatory = $true)]
        [string] $DeployRoot,
        [Parameter(Mandatory = $true)]
        [string] $StageRoot,
        [Parameter(Mandatory = $true)]
        [string] $BuildDirectory,
        [Parameter(Mandatory = $true)]
        [string] $Triplet
    )

    $noticeSource = Join-Path $RepositoryRoot "resources\SUBTITLE-OCR-RUNTIME.md"
    $manifestSource = Join-Path $RepositoryRoot "resources\subtitle-ocr-runtime-manifest.json"
    if (-not (Test-Path -LiteralPath $noticeSource -PathType Leaf)) {
        throw "Subtitle OCR runtime manifest source was not found: $noticeSource"
    }
    if (-not (Test-Path -LiteralPath $manifestSource -PathType Leaf)) {
        throw "Subtitle OCR runtime JSON manifest source was not found: $manifestSource"
    }
    try {
        $manifest = Get-Content -LiteralPath $manifestSource -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        throw "Subtitle OCR runtime manifest is invalid JSON: $($_.Exception.Message)"
    }
    if ($manifest.schemaVersion -ne 2 -or $manifest.automaticDownload -ne $false -or
        $manifest.userInitiatedDownload -ne $false -or $manifest.runtime.delivery -ne "bundled-vcpkg" -or
        $manifest.runtime.version -ne "5.5.1" -or $manifest.runtime.healthCheck -ne "tesseract --version" -or
        $manifest.runtime.healthCheckPassed -ne $false -or
        $null -eq $manifest.runtime.PSObject.Properties["healthCheckOutput"] -or
        $manifest.languageData.packages.Count -lt 6) {
        throw "Subtitle OCR runtime manifest is missing bundled-runtime or language-pack metadata"
    }
    $runtimeRoot = Join-Path $DeployRoot "subtitle-ocr"
    New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
    $vcpkgPrefix = Join-Path $BuildDirectory "vcpkg_installed\$Triplet"
    $tesseractSource = Join-Path $vcpkgPrefix "tools\tesseract\tesseract.exe"
    if (-not (Test-Path -LiteralPath $tesseractSource -PathType Leaf)) {
        throw "Pinned vcpkg Tesseract executable was not found: $tesseractSource"
    }
    $tesseractTarget = Join-Path $runtimeRoot "tesseract.exe"
    Copy-Item -LiteralPath $tesseractSource -Destination $tesseractTarget -Force
    $vcpkgBinDirectory = Join-Path $vcpkgPrefix "bin"
    $runtimeLibraries = Get-ChildItem -LiteralPath $vcpkgBinDirectory -Filter "*.dll" -File
    if ($runtimeLibraries.Count -eq 0) {
        throw "No vcpkg runtime DLLs were found for bundled Tesseract: $vcpkgBinDirectory"
    }
    foreach ($library in $runtimeLibraries) {
        Copy-Item -LiteralPath $library.FullName -Destination (Join-Path $runtimeRoot $library.Name) -Force
    }
    $healthOutput = & $tesseractTarget --version 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $healthOutput -notmatch '(?i)tesseract') {
        throw "Bundled Tesseract did not pass its package health check. Exit=$LASTEXITCODE Output=$healthOutput"
    }
    $manifest.runtime.binarySha256 = (Get-FileHash -LiteralPath $tesseractTarget -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifest.runtime.healthCheckPassed = $true
    $manifest.runtime.healthCheckOutput = (($healthOutput -split "`r?`n")[0]).Trim()
    Copy-Item -LiteralPath $noticeSource -Destination (Join-Path $runtimeRoot "README.txt") -Force
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runtimeRoot "runtime-manifest.json") -Encoding UTF8
    $licenseRoot = Join-Path $StageRoot "licenses\tesseract"
    New-Item -ItemType Directory -Path $licenseRoot -Force | Out-Null
    Copy-Item -LiteralPath $noticeSource -Destination (Join-Path $licenseRoot "RUNTIME-NOTICE.md") -Force
    Write-Host ">> Staged health-checked bundled Subtitle OCR runtime (no external installer)" -ForegroundColor Green
}

function Stage-PaddleOcrRuntime {
    param(
        [Parameter(Mandatory = $true)]
        [string] $RepositoryRoot,
        [Parameter(Mandatory = $true)]
        [string] $DeployRoot,
        [Parameter(Mandatory = $true)]
        [string] $StageRoot,
        [Parameter(Mandatory = $true)]
        [string] $PaddleRuntimeRoot
    )

    if ([string]::IsNullOrWhiteSpace($PaddleRuntimeRoot) -or
        -not (Test-Path -LiteralPath $PaddleRuntimeRoot -PathType Container)) {
        throw "PaddleOCR packaging requires -PaddleRuntimeRoot produced by the controlled isolated-runtime preparation step."
    }
    $adapterSource = Join-Path $RepositoryRoot "resources\paddle_ocr_worker.py"
    $noticeSource = Join-Path $RepositoryRoot "resources\PADDLE-OCR-RUNTIME.md"
    $runtimeSource = Join-Path $PaddleRuntimeRoot "runtime"
    $workerSource = Join-Path $PaddleRuntimeRoot "paddle_ocr_worker.py"
    $modelsSource = Join-Path $PaddleRuntimeRoot "model-cache"
    $manifestSource = Join-Path $PaddleRuntimeRoot "runtime-manifest.json"
    foreach ($required in @($adapterSource, $noticeSource, $runtimeSource, $workerSource, $modelsSource, $manifestSource)) {
        if (-not (Test-Path -LiteralPath $required)) {
            throw "PaddleOCR runtime preparation is incomplete: $required"
        }
    }
    if ((Get-FileHash -LiteralPath $adapterSource -Algorithm SHA256).Hash.ToLowerInvariant() -ne
        (Get-FileHash -LiteralPath $workerSource -Algorithm SHA256).Hash.ToLowerInvariant()) {
        throw "Prepared PaddleOCR worker differs from the reviewed source adapter."
    }
    try {
        $manifest = Get-Content -LiteralPath $manifestSource -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        throw "Prepared PaddleOCR manifest is invalid JSON: $($_.Exception.Message)"
    }
    $safeRelativePath = {
        param([string] $Value)
        return -not [string]::IsNullOrWhiteSpace($Value) -and
            -not [IO.Path]::IsPathRooted($Value) -and
            $Value -notmatch '(^|[\\/])\.\.([\\/]|$)'
    }
    $validSha = { param([string] $Value) return $Value -match '^[a-f0-9]{64}$' }
    if ($manifest.schemaVersion -ne 1 -or
        $manifest.engine.id -ne "paddleocr-ppocrv6-tiny" -or
        $manifest.engine.version -ne "3.7.0" -or
        $manifest.engine.upstreamRepository -ne "https://github.com/PaddlePaddle/PaddleOCR" -or
        $manifest.engine.upstreamCommit -ne "2661c7c0ef5c613e8f93c6e93b2e052399f0f854" -or
        $manifest.engine.license -ne "Apache-2.0" -or
        $manifest.models.detection -ne "PP-OCRv6_tiny_det" -or
        $manifest.models.recognition -ne "PP-OCRv6_tiny_rec" -or
        -not (& $safeRelativePath $manifest.models.cacheLayout) -or
        -not (& $safeRelativePath $manifest.runtime.pythonRelativePath) -or
        -not (& $safeRelativePath $manifest.worker.relativePath) -or
        $manifest.worker.relativePath -ne "paddle_ocr_worker.py" -or
        $manifest.runtime.delivery -ne "bundled-isolated-python" -or
        $manifest.runtime.automaticDownload -ne $false -or
        -not (& $validSha $manifest.models.treeSha256) -or
        -not (& $validSha $manifest.runtime.pythonSha256) -or
        -not (& $validSha $manifest.worker.sha256)) {
        throw "Prepared PaddleOCR manifest is invalid, incompatible, or missing required SHA-256 values."
    }

    $targetRoot = Join-Path $DeployRoot "subtitle-ocr\paddle"
    New-Item -ItemType Directory -Path $targetRoot -Force | Out-Null
    $runtimeTarget = Join-Path $targetRoot "runtime"
    $modelsTarget = Join-Path $targetRoot "model-cache"
    Copy-Item -LiteralPath $runtimeSource -Destination $runtimeTarget -Recurse -Force
    Copy-Item -LiteralPath $modelsSource -Destination $modelsTarget -Recurse -Force
    Copy-Item -LiteralPath $workerSource -Destination (Join-Path $targetRoot "paddle_ocr_worker.py") -Force
    Copy-Item -LiteralPath $manifestSource -Destination (Join-Path $targetRoot "runtime-manifest.json") -Force
    Copy-Item -LiteralPath $noticeSource -Destination (Join-Path $targetRoot "README.txt") -Force

    $pythonTarget = Join-Path $targetRoot $manifest.runtime.pythonRelativePath
    $workerTarget = Join-Path $targetRoot $manifest.worker.relativePath
    $manifestTarget = Join-Path $targetRoot "runtime-manifest.json"
    $expectedModels = Join-Path $targetRoot $manifest.models.cacheLayout
    if (-not (Test-Path -LiteralPath $pythonTarget -PathType Leaf) -or
        -not (Test-Path -LiteralPath $workerTarget -PathType Leaf) -or
        -not (Test-Path -LiteralPath $expectedModels -PathType Container)) {
        throw "PaddleOCR package layout does not match its prepared manifest."
    }
    $healthOutput = & $pythonTarget $workerTarget --cache-root $modelsTarget --manifest $manifestTarget --health 2>&1 | Out-String
    $healthExitCode = $LASTEXITCODE
    $health = $null
    $healthLines = @($healthOutput -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    [array]::Reverse($healthLines)
    foreach ($line in $healthLines) {
        try {
            $candidate = $line | ConvertFrom-Json -ErrorAction Stop
            if ($null -ne $candidate.ok) { $health = $candidate; break }
        } catch { }
    }
    if ($healthExitCode -ne 0 -or $null -eq $health -or $health.ok -ne $true -or
        $health.engineId -ne "paddleocr-ppocrv6-tiny" -or $health.engineVersion -ne "3.7.0" -or
        $health.manifestVerified -ne $true) {
        throw "Bundled PaddleOCR did not pass manifest and runtime health verification. Exit=$healthExitCode Output=$healthOutput"
    }
    $manifest.runtime.healthCheckPassed = $true
    $manifest.runtime.healthCheckOutput = (($health | ConvertTo-Json -Compress).Trim())
    # Windows PowerShell 5.1 writes a BOM for -Encoding UTF8. The isolated
    # Python worker deliberately reads JSON as UTF-8, where that BOM makes
    # json.loads reject the staged manifest. Keep the package contract
    # byte-for-byte UTF-8 without BOM and verify it after the final write.
    [IO.File]::WriteAllText($manifestTarget, ($manifest | ConvertTo-Json -Depth 8),
                            [Text.UTF8Encoding]::new($false))
    $finalHealthOutput = & $pythonTarget $workerTarget --cache-root $modelsTarget --manifest $manifestTarget --health 2>&1 | Out-String
    $finalHealthExitCode = $LASTEXITCODE
    $finalHealth = $null
    $finalHealthLines = @($finalHealthOutput -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    [array]::Reverse($finalHealthLines)
    foreach ($line in $finalHealthLines) {
        try {
            $candidate = $line | ConvertFrom-Json -ErrorAction Stop
            if ($null -ne $candidate.ok) { $finalHealth = $candidate; break }
        } catch { }
    }
    if ($finalHealthExitCode -ne 0 -or $null -eq $finalHealth -or $finalHealth.ok -ne $true -or
        $finalHealth.manifestVerified -ne $true) {
        throw "Final staged PaddleOCR manifest health verification failed. Exit=$finalHealthExitCode Output=$finalHealthOutput"
    }

    $licenseRoot = Join-Path $StageRoot "licenses\paddle-ocr-python"
    New-Item -ItemType Directory -Path $licenseRoot -Force | Out-Null
    Copy-Item -LiteralPath $noticeSource -Destination (Join-Path $licenseRoot "RUNTIME-NOTICE.md") -Force
    $sitePackages = Join-Path $runtimeTarget "Lib\site-packages"
    if (-not (Test-Path -LiteralPath $sitePackages -PathType Container)) {
        throw "Prepared PaddleOCR isolated runtime has no Lib\\site-packages directory."
    }
    foreach ($distribution in (Get-ChildItem -LiteralPath $sitePackages -Directory -Filter "*.dist-info")) {
        $distributionTarget = Join-Path $licenseRoot $distribution.Name
        $legalFiles = Get-ChildItem -LiteralPath $distribution.FullName -Recurse -File |
            Where-Object { $_.Name -match '^(LICENSE|NOTICE|COPYING|METADATA)' }
        if ($legalFiles.Count -eq 0) {
            throw "Prepared PaddleOCR distribution has no license metadata: $($distribution.Name)"
        }
        New-Item -ItemType Directory -Path $distributionTarget -Force | Out-Null
        foreach ($legalFile in $legalFiles) {
            # PowerShell does not treat backslash as an escape in single-quoted
            # strings. Pass one character here; "\\" is a two-character string
            # and causes TrimStart() to throw after the package has already
            # staged the executable and OCR runtime.
            $relative = $legalFile.FullName.Substring($distribution.FullName.Length).TrimStart('\', '/')
            $destination = Join-Path $distributionTarget $relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            Copy-Item -LiteralPath $legalFile.FullName -Destination $destination -Force
        }
    }
    foreach ($requiredDistribution in @("paddleocr-3.7.0.dist-info", "paddlepaddle-3.3.0.dist-info", "paddlex-3.7.2.dist-info")) {
        if (-not (Test-Path -LiteralPath (Join-Path $licenseRoot $requiredDistribution))) {
            throw "Required PaddleOCR distribution license metadata was not staged: $requiredDistribution"
        }
    }
    Write-Host ">> Staged health-checked isolated PaddleOCR CPU runtime" -ForegroundColor Green
}

function Copy-VcpkgRuntimeLibraries {
    param(
        [string] $BuildDirectory,
        [string] $Triplet,
        [string] $DeployDirectory
    )

    # vcpkg manifest mode places dynamic runtime dependencies in the build tree.
    # windeployqt deploys Qt libraries only, so these DLLs must be staged explicitly.
    $vcpkgBinDirectory = Join-Path $BuildDirectory "vcpkg_installed\\$Triplet\\bin"
    if (-not (Test-Path -LiteralPath $vcpkgBinDirectory)) {
        throw "vcpkg runtime directory was not found: $vcpkgBinDirectory"
    }

    $runtimeLibraries = Get-ChildItem -LiteralPath $vcpkgBinDirectory -Filter "*.dll" -File
    if ($runtimeLibraries.Count -eq 0) {
        throw "No vcpkg runtime DLLs were found in: $vcpkgBinDirectory"
    }

    foreach ($library in $runtimeLibraries) {
        Copy-Item -LiteralPath $library.FullName -Destination (Join-Path $DeployDirectory $library.Name) -Force
    }

    foreach ($requiredLibrary in @("libcurl.dll", "zlib1.dll")) {
        $stagedLibrary = Join-Path $DeployDirectory $requiredLibrary
        if (-not (Test-Path -LiteralPath $stagedLibrary)) {
            throw "Required runtime DLL was not staged: $stagedLibrary"
        }
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

# 1. Resolve build dependencies
$QtRoot = Resolve-QtRoot -Candidate $QtRoot
$VcpkgRoot = Resolve-VcpkgRoot -Candidate $VcpkgRoot
$LlamaCppSourceDir = Resolve-LlamaCppSourceDir -Candidate $LlamaCppSourceDir
$PaddleRuntimeRoot = if ([string]::IsNullOrWhiteSpace($PaddleRuntimeRoot)) {
    # The controlled preparation step writes the verified, isolated runtime
    # here. Keep the override for release engineering, but make the normal
    # internal package path complete instead of binding an empty argument.
    Join-Path $RepoRoot "out\paddle-ocr-runtime-ready"
} else {
    [IO.Path]::GetFullPath($PaddleRuntimeRoot)
}
$Version = Normalize-AppVersion -Value $Version
$ReleaseSuffix = Normalize-ReleaseSuffix -Value $ReleaseSuffix
$applicationExecutableName = Get-VersionedApplicationExecutableName -AppVersion $Version
$portableLayout = $SkipInstaller -and ($PortableInternalLayout -or [string]::IsNullOrWhiteSpace($StageDir))
$kitName = if ($Preset -like "*mingw*") { "mingw_64" } else { "msvc2022_64" }

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    throw "Qt root not found. Pass -QtRoot or set LA_QT environment variable."
}
if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    throw "vcpkg root not found. Pass -VcpkgRoot or set VCPKG_ROOT environment variable."
}
if ([string]::IsNullOrWhiteSpace($LlamaCppSourceDir)) {
    throw "llama.cpp b10036 headers not found. Pass -LlamaCppSourceDir, set LLAMA_CPP_SOURCE_DIR, or check out llama.cpp at '.deps\llama.cpp'."
}

& (Join-Path $PSScriptRoot "verify_runtime_abi.ps1")

$qtPrefixPath = Join-Path $QtRoot $kitName
$windeployqt = Join-Path $qtPrefixPath "bin\windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    throw "windeployqt not found at: $windeployqt"
}

# 2. Setup folders
$stageDir = Resolve-StageDirectory -Candidate $StageDir -RepositoryRoot $RepoRoot -InstallerRequested:(-not $SkipInstaller) -AppVersion $Version -PortableLayout:$portableLayout
Assert-StagingDirectoryCanBeRebuilt -StageRoot $stageDir -ApplicationExecutableName $applicationExecutableName
if (Test-Path $stageDir) {
    Write-Host ">> Cleaning old staging directory..." -ForegroundColor Cyan
    Remove-Item $stageDir -Recurse -Force -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

# 3. Configure, Build and Install via CMake
Write-Host ">> Configuring CMake..." -ForegroundColor Cyan
$toolchainFile = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
$buildDir = Join-Path $RepoRoot "out\build\$Preset"
Remove-StaleCMakeBuildDirectory -BuildDirectory $buildDir -ExpectedSourceDirectory $RepoRoot
if ($Preset -notlike "*mingw*" -and (Test-Path -LiteralPath $buildDir)) {
    # CMake writes the selected archiver into CMakeCXXCompiler.cmake during
    # the first compiler probe. Supplying -DCMAKE_AR later does not rewrite
    # that generated file, so a cache originally created with MinGW's ar.exe
    # can poison an otherwise MSVC package build. Rebuild that generated
    # directory only after proving it is inside this repository's build tree.
    $compilerInfo = Get-ChildItem -LiteralPath (Join-Path $buildDir "CMakeFiles") -Recurse -Filter "CMakeCXXCompiler.cmake" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $compilerInfo) {
        $archiverMatch = Select-String -LiteralPath $compilerInfo.FullName -Pattern '^set\(CMAKE_AR "([^"]+)"\)' | Select-Object -First 1
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

$cmakeArgs = @(
    "--preset", $Preset,
    "-DCMAKE_INSTALL_PREFIX=$($stageDir.Replace('\', '/'))",
    "-DCMAKE_PREFIX_PATH=$($qtPrefixPath.Replace('\', '/'))",
    "-DCMAKE_TOOLCHAIN_FILE=$($toolchainFile.Replace('\', '/'))",
    "-DVCPKG_ROOT=$($VcpkgRoot.Replace('\', '/'))",
    "-DLLAMA_CPP_SOURCE_DIR=$($LlamaCppSourceDir.Replace('\', '/'))"
)
if ($Preset -like "*mingw*") {
    $vcpkgTriplet = "x64-mingw-dynamic"
} else {
    $vcpkgTriplet = "x64-windows"
    # Keep the packaging configure step on the MSVC linker even when another
    # toolchain's ld.exe/ar.exe appears on PATH. CMake caches both tools, so
    # selecting only link.exe can still make a later archive step invoke GNU
    # ar.exe with MSVC flags.
    $linkerCommand = Get-Command "link.exe" -ErrorAction Stop
    $archiverCommand = Get-Command "lib.exe" -ErrorAction Stop
    $cmakeArgs += "-DCMAKE_LINKER=$($linkerCommand.Source.Replace('\', '/'))"
    $cmakeArgs += "-DCMAKE_AR=$($archiverCommand.Source.Replace('\', '/'))"
}
$cmakeArgs += "-DVCPKG_TARGET_TRIPLET=$vcpkgTriplet"
$cmakeArgs += "-DLASTUDIO_VERSION=$Version"
$cmakeArgs += "-DLASTUDIO_RELEASE_SUFFIX=$ReleaseSuffix"
$cmakeArgs += "-DLASTUDIO_PORTABLE_INTERNAL_LAYOUT=$(if ($portableLayout) { 'ON' } else { 'OFF' })"
$cmakeArgs += "-DBUILD_TESTING=OFF"

$env:VCPKG_ROOT = $VcpkgRoot
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

Write-Host ">> Building application..." -ForegroundColor Cyan
& cmake --build --preset $Preset --parallel $MaxParallelJobs
if ($LASTEXITCODE -ne 0) { throw "CMake build failed." }

Write-Host ">> Installing to staging folder..." -ForegroundColor Cyan
& cmake --install $buildDir
if ($LASTEXITCODE -ne 0) { throw "CMake install failed." }

# 4. Deploy Qt libraries and DLLs
$deployRoot = if ($portableLayout) { $stageDir } else { Join-Path $stageDir 'bin' }
$stagedExe = Join-Path $deployRoot $applicationExecutableName
if (-not (Test-Path $stagedExe)) {
    throw "Staged executable not found at: $stagedExe"
}

Write-Host ">> Running windeployqt to deploy runtime dependencies..." -ForegroundColor Cyan
# Keep the offscreen platform in portable builds so the shipped executable can
# be exercised by the same headless QML route smoke used by CI. The Windows
# platform remains the normal interactive path; qoffscreen is test-only.
& $windeployqt --verbose 0 --qmldir qml --no-translations --compiler-runtime --include-plugins qoffscreen $stagedExe
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed." }
Ensure-WebpImageFormatPlugin -QtPrefixPath $qtPrefixPath -DeployRoot $deployRoot
Write-Host ">> Deploying vcpkg runtime DLLs..." -ForegroundColor Cyan
Copy-VcpkgRuntimeLibraries -BuildDirectory $buildDir -Triplet $vcpkgTriplet -DeployDirectory $deployRoot
$sevenZipSource = Ensure-ArchiveExtractor -DeployRoot $deployRoot -VcpkgRoot $VcpkgRoot
Ensure-Bsdtar -RepositoryRoot $RepoRoot -DeployRoot $deployRoot -StageRoot $stageDir -BuildDirectory $buildDir -Triplet $vcpkgTriplet
Ensure-FfmpegRuntime -RepositoryRoot $RepoRoot -DeployRoot $deployRoot -StageRoot $stageDir
Ensure-YtDlpRuntime -RepositoryRoot $RepoRoot -DeployRoot $deployRoot -StageRoot $stageDir
Stage-SubtitleOcrRuntimeManifest -RepositoryRoot $RepoRoot -DeployRoot $deployRoot -StageRoot $stageDir -BuildDirectory $buildDir -Triplet $vcpkgTriplet
Stage-PaddleOcrRuntime -RepositoryRoot $RepoRoot -DeployRoot $deployRoot -StageRoot $stageDir -PaddleRuntimeRoot $PaddleRuntimeRoot
Stage-ThirdPartyLicenseTexts -RepositoryRoot $RepoRoot -StageRoot $stageDir -BuildDirectory $buildDir -Triplet $vcpkgTriplet -QtRoot $QtRoot -SevenZipSource $sevenZipSource
if ($AllowUnsignedEspeakForInternalBuild) {
    Write-Warning "INTERNAL BUILD ONLY: permitting the SHA-256-verified but unsigned eSpeak NG MSI. Do not distribute this package or promote it to a release."
}

Ensure-EspeakNgRuntime -RepositoryRoot $RepoRoot -DeployRoot $deployRoot -AllowUnsignedEspeakForInternalBuild:$AllowUnsignedEspeakForInternalBuild
Assert-StagedEspeakNgRuntime -DeployRoot $deployRoot
Assert-StagedMsvcRuntime -DeployRoot $deployRoot
Assert-StagedRuntimeManifest -DeployRoot $deployRoot -ApplicationExecutableName $applicationExecutableName
Assert-StagedLicenseManifest -StageRoot $stageDir

# 5. Build installer using Inno Setup
if ($SkipInstaller) {
    $kind = if ($portableLayout) { 'Portable application' } else { 'Application' }
    Write-Host "[SUCCESS] $kind staged successfully at: $stagedExe" -ForegroundColor Green
    exit 0
}

$isccPath = Find-Iscc
if ($null -eq $isccPath) {
    Write-Warning "Inno Setup Compiler (ISCC.exe) was not found."
    Write-Warning "The application has been successfully built and staged at: $stageDir"
    Write-Warning "To package it into an installer, please install Inno Setup 6 (https://jrsoftware.org/isdownload.php) and run: ISCC.exe $buildDir\installer.iss"
    exit 0
}

Write-Host ">> Compiling installer using Inno Setup..." -ForegroundColor Cyan
$installerScript = Join-Path $buildDir "installer.iss"
if (-not (Test-Path $installerScript)) {
    throw "Generated installer script not found at: $installerScript"
}
& $isccPath $installerScript
if ($LASTEXITCODE -ne 0) { throw "Installer compilation failed." }

$installerPath = Join-Path $RepoRoot "out\LA-Studio-Setup.exe"
Write-Host "[SUCCESS] Installer generated at: $installerPath" -ForegroundColor Green
