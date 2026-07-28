#Requires -Version 5.1

<#
.SYNOPSIS
    Contract test for scripts/smoke_remote_preflight.ps1.

.DESCRIPTION
    Starts a loopback-only fake Gateway and fake direct STT worker, then proves
    that the preflight runner sends exactly three requests with the credential
    scoped to each independent route. The JSON config is a temporary file and
    the report must not contain either test secret.
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

$port = Get-FreeLoopbackPort
$configFile = New-TemporaryFile
$configPath = $configFile.FullName
$reportPath = Join-Path $RepoRoot "out\remote-live-preflight-contract-$PID.json"
$serverJob = $null
$previousGatewayKey = $env:LASTUDIO_TEST_GATEWAY_KEY
$previousColabToken = $env:LASTUDIO_TEST_COLAB_STT_TOKEN

try {
    $config = [ordered]@{
        gateway = [ordered]@{
            baseUrl = "http://127.0.0.1:$port/gateway"
            apiKeyEnvironment = 'LASTUDIO_TEST_GATEWAY_KEY'
        }
        colabWorkers = @(
            [ordered]@{
                capability = 'stt'
                baseUrl = "http://127.0.0.1:$port/stt"
                bearerTokenEnvironment = 'LASTUDIO_TEST_COLAB_STT_TOKEN'
            }
        )
    } | ConvertTo-Json -Depth 5
    Set-Content -LiteralPath $configPath -Value $config -Encoding UTF8

    $serverJob = Start-Job -ScriptBlock {
        param([int] $Port)

        $listener = New-Object Net.HttpListener
        $listener.Prefixes.Add("http://127.0.0.1:$Port/")
        $requests = New-Object System.Collections.Generic.List[object]
        try {
            $listener.Start()
            Write-Output ([pscustomobject]@{ event = 'ready' })

            for ($index = 0; $index -lt 3; $index++) {
                $context = $listener.GetContext()
                $path = $context.Request.Url.AbsolutePath
                $authorization = [string] $context.Request.Headers['Authorization']
                $payload = switch ($path) {
                    '/gateway/v1/models' {
                        @{ data = @(@{ id = 'gateway-contract-model' }) } | ConvertTo-Json -Compress
                        break
                    }
                    '/stt/health' {
                        @{ ready = $true; device = 'cuda' } | ConvertTo-Json -Compress
                        break
                    }
                    '/stt/v1/capabilities' {
                        @{ capabilities = @(@{ id = 'stt'; models = @(@{ id = 'contract-stt'; device = 'cuda' }) }) } | ConvertTo-Json -Depth 5 -Compress
                        break
                    }
                    default { $null }
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
    } -ArgumentList $port

    Wait-ForMockReady -Job $serverJob
    $env:LASTUDIO_TEST_GATEWAY_KEY = 'gateway-live-contract-secret'
    $env:LASTUDIO_TEST_COLAB_STT_TOKEN = 'colab-live-contract-secret'

    & $RunnerPath -ConfigPath $configPath -Only gateway,stt -AllowHttpForLocalTest -ReportPath $reportPath
    if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
        throw 'Preflight runner did not create its report.'
    }
    $reportText = Get-Content -LiteralPath $reportPath -Raw
    if ($reportText -match 'gateway-live-contract-secret|colab-live-contract-secret|/gateway/|/stt/') {
        throw 'Preflight report leaked a secret or endpoint path.'
    }
    $report = $reportText | ConvertFrom-Json
    if (-not $report.succeeded -or @($report.checks).Count -ne 3) {
        throw 'Preflight report did not contain three successful checks.'
    }

    $serverJob | Wait-Job -Timeout 10 | Out-Null
    if ($serverJob.State -ne 'Completed') {
        throw "Mock server did not complete (state: $($serverJob.State))."
    }
    $serverEvents = @(Receive-Job -Job $serverJob)
    $completeEvent = @($serverEvents | Where-Object { $_.event -eq 'complete' } | Select-Object -Last 1)
    if ($completeEvent.Count -ne 1) { throw 'Mock server did not report completed requests.' }
    $requests = @($completeEvent[0].requests)
    $expectedPaths = @('/gateway/v1/models', '/stt/health', '/stt/v1/capabilities')
    $expectedHeaders = @('Bearer gateway-live-contract-secret', 'Bearer colab-live-contract-secret', 'Bearer colab-live-contract-secret')
    if ($requests.Count -ne $expectedPaths.Count) {
        throw "Expected $($expectedPaths.Count) requests, received $($requests.Count)."
    }
    for ($index = 0; $index -lt $expectedPaths.Count; $index++) {
        if ($requests[$index].path -ne $expectedPaths[$index] -or $requests[$index].authorization -ne $expectedHeaders[$index]) {
            throw "Credential scope mismatch at request $index."
        }
    }

    Write-Host 'Remote live preflight contract test passed.' -ForegroundColor Green
}
finally {
    if ($null -ne $serverJob) {
        if ($serverJob.State -eq 'Running') { Stop-Job -Job $serverJob -ErrorAction SilentlyContinue }
        Remove-Job -Job $serverJob -Force -ErrorAction SilentlyContinue
    }
    $env:LASTUDIO_TEST_GATEWAY_KEY = $previousGatewayKey
    $env:LASTUDIO_TEST_COLAB_STT_TOKEN = $previousColabToken
    if (Test-Path -LiteralPath $configPath) { Remove-Item -LiteralPath $configPath -Force }
    if (Test-Path -LiteralPath $reportPath) { Remove-Item -LiteralPath $reportPath -Force }
}
