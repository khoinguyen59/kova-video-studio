param(
    [int]$Width = 1680,
    [int]$Height = 980
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$qmlScene = (Get-Command qmlscene.exe -ErrorAction SilentlyContinue).Source
if (-not $qmlScene) {
    throw 'qmlscene.exe was not found. Install a Qt QML runtime or add it to PATH.'
}

$preview = Join-Path $root 'tools\qml\DubbingLayoutPreview.qml'
if (-not (Test-Path -LiteralPath $preview)) {
    throw "Preview file not found: $preview"
}

& $qmlScene "--geometry=$($Width)x$($Height)" $preview
