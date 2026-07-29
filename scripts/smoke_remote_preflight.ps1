#Requires -Version 5.1

<#
.SYNOPSIS
    Checks live API Gateway and direct Colab worker readiness without persisting secrets.

.DESCRIPTION
    Reads endpoint URLs and *environment-variable names* from a JSON file, then
    verifies each selected route independently. The runner never writes a
    credential, request header, raw response, or endpoint path to disk. It does
    not submit inference, upload media, or create a voice profile.

    This is intentionally a preflight gate rather than a substitute for the
    feature-specific live smoke tests. Run one capability at a time before
    selecting it in LA Studio.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $ConfigPath,
    [ValidateRange(1, 120)]
    [int] $TimeoutSeconds = 20,
    [string[]] $Only,
    [string] $ReportPath,
    [switch] $AllowHttpForLocalTest,
    [switch] $DryRun
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$OutRoot = [IO.Path]::GetFullPath((Join-Path $RepoRoot 'out')).TrimEnd([IO.Path]::DirectorySeparatorChar)
$KnownCapabilities = @('stt', 'tts', 'voice-cloning', 'voice-design', 'forced-alignment', 'voice-isolation', 'translation', 'chat')

function Get-RequiredStringProperty {
    param(
        [Parameter(Mandatory)] [object] $Object,
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $Context
    )

    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or [string]::IsNullOrWhiteSpace([string] $property.Value)) {
        throw "$Context requires a non-empty '$Name' property."
    }
    return ([string] $property.Value).Trim()
}

