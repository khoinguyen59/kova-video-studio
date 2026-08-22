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

& python (Join-Path $repoRoot 'scripts\verify_generated_colab_notebooks.py')
if ($LASTEXITCODE -ne 0) {
    throw 'Generated exact-model Colab notebooks are stale or incomplete.'
}

& python (Join-Path $repoRoot 'scripts\verify_colab_model_bindings.py')
if ($LASTEXITCODE -ne 0) {
    throw 'Controller/UI/notebook exact-model bindings are incomplete or stale.'
}

& python (Join-Path $repoRoot 'scripts\test_live_colab_acceptance_contract.py')
if ($LASTEXITCODE -ne 0) {
    throw 'The live Colab acceptance runner contract failed.'
}

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

function Assert-NotContains {
    param([Parameter(Mandatory)][string] $Text, [Parameter(Mandatory)][string] $Needle,
          [Parameter(Mandatory)][string] $Context)
    if ($Text.Contains($Needle)) { throw "$Context must not duplicate '$Needle'." }
}

$features = @(
    @{ id = 'stt'; qml = 'qml/components/stt/SttSettingsPanel.qml'; shell = 'qml/components/stt/SttStudioView.qml'; notebook = 'LA_STUDIO_STT_WHISPER_GPU.ipynb'; qmlNeedle = 'colabNotebookFile'; cudaGuard = 'torch.cuda.is_available()'; capability = 'MODEL_ID'; endpoint = '/v2/jobs/transcriptions'; url = 'LA_STUDIO_COLAB_STT_URL'; token = 'LA_STUDIO_COLAB_STT_TOKEN' },
    @{ id = 'tts'; qml = 'qml/components/tts/TtsSettingsPanel.qml'; shell = 'qml/components/tts/TtsStudioView.qml'; notebook = 'LA_STUDIO_TTS_KOKORO_GPU.ipynb'; qmlNeedle = 'colabNotebookFile'; cudaGuard = 'torch.cuda.is_available()'; capability = '"capability": "tts"'; endpoint = '/v1/audio/speech'; url = 'LA_STUDIO_COLAB_TTS_URL'; token = 'LA_STUDIO_COLAB_TTS_TOKEN' },
    @{ id = 'voice-cloning'; qml = 'qml/components/voicecloning/VoiceSettingsPanel.qml'; shell = 'qml/components/voicecloning/VoiceCloningStudioView.qml'; notebook = 'LA_STUDIO_VOICE_CLONE_OMNIVOICE_GPU.ipynb'; qmlNeedle = 'colabNotebookFile'; cudaGuard = 'torch.cuda.is_available()'; capability = '"capability": "voice-cloning"'; endpoint = '/v2/jobs/profile'; url = 'LA_STUDIO_COLAB_VOICE_CLONE_URL'; token = 'LA_STUDIO_COLAB_VOICE_CLONE_TOKEN' },
    @{ id = 'voice-design'; qml = 'qml/components/voicedesign/VoiceDesignSettingsPanel.qml'; shell = 'qml/components/voicedesign/VoiceDesignStudioView.qml'; notebook = 'LA_STUDIO_VOICE_DESIGN_QWEN3_1_7B_GPU.ipynb'; qmlNeedle = 'colabNotebookFile'; cudaGuard = 'torch.cuda.is_available()'; capability = '"capability": "voice-design"'; endpoint = '/v1/audio/voice_designs'; url = 'LA_STUDIO_COLAB_VOICE_DESIGN_URL'; token = 'LA_STUDIO_COLAB_VOICE_DESIGN_TOKEN' },
    @{ id = 'forced-alignment'; qml = 'qml/components/alignment/AlignmentSetupPanel.qml'; shell = 'qml/components/alignment/AlignmentStudioView.qml'; notebook = 'LA_STUDIO_ALIGNMENT_MMS_ONNX_GPU.ipynb'; qmlNeedle = 'colabNotebookFile'; cudaGuard = 'torch.cuda.is_available()'; capability = '"forced-alignment"'; endpoint = '/v1/audio/alignments'; url = 'LA_STUDIO_COLAB_ALIGNMENT_URL'; token = 'LA_STUDIO_COLAB_ALIGNMENT_TOKEN' },
    # The notebook validates CUDA availability before it downloads the signed
    # worker template.  sherpa_onnx is intentionally imported only inside
    # that downloaded worker, so requiring its version string in the notebook
    # made this surface verifier reject a valid GPU contract.
    @{ id = 'voice-isolation'; qml = 'qml/components/voiceisolator/VoiceIsolatorStudioView.qml'; shell = 'qml/components/voiceisolator/VoiceIsolatorStudioView.qml'; notebook = 'LA_STUDIO_SEPARATION_SPLEETER_2STEMS_GPU.ipynb'; qmlNeedle = 'colabNotebookFile'; cudaGuard = 'torch.cuda.is_available()'; capability = '"voice-isolation"'; endpoint = '/v1/audio/separations'; url = 'LA_STUDIO_COLAB_SEPARATION_URL'; token = 'LA_STUDIO_COLAB_SEPARATION_TOKEN' },
    @{ id = 'translation'; qml = 'qml/components/translation/TranslationStudioView.qml'; shell = 'qml/components/translation/TranslationStudioView.qml'; notebook = 'LA_STUDIO_TRANSLATION_M2M100_418M_GPU.ipynb'; qmlNeedle = 'colabNotebookFile'; cudaGuard = 'torch.cuda.is_available()'; capability = '\"translation\"'; endpoint = '/v1/translations'; url = 'LA_STUDIO_COLAB_TRANSLATION_URL'; token = 'LA_STUDIO_COLAB_TRANSLATION_TOKEN' },
    @{ id = 'chat'; qml = 'qml/components/llm/LlmChatStudioView.qml'; shell = 'qml/components/llm/LlmChatStudioView.qml'; notebook = 'LA_STUDIO_LLM_QWEN3_5_2B_GPU.ipynb'; qmlNeedle = 'colabNotebookFile'; cudaGuard = 'torch.cuda.is_available()'; capability = '\"llm-chat\"'; endpoint = '/v1/chat/completions'; url = 'LA_STUDIO_COLAB_CHAT_URL'; token = 'LA_STUDIO_COLAB_CHAT_TOKEN' }
)

