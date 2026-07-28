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
    @{ id = 'stt'; qml = 'qml/components/stt/SttSettingsPanel.qml'; shell = 'qml/components/stt/SttStudioView.qml'; notebook = 'LA_STUDIO_STT_WHISPER_GPU.ipynb'; qmlNeedle = 'colabNotebookFile'; cudaGuard = 'torch.cuda.is_available()'; capability = 'MODEL_ID'; endpoint = '/v1/audio/transcriptions'; url = 'LA_STUDIO_COLAB_STT_URL'; token = 'LA_STUDIO_COLAB_STT_TOKEN' },
    @{ id = 'tts'; qml = 'qml/components/tts/TtsSettingsPanel.qml'; shell = 'qml/components/tts/TtsStudioView.qml'; notebook = 'LA_STUDIO_TTS_KOKORO_GPU.ipynb'; qmlNeedle = 'colabNotebookFile'; cudaGuard = 'torch.cuda.is_available()'; capability = '"capability": "tts"'; endpoint = '/v1/audio/speech'; url = 'LA_STUDIO_COLAB_TTS_URL'; token = 'LA_STUDIO_COLAB_TTS_TOKEN' },
    @{ id = 'voice-cloning'; qml = 'qml/components/voicecloning/VoiceSettingsPanel.qml'; shell = 'qml/components/voicecloning/VoiceCloningStudioView.qml'; notebook = 'LA_STUDIO_VOICE_CLONE_OMNIVOICE_GPU.ipynb'; qmlNeedle = 'colabNotebookFile'; cudaGuard = 'torch.cuda.is_available()'; capability = '"capability": "voice-cloning"'; endpoint = '/v2/jobs/profile'; url = 'LA_STUDIO_COLAB_VOICE_CLONE_URL'; token = 'LA_STUDIO_COLAB_VOICE_CLONE_TOKEN' },
    @{ id = 'voice-design'; qml = 'qml/components/voicedesign/VoiceDesignSettingsPanel.qml'; shell = 'qml/components/voicedesign/VoiceDesignStudioView.qml'; notebook = 'LA_STUDIO_VOICE_DESIGN_QWEN3_1_7B_GPU.ipynb'; qmlNeedle = 'colabNotebookFile'; cudaGuard = 'torch.cuda.is_available()'; capability = '"capability": "voice-design"'; endpoint = '/v1/audio/voice_designs'; url = 'LA_STUDIO_COLAB_VOICE_DESIGN_URL'; token = 'LA_STUDIO_COLAB_VOICE_DESIGN_TOKEN' },
    @{ id = 'forced-alignment'; qml = 'qml/components/alignment/AlignmentSetupPanel.qml'; shell = 'qml/components/alignment/AlignmentStudioView.qml'; notebook = 'LA_STUDIO_ALIGNMENT_MMS_ONNX_GPU.ipynb'; qmlNeedle = 'colabNotebookFile'; cudaGuard = 'torch.cuda.is_available()'; capability = '"forced-alignment"'; endpoint = '/v1/audio/alignments'; url = 'LA_STUDIO_COLAB_ALIGNMENT_URL'; token = 'LA_STUDIO_COLAB_ALIGNMENT_TOKEN' },
    @{ id = 'voice-isolation'; qml = 'qml/components/voiceisolator/VoiceIsolatorStudioView.qml'; shell = 'qml/components/voiceisolator/VoiceIsolatorStudioView.qml'; notebook = 'LA_STUDIO_SEPARATION_SPLEETER_2STEMS_GPU.ipynb'; qmlNeedle = 'colabNotebookFile'; cudaGuard = 'sherpa_onnx.__version__'; capability = '"voice-isolation"'; endpoint = '/v1/audio/separations'; url = 'LA_STUDIO_COLAB_SEPARATION_URL'; token = 'LA_STUDIO_COLAB_SEPARATION_TOKEN' },
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

    $qmlNeedle = if ($feature.qmlNeedle) { $feature.qmlNeedle } else { $feature.notebook }
    Assert-Contains $panel $qmlNeedle "$($feature.id) panel"
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

