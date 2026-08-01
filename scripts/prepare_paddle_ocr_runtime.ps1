param(
    [Parameter(Mandatory = $true)]
    [string] $BootstrapPython,
    [Parameter(Mandatory = $true)]
    [string] $EmbeddedPythonArchive,
    [Parameter(Mandatory = $true)]
    [string] $Wheelhouse,
    [Parameter(Mandatory = $true)]
    [string] $ModelCache,
    [Parameter(Mandatory = $true)]
    [string] $OutputRoot,
    [switch] $KeepFailedOutput
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Require-File([string] $Path, [string] $Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label was not found: $Path"
    }
}

function Require-Directory([string] $Path, [string] $Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Label was not found: $Path"
    }
}

function Get-LowerSha256([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Write-Utf8NoBom([string] $Path, [string] $Content) {
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($Path), $Content,
                             [Text.UTF8Encoding]::new($false))
}

function Get-ModelTreeSha256([string] $Root) {
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd([char]92, [char]47)
    $files = @(Get-ChildItem -LiteralPath $rootPath -File -Recurse | Sort-Object {
        $_.FullName.Substring($rootPath.Length).TrimStart([char]92, [char]47).Replace(([string][char]92), '/')
    })
    if ($files.Count -eq 0) { throw "PaddleOCR model cache is empty: $rootPath" }
    $hash = [System.Security.Cryptography.SHA256]::Create()
    try {
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($rootPath.Length).TrimStart([char]92, [char]47).Replace(([string][char]92), '/')
            foreach ($bytes in @(
                [Text.Encoding]::UTF8.GetBytes($relative),
                [byte[]]@(0),
                [Text.Encoding]::ASCII.GetBytes((Get-LowerSha256 $file.FullName)),
                [byte[]]@(10))) {
                [void] $hash.TransformBlock($bytes, 0, $bytes.Length, $bytes, 0)
            }
        }
        [void] $hash.TransformFinalBlock([byte[]]@(), 0, 0)
        return ([BitConverter]::ToString($hash.Hash) -replace '-', '').ToLowerInvariant()
    } finally {
        $hash.Dispose()
    }
}

function Read-FinalJsonLine([string] $Output) {
    $lines = @($Output -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    [array]::Reverse($lines)
    foreach ($line in $lines) {
        try {
            $candidate = $line | ConvertFrom-Json -ErrorAction Stop
            if ($null -ne $candidate.ok) { return $candidate }
        } catch { }
    }
    return $null
}

Require-File $BootstrapPython 'Explicit bootstrap Python interpreter'
Require-File $EmbeddedPythonArchive 'Official CPython embeddable archive'
Require-Directory $Wheelhouse 'Offline PaddleOCR wheelhouse'
Require-Directory $ModelCache 'Prepared PaddleOCR model cache'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$workerSource = Join-Path $repositoryRoot 'resources\paddle_ocr_worker.py'
$manifestSource = Join-Path $repositoryRoot 'resources\paddle-ocr-runtime-manifest.json'
Require-File $workerSource 'Reviewed PaddleOCR worker adapter'
Require-File $manifestSource 'PaddleOCR runtime manifest template'

$detector = Join-Path $ModelCache 'official_models\PP-OCRv6_tiny_det\inference.pdiparams'
$recognizer = Join-Path $ModelCache 'official_models\PP-OCRv6_tiny_rec\inference.pdiparams'
Require-File $detector 'PP-OCRv6 tiny detection model'
Require-File $recognizer 'PP-OCRv6 tiny recognition model'

$output = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $output) {
    throw "Refusing to overwrite an existing isolated runtime: $output"
}
New-Item -ItemType Directory -Path $output | Out-Null

