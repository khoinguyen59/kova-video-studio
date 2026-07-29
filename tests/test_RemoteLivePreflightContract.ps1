#Requires -Version 5.1

<#
.SYNOPSIS
    Contract test for scripts/smoke_remote_preflight.ps1.

.DESCRIPTION
    Starts a loopback-only fake Gateway and all eight direct Colab capability
    routes. It proves that every preflight request uses the credential scoped
    to that route, including the two independent Language-worker sessions.
    The JSON config is temporary and the report must not contain test secrets
    or endpoint paths.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$RunnerPath = Join-Path $RepoRoot 'scripts\smoke_remote_preflight.ps1'
if (-not (Test-Path -LiteralPath $RunnerPath -PathType Leaf)) {
    throw "Remote preflight runner was not found: $RunnerPath"
}

function Get-FreeLoopbackPort {
    $listener = New-Object System.Net.Sockets.TcpListener([Net.IPAddress]::Loopback, 0)
    try {
        $listener.Start()
        return ([Net.IPEndPoint] $listener.LocalEndpoint).Port
    }
    finally {
        $listener.Stop()
    }
}

function Wait-ForMockReady {
    param([Parameter(Mandatory)] [System.Management.Automation.Job] $Job)

    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    while ([DateTime]::UtcNow -lt $deadline) {
        $events = @(Receive-Job -Job $Job -Keep -ErrorAction Stop)
        if (@($events | Where-Object { $_.event -eq 'ready' }).Count -gt 0) {
            return
        }
        if ($Job.State -in @('Failed', 'Stopped')) {
            throw "Mock server did not start (state: $($Job.State))."
        }
        Start-Sleep -Milliseconds 80
    }
    throw 'Timed out while starting loopback mock server.'
}

$workers = @(
    [pscustomobject]@{ capability = 'stt'; route = 'stt'; model = 'stt-contract-model'; environment = 'LASTUDIO_TEST_COLAB_STT_TOKEN' },
    [pscustomobject]@{ capability = 'tts'; route = 'tts'; model = 'tts-contract-model'; environment = 'LASTUDIO_TEST_COLAB_TTS_TOKEN' },
    [pscustomobject]@{ capability = 'voice-cloning'; route = 'voice-cloning'; model = 'voice-cloning-contract-model'; environment = 'LASTUDIO_TEST_COLAB_VOICE_CLONING_TOKEN' },
    [pscustomobject]@{ capability = 'voice-design'; route = 'voice-design'; model = 'voice-design-contract-model'; environment = 'LASTUDIO_TEST_COLAB_VOICE_DESIGN_TOKEN' },
    [pscustomobject]@{ capability = 'forced-alignment'; route = 'forced-alignment'; model = 'forced-alignment-contract-model'; environment = 'LASTUDIO_TEST_COLAB_ALIGNMENT_TOKEN' },
    [pscustomobject]@{ capability = 'voice-isolation'; route = 'voice-isolation'; model = 'voice-isolation-contract-model'; environment = 'LASTUDIO_TEST_COLAB_SEPARATION_TOKEN' },
    [pscustomobject]@{ capability = 'translation'; route = 'language'; model = 'translation-contract-model'; environment = 'LASTUDIO_TEST_COLAB_TRANSLATION_TOKEN' },
    [pscustomobject]@{ capability = 'chat'; route = 'language'; model = 'chat-contract-model'; environment = 'LASTUDIO_TEST_COLAB_CHAT_TOKEN' }
)

$port = Get-FreeLoopbackPort
# New-TemporaryFile is not a PowerShell built-in and is absent on the hosted
# GitHub Windows image.  The .NET API is available in both Windows PowerShell
# 5.1 and PowerShell 7, creates the file atomically, and is removed in finally.
$configPath = [System.IO.Path]::GetTempFileName()
$reportPath = Join-Path $RepoRoot "out\remote-live-preflight-contract-$PID.json"
$wrongModelReportPath = Join-Path $RepoRoot "out\remote-live-preflight-wrong-model-contract-$PID.json"
$serverJob = $null
$previousSecrets = @{}
$gatewayEnvironment = 'LASTUDIO_TEST_GATEWAY_KEY'
$positiveRequestCount = 3 + ($workers.Count * 2)
$expectedRequestCount = $positiveRequestCount + 2

