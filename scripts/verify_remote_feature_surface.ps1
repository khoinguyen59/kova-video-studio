#Requires -Version 5.1

<#
.SYNOPSIS
    Verifies the checked-in Colab notebook and QML route surface contracts.

.DESCRIPTION
    This is intentionally a static integration gate, not a mock inference
    test. It proves that every direct-GPU feature exposes its matching
    notebook, URL/token controls and an ungated configuration surface before
    a local model is loaded. Live Gateway/Colab preflight remains separate.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

function Get-SourceText {
    param([Parameter(Mandatory)][string] $RelativePath)
    $path = Join-Path $repoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path)) { throw "Required file is missing: $RelativePath" }
    return Get-Content -LiteralPath $path -Raw
}

function Assert-Contains {
    param([Parameter(Mandatory)][string] $Text, [Parameter(Mandatory)][string] $Needle,
          [Parameter(Mandatory)][string] $Context)
    if (-not $Text.Contains($Needle)) { throw "$Context is missing '$Needle'." }
}

$features = @(
    @{ id = 'stt'; qml = 'qml/components/stt/SttSettingsPanel.qml'; shell = 'qml/components/stt/SttStudioView.qml'; notebook = 'LA_STUDIO_SPEECH_GPU.ipynb'; cudaGuard = 'get_cuda_device_count()'; capability = "'stt'"; endpoint = '/v1/audio/transcriptions'; url = 'LA_STUDIO_COLAB_STT_URL'; token = 'LA_STUDIO_COLAB_STT_TOKEN' },
    @{ id = 'tts'; qml = 'qml/components/tts/TtsSettingsPanel.qml'; shell = 'qml/components/tts/TtsStudioView.qml'; notebook = 'LA_STUDIO_VOICE_GPU.ipynb'; cudaGuard = 'torch.cuda.is_available()'; capability = "'tts'"; endpoint = '/v1/audio/speech'; url = 'LA_STUDIO_COLAB_TTS_URL'; token = 'LA_STUDIO_COLAB_TTS_TOKEN' },
    @{ id = 'voice-cloning'; qml = 'qml/components/voicecloning/VoiceSettingsPanel.qml'; shell = 'qml/components/voicecloning/VoiceCloningStudioView.qml'; notebook = 'LA_STUDIO_VOICE_CLONE_GPU.ipynb'; cudaGuard = 'torch.cuda.is_available()'; capability = "'voice-cloning'"; endpoint = '/v2/jobs/profile'; url = 'LA_STUDIO_COLAB_VOICE_CLONE_URL'; token = 'LA_STUDIO_COLAB_VOICE_CLONE_TOKEN' },
    @{ id = 'voice-design'; qml = 'qml/components/voicedesign/VoiceDesignSettingsPanel.qml'; shell = 'qml/components/voicedesign/VoiceDesignStudioView.qml'; notebook = 'LA_STUDIO_VOICE_DESIGN_GPU.ipynb'; cudaGuard = 'torch.cuda.is_available()'; capability = "'voice-design'"; endpoint = '/v1/audio/voice_designs'; url = 'LA_STUDIO_COLAB_VOICE_DESIGN_URL'; token = 'LA_STUDIO_COLAB_VOICE_DESIGN_TOKEN' },
    @{ id = 'forced-alignment'; qml = 'qml/components/alignment/AlignmentSetupPanel.qml'; shell = 'qml/components/alignment/AlignmentStudioView.qml'; notebook = 'LA_STUDIO_ALIGNMENT_GPU.ipynb'; cudaGuard = 'torch.cuda.is_available()'; capability = "'forced-alignment'"; endpoint = '/v1/audio/alignments'; url = 'LA_STUDIO_COLAB_ALIGNMENT_URL'; token = 'LA_STUDIO_COLAB_ALIGNMENT_TOKEN' },
    @{ id = 'voice-isolation'; qml = 'qml/components/voiceisolator/VoiceIsolatorStudioView.qml'; shell = 'qml/components/voiceisolator/VoiceIsolatorStudioView.qml'; notebook = 'LA_STUDIO_SEPARATION_GPU.ipynb'; cudaGuard = 'torch.cuda.is_available()'; capability = "'voice-isolation'"; endpoint = '/v1/audio/separations'; url = 'LA_STUDIO_COLAB_SEPARATION_URL'; token = 'LA_STUDIO_COLAB_SEPARATION_TOKEN' },
    @{ id = 'translation'; qml = 'qml/components/translation/TranslationStudioView.qml'; shell = 'qml/components/translation/TranslationStudioView.qml'; notebook = 'LA_STUDIO_LANGUAGE_GPU.ipynb'; cudaGuard = 'torch.cuda.is_available()'; capability = "'translation'"; endpoint = '/v1/translations'; url = 'LA_STUDIO_LANGUAGE_URL'; token = 'LA_STUDIO_LANGUAGE_TOKEN' },
    @{ id = 'chat'; qml = 'qml/components/llm/LlmChatStudioView.qml'; shell = 'qml/components/llm/LlmChatStudioView.qml'; notebook = 'LA_STUDIO_LANGUAGE_GPU.ipynb'; cudaGuard = 'torch.cuda.is_available()'; capability = "'chat'"; endpoint = '/v1/chat/completions'; url = 'LA_STUDIO_LANGUAGE_URL'; token = 'LA_STUDIO_LANGUAGE_TOKEN' }
)

$notebookLink = Get-SourceText 'qml/components/base/ColabNotebookLink.qml'
Assert-Contains $notebookLink 'colab.research.google.com/github/khoinguyen59/kova-video-studio' 'Colab notebook link'
Assert-Contains $notebookLink 'Open this notebook in Colab' 'Colab notebook link'
Assert-Contains $notebookLink 'openColabNotebooksDirectory()' 'Colab notebook link'

$passed = 0
foreach ($feature in $features) {
    $panel = Get-SourceText $feature.qml
    $shell = Get-SourceText $feature.shell
    $notebookPath = Join-Path $repoRoot (Join-Path 'notebooks' $feature.notebook)
    if (-not (Test-Path -LiteralPath $notebookPath)) { throw "$($feature.id): notebook is missing." }
    $notebook = Get-Content -LiteralPath $notebookPath -Raw

    Assert-Contains $panel $feature.notebook "$($feature.id) panel"
    Assert-Contains $panel 'Worker URL' "$($feature.id) panel"
    Assert-Contains $panel 'Session token' "$($feature.id) panel"
    Assert-Contains $shell 'settingsRequiresReady: false' "$($feature.id) studio shell"
    Assert-Contains $notebook $feature.cudaGuard "$($feature.id) notebook"
    Assert-Contains $notebook 'cloudflared' "$($feature.id) notebook"
    Assert-Contains $notebook $feature.capability "$($feature.id) notebook"
    Assert-Contains $notebook $feature.endpoint "$($feature.id) notebook"
    Assert-Contains $notebook $feature.url "$($feature.id) notebook"
    Assert-Contains $notebook $feature.token "$($feature.id) notebook"
    $passed++
}

Write-Host "Remote feature surface verified: $passed/$($features.Count) direct Colab routes." -ForegroundColor Green
