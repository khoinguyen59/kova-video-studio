function Get-LaStudioFileSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path
    )

    # Do not rely on Get-FileHash being auto-loaded.  CTest can launch
    # Windows PowerShell with a constrained module path, where that cmdlet is
    # unavailable even though the .NET SHA-256 API remains available.
    $stream = [System.IO.File]::OpenRead($Path)
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($sha256.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha256.Dispose()
        $stream.Dispose()
    }
}

function Ensure-EspeakNgRuntime {
    param(
        [Parameter(Mandatory = $true)]
        [string] $RepositoryRoot,
        [Parameter(Mandatory = $true)]
        [string] $DeployRoot,
        [switch] $AllowUnsignedEspeakForInternalBuild
    )

    $version = "1.52.0"
    $runtimeRoot = Join-Path $DeployRoot "espeak-ng"
    $dllTarget = Join-Path $runtimeRoot "libespeak-ng.dll"
    $exeTarget = Join-Path $runtimeRoot "espeak-ng.exe"
    $dataTarget = Join-Path $runtimeRoot "espeak-ng-data"
    $cacheRoot = Join-Path $RepositoryRoot ".deps\espeak-ng"
    $msiPath = Join-Path $cacheRoot "espeak-ng-$version.msi"
    $url = "https://github.com/espeak-ng/espeak-ng/releases/download/$version/espeak-ng.msi"
    $expectedSha256 = "7f673c709ea5dd579d3b5ebb98688cc575328a6ab7438d2bc405b88cedaeafb9"
    New-Item -ItemType Directory -Path $cacheRoot -Force | Out-Null
    if (-not (Test-Path -LiteralPath $msiPath)) {
        Write-Host ">> Downloading eSpeak NG $version" -ForegroundColor Cyan
        Invoke-WebRequest -Uri $url -OutFile $msiPath -UseBasicParsing
    }

    $actualSha256 = Get-LaStudioFileSha256 -Path $msiPath
    if ($actualSha256 -ne $expectedSha256) {
        throw "eSpeak NG MSI SHA-256 mismatch. Expected $expectedSha256 but got $actualSha256."
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $msiPath
    if ($signature.Status -ne "Valid") {
        if (-not $AllowUnsignedEspeakForInternalBuild) {
            throw "eSpeak NG MSI Authenticode signature is not valid: $($signature.Status)"
        }

        Write-Warning "INTERNAL BUILD ONLY: eSpeak NG MSI signature status is $($signature.Status). The SHA-256 was verified, but this payload must not be used for a distributable release."
    }

    if ((Test-Path -LiteralPath $dllTarget) -and
        (Test-Path -LiteralPath $exeTarget) -and
        (Test-Path -LiteralPath (Join-Path $dataTarget "voices"))) {
        Write-Host ">> eSpeak NG runtime already staged: $runtimeRoot" -ForegroundColor DarkGray
        return
    }

    $msiexec = Join-Path $env:SystemRoot "System32\msiexec.exe"
    if (-not (Test-Path -LiteralPath $msiexec)) {
        throw "Windows Installer executable was not found: $msiexec"
    }

    $extractRoot = Join-Path $DeployRoot ".espeak-ng-extract"
    if (Test-Path -LiteralPath $extractRoot) {
        Remove-Item -LiteralPath $extractRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $extractRoot -Force | Out-Null
    Write-Host ">> Extracting eSpeak NG runtime" -ForegroundColor Cyan
    if ($AllowUnsignedEspeakForInternalBuild) {
        $sevenZip = Join-Path $DeployRoot "7z.exe"
        if (-not (Test-Path -LiteralPath $sevenZip -PathType Leaf)) {
            throw "Internal eSpeak NG staging requires the packaged 7z.exe: $sevenZip"
        }

        & $sevenZip x $msiPath "-o$extractRoot" -y | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to extract eSpeak NG MSI with 7-Zip (exit code $LASTEXITCODE): $msiPath"
        }

        $installer = New-Object -ComObject WindowsInstaller.Installer
        $database = $installer.OpenDatabase($msiPath, 0)
        $directories = @{}
        $directoryView = $database.OpenView('SELECT `Directory`, `Directory_Parent`, `DefaultDir` FROM `Directory`')
        $directoryView.Execute()
        while ($true) {
            $record = $directoryView.Fetch()
            if ($null -eq $record) { break }
            $directories[$record.StringData(1)] = @{
                Parent = $record.StringData(2)
                Name = ($record.StringData(3) -split '\|')[-1]
            }
        }
        $directoryView.Close()

        function Get-EspeakMsiRelativeDirectory {
            param([string] $DirectoryId)

            $segments = New-Object System.Collections.Generic.List[string]
            while ($DirectoryId -ne "INSTALLDIR") {
                if (-not $directories.ContainsKey($DirectoryId)) {
                    throw "eSpeak NG MSI directory metadata is incomplete at '$DirectoryId'."
                }
                $entry = $directories[$DirectoryId]
                if ($entry.Name -ne "." -and -not [string]::IsNullOrWhiteSpace($entry.Name)) {
                    $segments.Insert(0, $entry.Name)
                }
                $DirectoryId = $entry.Parent
            }
            return ($segments -join [IO.Path]::DirectorySeparatorChar)
        }

        $components = @{}
        $componentView = $database.OpenView('SELECT `Component`, `Directory_` FROM `Component`')
        $componentView.Execute()
        while ($true) {
            $record = $componentView.Fetch()
            if ($null -eq $record) { break }
            $components[$record.StringData(1)] = $record.StringData(2)
        }
        $componentView.Close()

        New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
        $fileView = $database.OpenView('SELECT `File`, `Component_`, `FileName` FROM `File`')
        $fileView.Execute()
        while ($true) {
            $record = $fileView.Fetch()
            if ($null -eq $record) { break }
            $source = Join-Path $extractRoot $record.StringData(1)
            if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
                throw "7-Zip did not extract expected eSpeak NG MSI file: $($record.StringData(1))"
            }
            $component = $record.StringData(2)
            if (-not $components.ContainsKey($component)) {
                throw "eSpeak NG MSI component metadata is incomplete at '$component'."
            }
            $relativeDirectory = Get-EspeakMsiRelativeDirectory -DirectoryId $components[$component]
            $destinationDirectory = if ([string]::IsNullOrWhiteSpace($relativeDirectory)) {
                $runtimeRoot
            } else {
                Join-Path $runtimeRoot $relativeDirectory
            }
            New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
            $destinationName = (($record.StringData(3) -split '\|')[-1]).Trim()
            Copy-Item -LiteralPath $source -Destination (Join-Path $destinationDirectory $destinationName) -Force
        }
        $fileView.Close()
    } else {
        $installer = Start-Process -FilePath $msiexec -ArgumentList @(
            "/a", "`"$msiPath`"", "/qn", "TARGETDIR=`"$extractRoot`""
        ) -Wait -PassThru
        if ($installer.ExitCode -ne 0) {
            throw "Failed to extract eSpeak NG MSI (exit code $($installer.ExitCode)): $msiPath"
        }

        $dllSource = Get-ChildItem -Path $extractRoot -Filter "libespeak-ng.dll" -Recurse -File | Select-Object -First 1
        $exeSource = Get-ChildItem -Path $extractRoot -Filter "espeak-ng.exe" -Recurse -File | Select-Object -First 1
        $dataSource = Get-ChildItem -Path $extractRoot -Directory -Recurse |
            Where-Object { $_.Name -eq "espeak-ng-data" } | Select-Object -First 1
        if ($null -eq $dllSource -or $null -eq $exeSource -or $null -eq $dataSource) {
            Remove-Item -LiteralPath $extractRoot -Recurse -Force -ErrorAction SilentlyContinue
            throw "eSpeak NG MSI did not contain libespeak-ng.dll, espeak-ng.exe, and espeak-ng-data."
        }

        New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
        Copy-Item -LiteralPath $dllSource.FullName -Destination $dllTarget -Force
        Copy-Item -LiteralPath $exeSource.FullName -Destination $exeTarget -Force
        if (Test-Path -LiteralPath $dataTarget) {
            Remove-Item -LiteralPath $dataTarget -Recurse -Force
        }
        Copy-Item -LiteralPath $dataSource.FullName -Destination $dataTarget -Recurse -Force
    }
    Remove-Item -LiteralPath $extractRoot -Recurse -Force

    if (-not (Test-Path -LiteralPath $dllTarget) -or
        -not (Test-Path -LiteralPath (Join-Path $dataTarget "voices"))) {
        throw "eSpeak NG runtime staging was incomplete: $runtimeRoot"
    }
    Write-Host ">> Staged eSpeak NG runtime: $runtimeRoot" -ForegroundColor Green
}

