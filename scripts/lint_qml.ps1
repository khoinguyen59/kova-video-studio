#Requires -Version 5.1

<#+
.SYNOPSIS
    Run the LA Studio QML lint gate using the CMake-generated import graph.

.DESCRIPTION
    Qt 6.9's static analyzer cannot resolve QObject pointer properties exposed
    by the LAStudio C++ module and cannot statically model Loader delegates.
    Those categories are covered by the QML route smoke test. This gate keeps
    the remaining QML warnings at zero and fails on new actionable warnings.
#>

[CmdletBinding()]
param(
    [string] $Preset = "windows-msvc-release",
    [string] $QtRoot
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "out\build\$Preset"
$ResponseFile = Join-Path $BuildDir ".rcc\qmllint\LAStudio.rsp"

if (-not (Test-Path -LiteralPath $ResponseFile)) {
    throw "QML lint response file not found: $ResponseFile. Configure the preset first with scripts/build.ps1."
}

$qmllint = $null
if (-not [string]::IsNullOrWhiteSpace($QtRoot)) {
    $kit = if ($Preset -like "*mingw*") { "mingw_64" } else { "msvc2022_64" }
    $candidate = Join-Path $QtRoot "$kit\bin\qmllint.exe"
    if (Test-Path -LiteralPath $candidate) {
        $qmllint = $candidate
    }
}

if (-not $qmllint) {
    $command = Get-Command qmllint.exe -ErrorAction SilentlyContinue
    if ($command) {
        $qmllint = $command.Source
    }
}

if (-not $qmllint) {
    throw "qmllint.exe was not found. Pass -QtRoot or add the Qt bin directory to PATH."
}

Write-Host ">> Running QML lint gate" -ForegroundColor Cyan
& $qmllint `
    --unresolved-type disable `
    --unqualified disable `
    --missing-property disable `
    --max-warnings 0 `
    "@$ResponseFile"
exit $LASTEXITCODE