$notebookUrlHelper = Get-SourceText 'qml/components/base/colabNotebookUrls.js'
Assert-Contains $notebookUrlHelper 'colab.research.google.com/github/khoinguyen59/kova-video-studio/blob/main/notebooks/' 'Shared Colab notebook URL helper'
Assert-Contains $notebookUrlHelper 'function forNotebookFile(fileName)' 'Shared Colab notebook URL helper'

# The app and the operational acceptance guides must name the same release
# branch.  A stale guide can make a user open an older notebook manually and
# then appear to hit an exact-model failure even though the desktop selected
# the right current model.
$operationalColabDocs = @(
    'docs/DUBBING_REMOTE_EXECUTION_AUDIT.md',
    'docs/REMOTE_FEATURE_ACCEPTANCE_AUDIT.md',
    'docs/COLAB_EXACT_MODEL_STATUS_0.0.1.1.md',
    'docs/COLAB_IMPLEMENTATION_STATUS_0.0.1.5.md',
    'docs/STT_COLAB_MODEL_AUDIT_0.0.0.9.md'
)
foreach ($docPath in $operationalColabDocs) {
    $doc = Get-SourceText $docPath
    Assert-Contains $doc 'main' "$docPath Colab branch guidance"
    Assert-NotContains $doc 'codex/remote-inference' "$docPath Colab branch guidance"
}

$notebookLink = Get-SourceText 'qml/components/base/ColabNotebookLink.qml'
Assert-Contains $notebookLink 'ColabNotebookUrls.forNotebookFile(notebookFile)' 'Colab notebook link'
Assert-Contains $notebookLink 'Open this notebook in Colab' 'Colab notebook link'
Assert-Contains $notebookLink 'openColabNotebooksDirectory()' 'Colab notebook link'