function Assert-StagedEspeakNgRuntime {
    param(
        [Parameter(Mandatory = $true)]
        [string] $DeployRoot
    )

    $runtimeRoot = Join-Path $DeployRoot "espeak-ng"
    $executable = Join-Path $runtimeRoot "espeak-ng.exe"
    $dataRoot = Join-Path $runtimeRoot "espeak-ng-data"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $dataRoot "voices") -PathType Container)) {
        throw "eSpeak NG runtime test requires its executable and data directory: $runtimeRoot"
    }

    # eSpeak NG 1.52.0 does not derive its data location from the executable
    # directory when invoked as a standalone command. LA Studio passes this
    # same location explicitly to the library, and the package gate proves
    # that the staged binary can read it before release.
    $previousDataPath = [Environment]::GetEnvironmentVariable("ESPEAK_DATA_PATH", "Process")
    try {
        [Environment]::SetEnvironmentVariable("ESPEAK_DATA_PATH", $dataRoot, "Process")
        & $executable --version | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Staged eSpeak NG executable could not read its data directory (exit code $LASTEXITCODE)."
        }
    } finally {
        [Environment]::SetEnvironmentVariable("ESPEAK_DATA_PATH", $previousDataPath, "Process")
    }
    Write-Host ">> Staged eSpeak NG runtime verified" -ForegroundColor Green
}