function Get-OptionalProperty {
    param([object] $Object, [string] $Name)
    if ($null -eq $Object) { return $null }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Resolve-Endpoint {
    param(
        [Parameter(Mandatory)] [string] $Value,
        [Parameter(Mandatory)] [string] $Context
    )

    $uri = $null
    if (-not [Uri]::TryCreate($Value, [UriKind]::Absolute, [ref] $uri)) {
        throw "$Context endpoint is not an absolute URL."
    }
    if ($uri.UserInfo -or $uri.Query -or $uri.Fragment) {
        throw "$Context endpoint must not contain user info, a query string, or a fragment."
    }

    if ($uri.Scheme -ne 'https') {
        $isLoopback = $uri.Host -in @('localhost', '127.0.0.1', '::1')
        if (-not ($AllowHttpForLocalTest -and $uri.Scheme -eq 'http' -and $isLoopback)) {
            throw "$Context endpoint must use HTTPS. HTTP is allowed only for loopback with -AllowHttpForLocalTest."
        }
    }
    return $uri
}

function Get-RedactedEndpoint {
    param([Parameter(Mandatory)] [Uri] $Uri)
    $authority = $Uri.Host
    if (-not $Uri.IsDefaultPort) { $authority = "${authority}:$($Uri.Port)" }
    return "$($Uri.Scheme)://$authority"
}

function Join-EndpointPath {
    param(
        [Parameter(Mandatory)] [Uri] $BaseUri,
        [Parameter(Mandatory)] [string] $Path
    )

    $base = $BaseUri.AbsoluteUri.TrimEnd('/')
    return "$base/$($Path.TrimStart('/'))"
}

function Get-GatewayModelCatalogUrl {
    param(
        [Parameter(Mandatory)] [Uri] $BaseUri,
        [Parameter(Mandatory)] [ValidateSet('llm', 'stt', 'tts')] [string] $Capability
    )

    $base = $BaseUri.AbsoluteUri.TrimEnd('/')
    $path = $BaseUri.AbsolutePath.TrimEnd('/')
    $catalogPath = if ($Capability -eq 'llm') { 'models' } else { "models/$Capability" }
    if ($path.EndsWith('/v1', [StringComparison]::OrdinalIgnoreCase)) {
        return "$base/$catalogPath"
    }
    return "$base/v1/$catalogPath"
}

function Get-SecretFromEnvironment {
    param(
        [Parameter(Mandatory)] [string] $EnvironmentName,
        [Parameter(Mandatory)] [string] $Context
    )

    if ($EnvironmentName -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') {
        throw "$Context environment variable name is invalid."
    }
    $value = [Environment]::GetEnvironmentVariable($EnvironmentName)
    if ([string]::IsNullOrWhiteSpace($value)) {
        throw "$Context secret is unavailable. Set environment variable '$EnvironmentName' only in the shell that runs this script."
    }
    return $value.Trim()
}

function Get-ErrorSummary {
    param([Parameter(Mandatory)] [System.Management.Automation.ErrorRecord] $ErrorRecord)

    # Assertion failures below use fixed identifiers.  They make the report
    # actionable without reflecting values supplied by a remote endpoint
    # (which could otherwise contain an endpoint, token, or untrusted text).
    $knownValidationFailures = @{
        'preflight.health-not-cuda' = 'Worker is not ready on CUDA or reports CPU fallback.'
        'preflight.health-model-mismatch' = 'Worker health reports a model other than the configured expected model.'
        'preflight.capability-contract-version' = 'Worker capability contract version is unsupported.'
        'preflight.capability-missing' = 'Worker does not advertise the configured capability.'
        'preflight.capability-model-missing' = 'Worker does not advertise the configured expected model.'
        'preflight.capability-model-metadata-missing' = 'Worker does not provide metadata for the configured expected model.'
        'preflight.capability-model-not-cuda' = 'Configured expected model is not loaded on CUDA.'
    }
    $message = [string] $ErrorRecord.Exception.Message
    if ($knownValidationFailures.ContainsKey($message)) {
        return $knownValidationFailures[$message]
    }
    $response = $ErrorRecord.Exception.Response
    if ($null -ne $response -and $null -ne $response.StatusCode) {
        return "HTTP $([int] $response.StatusCode) request failed."
    }
    return "Request failed ($($ErrorRecord.Exception.GetType().Name))."
}

function Invoke-JsonGet {
    param(
        [Parameter(Mandatory)] [string] $Uri,
        [Parameter(Mandatory)] [hashtable] $Headers
    )

    return Invoke-RestMethod -Method Get -Uri $Uri -Headers $Headers -TimeoutSec $TimeoutSeconds -UseBasicParsing
}

function Get-CapabilityIds {
    param([Parameter(Mandatory)] [object] $Payload)

    $ids = New-Object System.Collections.Generic.List[string]
    $capabilities = Get-OptionalProperty -Object $Payload -Name 'capabilities'
    foreach ($capability in @($capabilities)) {
        $id = Get-OptionalProperty -Object $capability -Name 'id'
        if (-not [string]::IsNullOrWhiteSpace([string] $id)) { [void] $ids.Add(([string] $id).Trim()) }
    }

    # The combined language notebook exposes these as two top-level arrays.
    foreach ($languageCapability in @('translation', 'chat')) {
        if ($null -ne (Get-OptionalProperty -Object $Payload -Name $languageCapability)) {
            [void] $ids.Add($languageCapability)
        }
    }
    return @($ids | Select-Object -Unique)
}

function Get-CapabilityModelIds {
    param(
        [Parameter(Mandatory)] [object] $Payload,
        [Parameter(Mandatory)] [string] $ExpectedCapability
    )

    $modelIds = New-Object System.Collections.Generic.List[string]
    $capabilities = Get-OptionalProperty -Object $Payload -Name 'capabilities'
    foreach ($capability in @($capabilities)) {
        $capabilityId = [string] (Get-OptionalProperty -Object $capability -Name 'id')
        if (-not $capabilityId.Equals($ExpectedCapability, [StringComparison]::OrdinalIgnoreCase)) { continue }
        foreach ($model in @(Get-OptionalProperty -Object $capability -Name 'models')) {
            $id = [string] (Get-OptionalProperty -Object $model -Name 'id')
            if (-not [string]::IsNullOrWhiteSpace($id)) { [void] $modelIds.Add($id.Trim()) }
        }
    }

    # The combined language worker publishes translation and chat model arrays
    # at the top level rather than beneath the generic capabilities array.
    if ($ExpectedCapability -in @('translation', 'chat')) {
        foreach ($model in @(Get-OptionalProperty -Object $Payload -Name $ExpectedCapability)) {
            $id = [string] (Get-OptionalProperty -Object $model -Name 'id')
            if (-not [string]::IsNullOrWhiteSpace($id)) { [void] $modelIds.Add($id.Trim()) }
        }
    }
    return @($modelIds | Sort-Object -Unique)
}

function Get-GatewayModelIds {
    param([Parameter(Mandatory)] [object] $Payload)

    $data = Get-OptionalProperty -Object $Payload -Name 'data'
    if ($null -eq $data) { throw 'Gateway response is missing its data array.' }

    $modelIds = New-Object System.Collections.Generic.List[string]
    foreach ($model in @($data)) {
        $id = [string] (Get-OptionalProperty -Object $model -Name 'id')
        if ([string]::IsNullOrWhiteSpace($id)) {
            throw 'Gateway returned a model entry without an ID.'
        }
        [void] $modelIds.Add($id.Trim())
    }
    if ($modelIds.Count -lt 1) { throw 'Gateway returned no models.' }
    return @($modelIds | Sort-Object -Unique)
}

function Add-Check {
    param(
        [Parameter(Mandatory)] [string] $Scope,
        [Parameter(Mandatory)] [string] $Check,
        [Parameter(Mandatory)] [bool] $Passed,
        [Parameter(Mandatory)] [string] $Detail
    )

    [void] $script:Checks.Add([pscustomobject]@{
        scope = $Scope
        check = $Check
        passed = $Passed
        detail = $Detail
    })
}

function Invoke-Check {
    param(
        [Parameter(Mandatory)] [string] $Scope,
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [scriptblock] $Action
    )

    try {
        $detail = & $Action
        Add-Check -Scope $Scope -Check $Name -Passed $true -Detail ([string] $detail)
    }
    catch {
        Add-Check -Scope $Scope -Check $Name -Passed $false -Detail (Get-ErrorSummary -ErrorRecord $_)
    }
}

function Assert-ReadyCudaHealth {
    param(
        [Parameter(Mandatory)] [object] $Health,
        [Parameter(Mandatory)] [string] $ExpectedModel
    )

    $ready = Get-OptionalProperty -Object $Health -Name 'ready'
    $device = [string] (Get-OptionalProperty -Object $Health -Name 'device')
    $cpuFallback = Get-OptionalProperty -Object $Health -Name 'cpu_fallback'
    $reportedModel = ([string] (Get-OptionalProperty -Object $Health -Name 'model')).Trim()
    if ($ready -ne $true -or -not $device.Equals('cuda', [StringComparison]::OrdinalIgnoreCase) -or $cpuFallback -ne $false) {
        throw 'preflight.health-not-cuda'
    }
    if (-not $reportedModel.Equals($ExpectedModel, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'preflight.health-model-mismatch'
    }
    return "ready=true; device=cuda; cpu_fallback=false; model=$reportedModel"
}

function Assert-WorkerCapability {
    param(
        [Parameter(Mandatory)] [object] $Capabilities,
        [Parameter(Mandatory)] [string] $ExpectedCapability,
        [Parameter(Mandatory)] [string] $ExpectedModel
    )

    $contractVersion = Get-OptionalProperty -Object $Capabilities -Name 'contract_version'
    if ($contractVersion -ne 1) {
        throw 'preflight.capability-contract-version'
    }
    $ids = Get-CapabilityIds -Payload $Capabilities
    if ($ExpectedCapability -notin $ids) {
        throw 'preflight.capability-missing'
    }
    $modelIds = Get-CapabilityModelIds -Payload $Capabilities -ExpectedCapability $ExpectedCapability
    if ($modelIds.Count -eq 0) {
        throw 'preflight.capability-model-missing'
    }
    $matchingModel = @($modelIds | Where-Object {
        $_.Equals($ExpectedModel, [StringComparison]::OrdinalIgnoreCase)
    })
    if ($matchingModel.Count -ne 1) {
        throw 'preflight.capability-model-missing'
    }

    $modelEntries = New-Object System.Collections.Generic.List[object]
    foreach ($capability in @(Get-OptionalProperty -Object $Capabilities -Name 'capabilities')) {
        $capabilityId = [string] (Get-OptionalProperty -Object $capability -Name 'id')
        if ($capabilityId.Equals($ExpectedCapability, [StringComparison]::OrdinalIgnoreCase)) {
            foreach ($model in @(Get-OptionalProperty -Object $capability -Name 'models')) {
                [void] $modelEntries.Add($model)
            }
        }
    }
    if ($ExpectedCapability -in @('translation', 'chat')) {
        foreach ($model in @(Get-OptionalProperty -Object $Capabilities -Name $ExpectedCapability)) {
            [void] $modelEntries.Add($model)
        }
    }
    $exactEntry = @($modelEntries | Where-Object {
        ([string] (Get-OptionalProperty -Object $_ -Name 'id')).Equals($ExpectedModel, [StringComparison]::OrdinalIgnoreCase)
    } | Select-Object -First 1)
    if ($exactEntry.Count -ne 1) {
        throw 'preflight.capability-model-metadata-missing'
    }
    $entryDevice = [string] (Get-OptionalProperty -Object $exactEntry[0] -Name 'device')
    $entryLoaded = Get-OptionalProperty -Object $exactEntry[0] -Name 'loaded'
    if (-not $entryDevice.Equals('cuda', [StringComparison]::OrdinalIgnoreCase) -or $entryLoaded -ne $true) {
        throw 'preflight.capability-model-not-cuda'
    }
    return "contractVersion=1; advertises $ExpectedCapability; models=$($modelIds -join ','); exactModel=$ExpectedModel; loaded=true; device=cuda"
}

function Resolve-ReportPath {
    param([string] $Candidate)

    if ([string]::IsNullOrWhiteSpace($Candidate)) {
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        return Join-Path $OutRoot "remote-live-preflight-$stamp.json"
    }
    $resolved = if ([IO.Path]::IsPathRooted($Candidate)) {
        [IO.Path]::GetFullPath($Candidate)
    } else {
        [IO.Path]::GetFullPath((Join-Path $RepoRoot $Candidate))
    }
    $prefix = $OutRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "ReportPath must be below '$OutRoot'."
    }
    return $resolved
}

$ConfigPath = [IO.Path]::GetFullPath($ConfigPath)
if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
    throw "Config file was not found: $ConfigPath"
}
try {
    $config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
}
catch {
    throw 'Config file is not valid JSON.'
}

