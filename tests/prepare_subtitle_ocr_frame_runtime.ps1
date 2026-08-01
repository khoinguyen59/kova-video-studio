[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $RepositoryRoot,
    [Parameter(Mandatory = $true)]
    [string] $DeployRoot
)

$ErrorActionPreference = 'Stop'

$RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
$DeployRoot = [IO.Path]::GetFullPath($DeployRoot)
$runtimeHelpers = Join-Path $RepositoryRoot 'scripts\runtime_helpers.ps1'
if (-not (Test-Path -LiteralPath $runtimeHelpers -PathType Leaf)) {
    throw "LA Studio runtime helper is missing: $runtimeHelpers"
}

# The integration test intentionally stages the exact pinned FFmpeg payload
# used by package.ps1. It never resolves a machine-global ffmpeg.exe.
$sevenZip = Get-Command 7z.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -eq $sevenZip) {
    $sevenZip = Get-Command 7z -ErrorAction SilentlyContinue | Select-Object -First 1
}
if ($null -eq $sevenZip) {
    $installedSevenZip = Join-Path $env:ProgramFiles '7-Zip\7z.exe'
    if (Test-Path -LiteralPath $installedSevenZip -PathType Leaf) {
        $sevenZipPath = $installedSevenZip
    }
    # Local developers who already built the preceding internal candidate can
    # reuse its staged extractor. CI installs 7-Zip explicitly below.
    $priorCandidateExtractor = Join-Path $RepositoryRoot 'out\LA-Studio-0.0.2.14\7z.exe'
    if ([string]::IsNullOrWhiteSpace($sevenZipPath) -and
        (Test-Path -LiteralPath $priorCandidateExtractor -PathType Leaf)) {
        $sevenZipPath = $priorCandidateExtractor
    }
    if ([string]::IsNullOrWhiteSpace($sevenZipPath)) {
        throw '7z.exe is required to stage the pinned FFmpeg integration-test runtime.'
    }
} else {
    $sevenZipPath = $sevenZip.Source
}

New-Item -ItemType Directory -Path $DeployRoot -Force | Out-Null
# Ensure-FfmpegRuntime intentionally rejects an existing media-tools root: on
# a second CTest invocation it would otherwise discover the old ffmpeg.exe as
# the newly extracted payload. This is a generated test-only directory, but
# still verify the resolved target is inside this build tree before cleanup.
$testBuildRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'out\build')).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$normalizedDeployRoot = $DeployRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $normalizedDeployRoot.StartsWith($testBuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to reset a Subtitle OCR test runtime outside out\\build: $DeployRoot"
}
foreach ($generatedPath in @(
    (Join-Path $DeployRoot 'media-tools'),
    (Join-Path $DeployRoot 'stage')
)) {
    if (Test-Path -LiteralPath $generatedPath) {
        Remove-Item -LiteralPath $generatedPath -Recurse -Force
    }
}
Copy-Item -LiteralPath $sevenZipPath -Destination (Join-Path $DeployRoot '7z.exe') -Force
. $runtimeHelpers
Ensure-FfmpegRuntime -RepositoryRoot $RepositoryRoot -DeployRoot $DeployRoot -StageRoot (Join-Path $DeployRoot 'stage')

$ffmpeg = Join-Path $DeployRoot 'media-tools\ffmpeg.exe'
$ffprobe = Join-Path $DeployRoot 'media-tools\ffprobe.exe'
if (-not (Test-Path -LiteralPath $ffmpeg -PathType Leaf) -or
    -not (Test-Path -LiteralPath $ffprobe -PathType Leaf)) {
    throw "Pinned FFmpeg test runtime staging was incomplete: $DeployRoot"
}