function Ensure-FfmpegRuntime {
    param(
        [Parameter(Mandatory = $true)]
        [string] $RepositoryRoot,
        [Parameter(Mandatory = $true)]
        [string] $DeployRoot,
        [Parameter(Mandatory = $true)]
        [string] $StageRoot
    )

    # A fixed release tag and archive hash make the portable package
    # reproducible. Do not replace these with BtbN's moving `latest` URL.
    $releaseTag = "autobuild-2026-07-28-13-32"
    $archiveName = "ffmpeg-N-125829-gfe953596e9-win64-lgpl-shared.zip"
    $expectedSha256 = "51af6309b252e9eddb4a68b0c4b2122f4b1150a558ab390bbbb9e49cf3bc2d08"
    $archiveUrl = "https://github.com/BtbN/FFmpeg-Builds/releases/download/$releaseTag/$archiveName"
    $cacheRoot = Join-Path $RepositoryRoot ".deps\ffmpeg"
    $archivePath = Join-Path $cacheRoot $archiveName
    $runtimeRoot = Join-Path $DeployRoot "media-tools"
    $ffmpegTarget = Join-Path $runtimeRoot "ffmpeg.exe"
    $ffprobeTarget = Join-Path $runtimeRoot "ffprobe.exe"

    New-Item -ItemType Directory -Path $cacheRoot -Force | Out-Null
    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        Write-Host ">> Downloading pinned FFmpeg runtime" -ForegroundColor Cyan
        Invoke-WebRequest -Headers @{ "User-Agent" = "LA-Studio-packaging" } -Uri $archiveUrl -OutFile $archivePath -UseBasicParsing
    }
    $actualSha256 = Get-LaStudioFileSha256 -Path $archivePath
    if ($actualSha256 -ne $expectedSha256) {
        throw "FFmpeg runtime SHA-256 mismatch. Expected $expectedSha256 but got $actualSha256."
    }

    $normalizedDeployRoot = [IO.Path]::GetFullPath($DeployRoot).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    $normalizedRuntimeRoot = [IO.Path]::GetFullPath($runtimeRoot)
    if (-not $normalizedRuntimeRoot.StartsWith($normalizedDeployRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to extract FFmpeg outside the deployment root: $runtimeRoot"
    }

    $payloadRoot = $null
    try {
        Write-Host ">> Extracting pinned FFmpeg runtime" -ForegroundColor Cyan
        # Expand-Archive can fail after successfully expanding this archive:
        # its cleanup pass treats a concurrently removed duplicate LICENSE.txt
        # entry as a terminating error. Use the pinned 7-Zip binary that the
        # package already stages; unlike our minimal bsdtar build, it supports
        # selecting only the needed archive entries. Extract directly into the
        # runtime directory, then move its payload into place on the same
        # volume. This avoids a second full copy of the large FFmpeg binaries.
        # Extract only the runtime binaries and license, not the archive's
        # development headers and import libraries;
        # those are not shipped and can consume several additional GiB of
        # temporary disk space. Validate the exact executables below before
        # accepting the extraction.
        $sevenZipPath = Join-Path $DeployRoot "7z.exe"
        if (-not (Test-Path -LiteralPath $sevenZipPath -PathType Leaf)) {
            throw "Pinned 7-Zip extractor was not staged: $sevenZipPath"
        }
        New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
        & $sevenZipPath x $archivePath "-o$runtimeRoot" -y "*/bin/*" "*/LICENSE.txt"
        if ($LASTEXITCODE -ne 0) {
            throw "Pinned 7-Zip could not extract the verified FFmpeg runtime entries (exit code $LASTEXITCODE)."
        }
        $ffmpegSource = Get-ChildItem -LiteralPath $runtimeRoot -Filter "ffmpeg.exe" -Recurse -File | Select-Object -First 1
        if ($null -eq $ffmpegSource) {
            throw "Pinned FFmpeg archive did not contain ffmpeg.exe."
        }
        $sourceBin = Split-Path -Parent $ffmpegSource.FullName
        $ffprobeSource = Join-Path $sourceBin "ffprobe.exe"
        if (-not (Test-Path -LiteralPath $ffprobeSource -PathType Leaf)) {
            throw "Pinned FFmpeg archive did not contain ffprobe.exe beside ffmpeg.exe."
        }

        $payloadRoot = Split-Path -Parent $sourceBin
        $normalizedPayloadRoot = [IO.Path]::GetFullPath($payloadRoot)
        if (-not $normalizedPayloadRoot.StartsWith($normalizedDeployRoot, [StringComparison]::OrdinalIgnoreCase) -or
            [string]::Equals($normalizedPayloadRoot, $normalizedRuntimeRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Pinned FFmpeg archive payload resolved outside the isolated runtime payload: $payloadRoot"
        }
        Get-ChildItem -LiteralPath $sourceBin -File | ForEach-Object {
            Move-Item -LiteralPath $_.FullName -Destination (Join-Path $runtimeRoot $_.Name) -Force
        }

        $licenseSource = Get-ChildItem -LiteralPath $payloadRoot -Filter "LICENSE.txt" -Recurse -File | Select-Object -First 1
        if ($null -eq $licenseSource) {
            throw "Pinned FFmpeg archive did not contain LICENSE.txt."
        }
        $licenseRoot = Join-Path $StageRoot "licenses\ffmpeg"
        New-Item -ItemType Directory -Path $licenseRoot -Force | Out-Null
        Copy-Item -LiteralPath $licenseSource.FullName -Destination (Join-Path $licenseRoot "LICENSE.txt") -Force
        $noticeSource = Join-Path $RepositoryRoot "resources\FFMPEG-RUNTIME-NOTICE.txt"
        if (-not (Test-Path -LiteralPath $noticeSource -PathType Leaf)) {
            throw "FFmpeg runtime notice was not found: $noticeSource"
        }
        Copy-Item -LiteralPath $noticeSource -Destination (Join-Path $licenseRoot "NOTICE.txt") -Force
    } finally {
        if ($null -ne $payloadRoot -and (Test-Path -LiteralPath $payloadRoot)) {
            Remove-Item -LiteralPath $payloadRoot -Recurse -Force
        }
    }

    if (-not (Test-Path -LiteralPath $ffmpegTarget -PathType Leaf) -or
        -not (Test-Path -LiteralPath $ffprobeTarget -PathType Leaf)) {
        throw "FFmpeg runtime staging was incomplete: $runtimeRoot"
    }
    Write-Host ">> Staged pinned FFmpeg runtime: $runtimeRoot" -ForegroundColor Green
}

function Ensure-YtDlpRuntime {
    param(
        [Parameter(Mandatory = $true)]
        [string] $RepositoryRoot,
        [Parameter(Mandatory = $true)]
        [string] $DeployRoot,
        [Parameter(Mandatory = $true)]
        [string] $StageRoot
    )

    # Public page resolution is a local CPU task.  Keep yt-dlp pinned by both
    # release tag and SHA-256; do not replace this with a moving `latest`
    # download or silently route the download through a Colab worker.
    $version = "2026.07.04"
    $expectedSha256 = "52fe3c26dcf71fbdc85b528589020bb0b8e383155cfa81b64dd447bbe35e24b8"
    $cacheRoot = Join-Path $RepositoryRoot ".deps"
    $cachePath = Join-Path $cacheRoot "yt-dlp-$version.exe"
    $target = Join-Path $DeployRoot "yt-dlp.exe"
    $downloadUrl = "https://github.com/yt-dlp/yt-dlp/releases/download/$version/yt-dlp.exe"

    New-Item -ItemType Directory -Path $cacheRoot -Force | Out-Null
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
        Write-Host ">> Downloading pinned yt-dlp runtime" -ForegroundColor Cyan
        Invoke-WebRequest -Headers @{ "User-Agent" = "LA-Studio-packaging" } -Uri $downloadUrl -OutFile $cachePath -UseBasicParsing
    }
    $actualSha256 = Get-LaStudioFileSha256 -Path $cachePath
    if ($actualSha256 -ne $expectedSha256) {
        throw "yt-dlp runtime SHA-256 mismatch. Expected $expectedSha256 but got $actualSha256."
    }

    Copy-Item -LiteralPath $cachePath -Destination $target -Force
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        throw "yt-dlp runtime staging was incomplete: $target"
    }
    & $target --version | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Staged yt-dlp runtime could not start (exit code $LASTEXITCODE)."
    }

    $licenseRoot = Join-Path $StageRoot "licenses\yt-dlp"
    New-Item -ItemType Directory -Path $licenseRoot -Force | Out-Null
    @(
        "yt-dlp $version",
        "Source: https://github.com/yt-dlp/yt-dlp",
        "License: Unlicense (https://unlicense.org/)"
    ) | Set-Content -LiteralPath (Join-Path $licenseRoot "UNLICENSE.txt") -Encoding UTF8
    Write-Host ">> Staged pinned yt-dlp runtime: $target" -ForegroundColor Green
}