$requested = @(
    foreach ($onlyValue in @($Only)) {
        if ($null -eq $onlyValue) { continue }
        $normalized = ([string] $onlyValue).Trim()
        if (-not [string]::IsNullOrWhiteSpace($normalized)) {
            $normalized.ToLowerInvariant()
        }
    }
)
foreach ($item in $requested) {
    if ($item -ne 'gateway' -and $item -notin $KnownCapabilities) {
        throw "Unknown -Only value '$item'. Use gateway or one of: $($KnownCapabilities -join ', ')."
    }
}

$gateway = Get-OptionalProperty -Object $config -Name 'gateway'
$workers = @((Get-OptionalProperty -Object $config -Name 'colabWorkers'))
if ($null -eq $gateway -and $workers.Count -eq 0) {
    throw "Config must define 'gateway', 'colabWorkers', or both."
}

$workerDefinitions = New-Object System.Collections.Generic.List[object]
$seenCapabilities = @{}
foreach ($worker in $workers) {
    if ($null -eq $worker) { throw 'colabWorkers must not contain null entries.' }
    $capability = (Get-RequiredStringProperty -Object $worker -Name 'capability' -Context 'Colab worker').ToLowerInvariant()
    if ($capability -notin $KnownCapabilities) {
        throw "Unsupported Colab worker capability '$capability'."
    }
    if ($seenCapabilities.ContainsKey($capability)) {
        throw "Colab worker capability '$capability' appears more than once. Use one temporary session per capability."
    }
    $seenCapabilities[$capability] = $true
    [void] $workerDefinitions.Add([pscustomobject]@{
        capability = $capability
        endpoint = Resolve-Endpoint -Value (Get-RequiredStringProperty -Object $worker -Name 'baseUrl' -Context "Colab worker '$capability'") -Context "Colab worker '$capability'"
        tokenEnvironment = Get-RequiredStringProperty -Object $worker -Name 'bearerTokenEnvironment' -Context "Colab worker '$capability'"
        expectedModel = (Get-RequiredStringProperty -Object $worker -Name 'expectedModel' -Context "Colab worker '$capability'").ToLowerInvariant()
    })
}

