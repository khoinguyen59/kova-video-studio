#Requires -Version 5.1

<#
.SYNOPSIS
    Writes the build inputs needed to diagnose a Windows release.
.DESCRIPTION
    The release workflow stores this JSON beside the installer. It intentionally
    records only toolchain metadata, never build paths, credentials, or source
    contents.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $OutputPath,
    [string] $QtRoot,
    [string] $VcpkgRoot,
    [string] $ReleaseTag = ""
)

$ErrorActionPreference = "Stop"

function Get-CommandOutput {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [string[]] $Arguments = @()
    )

    try {
        $output = & $FilePath @Arguments 2>&1 | ForEach-Object { $_.ToString() }
        return @($output)
    } catch {
        return @("Unavailable: $($_.Exception.Message)")
    }
}

function Get-GitCommit {
    param([string] $RepositoryPath)

    if ([string]::IsNullOrWhiteSpace($RepositoryPath) -or -not (Test-Path -LiteralPath $RepositoryPath)) {
        return "Unavailable"
    }
    $commit = Get-CommandOutput -FilePath "git" -Arguments @("-C", $RepositoryPath, "rev-parse", "HEAD") |
        Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($commit) -or $commit -like "Unavailable:*") {
        return "Unavailable"
    }
    return $commit.Trim()
}

function Get-ChocolateyPackageVersions {
    if ($null -eq (Get-Command "choco.exe" -ErrorAction SilentlyContinue)) {
        return @("Unavailable")
    }
    $packages = Get-CommandOutput -FilePath "choco.exe" -Arguments @("list", "--local-only", "--limit-output")
    return @($packages | Where-Object { $_ -match '^(innosetup|7zip)\|' })
}

$clPath = (Get-Command "cl.exe" -ErrorAction SilentlyContinue).Source
$clVersion = if ($clPath) { Get-CommandOutput -FilePath $clPath | Select-Object -First 2 } else { @("Unavailable") }

$qmakePath = $null
if (-not [string]::IsNullOrWhiteSpace($QtRoot) -and (Test-Path -LiteralPath $QtRoot)) {
    $qmakePath = Get-ChildItem -Path $QtRoot -Filter "qmake.exe" -Recurse -File -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
$qtVersion = if ($qmakePath) {
    Get-CommandOutput -FilePath $qmakePath -Arguments @("-query", "QT_VERSION") | Select-Object -First 1
} else {
    "Unavailable"
}

$sdkVersion = $env:WindowsSDKVersion
if ([string]::IsNullOrWhiteSpace($sdkVersion)) {
    $sdkVersion = "Unavailable"
} else {
    $sdkVersion = $sdkVersion.TrimEnd("\\")
}

$outputDirectory = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$manifest = [ordered]@{
    schemaVersion = 1
    generatedAtUtc = [DateTime]::UtcNow.ToString("o")
    releaseTag = $ReleaseTag
    github = [ordered]@{
        repository = $env:GITHUB_REPOSITORY
        workflow = $env:GITHUB_WORKFLOW
        runId = $env:GITHUB_RUN_ID
        commit = $env:GITHUB_SHA
    }
    msvc = [ordered]@{
        compilerPath = if ($clPath) { $clPath } else { "Unavailable" }
        version = @($clVersion)
    }
    windowsSdk = $sdkVersion
    qt = [ordered]@{
        root = if ($QtRoot) { $QtRoot } else { "Unavailable" }
        qmakePath = if ($qmakePath) { $qmakePath } else { "Unavailable" }
        version = $qtVersion
    }
    vcpkg = [ordered]@{
        root = if ($VcpkgRoot) { $VcpkgRoot } else { "Unavailable" }
        commit = Get-GitCommit -RepositoryPath $VcpkgRoot
    }
    cmake = @((Get-CommandOutput -FilePath "cmake.exe" -Arguments @("--version") | Select-Object -First 1))
    ninja = @((Get-CommandOutput -FilePath "ninja.exe" -Arguments @("--version") | Select-Object -First 1))
    chocolateyPackages = @(Get-ChocolateyPackageVersions)
}

$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Host "Wrote toolchain manifest: $OutputPath" -ForegroundColor Cyan
