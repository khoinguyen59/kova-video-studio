#Requires -Version 5.1

<#
.SYNOPSIS
    Performs a clean-machine smoke test of a built LA Studio installer.
.DESCRIPTION
    Installs to an explicit temporary directory, verifies both shipped
    executables can remain running, checks that the application creates its
    log, and then uninstalls. Intended for an ephemeral Windows CI runner.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $InstallerPath,
    [Parameter(Mandatory)]
    [string] $InstallDir,
    [Parameter(Mandatory)]
    [string] $DataDir
)

$ErrorActionPreference = "Stop"

function Assert-NonRootPath {
    param([string] $Path, [string] $Name)

    $fullPath = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($fullPath)
    if ([string]::IsNullOrWhiteSpace($fullPath) -or $fullPath.TrimEnd('\\') -eq $root.TrimEnd('\\')) {
        throw "$Name must be a non-root directory."
    }
    return $fullPath
}

function Stop-SmokeProcess {
    param([System.Diagnostics.Process] $Process)

    if ($null -eq $Process) { return }
    $Process.Refresh()
    if (-not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
        $Process.WaitForExit(5000) | Out-Null
    }
}

function Start-AndAssertAlive {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [string[]] $ArgumentList = @(),
        [int] $StartupWaitMs = 4000
    )

    $process = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList -PassThru
    Start-Sleep -Milliseconds $StartupWaitMs
    $process.Refresh()
    if ($process.HasExited) {
        throw "'$FilePath' exited during startup with code $($process.ExitCode)."
    }
    return $process
}

$InstallerPath = [IO.Path]::GetFullPath($InstallerPath)
if (-not (Test-Path -LiteralPath $InstallerPath -PathType Leaf)) {
    throw "Installer was not found: $InstallerPath"
}
$InstallDir = Assert-NonRootPath -Path $InstallDir -Name "InstallDir"
$DataDir = Assert-NonRootPath -Path $DataDir -Name "DataDir"

$previousDataDir = $env:LASTUDIO_DATA_DIR
$previousHostToken = $env:LASTUDIO_RUNTIME_HOST_TOKEN
$appProcess = $null
$hostProcess = $null
$uninstaller = $null

try {
    if (Test-Path -LiteralPath $InstallDir) {
        throw "InstallDir must not already exist: $InstallDir"
    }
    New-Item -ItemType Directory -Path $DataDir -Force | Out-Null

    Write-Host ">> Smoke installing to $InstallDir" -ForegroundColor Cyan
    $installerArgs = @(
        "/VERYSILENT",
        "/SUPPRESSMSGBOXES",
        "/NORESTART",
        "/SP-",
        "/DIR=`"$InstallDir`""
    )
    $installProcess = Start-Process -FilePath $InstallerPath -ArgumentList $installerArgs -Wait -PassThru
    if ($installProcess.ExitCode -ne 0) {
        throw "Installer exited with code $($installProcess.ExitCode)."
    }

    $appCandidates = @(Get-ChildItem -LiteralPath (Join-Path $InstallDir 'bin') -Filter 'LA-Studio-*.exe' -File)
    if ($appCandidates.Count -ne 1) {
        throw "Expected exactly one versioned LA Studio executable in '$InstallDir\bin'; found $($appCandidates.Count)."
    }
    $appPath = $appCandidates[0].FullName
    $hostPath = Join-Path $InstallDir "bin\LAStudioRuntimeHost.exe"
    foreach ($path in @($appPath, $hostPath)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Installed executable was not found: $path"
        }
    }
    $uninstaller = Join-Path $InstallDir "unins000.exe"
    if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
        throw "Installed uninstaller was not found: $uninstaller"
    }

    $env:LASTUDIO_DATA_DIR = $DataDir
    $appProcess = Start-AndAssertAlive -FilePath $appPath
    Stop-SmokeProcess -Process $appProcess
    $appProcess = $null

    $logPath = Join-Path $DataDir "logs\app.log"
    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
        throw "Application did not create its log: $logPath"
    }
    if (-not (Select-String -LiteralPath $logPath -Pattern "LA Studio Starting" -Quiet)) {
        throw "Application log did not contain the startup banner: $logPath"
    }

    $socketName = "lastudio-smoke-$PID-$([guid]::NewGuid().ToString('N'))"
    $env:LASTUDIO_RUNTIME_HOST_TOKEN = [guid]::NewGuid().ToString('N')
    $hostProcess = Start-AndAssertAlive -FilePath $hostPath -ArgumentList @("--socket", $socketName)
    Stop-SmokeProcess -Process $hostProcess
    $hostProcess = $null

    Write-Host "Installer smoke test passed." -ForegroundColor Green
}
finally {
    Stop-SmokeProcess -Process $appProcess
    Stop-SmokeProcess -Process $hostProcess

    if ($uninstaller -and (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
        Write-Host ">> Smoke uninstalling" -ForegroundColor Cyan
        $uninstallProcess = Start-Process -FilePath $uninstaller -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-") -Wait -PassThru
        if ($uninstallProcess.ExitCode -ne 0) {
            throw "Uninstaller exited with code $($uninstallProcess.ExitCode)."
        }
    }
    if (Test-Path -LiteralPath $InstallDir) {
        throw "Installer smoke cleanup left the installation directory behind: $InstallDir"
    }

    $env:LASTUDIO_DATA_DIR = $previousDataDir
    $env:LASTUDIO_RUNTIME_HOST_TOKEN = $previousHostToken
}