function Assert-StagedRuntimeManifest {
    param(
        [Parameter(Mandatory = $true)]
        [string] $DeployRoot,
        [Parameter(Mandatory = $true)]
        [string] $ApplicationExecutableName
    )

    $required = @(
        $ApplicationExecutableName,
        "LAStudioRuntimeHost.exe",
        "Qt6Core.dll",
        "Qt6Quick.dll",
        "Qt6Multimedia.dll",
        "platforms\qwindows.dll",
        "platforms\qoffscreen.dll",
        "imageformats\qwebp.dll",
        "libcurl.dll",
        "zlib1.dll",
        "7z.exe",
        "bsdtar.exe",
        "subtitle-ocr\README.txt",
        "subtitle-ocr\runtime-manifest.json",
        "media-tools\ffmpeg.exe",
        "media-tools\ffprobe.exe",
        "yt-dlp.exe",
        "espeak-ng\libespeak-ng.dll",
        "espeak-ng\espeak-ng-data\voices"
    )
    $missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $DeployRoot $_)) })
    if ($missing.Count -gt 0) {
        throw "Staging manifest is incomplete. Missing: $($missing -join ', ')"
    }
    Write-Host ">> Staging manifest verified ($($required.Count) required artifacts)" -ForegroundColor Green
}