# Every model-selection entry point must use the one shared URL helper.  This
# prevents a future branch/repository move from silently opening a notebook
# different from the exact one selected by its controller.
$modelSelectionPages = @(
    'qml/pages/SttPage.qml',
    'qml/pages/TtsPage.qml',
    'qml/pages/VoiceCloningPage.qml',
    'qml/pages/VoiceDesignPage.qml',
    'qml/pages/VoiceIsolatorPage.qml',
    'qml/pages/AlignmentPage.qml',
    'qml/pages/TranslationPage.qml',
    'qml/pages/LlmPage.qml'
)
foreach ($pagePath in $modelSelectionPages) {
    $page = Get-SourceText $pagePath
    Assert-Contains $page 'colabNotebookUrls.js" as ColabNotebookUrls' "$pagePath"
    Assert-Contains $page 'ColabNotebookUrls.forNotebookFile' "$pagePath"
    Assert-NotContains $page 'colab.research.google.com/github/khoinguyen59/kova-video-studio' "$pagePath"
}

# Dubbing connects each workflow node through the same asynchronous Colab
# handshake.  Its dialog must remain available while the worker is checking
# and must surface a failed verification; otherwise a wrong-model or expired
# worker appears to have connected and the user has no place to read the
# error or retry.
$dubbingConnectionPanels = @(
    'qml/components/dubbing/DubbingNodeSettingsPanel.qml',
    'qml/components/dubbing/DubbingNodeInspector.qml'
)
foreach ($panelPath in $dubbingConnectionPanels) {
    $panel = Get-SourceText $panelPath
    Assert-Contains $panel 'awaitingVerification' "$panelPath async Colab dialog"
    Assert-Contains $panel 'onVerificationFinished' "$panelPath async Colab dialog"
    Assert-Contains $panel 'disconnectTemporaryWorker()' "$panelPath async Colab dialog"
}

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
    if ($feature.id -eq 'voice-isolation') {
        # The Spleeter notebook deliberately bootstraps two SHA-verified
        # worker templates.  It must therefore be checked as a three-file
        # delivery unit: notebook integrity/materialization, launcher tunnel
        # and worker endpoint/capability.  Checking only the notebook for
        # cloudflared produced a false negative even though the actual
        # launched template owned that dependency.
        $workerTemplatePath = 'notebooks/workers/LA_STUDIO_SEPARATION_SPLEETER_2STEMS_WORKER.py'
        $launcherTemplatePath = 'notebooks/workers/LA_STUDIO_SEPARATION_SPLEETER_2STEMS_LAUNCHER.py'
        Assert-Contains $notebook $workerTemplatePath "$($feature.id) notebook worker template"
        Assert-Contains $notebook $launcherTemplatePath "$($feature.id) notebook launcher template"
        Assert-Contains $notebook 'actual_sha256 != expected_sha256' "$($feature.id) notebook integrity check"
        Assert-Contains $notebook "Path('/content', destination).write_bytes(payload)" "$($feature.id) notebook worker materialization"

        $workerTemplate = Get-SourceText $workerTemplatePath
        $launcherTemplate = Get-SourceText $launcherTemplatePath
        Assert-Contains $launcherTemplate 'cloudflared' "$($feature.id) launcher template"
        Assert-Contains $workerTemplate $feature.capability "$($feature.id) worker template"
        Assert-Contains $workerTemplate $feature.endpoint "$($feature.id) worker template"
        Assert-Contains $launcherTemplate $feature.url "$($feature.id) launcher template"
        Assert-Contains $launcherTemplate $feature.token "$($feature.id) launcher template"
    } else {
        Assert-Contains $notebook 'cloudflared' "$($feature.id) notebook"
        Assert-Contains $notebook $feature.capability "$($feature.id) notebook"
        Assert-Contains $notebook $feature.endpoint "$($feature.id) notebook"
        Assert-Contains $notebook $feature.url "$($feature.id) notebook"
        Assert-Contains $notebook $feature.token "$($feature.id) notebook"
    }
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
    Assert-Contains $source 'async def create_transcription_job(' "stt notebook $($entry.file)"
    Assert-Contains $source 'status_code=202' "stt notebook $($entry.file)"
    Assert-Contains $source 'async def transcription_job_status(' "stt notebook $($entry.file)"
    Assert-Contains $source 'async def cancel_transcription_job(' "stt notebook $($entry.file)"
    Assert-Contains $source 'transcription_jobs' "stt notebook $($entry.file)"
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
    if ($entry.file -eq 'LA_STUDIO_SEPARATION_SPLEETER_2STEMS_GPU.ipynb') {
        # Spleeter is intentionally a signed-template notebook.  The exact
        # model binding and CUDA-only endpoint live in the downloaded worker,
        # whose repository paths and SHA check are part of the notebook.
        Assert-Contains $source 'torch.cuda.is_available()' "voice-isolation notebook $($entry.file)"
        Assert-Contains $source 'LA_STUDIO_SEPARATION_SPLEETER_2STEMS_WORKER.py' "voice-isolation notebook $($entry.file)"
        Assert-Contains $source 'LA_STUDIO_SEPARATION_SPLEETER_2STEMS_LAUNCHER.py' "voice-isolation notebook $($entry.file)"
        Assert-Contains $source 'actual_sha256 != expected_sha256' "voice-isolation notebook $($entry.file)"
        $workerTemplate = Get-SourceText 'notebooks/workers/LA_STUDIO_SEPARATION_SPLEETER_2STEMS_WORKER.py'
        $launcherTemplate = Get-SourceText 'notebooks/workers/LA_STUDIO_SEPARATION_SPLEETER_2STEMS_LAUNCHER.py'
        Assert-Contains $workerTemplate 'require_exact_model(model)' "voice-isolation Spleeter worker template"
        Assert-Contains $workerTemplate 'CUDAExecutionProvider' "voice-isolation Spleeter worker template"
        Assert-Contains $launcherTemplate 'LA_STUDIO_COLAB_SEPARATION_MODEL' "voice-isolation Spleeter launcher template"
    } else {
        Assert-Contains $source 'require_exact_model(model)' "voice-isolation notebook $($entry.file)"
        Assert-Contains $source 'provider=\"cuda\"' "voice-isolation notebook $($entry.file)"
        Assert-Contains $source 'LA_STUDIO_COLAB_SEPARATION_MODEL' "voice-isolation notebook $($entry.file)"
    }
}