$sttNotebooks = @(
    @{ file = 'LA_STUDIO_STT_NEMOTRON_3_5_0_6B_GPU.ipynb'; model = 'nemotron-3.5-asr-streaming-0.6b' },
    @{ file = 'LA_STUDIO_STT_WHISPER_GPU.ipynb'; model = 'whisper.cpp' },
    @{ file = 'LA_STUDIO_STT_QWEN3_ASR_0_6B_GPU.ipynb'; model = 'qwen3-asr-0.6b' },
    @{ file = 'LA_STUDIO_STT_QWEN3_ASR_1_7B_GPU.ipynb'; model = 'qwen3-asr-1.7b' }
)
foreach ($entry in $sttNotebooks) {
    $path = Join-Path $repoRoot (Join-Path 'notebooks' $entry.file)
    if (-not (Test-Path -LiteralPath $path)) { throw "stt: notebook is missing: $($entry.file)" }
    $source = Get-Content -LiteralPath $path -Raw
    Assert-Contains $source $entry.model "stt notebook $($entry.file)"
    Assert-Contains $source 'if model.strip().lower() != MODEL_ID' "stt notebook $($entry.file)"
    Assert-Contains $source 'LA_STUDIO_COLAB_STT_MODEL' "stt notebook $($entry.file)"
}

$ttsNotebooks = @(
    @{ file = 'LA_STUDIO_TTS_KOKORO_GPU.ipynb'; model = 'kokoro' },
    @{ file = 'LA_STUDIO_TTS_KOKORO_VIETNAMESE_GPU.ipynb'; model = 'kokoro-vietnamese' },
    @{ file = 'LA_STUDIO_TTS_OMNIVOICE_GPU.ipynb'; model = 'omnivoice' },
    @{ file = 'LA_STUDIO_TTS_QWEN3_CUSTOMVOICE_1_7B_GPU.ipynb'; model = 'qwen3-tts-1.7b-customvoice' },
    @{ file = 'LA_STUDIO_TTS_VIBEVOICE_0_5B_GPU.ipynb'; model = 'vibevoice' },
    @{ file = 'LA_STUDIO_TTS_VIENEU_V2_TURBO_GPU.ipynb'; model = 'vieneu-tts-v2-turbo' },
    @{ file = 'LA_STUDIO_TTS_VIENEU_V3_TURBO_GPU.ipynb'; model = 'vieneu-tts-v3-turbo' },
    @{ file = 'LA_STUDIO_TTS_VOXCPM2_GPU.ipynb'; model = 'voxcpm2' }
)
foreach ($entry in $ttsNotebooks) {
    $path = Join-Path $repoRoot (Join-Path 'notebooks' $entry.file)
    if (-not (Test-Path -LiteralPath $path)) { throw "tts: notebook is missing: $($entry.file)" }
    $source = Get-Content -LiteralPath $path -Raw
    Assert-Contains $source $entry.model "tts notebook $($entry.file)"
    Assert-Contains $source 'if request.model.strip().lower() != MODEL_ID' "tts notebook $($entry.file)"
    Assert-Contains $source 'LA_STUDIO_COLAB_TTS_MODEL' "tts notebook $($entry.file)"
}

$voiceCloneNotebooks = @(
    @{ file = 'LA_STUDIO_VOICE_CLONE_OMNIVOICE_GPU.ipynb'; model = 'omnivoice' },
    @{ file = 'LA_STUDIO_VOICE_CLONE_QWEN3_BASE_0_6B_GPU.ipynb'; model = 'qwen3-tts-0.6b-base' },
    @{ file = 'LA_STUDIO_VOICE_CLONE_QWEN3_BASE_1_7B_GPU.ipynb'; model = 'qwen3-tts-1.7b-base' },
    @{ file = 'LA_STUDIO_VOICE_CLONE_VIENEU_V2_TURBO_GPU.ipynb'; model = 'vieneu-tts-v2-turbo' },
    @{ file = 'LA_STUDIO_VOICE_CLONE_VIENEU_V3_TURBO_GPU.ipynb'; model = 'vieneu-tts-v3-turbo' },
    @{ file = 'LA_STUDIO_VOICE_CLONE_VOXCPM2_GPU.ipynb'; model = 'voxcpm2' }
)
foreach ($entry in $voiceCloneNotebooks) {
    $path = Join-Path $repoRoot (Join-Path 'notebooks' $entry.file)
    if (-not (Test-Path -LiteralPath $path)) { throw "voice-cloning: notebook is missing: $($entry.file)" }
    $source = Get-Content -LiteralPath $path -Raw
    Assert-Contains $source $entry.model "voice-cloning notebook $($entry.file)"
    Assert-Contains $source 'require_exact_model' "voice-cloning notebook $($entry.file)"
    Assert-Contains $source 'LA_STUDIO_COLAB_VOICE_CLONE_MODEL' "voice-cloning notebook $($entry.file)"
}

