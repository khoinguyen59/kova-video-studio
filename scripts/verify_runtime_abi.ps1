#Requires -Version 5.1

<#
.SYNOPSIS
    Verifies that the catalog, build header ref, and compiled runtime ABI agree.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$abiPath = Join-Path $repoRoot "cmake\RuntimeAbi.cmake"
$catalogPath = Join-Path $repoRoot "data\catalog.json"

$abi = Get-Content -LiteralPath $abiPath -Raw
$refMatch = [regex]::Match($abi, 'set\(LASTUDIO_LLAMA_CPP_REF\s+"([^"]+)"\)')
$protocolMatch = [regex]::Match($abi, 'set\(LASTUDIO_LLAMA_PROTOCOL_VERSION\s+"([^"]+)"\)')
if (-not ($refMatch.Success -and $protocolMatch.Success)) {
    throw "Could not read the llama.cpp ref and protocol from '$abiPath'."
}

$ref = $refMatch.Groups[1].Value
$protocol = $protocolMatch.Groups[1].Value
if ($protocol -ne "llama-c-api-$ref") {
    throw "Runtime ABI protocol '$protocol' must be derived from llama.cpp ref '$ref'."
}

$catalog = Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
$families = @($catalog.sttFamilies) + @($catalog.llmFamilies) + @($catalog.ttsFamilies)
$llamaRuntimes = @(
    foreach ($family in $families) {
        foreach ($runtime in @($family.runtimes)) {
            if ($runtime.engineFamily -eq "llama") { $runtime }
        }
    }
)
if ($llamaRuntimes.Count -eq 0) {
    throw "No llama runtimes were found in '$catalogPath'."
}

foreach ($runtime in $llamaRuntimes) {
    if ($runtime.protocolVersion -ne $protocol) {
        throw "Catalog runtime '$($runtime.id)' declares protocolVersion '$($runtime.protocolVersion)', expected '$protocol'."
    }
    if ($runtime.version -ne $ref) {
        throw "Catalog runtime '$($runtime.id)' declares version '$($runtime.version)', expected '$ref'."
    }
}

Write-Host "Runtime ABI verified: $($llamaRuntimes.Count) llama runtime entries use $protocol / $ref." -ForegroundColor Green