function Assert-StagedMsvcRuntime {
    param(
        [Parameter(Mandatory = $true)]
        [string] $DeployRoot
    )

    $redist = Join-Path $DeployRoot "vc_redist.x64.exe"
    if (Test-Path -LiteralPath $redist -PathType Leaf) {
        Write-Host ">> MSVC redistributable staged for installer execution: $redist" -ForegroundColor Green
        return
    }

    $runtimeDlls = @(
        Get-ChildItem -LiteralPath $DeployRoot -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^(vcruntime|msvcp|concrt|vcomp)\d.*\.dll$' }
    )
    if ($runtimeDlls.Count -eq 0) {
        throw "windeployqt --compiler-runtime staged neither vc_redist.x64.exe nor MSVC runtime DLLs in '$DeployRoot'."
    }
    Write-Host ">> MSVC runtime DLLs staged privately: $($runtimeDlls.Name -join ', ')" -ForegroundColor Green
}

function Assert-StagedLicenseManifest {
    param(
        [Parameter(Mandatory = $true)]
        [string] $StageRoot
    )

    $required = @(
        "LICENSE",
        "THIRD-PARTY-NOTICES.md",
        "licenses\AGPL-3.0.txt",
        "licenses\GPL-3.0.txt",
        "licenses\LGPL-3.0.txt",
        "licenses\THIRD-PARTY-NOTICES.md",
        "licenses\curl\LICENSE",
        "licenses\zlib\LICENSE",
        "licenses\bzip2\LICENSE",
        "licenses\tesseract\LICENSE",
        "licenses\7-Zip\License.txt",
        "licenses\ffmpeg\LICENSE.txt",
        "licenses\ffmpeg\NOTICE.txt",
        "licenses\qt",
        "licenses\vietnorm\LICENSE",
        "licenses\vietnorm\NOTICE",
        "licenses\libarchive\LICENSE",
        "licenses\tesseract\RUNTIME-NOTICE.md"
    )
    $missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $StageRoot $_)) })
    if ($missing.Count -gt 0) {
        throw "License staging manifest is incomplete. Missing: $($missing -join ', ')"
    }
    Write-Host ">> License staging manifest verified ($($required.Count) required artifacts)" -ForegroundColor Green
}