$voiceDesignNotebooks = @(
    @{ file = 'LA_STUDIO_VOICE_DESIGN_OMNIVOICE_GPU.ipynb'; model = 'omnivoice' },
    @{ file = 'LA_STUDIO_VOICE_DESIGN_QWEN3_1_7B_GPU.ipynb'; model = 'qwen3-tts-1.7b-voicedesign' },
    @{ file = 'LA_STUDIO_VOICE_DESIGN_VOXCPM2_GPU.ipynb'; model = 'voxcpm2' }
)
foreach ($entry in $voiceDesignNotebooks) {
    $path = Join-Path $repoRoot (Join-Path 'notebooks' $entry.file)
    if (-not (Test-Path -LiteralPath $path)) { throw "voice-design: notebook is missing: $($entry.file)" }
    $source = Get-Content -LiteralPath $path -Raw
    Assert-Contains $source $entry.model "voice-design notebook $($entry.file)"
    Assert-Contains $source 'if request.model.strip().lower() != MODEL_ID' "voice-design notebook $($entry.file)"
    Assert-Contains $source 'LA_STUDIO_COLAB_VOICE_DESIGN_MODEL' "voice-design notebook $($entry.file)"
}

$alignmentNotebooks = @(
    @{ file = 'LA_STUDIO_ALIGNMENT_WAV2VEC2_ZH_GPU.ipynb'; model = 'wav2vec2-aligner-zh' },
    @{ file = 'LA_STUDIO_ALIGNMENT_CANARY_CTC_GPU.ipynb'; model = 'canary-ctc-aligner' },
    @{ file = 'LA_STUDIO_ALIGNMENT_MMS_ONNX_GPU.ipynb'; model = 'mms-forced-aligner-onnx' },
    @{ file = 'LA_STUDIO_ALIGNMENT_QWEN3_0_6B_GPU.ipynb'; model = 'qwen3-forced-aligner-0.6b' }
)
foreach ($entry in $alignmentNotebooks) {
    $path = Join-Path $repoRoot (Join-Path 'notebooks' $entry.file)
    if (-not (Test-Path -LiteralPath $path)) { throw "forced-alignment: notebook is missing: $($entry.file)" }
    $source = Get-Content -LiteralPath $path -Raw
    Assert-Contains $source $entry.model "forced-alignment notebook $($entry.file)"
    Assert-Contains $source 'require_exact_model(model)' "forced-alignment notebook $($entry.file)"
    Assert-Contains $source 'LA_STUDIO_COLAB_ALIGNMENT_MODEL' "forced-alignment notebook $($entry.file)"
}

$separationNotebooks = @(
    @{ file = 'LA_STUDIO_SEPARATION_SPLEETER_2STEMS_GPU.ipynb'; model = 'sherpa-onnx-spleeter-2stems-fp16' },
    @{ file = 'LA_STUDIO_SEPARATION_UVR_VOCALS_GPU.ipynb'; model = 'sherpa-onnx-uvr-vocals-ft' }
)
foreach ($entry in $separationNotebooks) {
    $path = Join-Path $repoRoot (Join-Path 'notebooks' $entry.file)
    if (-not (Test-Path -LiteralPath $path)) { throw "voice-isolation: notebook is missing: $($entry.file)" }
    $source = Get-Content -LiteralPath $path -Raw
    Assert-Contains $source $entry.model "voice-isolation notebook $($entry.file)"
    Assert-Contains $source 'require_exact_model(model)' "voice-isolation notebook $($entry.file)"
    Assert-Contains $source 'provider=\"cuda\"' "voice-isolation notebook $($entry.file)"
    Assert-Contains $source 'LA_STUDIO_COLAB_SEPARATION_MODEL' "voice-isolation notebook $($entry.file)"
}

Write-Host "Remote feature surface verified: $passed/$($features.Count) direct Colab routes." -ForegroundColor Green