try {
    $colabWorkers = @(
        foreach ($worker in $workers) {
            [ordered]@{
                capability = $worker.capability
                expectedModel = $worker.model
                baseUrl = "http://127.0.0.1:$port/$($worker.route)"
                bearerTokenEnvironment = $worker.environment
            }
        }
    )
    $config = [ordered]@{
        # Including /v1 verifies the same normalisation used by the desktop.
        gateway = [ordered]@{
            baseUrl = "http://127.0.0.1:$port/gateway/v1"
            apiKeyEnvironment = $gatewayEnvironment
        }
        colabWorkers = $colabWorkers
    }
    Set-Content -LiteralPath $configPath -Value ($config | ConvertTo-Json -Depth 5) -Encoding UTF8

    $serverJob = Start-Job -ScriptBlock {
        param([int] $Port, [int] $ExpectedCount)

        $listener = New-Object Net.HttpListener
        $listener.Prefixes.Add("http://127.0.0.1:$Port/")
        $requests = New-Object System.Collections.Generic.List[object]
        $routeCapabilities = @{
            'stt' = 'stt'
            'tts' = 'tts'
            'voice-cloning' = 'voice-cloning'
            'voice-design' = 'voice-design'
            'forced-alignment' = 'forced-alignment'
            'voice-isolation' = 'voice-isolation'
        }
        try {
            $listener.Start()
            Write-Output ([pscustomobject]@{ event = 'ready' })

            for ($index = 0; $index -lt $ExpectedCount; $index++) {
                $context = $listener.GetContext()
                $path = $context.Request.Url.AbsolutePath
                $authorization = [string] $context.Request.Headers['Authorization']
                $payload = $null

                if ($path -eq '/gateway/v1/models') {
                    $payload = @{ data = @(@{ id = 'gateway-llm-contract-model' }) } | ConvertTo-Json -Compress
                }
                elseif ($path -eq '/gateway/v1/models/stt') {
                    $payload = @{ data = @(@{ id = 'gateway-stt-contract-model' }) } | ConvertTo-Json -Compress
                }
                elseif ($path -eq '/gateway/v1/models/tts') {
                    $payload = @{ data = @(@{ id = 'gateway-tts-contract-model' }) } | ConvertTo-Json -Compress
                }
                elseif ($path -match '^/([^/]+)/health$') {
                    $route = $Matches[1]
                    $model = if ($route -eq 'language') {
                        if ($authorization -match 'translation') { 'translation-contract-model' } else { 'chat-contract-model' }
                    } else {
                        "$route-contract-model"
                    }
                    $payload = @{ ready = $true; device = 'cuda'; cpu_fallback = $false; model = $model } | ConvertTo-Json -Compress
                }
                elseif ($path -match '^/([^/]+)/v1/capabilities$') {
                    $route = $Matches[1]
                    if ($route -eq 'language') {
                        $payload = @{
                            contract_version = 1
                            device = 'cuda'
                            translation = @(@{ id = 'translation-contract-model'; device = 'cuda'; loaded = $true })
                            chat = @(@{ id = 'chat-contract-model'; device = 'cuda'; loaded = $true })
                        } | ConvertTo-Json -Depth 5 -Compress
                    }
                    elseif ($routeCapabilities.ContainsKey($route)) {
                        $capability = $routeCapabilities[$route]
                        $payload = @{
                            contract_version = 1
                            capabilities = @(@{
                                id = $capability
                                models = @(@{ id = "$capability-contract-model"; device = 'cuda'; loaded = $true })
                            })
                        } | ConvertTo-Json -Depth 5 -Compress
                    }
                }

                if ($null -eq $payload) {
                    $context.Response.StatusCode = 404
                    $payload = '{"error":"unknown route"}'
                }
                else {
                    $context.Response.StatusCode = 200
                }
                $bytes = [Text.Encoding]::UTF8.GetBytes($payload)
                $context.Response.ContentType = 'application/json'
                $context.Response.ContentLength64 = $bytes.Length
                $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
                $context.Response.Close()
                [void] $requests.Add([pscustomobject]@{
                    path = $path
                    authorization = $authorization
                })
            }
        }
        finally {
            if ($listener.IsListening) { $listener.Stop() }
            $listener.Close()
        }
        Write-Output ([pscustomobject]@{ event = 'complete'; requests = @($requests.ToArray()) })
    } -ArgumentList $port, $expectedRequestCount

    Wait-ForMockReady -Job $serverJob

    $previousSecrets[$gatewayEnvironment] = [Environment]::GetEnvironmentVariable($gatewayEnvironment, 'Process')
    [Environment]::SetEnvironmentVariable($gatewayEnvironment, 'gateway-live-contract-secret', 'Process')
    foreach ($worker in $workers) {
        $previousSecrets[$worker.environment] = [Environment]::GetEnvironmentVariable($worker.environment, 'Process')
        [Environment]::SetEnvironmentVariable($worker.environment, "colab-$($worker.capability)-live-contract-secret", 'Process')
    }

    & $RunnerPath -ConfigPath $configPath -AllowHttpForLocalTest -ReportPath $reportPath
    if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
        throw 'Preflight runner did not create its report.'
    }
    $reportText = Get-Content -LiteralPath $reportPath -Raw
    $secrets = @('gateway-live-contract-secret') + @($workers | ForEach-Object { "colab-$($_.capability)-live-contract-secret" })
    $forbiddenReportText = @($secrets | ForEach-Object { [Regex]::Escape($_) }) + @('/gateway/', '/stt/', '/tts/', '/language/')
    if ($reportText -match ($forbiddenReportText -join '|')) {
        throw 'Preflight report leaked a secret or endpoint path.'
    }
    $report = $reportText | ConvertFrom-Json
    if (-not $report.succeeded -or @($report.checks).Count -ne $positiveRequestCount) {
        throw "Preflight report did not contain $positiveRequestCount successful checks."
    }
    foreach ($catalog in @(
            [pscustomobject]@{ check = 'models-llm'; model = 'gateway-llm-contract-model' },
            [pscustomobject]@{ check = 'models-stt'; model = 'gateway-stt-contract-model' },
            [pscustomobject]@{ check = 'models-tts'; model = 'gateway-tts-contract-model' }
        )) {
        $catalogCheck = @($report.checks | Where-Object { $_.scope -eq 'gateway' -and $_.check -eq $catalog.check })
        if ($catalogCheck.Count -ne 1 -or $catalogCheck[0].detail -notmatch [Regex]::Escape("models=$($catalog.model)")) {
            throw "Preflight report did not record the advertised $($catalog.check) model ID."
        }
    }
    foreach ($worker in $workers) {
        $capabilityCheck = @($report.checks | Where-Object {
            $_.scope -eq "colab:$($worker.capability)" -and $_.check -eq 'capabilities'
        })
        if ($capabilityCheck.Count -ne 1 -or $capabilityCheck[0].detail -notmatch [Regex]::Escape("exactModel=$($worker.model)")) {
            throw "Preflight report did not verify the expected $($worker.capability) model ID."
        }
        if ($capabilityCheck[0].detail -notmatch 'contractVersion=1') {
            throw "Preflight report did not validate contract version for $($worker.capability)."
        }
    }

    $colabWorkers[0].expectedModel = 'wrong-model-contract'
    ($config | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath $configPath -Encoding UTF8
    $wrongModelRejected = $false
    try {
        & $RunnerPath -ConfigPath $configPath -Only stt -AllowHttpForLocalTest -ReportPath $wrongModelReportPath
    }
    catch {
        $wrongModelRejected = $true
    }
    if (-not $wrongModelRejected -or -not (Test-Path -LiteralPath $wrongModelReportPath -PathType Leaf)) {
        throw 'Preflight runner accepted a worker with the wrong exact model.'
    }
    $wrongModelText = Get-Content -LiteralPath $wrongModelReportPath -Raw
    foreach ($secret in $forbiddenReportText) {
        if ($wrongModelText.Contains($secret)) {
            throw 'Wrong-model preflight report leaked a credential or endpoint path.'
        }
    }
    $wrongModelReport = $wrongModelText | ConvertFrom-Json
    if ($wrongModelReport.succeeded -or @($wrongModelReport.checks | Where-Object { -not $_.passed }).Count -ne 2) {
        throw 'Wrong-model preflight did not report both failed exact-model checks.'
    }
    $wrongModelDetails = @($wrongModelReport.checks | Where-Object { -not $_.passed } | ForEach-Object { $_.detail })
    if ($wrongModelDetails -notcontains 'Worker health reports a model other than the configured expected model.' -or
        $wrongModelDetails -notcontains 'Worker does not advertise the configured expected model.') {
        throw 'Wrong-model preflight did not report the exact-model validation failure.'
    }

    $serverJob | Wait-Job -Timeout 12 | Out-Null
    if ($serverJob.State -ne 'Completed') {
        throw "Mock server did not complete (state: $($serverJob.State))."
    }
    $serverEvents = @(Receive-Job -Job $serverJob)
    $completeEvent = @($serverEvents | Where-Object { $_.event -eq 'complete' } | Select-Object -Last 1)
    if ($completeEvent.Count -ne 1) { throw 'Mock server did not report completed requests.' }
    $requests = @($completeEvent[0].requests)

    $expectedPaths = New-Object System.Collections.Generic.List[string]
    $expectedHeaders = New-Object System.Collections.Generic.List[string]
    [void] $expectedPaths.Add('/gateway/v1/models')
    [void] $expectedPaths.Add('/gateway/v1/models/stt')
    [void] $expectedPaths.Add('/gateway/v1/models/tts')
    [void] $expectedHeaders.Add('Bearer gateway-live-contract-secret')
    [void] $expectedHeaders.Add('Bearer gateway-live-contract-secret')
    [void] $expectedHeaders.Add('Bearer gateway-live-contract-secret')
    foreach ($worker in $workers) {
        [void] $expectedPaths.Add("/$($worker.route)/health")
        [void] $expectedPaths.Add("/$($worker.route)/v1/capabilities")
        $header = "Bearer colab-$($worker.capability)-live-contract-secret"
        [void] $expectedHeaders.Add($header)
        [void] $expectedHeaders.Add($header)
    }
    [void] $expectedPaths.Add('/stt/health')
    [void] $expectedPaths.Add('/stt/v1/capabilities')
    [void] $expectedHeaders.Add('Bearer colab-stt-live-contract-secret')
    [void] $expectedHeaders.Add('Bearer colab-stt-live-contract-secret')
    if ($requests.Count -ne $expectedPaths.Count) {
        throw "Expected $($expectedPaths.Count) requests, received $($requests.Count)."
    }
    for ($index = 0; $index -lt $expectedPaths.Count; $index++) {
        if ($requests[$index].path -ne $expectedPaths[$index] -or $requests[$index].authorization -ne $expectedHeaders[$index]) {
            throw "Credential scope mismatch at request $index."
        }
    }

    Write-Host 'Remote live preflight contract test passed for Gateway and all Colab capabilities.' -ForegroundColor Green
}
finally {
    if ($null -ne $serverJob) {
        if ($serverJob.State -eq 'Running') { Stop-Job -Job $serverJob -ErrorAction SilentlyContinue }
        Remove-Job -Job $serverJob -Force -ErrorAction SilentlyContinue
    }
    foreach ($environmentName in $previousSecrets.Keys) {
        [Environment]::SetEnvironmentVariable($environmentName, $previousSecrets[$environmentName], 'Process')
    }
    if (Test-Path -LiteralPath $configPath) { Remove-Item -LiteralPath $configPath -Force }
    if (Test-Path -LiteralPath $reportPath) { Remove-Item -LiteralPath $reportPath -Force }
    if (Test-Path -LiteralPath $wrongModelReportPath) { Remove-Item -LiteralPath $wrongModelReportPath -Force }
}
