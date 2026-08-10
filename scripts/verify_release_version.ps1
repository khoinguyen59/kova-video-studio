#Requires -Version 5.1

<#
.SYNOPSIS
    Validates that a release tag and the source version describe the same build.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Tag
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$cmakePath = Join-Path $repoRoot "CMakeLists.txt"

if ($Tag -notmatch '^v([0-9]\.[0-9]\.[0-9]\.[0-9])(-(alpha|beta|rc)\.[1-9][0-9]*)?$') {
    throw "Release tag must use four single digits with carry at 9 (for example v0.0.1.0), optionally followed by -(alpha|beta|rc).N; got '$Tag'."
}

$tagVersion = $Matches[1]
$tagSuffix = $Matches[2]
$source = Get-Content -LiteralPath $cmakePath -Raw
$versionMatch = [regex]::Match($source, 'set\(LASTUDIO_VERSION\s+"([0-9]\.[0-9]\.[0-9]\.[0-9])"')
if (-not $versionMatch.Success) {
    throw "Could not find a valid LASTUDIO_VERSION in '$cmakePath'."
}

$sourceVersion = $versionMatch.Groups[1].Value
if ($sourceVersion -ne $tagVersion) {
    throw "Release tag '$Tag' does not match LASTUDIO_VERSION '$sourceVersion'. Update the source version before creating the tag."
}

Write-Host "Release version verified: $Tag matches LASTUDIO_VERSION $sourceVersion (suffix '$tagSuffix')" -ForegroundColor Green