try {
    $runtimeRoot = Join-Path $output 'runtime'
    Expand-Archive -LiteralPath $EmbeddedPythonArchive -DestinationPath $runtimeRoot
    $pthPath = Join-Path $runtimeRoot 'python311._pth'
    Require-File $pthPath 'CPython 3.11 embedded path configuration'
    $pth = Get-Content -LiteralPath $pthPath -Raw -Encoding UTF8
    $pth = $pth -replace '(?m)^#import site\s*$', 'import site'
    if ($pth -notmatch '(?m)^Lib/site-packages$') { $pth = $pth.TrimEnd() + "`r`nLib/site-packages`r`n" }
    Write-Utf8NoBom $pthPath $pth

    $sitePackages = Join-Path $runtimeRoot 'Lib\site-packages'
    New-Item -ItemType Directory -Path $sitePackages -Force | Out-Null
    & $BootstrapPython -m pip install --disable-pip-version-check --no-index --only-binary=:all: `
        --find-links $Wheelhouse --target $sitePackages `
        'paddleocr==3.7.0' 'paddlepaddle==3.3.0' 'paddlex==3.7.2'
    if ($LASTEXITCODE -ne 0) { throw "Offline PaddleOCR dependency installation failed with exit code $LASTEXITCODE." }

    $modelsTarget = Join-Path $output 'model-cache'
    Copy-Item -LiteralPath (Join-Path $ModelCache 'official_models') -Destination (Join-Path $modelsTarget 'official_models') -Recurse
    Copy-Item -LiteralPath $workerSource -Destination (Join-Path $output 'paddle_ocr_worker.py')

    $manifest = Get-Content -LiteralPath $manifestSource -Raw -Encoding UTF8 | ConvertFrom-Json
    $pythonTarget = Join-Path $runtimeRoot 'python.exe'
    $workerTarget = Join-Path $output 'paddle_ocr_worker.py'
    Require-File $pythonTarget 'Isolated PaddleOCR Python executable'
    $manifest.models.treeSha256 = Get-ModelTreeSha256 $modelsTarget
    $manifest.runtime.pythonSha256 = Get-LowerSha256 $pythonTarget
    $manifest.worker.sha256 = Get-LowerSha256 $workerTarget
    $manifest.runtime.healthCheckPassed = $false
    $manifest.runtime | Add-Member -NotePropertyName healthCheckOutput -NotePropertyValue '' -Force
    $manifest.runtime.healthCheckOutput = ''
    $manifestTarget = Join-Path $output 'runtime-manifest.json'
    Write-Utf8NoBom $manifestTarget ($manifest | ConvertTo-Json -Depth 8)

    # Paddle may write native library diagnostics to stderr.  Capture both
    # streams explicitly so PowerShell cannot turn stderr into a host error
    # before the JSON health contract and the worker's exit code are checked.
    $healthStdoutPath = Join-Path $output 'paddle-health.stdout'
    $healthStderrPath = Join-Path $output 'paddle-health.stderr'
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        # Native stderr must be preserved for the worker's own error report;
        # PowerShell's Stop policy would otherwise interrupt this command
        # before its non-zero exit code can be evaluated below.
        $ErrorActionPreference = 'Continue'
        & $pythonTarget $workerTarget --cache-root $modelsTarget --manifest $manifestTarget --health `
            1> $healthStdoutPath 2> $healthStderrPath
        $healthExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    $healthOutput = ((Get-Content -LiteralPath $healthStdoutPath -Raw -ErrorAction SilentlyContinue) +
                     (Get-Content -LiteralPath $healthStderrPath -Raw -ErrorAction SilentlyContinue))
    $health = Read-FinalJsonLine $healthOutput
    if ($healthExitCode -ne 0 -or $null -eq $health -or $health.ok -ne $true -or
        $health.engineId -ne 'paddleocr-ppocrv6-tiny' -or $health.engineVersion -ne '3.7.0' -or
        $health.manifestVerified -ne $true) {
        throw "Isolated PaddleOCR runtime health check failed. Exit=$healthExitCode Output=$healthOutput"
    }
    $manifest.runtime.healthCheckPassed = $true
    $manifest.runtime.healthCheckOutput = ($health | ConvertTo-Json -Compress)
    Write-Utf8NoBom $manifestTarget ($manifest | ConvertTo-Json -Depth 8)
    Remove-Item -LiteralPath $healthStdoutPath, $healthStderrPath -Force -ErrorAction SilentlyContinue
    Write-Host "Prepared isolated PaddleOCR CPU runtime: $output" -ForegroundColor Green
} catch {
    if (-not $KeepFailedOutput -and (Test-Path -LiteralPath $output)) {
        Remove-Item -LiteralPath $output -Recurse -Force
    }
    throw
}