$translationNotebooks = @(
    @{ file = 'LA_STUDIO_TRANSLATION_M2M100_418M_GPU.ipynb'; model = 'm2m100-418m' },
    @{ file = 'LA_STUDIO_TRANSLATION_MADLAD400_3B_GPU.ipynb'; model = 'madlad400-3b-mt' },
    @{ file = 'LA_STUDIO_TRANSLATION_HY_MT2_1_8B_GPU.ipynb'; model = 'hy-mt2-1.8b' }
)
foreach ($entry in $translationNotebooks) {
    $path = Join-Path $repoRoot (Join-Path 'notebooks' $entry.file)
    if (-not (Test-Path -LiteralPath $path)) { throw "translation: notebook is missing: $($entry.file)" }
    $source = Get-Content -LiteralPath $path -Raw
    Assert-Contains $source $entry.model "translation notebook $($entry.file)"
    Assert-Contains $source 'require_exact_model(request.model)' "translation notebook $($entry.file)"
    Assert-Contains $source 'LA_STUDIO_COLAB_TRANSLATION_MODEL' "translation notebook $($entry.file)"
}

$chatNotebook = Join-Path $repoRoot 'notebooks/LA_STUDIO_LLM_QWEN3_5_2B_GPU.ipynb'
if (-not (Test-Path -LiteralPath $chatNotebook)) { throw 'chat: exact Qwen3.5 notebook is missing.' }
$chatSource = Get-Content -LiteralPath $chatNotebook -Raw
Assert-Contains $chatSource 'qwen3.5-2b' 'chat notebook'
Assert-Contains $chatSource 'require_exact_model(request.model)' 'chat notebook'
Assert-Contains $chatSource 'LA_STUDIO_COLAB_CHAT_MODEL' 'chat notebook'
Assert-Contains $chatSource 'context_tokens' 'chat notebook'
Assert-Contains $chatSource 'top_k' 'chat notebook'
Assert-Contains $chatSource 'repeat_penalty' 'chat notebook'

Write-Host "Remote feature surface verified: $passed/$($features.Count) direct Colab routes." -ForegroundColor Green