$gatewayDefinition = $null
if ($null -ne $gateway) {
    $gatewayDefinition = [pscustomobject]@{
        endpoint = Resolve-Endpoint -Value (Get-RequiredStringProperty -Object $gateway -Name 'baseUrl' -Context 'Gateway') -Context 'Gateway'
        apiKeyEnvironment = Get-RequiredStringProperty -Object $gateway -Name 'apiKeyEnvironment' -Context 'Gateway'
    }
}

$selectedGateway = $null -ne $gatewayDefinition -and ($requested.Count -eq 0 -or 'gateway' -in $requested)
$selectedWorkers = @($workerDefinitions | Where-Object { $requested.Count -eq 0 -or $_.capability -in $requested })
if (-not $selectedGateway -and $selectedWorkers.Count -eq 0) {
    throw 'No configured route matched -Only.'
}

$gatewayCatalogs = @(
    [pscustomobject]@{ check = 'models-llm'; capability = 'llm'; label = 'LLM' },
    [pscustomobject]@{ check = 'models-stt'; capability = 'stt'; label = 'STT' },
    [pscustomobject]@{ check = 'models-tts'; capability = 'tts'; label = 'TTS' }
)

$script:Checks = New-Object System.Collections.Generic.List[object]

if ($DryRun) {
    if ($selectedGateway) {
        foreach ($catalog in $gatewayCatalogs) {
            Add-Check -Scope 'gateway' -Check $catalog.check -Passed $true -Detail "planned: Gateway $($catalog.label) model-catalog request; endpoint=$(Get-RedactedEndpoint $gatewayDefinition.endpoint)"
        }
    }
    foreach ($worker in $selectedWorkers) {
        $scope = "colab:$($worker.capability)"
        Add-Check -Scope $scope -Check 'health' -Passed $true -Detail "planned: worker health request for model=$($worker.expectedModel); endpoint=$(Get-RedactedEndpoint $worker.endpoint)"
        Add-Check -Scope $scope -Check 'capabilities' -Passed $true -Detail "planned: worker capability request for model=$($worker.expectedModel); endpoint=$(Get-RedactedEndpoint $worker.endpoint)"
    }
}
else {
    if ($selectedGateway) {
        $gatewayScope = 'gateway'
        foreach ($catalog in $gatewayCatalogs) {
            Invoke-Check -Scope $gatewayScope -Name $catalog.check -Action {
                $secret = Get-SecretFromEnvironment -EnvironmentName $gatewayDefinition.apiKeyEnvironment -Context 'Gateway API key'
                $headers = @{ Authorization = "Bearer $secret"; Accept = 'application/json' }
                $modelsUrl = Get-GatewayModelCatalogUrl -BaseUri $gatewayDefinition.endpoint -Capability $catalog.capability
                $models = Invoke-JsonGet -Uri $modelsUrl -Headers $headers
                $modelIds = Get-GatewayModelIds -Payload $models
                return "capability=$($catalog.capability); modelCount=$($modelIds.Count); models=$($modelIds -join ','); endpoint=$(Get-RedactedEndpoint $gatewayDefinition.endpoint)"
            }
        }
    }

    foreach ($worker in $selectedWorkers) {
        $scope = "colab:$($worker.capability)"
        Invoke-Check -Scope $scope -Name 'health' -Action {
            $secret = Get-SecretFromEnvironment -EnvironmentName $worker.tokenEnvironment -Context "Colab $($worker.capability) bearer token"
            $headers = @{ Authorization = "Bearer $secret"; Accept = 'application/json' }
            $health = Invoke-JsonGet -Uri (Join-EndpointPath -BaseUri $worker.endpoint -Path 'health') -Headers $headers
            return "$(Assert-ReadyCudaHealth -Health $health -ExpectedModel $worker.expectedModel); endpoint=$(Get-RedactedEndpoint $worker.endpoint)"
        }
        Invoke-Check -Scope $scope -Name 'capabilities' -Action {
            $secret = Get-SecretFromEnvironment -EnvironmentName $worker.tokenEnvironment -Context "Colab $($worker.capability) bearer token"
            $headers = @{ Authorization = "Bearer $secret"; Accept = 'application/json' }
            $capabilities = Invoke-JsonGet -Uri (Join-EndpointPath -BaseUri $worker.endpoint -Path 'v1/capabilities') -Headers $headers
            return Assert-WorkerCapability -Capabilities $capabilities -ExpectedCapability $worker.capability -ExpectedModel $worker.expectedModel
        }
    }
}

$checkItems = @($script:Checks.ToArray())
$failedChecks = @($checkItems | Where-Object { -not $_.passed })
$report = [pscustomobject]@{
    schemaVersion = 1
    generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    mode = if ($DryRun) { 'dry-run' } else { 'live-preflight' }
    succeeded = ($failedChecks.Count -eq 0)
    checks = $checkItems
}
$resolvedReportPath = Resolve-ReportPath -Candidate $ReportPath
New-Item -ItemType Directory -Path (Split-Path -Parent $resolvedReportPath) -Force | Out-Null
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resolvedReportPath -Encoding UTF8

$report.checks | Format-Table scope, check, passed, detail -AutoSize
Write-Host "Report: $resolvedReportPath" -ForegroundColor Cyan
if (-not $report.succeeded) {
    throw 'Remote live preflight failed. Credentials remain in the calling process only; inspect the redacted report for failing checks.'
}
Write-Host 'Remote live preflight passed.' -ForegroundColor Green
