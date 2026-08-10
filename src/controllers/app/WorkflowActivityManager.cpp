#include "controllers/app/WorkflowActivityManager.h"

#include "IModelSession.h"
#include "ModelSessionRegistry.h"
#include "controllers/alignment/AlignmentExecutionService.h"
#include "controllers/alignment/ColabAlignmentController.h"
#include "controllers/dubbing/DubbingController.h"
#include "controllers/llm/LlmChatController.h"
#include "controllers/separation/ColabVoiceIsolatorController.h"
#include "controllers/separation/VoiceIsolatorController.h"
#include "controllers/separation/VoiceCloneReferenceIsolatorController.h"
#include "controllers/subtitles/SubtitleOcrController.h"
#include "controllers/translation/TranslationController.h"
#include "controllers/tts/ColabTtsController.h"
#include "controllers/tts/ColabVoiceCloneController.h"
#include "controllers/tts/ColabVoiceDesignController.h"
#include "controllers/tts/GatewayTtsController.h"
#include "SttSessionController.h"
#include "tts/TtsEngine.h"

#include <QFileInfo>
#include <QTimer>

namespace LAStudio {

namespace {

void addExecutionDetails(QVariantMap *workflow, const QString &route,
                         const QString &model = {}, const QString &variant = {},
                         const QString &error = {})
{
    if (!workflow) return;
    workflow->insert(QStringLiteral("executionRoute"), route);
    workflow->insert(QStringLiteral("model"), model);
    workflow->insert(QStringLiteral("variant"), variant);
    workflow->insert(QStringLiteral("error"), error);
}

} // namespace

WorkflowActivityManager::WorkflowActivityManager(ModelSessionRegistry *sessionRegistry,
                                             TtsEngine *tts,
                                             SttSessionController *sttSession,
                                             AlignmentExecutionService *alignment,
                                             DubbingController *dubbing,
                                             GatewayTtsController *gatewayTts,
                                             ColabTtsController *colabTts,
                                             ColabVoiceCloneController *colabVoiceClone,
                                             ColabVoiceDesignController *colabVoiceDesign,
                                             ColabAlignmentController *colabAlignment,
                                             VoiceIsolatorController *voiceIsolator,
                                             ColabVoiceIsolatorController *colabVoiceIsolator,
                                             VoiceCloneReferenceIsolatorController *voiceCloneReferenceIsolator,
                                             TranslationController *translation,
                                             SubtitleOcrController *subtitleOcr,
                                             LlmChatController *llmChat,
                                             QObject *parent)
    : QObject(parent)
    , m_sessionRegistry(sessionRegistry)
    , m_tts(tts)
    , m_sttSession(sttSession)
    , m_alignment(alignment)
    , m_dubbing(dubbing)
    , m_gatewayTts(gatewayTts)
    , m_colabTts(colabTts)
    , m_colabVoiceClone(colabVoiceClone)
    , m_colabVoiceDesign(colabVoiceDesign)
    , m_colabAlignment(colabAlignment)
    , m_voiceIsolator(voiceIsolator)
    , m_colabVoiceIsolator(colabVoiceIsolator)
    , m_voiceCloneReferenceIsolator(voiceCloneReferenceIsolator)
    , m_translation(translation)
    , m_subtitleOcr(subtitleOcr)
    , m_llmChat(llmChat)
{
    if (m_sessionRegistry) {
        for (IModelSession *session : m_sessionRegistry->sessions()) {
            if (!session) {
                continue;
            }
            connect(session, &IModelSession::stateChanged, this, &WorkflowActivityManager::refresh);
            connect(session, &IModelSession::activeConfigurationChanged, this, &WorkflowActivityManager::refresh);
            connect(session, &IModelSession::activeSignatureChanged, this, &WorkflowActivityManager::refresh);
        }
    }

    if (m_tts) {
        connect(m_tts, &TtsEngine::processingChanged, this, &WorkflowActivityManager::refresh);
        connect(m_tts, &TtsEngine::stateChanged, this, &WorkflowActivityManager::refresh);
        connect(m_tts, &TtsEngine::generationProgressChanged, this, &WorkflowActivityManager::refresh);
        connect(m_tts, &TtsEngine::lastGenerationModeChanged, this, &WorkflowActivityManager::refresh);
        connect(m_tts, &TtsEngine::cpuUsageChanged, this, &WorkflowActivityManager::refresh);
        connect(m_tts, &TtsEngine::memoryUsageChanged, this, &WorkflowActivityManager::refresh);
    }

    if (m_sttSession) {
        connect(m_sttSession, &SttSessionController::processingChanged, this, &WorkflowActivityManager::refresh);
        connect(m_sttSession, &SttSessionController::progressChanged, this, &WorkflowActivityManager::refresh);
    }

    if (m_alignment) {
        connect(m_alignment, &AlignmentExecutionService::stateChanged, this, &WorkflowActivityManager::refresh);
        connect(m_alignment, &AlignmentExecutionService::completed, this, &WorkflowActivityManager::refresh);
        connect(m_alignment, &AlignmentExecutionService::failed, this, &WorkflowActivityManager::refresh);
    }
    if (m_dubbing) {
        connect(m_dubbing, &DubbingController::processingChanged, this, &WorkflowActivityManager::refresh);
        // Automatic setup changes its selected stage and measured-download
        // state through workflowChanged.  Refresh immediately instead of
        // waiting for the one-second elapsed timer to repaint the Activity
        // card with a stale generic "model-setup" label.
        connect(m_dubbing, &DubbingController::workflowChanged, this, &WorkflowActivityManager::refresh);
    }
    if (m_gatewayTts) {
        connect(m_gatewayTts, &GatewayTtsController::processingChanged, this, &WorkflowActivityManager::refresh);
        connect(m_gatewayTts, &GatewayTtsController::progressChanged, this, &WorkflowActivityManager::refresh);
    }
    if (m_colabTts) {
        connect(m_colabTts, &ColabTtsController::processingChanged, this, &WorkflowActivityManager::refresh);
        connect(m_colabTts, &ColabTtsController::progressChanged, this, &WorkflowActivityManager::refresh);
    }
    if (m_colabVoiceClone) {
        connect(m_colabVoiceClone, &ColabVoiceCloneController::processingChanged, this, &WorkflowActivityManager::refresh);
        connect(m_colabVoiceClone, &ColabVoiceCloneController::progressChanged, this, &WorkflowActivityManager::refresh);
    }
    if (m_colabVoiceDesign) {
        connect(m_colabVoiceDesign, &ColabVoiceDesignController::processingChanged, this, &WorkflowActivityManager::refresh);
        connect(m_colabVoiceDesign, &ColabVoiceDesignController::progressChanged, this, &WorkflowActivityManager::refresh);
    }
    if (m_colabAlignment)
        connect(m_colabAlignment, &ColabAlignmentController::stateChanged, this, &WorkflowActivityManager::refresh);
    if (m_voiceIsolator)
        connect(m_voiceIsolator, &VoiceIsolatorController::stateChanged, this, &WorkflowActivityManager::refresh);
    if (m_colabVoiceIsolator)
        connect(m_colabVoiceIsolator, &ColabVoiceIsolatorController::stateChanged, this, &WorkflowActivityManager::refresh);
    if (m_voiceCloneReferenceIsolator)
        connect(m_voiceCloneReferenceIsolator, &VoiceCloneReferenceIsolatorController::stateChanged,
                this, &WorkflowActivityManager::refresh);
    if (m_translation)
        connect(m_translation, &TranslationController::processingChanged, this, &WorkflowActivityManager::refresh);
    if (m_subtitleOcr) {
        connect(m_subtitleOcr, &SubtitleOcrController::processingChanged, this, &WorkflowActivityManager::refresh);
        connect(m_subtitleOcr, &SubtitleOcrController::progressChanged, this, &WorkflowActivityManager::refresh);
        connect(m_subtitleOcr, &SubtitleOcrController::sourceImportChanged, this, &WorkflowActivityManager::refresh);
    }
    if (m_llmChat)
        connect(m_llmChat, &LlmChatController::generatingChanged, this, &WorkflowActivityManager::refresh);

    auto *elapsedTimer = new QTimer(this);
    elapsedTimer->setInterval(1000);
    connect(elapsedTimer, &QTimer::timeout, this, [this]() {
        if (hasActiveWorkflows()) {
            emit workflowsChanged();
        }
    });
    elapsedTimer->start();
}

QVariantList WorkflowActivityManager::activeWorkflows() const
{
    QVariantList workflows;

    const QVariantMap tts = ttsWorkflow();
    if (!tts.isEmpty()) {
        workflows.append(tts);
    }

    const QVariantMap stt = sttWorkflow();
    if (!stt.isEmpty()) {
        workflows.append(stt);
    }

    const QVariantMap alignment = alignmentWorkflow();
    if (!alignment.isEmpty()) {
        workflows.append(alignment);
    }

    const QVariantMap dubbing = dubbingWorkflow();
    if (!dubbing.isEmpty()) {
        workflows.append(dubbing);
    }

    const QList<QVariantMap> directFeatureWorkflows{
        gatewayTtsWorkflow(), colabTtsWorkflow(), voiceCloneWorkflow(),
        voiceDesignWorkflow(), colabAlignmentWorkflow(), localVoiceIsolationWorkflow(),
        colabVoiceIsolationWorkflow(), voiceCloneReferenceIsolationWorkflow(), translationWorkflow(), subtitleOcrWorkflow(),
        llmChatWorkflow()
    };
    for (const QVariantMap &workflow : directFeatureWorkflows) {
        if (!workflow.isEmpty())
            workflows.append(workflow);
    }

    updateActiveStartTimes(workflows);
    return workflows;
}

QVariantList WorkflowActivityManager::activeSessions() const
{
    QVariantList sessions;
    if (!m_sessionRegistry) {
        return sessions;
    }

    for (IModelSession *session : m_sessionRegistry->sessions()) {
        const QVariantList items = sessionItems(session);
        for (const QVariant &item : items) {
            sessions.append(item);
        }
    }

    return sessions;
}

int WorkflowActivityManager::activeCount() const
{
    return runningCount();
}

int WorkflowActivityManager::sessionCount() const
{
    return activeSessions().size();
}

int WorkflowActivityManager::runningCount() const
{
    return activeWorkflows().size();
}

void WorkflowActivityManager::stopWorkflow(const QString &id)
{
    if (id == QStringLiteral("tts-active")) {
        if (m_tts && m_tts->isProcessing()) {
            m_stoppingIds.insert(id);
            m_tts->cancelProcessing();
            refresh();
        }
        return;
    }

    if (id == QStringLiteral("stt-active")) {
        if (m_sttSession && m_sttSession->processing()) {
            m_stoppingIds.insert(id);
            m_sttSession->cancelProcessing();
            refresh();
        }
        return;
    }

    if (id == QStringLiteral("alignment-active")) {
        if (m_alignment && m_alignment->processing()) {
            m_stoppingIds.insert(id);
            m_alignment->cancel();
            refresh();
        }
    }

    if (id == QStringLiteral("dubbing-active")) {
        if (m_dubbing && m_dubbing->processing()) {
            m_stoppingIds.insert(id);
            m_dubbing->cancelProcessing();
            refresh();
        }
    }

    if (id == QStringLiteral("gateway-tts-active") && m_gatewayTts) {
        m_stoppingIds.insert(id);
        m_gatewayTts->cancelProcessing();
        refresh();
        return;
    }
    if (id == QStringLiteral("colab-tts-active") && m_colabTts) {
        m_stoppingIds.insert(id);
        m_colabTts->cancelProcessing();
        refresh();
        return;
    }
    if (id == QStringLiteral("voice-clone-active") && m_colabVoiceClone) {
        m_stoppingIds.insert(id);
        m_colabVoiceClone->cancelProcessing();
        refresh();
        return;
    }
    if (id == QStringLiteral("voice-design-active") && m_colabVoiceDesign) {
        m_stoppingIds.insert(id);
        m_colabVoiceDesign->cancelProcessing();
        refresh();
        return;
    }
    if (id == QStringLiteral("colab-alignment-active") && m_colabAlignment) {
        m_stoppingIds.insert(id);
        m_colabAlignment->cancel();
        refresh();
        return;
    }
    if (id == QStringLiteral("voice-isolation-active") && m_voiceIsolator) {
        m_stoppingIds.insert(id);
        m_voiceIsolator->cancel();
        refresh();
        return;
    }
    if (id == QStringLiteral("colab-voice-isolation-active") && m_colabVoiceIsolator) {
        m_stoppingIds.insert(id);
        m_colabVoiceIsolator->cancel();
        refresh();
        return;
    }
    if (id == QStringLiteral("voice-clone-reference-isolation-active") && m_voiceCloneReferenceIsolator) {
        m_stoppingIds.insert(id);
        m_voiceCloneReferenceIsolator->cancel();
        refresh();
        return;
    }
    if (id == QStringLiteral("translation-active") && m_translation) {
        m_stoppingIds.insert(id);
        m_translation->cancel();
        refresh();
        return;
    }
    if (id == QStringLiteral("subtitle-ocr-active") && m_subtitleOcr) {
        m_stoppingIds.insert(id);
        if (m_subtitleOcr->sourceImporting()) m_subtitleOcr->cancelSourceImport();
        else m_subtitleOcr->cancel();
        refresh();
        return;
    }
    if (id == QStringLiteral("llm-chat-active") && m_llmChat) {
        m_stoppingIds.insert(id);
        m_llmChat->stopGeneration();
        refresh();
    }
}

void WorkflowActivityManager::openWorkflow(const QString &id)
{
    if (id == QStringLiteral("stt-active")) {
        emit openRequested(studioRouteForCapability(QStringLiteral("stt")));
        return;
    }

    if (id == QStringLiteral("alignment-active")) {
        emit openRequested(studioRouteForCapability(QStringLiteral("forced-alignment")));
        return;
    }

    if (id == QStringLiteral("dubbing-active")) {
        emit openRequested(QStringLiteral("studio-dubbing"));
        return;
    }

    if (id == QStringLiteral("gateway-tts-active") || id == QStringLiteral("colab-tts-active")) {
        emit openRequested(studioRouteForCapability(QStringLiteral("tts")));
        return;
    }
    if (id == QStringLiteral("voice-clone-active")) {
        emit openRequested(studioRouteForCapability(QStringLiteral("voice-cloning")));
        return;
    }
    if (id == QStringLiteral("voice-design-active")) {
        emit openRequested(studioRouteForCapability(QStringLiteral("voice-design")));
        return;
    }
    if (id == QStringLiteral("colab-alignment-active")) {
        emit openRequested(studioRouteForCapability(QStringLiteral("forced-alignment")));
        return;
    }
    if (id == QStringLiteral("voice-isolation-active") || id == QStringLiteral("colab-voice-isolation-active")) {
        emit openRequested(studioRouteForCapability(QStringLiteral("voice-isolation")));
        return;
    }
    if (id == QStringLiteral("voice-clone-reference-isolation-active")) {
        emit openRequested(studioRouteForCapability(QStringLiteral("voice-cloning")));
        return;
    }
    if (id == QStringLiteral("translation-active")) {
        emit openRequested(studioRouteForCapability(QStringLiteral("translation")));
        return;
    }
    if (id == QStringLiteral("subtitle-ocr-active")) {
        emit openRequested(studioRouteForCapability(QStringLiteral("subtitle-ocr")));
        return;
    }
    if (id == QStringLiteral("llm-chat-active")) {
        emit openRequested(studioRouteForCapability(QStringLiteral("llm-chat")));
        return;
    }

    if (id.startsWith(QStringLiteral("session:"))) {
        const QString payload = id.mid(QStringLiteral("session:").size());
        const QString capabilityId = payload.section(QLatin1Char(':'), 0, 0);
        emit openRequested(studioRouteForCapability(capabilityId));
        return;
    }

    if (id == QStringLiteral("tts-active") && m_tts) {
        const QString mode = m_tts->lastGenerationMode();
        if (mode == QStringLiteral("voice-cloning")) {
            emit openRequested(studioRouteForCapability(QStringLiteral("voice-cloning")));
        } else if (mode == QStringLiteral("voice-design")) {
            emit openRequested(studioRouteForCapability(QStringLiteral("voice-design")));
        } else {
            emit openRequested(studioRouteForCapability(QStringLiteral("tts")));
        }
    }
}

void WorkflowActivityManager::openStudioRoute(const QString &routeId)
{
    const QString normalized = routeId.trimmed();
    // The Dubbing entry gate needs a real, explicit Leave action.  "welcome"
    // is the only non-studio route this controller exposes; arbitrary route
    // IDs remain rejected.
    if (normalized != QStringLiteral("welcome")
        && !normalized.startsWith(QStringLiteral("studio-"))) return;
    emit openRequested(normalized);
}

void WorkflowActivityManager::openVoiceCloningStudio()
{
    emit openRequested(studioRouteForCapability(QStringLiteral("voice-cloning")));
}

void WorkflowActivityManager::refresh()
{
    emit workflowsChanged();
}

QVariantMap WorkflowActivityManager::ttsWorkflow() const
{
    if (!m_tts || !m_tts->isProcessing()) {
        return {};
    }

    const QString mode = m_tts->lastGenerationMode();
    QString routeId = studioRouteForCapability(QStringLiteral("tts"));
    QString iconName = QStringLiteral("volume");
    if (mode == QStringLiteral("voice-cloning")) {
        routeId = studioRouteForCapability(QStringLiteral("voice-cloning"));
        iconName = QStringLiteral("mic");
    } else if (mode == QStringLiteral("voice-design")) {
        routeId = studioRouteForCapability(QStringLiteral("voice-design"));
        iconName = QStringLiteral("spark");
    }

    return makeWorkflow(QStringLiteral("tts-active"),
                        mode.isEmpty() ? QStringLiteral("tts") : mode,
                        ttsTitleForMode(mode),
                        routeId,
                        iconName,
                        m_tts->generationProgress(),
                        m_tts->generationProgressEstimated(),
                        m_tts->generationProgressLabel(),
                        true);
}

QVariantList WorkflowActivityManager::sessionItems(IModelSession *session) const
{
    QVariantList items;
    if (!session) {
        return items;
    }

    const QString activeSignature = session->activeSignature();
    for (const SessionConfiguration &config : session->loadedConfigurations()) {
        const QString capabilityId = routeForCapability(config.capabilityId);
        const QString title = config.familyConfig
            .value(QStringLiteral("title"),
                   config.familyConfig.value(QStringLiteral("name"),
                       config.selection.familyId.isEmpty()
                           ? fallbackTitleForCapability(capabilityId)
                           : config.selection.familyId))
            .toString();
        const QString runtimeId = config.selection.runtimeId;
        const QString runtimeVersion = config.selection.runtimeVersion;
        const QString runtimeLabel = runtimeVersion.isEmpty()
            ? runtimeId
            : QStringLiteral("%1 %2").arg(runtimeId, runtimeVersion);

        QVariantList files;
        for (const QString &path : config.resolvedModelPaths) {
            QVariantMap file;
            file.insert(QStringLiteral("name"), QFileInfo(path).fileName());
            file.insert(QStringLiteral("path"), path);
            files.append(file);
        }

        const bool isTtsShared = capabilityId == QStringLiteral("tts") ||
                                 capabilityId == QStringLiteral("voice-cloning") ||
                                 capabilityId == QStringLiteral("voice-design");
        const bool isDefault = config.signature == activeSignature;
        const QString status = isDefault ? stateLabel(static_cast<int>(session->state())) : QStringLiteral("Ready");

        QVariantMap item;
        item.insert(QStringLiteral("id"), QStringLiteral("session:%1:%2").arg(capabilityId, config.signature));
        item.insert(QStringLiteral("capabilityId"), capabilityId);
        item.insert(QStringLiteral("title"), title);
        item.insert(QStringLiteral("routeId"), studioRouteForCapability(capabilityId));
        item.insert(QStringLiteral("iconName"), iconForCapability(capabilityId));
        item.insert(QStringLiteral("status"), status);
        item.insert(QStringLiteral("statusLabel"), isDefault ? QStringLiteral("Default") : QStringLiteral("Loaded"));
        item.insert(QStringLiteral("runtime"), runtimeLabel);
        item.insert(QStringLiteral("modelFileCount"), config.resolvedModelPaths.size());
        item.insert(QStringLiteral("modelFiles"), files);
        item.insert(QStringLiteral("active"), isDefault);
        item.insert(QStringLiteral("loaded"), true);
        item.insert(QStringLiteral("cpuUsage"), isDefault && isTtsShared && m_tts ? m_tts->cpuUsage() : 0.0);
        item.insert(QStringLiteral("ramUsage"), isDefault && isTtsShared && m_tts ? m_tts->estimatedRamUsage() : QString());
        item.insert(QStringLiteral("vramUsage"), isDefault && isTtsShared && m_tts ? m_tts->estimatedVramUsage() : QString());
        item.insert(QStringLiteral("hardwareRealtime"), isDefault && isTtsShared);
        items.append(item);
    }

    return items;
}

QVariantMap WorkflowActivityManager::sttWorkflow() const
{
    if (!m_sttSession || !m_sttSession->processing()) {
        return {};
    }

    const bool hasMeasuredProgress = m_sttSession->progressAvailable();
    QVariantMap workflow = makeWorkflow(QStringLiteral("stt-active"),
                        QStringLiteral("stt"),
                        QStringLiteral("Transcribing audio"),
                        studioRouteForCapability(QStringLiteral("stt")),
                        QStringLiteral("waves"),
                        m_sttSession->progress(),
                        !hasMeasuredProgress,
                        QStringLiteral("Speech to text"),
                        true);
    workflow.insert(QStringLiteral("progressAvailable"), hasMeasuredProgress);
    const QString route = m_sttSession->colabActive() ? QStringLiteral("Direct Colab GPU")
        : (m_sttSession->gatewayActive() ? QStringLiteral("API Gateway")
                                         : QStringLiteral("Local CPU"));
    const QString model = m_sttSession->colabActive() ? m_sttSession->colabModel()
        : (m_sttSession->gatewayActive() ? m_sttSession->gatewayModel() : QString());
    addExecutionDetails(&workflow, route, model,
                        m_sttSession->colabActive() ? QStringLiteral("fixed") : QString(),
                        m_sttSession->inputError());
    return workflow;
}

QVariantMap WorkflowActivityManager::alignmentWorkflow() const
{
    if (!m_alignment || !m_alignment->processing()) {
        return {};
    }

    QVariantMap workflow = makeWorkflow(QStringLiteral("alignment-active"),
                        QStringLiteral("forced-alignment"),
                        QStringLiteral("Aligning transcript"),
                        studioRouteForCapability(QStringLiteral("forced-alignment")),
                        QStringLiteral("sliders"),
                        0,
                        true,
                        m_alignment->statusText().isEmpty() ? QStringLiteral("Alignment") : m_alignment->statusText(),
                        true);
    // The local aligner exposes state but no completed-unit counter.
    workflow.insert(QStringLiteral("progressAvailable"), false);
    addExecutionDetails(&workflow, QStringLiteral("Local CPU"));
    return workflow;
}

QVariantMap WorkflowActivityManager::dubbingWorkflow() const
{
    if (!m_dubbing || !m_dubbing->processing()) return {};
    const QVariantMap stageInfo = m_dubbing->activityStageInfo();
    const QString stageTitle = stageInfo.value(QStringLiteral("title"),
                                               QStringLiteral("Dubbing")).toString();
    const int stageIndex = stageInfo.value(QStringLiteral("index")).toInt();
    const int stageCount = stageInfo.value(QStringLiteral("count")).toInt();
    QString stageLabel = stageTitle;
    if (stageIndex > 0 && stageCount > 0) {
        stageLabel += QStringLiteral(" (%1/%2)").arg(stageIndex).arg(stageCount);
    }
    const QString setupStatus = stageInfo.value(QStringLiteral("status")).toString().trimmed();
    if (!setupStatus.isEmpty() && setupStatus != stageTitle) {
        stageLabel += QStringLiteral(" — %1").arg(setupStatus);
    }
    // A ready Colab separation job still has potentially large WAV artifacts
    // to download. Its byte counter is a percentage of the current artifact,
    // never a fabricated percentage of the whole workflow.
    const QVariantMap artifactTransfer = stageInfo.value(QStringLiteral("artifactTransfer")).toMap();
    const bool transferProgressAvailable = artifactTransfer.value(QStringLiteral("available")).toBool();
    const bool useArtifactTransferProgress = !artifactTransfer.isEmpty();
    const int visibleProgress = useArtifactTransferProgress
        ? artifactTransfer.value(QStringLiteral("percent")).toInt()
        : m_dubbing->progress();
    const bool visibleProgressAvailable = useArtifactTransferProgress
        ? transferProgressAvailable : m_dubbing->progressAvailable();
    QVariantMap workflow = makeWorkflow(QStringLiteral("dubbing-active"),
                        QStringLiteral("dubbing"),
                        QStringLiteral("Dubbing — %1").arg(stageTitle),
                        QStringLiteral("studio-dubbing"),
                        QStringLiteral("waves"),
                        visibleProgress,
                        !visibleProgressAvailable,
                        stageLabel,
                        true);
    workflow.insert(QStringLiteral("progressAvailable"), visibleProgressAvailable);
    if (useArtifactTransferProgress) {
        const QString artifact = artifactTransfer.value(QStringLiteral("artifact")).toString().trimmed();
        workflow.insert(QStringLiteral("progressScope"), QStringLiteral("artifact"));
        workflow.insert(QStringLiteral("artifact"), artifact);
        // This byte counter belongs only to the file currently being
        // transferred, never to the entire multi-stage Dubbing workflow.
        workflow.insert(QStringLiteral("progressLabel"),
                        artifact.isEmpty()
                            ? QStringLiteral("Artifact transfer")
                            : QStringLiteral("Artifact: %1").arg(artifact));
        workflow.insert(QStringLiteral("receivedBytes"), artifactTransfer.value(QStringLiteral("receivedBytes")));
        workflow.insert(QStringLiteral("totalBytes"), artifactTransfer.value(QStringLiteral("totalBytes")));
    }
    workflow.insert(QStringLiteral("workflowId"), m_dubbing->workflowId());
    workflow.insert(QStringLiteral("workflowVersion"), m_dubbing->workflowVersion());
    workflow.insert(QStringLiteral("runId"), m_dubbing->workflowRunId());
    workflow.insert(QStringLiteral("nodeRunId"), m_dubbing->workflowNodeRunId());
    addExecutionDetails(&workflow,
        stageInfo.value(QStringLiteral("route"),
                        m_dubbing->workflowMode() == QStringLiteral("automatic")
                            ? QStringLiteral("Automatic workflow")
                            : QStringLiteral("Step-by-step workflow")).toString(),
        stageInfo.value(QStringLiteral("model")).toString());
    return workflow;
}

QVariantMap WorkflowActivityManager::gatewayTtsWorkflow() const
{
    if (!m_gatewayTts || !m_gatewayTts->processing()) return {};
    QVariantMap workflow = makeWorkflow(QStringLiteral("gateway-tts-active"),
        QStringLiteral("tts"), QStringLiteral("Generating speech via API Gateway"),
        studioRouteForCapability(QStringLiteral("tts")), QStringLiteral("volume"),
        0, true, QStringLiteral("Waiting for API Gateway audio"), true);
    // OpenAI-compatible TTS responds with the completed audio payload only;
    // no intermediate count exists to display truthfully.
    workflow.insert(QStringLiteral("progressAvailable"), false);
    addExecutionDetails(&workflow, QStringLiteral("API Gateway"), m_gatewayTts->gatewayModel());
    return workflow;
}

QVariantMap WorkflowActivityManager::colabTtsWorkflow() const
{
    if (!m_colabTts || !m_colabTts->processing()) return {};
    QVariantMap workflow = makeWorkflow(QStringLiteral("colab-tts-active"),
        QStringLiteral("tts"), QStringLiteral("Generating speech on Direct Colab GPU"),
        studioRouteForCapability(QStringLiteral("tts")), QStringLiteral("volume"),
        0, true, QStringLiteral("Waiting for CUDA worker audio"), true);
    workflow.insert(QStringLiteral("progressAvailable"), false);
    addExecutionDetails(&workflow, QStringLiteral("Direct Colab GPU"),
                        m_colabTts->colabModel(), QStringLiteral("fixed"));
    return workflow;
}

QVariantMap WorkflowActivityManager::voiceCloneWorkflow() const
{
    if (!m_colabVoiceClone || !m_colabVoiceClone->processing()) return {};
    const QString stage = m_colabVoiceClone->progressStage();
    QVariantMap workflow = makeWorkflow(QStringLiteral("voice-clone-active"),
        QStringLiteral("voice-cloning"), QStringLiteral("Cloning voice on Direct Colab GPU"),
        studioRouteForCapability(QStringLiteral("voice-cloning")), QStringLiteral("mic"),
        m_colabVoiceClone->progress(), true,
        stage.isEmpty() ? QStringLiteral("Waiting for CUDA worker job") : stage, true);
    // Voice-clone job percentages are only available after the worker emits
    // one. Until then the activity list says Working rather than showing 0%
    // as an invented progress measure.
    workflow.insert(QStringLiteral("progressAvailable"), !stage.isEmpty());
    addExecutionDetails(&workflow, QStringLiteral("Direct Colab GPU"),
                        m_colabVoiceClone->model(), QStringLiteral("fixed"));
    return workflow;
}

QVariantMap WorkflowActivityManager::voiceDesignWorkflow() const
{
    if (!m_colabVoiceDesign || !m_colabVoiceDesign->processing()) return {};
    QVariantMap workflow = makeWorkflow(QStringLiteral("voice-design-active"),
        QStringLiteral("voice-design"), QStringLiteral("Designing voice on Direct Colab GPU"),
        studioRouteForCapability(QStringLiteral("voice-design")), QStringLiteral("spark"),
        0, true, QStringLiteral("Waiting for CUDA worker audio"), true);
    workflow.insert(QStringLiteral("progressAvailable"), false);
    addExecutionDetails(&workflow, QStringLiteral("Direct Colab GPU"),
                        m_colabVoiceDesign->model(), QStringLiteral("fixed"));
    return workflow;
}

QVariantMap WorkflowActivityManager::colabAlignmentWorkflow() const
{
    if (!m_colabAlignment || !m_colabAlignment->processing()) return {};
    QVariantMap workflow = makeWorkflow(QStringLiteral("colab-alignment-active"),
        QStringLiteral("forced-alignment"), QStringLiteral("Aligning on Direct Colab GPU"),
        studioRouteForCapability(QStringLiteral("forced-alignment")), QStringLiteral("sliders"),
        m_colabAlignment->progress(), true,
        m_colabAlignment->statusText().isEmpty() ? QStringLiteral("Waiting for CUDA worker")
                                                  : m_colabAlignment->statusText(), true);
    workflow.insert(QStringLiteral("progressAvailable"), false);
    addExecutionDetails(&workflow, QStringLiteral("Direct Colab GPU"),
                        m_colabAlignment->model(), QStringLiteral("fixed"),
                        m_colabAlignment->errorMessage());
    return workflow;
}

QVariantMap WorkflowActivityManager::localVoiceIsolationWorkflow() const
{
    if (m_voiceCloneReferenceIsolator && m_voiceCloneReferenceIsolator->processing()) return {};
    if (!m_voiceIsolator || !m_voiceIsolator->processing()) return {};
    QVariantMap workflow = makeWorkflow(QStringLiteral("voice-isolation-active"),
        QStringLiteral("voice-isolation"), QStringLiteral("Separating vocals locally"),
        studioRouteForCapability(QStringLiteral("voice-isolation")), QStringLiteral("waves"),
        m_voiceIsolator->progress(), false, QStringLiteral("Source separation"), true);
    addExecutionDetails(&workflow, QStringLiteral("Local CPU"),
                        QFileInfo(m_voiceIsolator->modelPath()).fileName(), QString(),
                        m_voiceIsolator->lastError());
    return workflow;
}

QVariantMap WorkflowActivityManager::colabVoiceIsolationWorkflow() const
{
    if (m_voiceCloneReferenceIsolator && m_voiceCloneReferenceIsolator->processing()) return {};
    if (!m_colabVoiceIsolator || !m_colabVoiceIsolator->processing()) return {};
    QVariantMap workflow = makeWorkflow(QStringLiteral("colab-voice-isolation-active"),
        QStringLiteral("voice-isolation"), QStringLiteral("Separating vocals on Direct Colab GPU"),
        studioRouteForCapability(QStringLiteral("voice-isolation")), QStringLiteral("waves"),
        m_colabVoiceIsolator->progress(), false, QStringLiteral("Source separation"), true);
    addExecutionDetails(&workflow, QStringLiteral("Direct Colab GPU"),
                        m_colabVoiceIsolator->model(), QStringLiteral("fixed"),
                        m_colabVoiceIsolator->lastError());
    return workflow;
}

QVariantMap WorkflowActivityManager::voiceCloneReferenceIsolationWorkflow() const
{
    if (!m_voiceCloneReferenceIsolator || !m_voiceCloneReferenceIsolator->processing()) return {};
    const QString route = m_voiceCloneReferenceIsolator->selectedRoute();
    QVariantMap workflow = makeWorkflow(QStringLiteral("voice-clone-reference-isolation-active"),
        QStringLiteral("voice-cloning"), QStringLiteral("Cleaning Voice Clone reference with Isolator"),
        studioRouteForCapability(QStringLiteral("voice-cloning")), QStringLiteral("waves"),
        m_voiceCloneReferenceIsolator->progress(), false,
        m_voiceCloneReferenceIsolator->statusText().isEmpty()
            ? QStringLiteral("Separating Vocals and Background")
            : m_voiceCloneReferenceIsolator->statusText(), true);
    addExecutionDetails(&workflow, route, m_voiceCloneReferenceIsolator->selectedModel(),
                        route == QStringLiteral("Direct Colab GPU") ? QStringLiteral("fixed") : QString(),
                        m_voiceCloneReferenceIsolator->lastError());
    return workflow;
}

QVariantMap WorkflowActivityManager::translationWorkflow() const
{
    if (!m_translation || !m_translation->processing()) return {};
    const bool measured = !m_translation->gatewayActive();
    QVariantMap workflow = makeWorkflow(QStringLiteral("translation-active"),
        QStringLiteral("translation"), QStringLiteral("Translating text"),
        studioRouteForCapability(QStringLiteral("translation")), QStringLiteral("translate"),
        m_translation->progress(), !measured,
        m_translation->statusText().isEmpty() ? QStringLiteral("Translation")
                                               : m_translation->statusText(), true);
    workflow.insert(QStringLiteral("progressAvailable"), measured);
    const QString route = m_translation->colabActive() ? QStringLiteral("Direct Colab GPU")
        : (m_translation->gatewayActive() ? QStringLiteral("API Gateway")
                                           : QStringLiteral("Local CPU"));
    const QString model = m_translation->colabActive() ? m_translation->colabModel()
        : (m_translation->gatewayActive() ? m_translation->gatewayModel() : QString());
    addExecutionDetails(&workflow, route, model,
                        m_translation->colabActive() ? QStringLiteral("fixed") : QString(),
                        m_translation->errorText());
    return workflow;
}

QVariantMap WorkflowActivityManager::subtitleOcrWorkflow() const
{
    if (!m_subtitleOcr || (!m_subtitleOcr->processing() && !m_subtitleOcr->sourceImporting())) return {};
    if (m_subtitleOcr->sourceImporting()) {
        const qint64 total = m_subtitleOcr->sourceImportTotalBytes();
        const bool measured = total > 0;
        const int percent = measured ? qBound(0, int(m_subtitleOcr->sourceImportReceivedBytes() * 100 / total), 100) : 0;
        QVariantMap workflow = makeWorkflow(QStringLiteral("subtitle-ocr-active"),
            QStringLiteral("subtitle-ocr"), QStringLiteral("Importing subtitle OCR source"),
            studioRouteForCapability(QStringLiteral("subtitle-ocr")), QStringLiteral("subtitle"),
            percent, !measured, m_subtitleOcr->sourceImportStatus(), true);
        workflow.insert(QStringLiteral("progressAvailable"), measured);
        addExecutionDetails(&workflow, QStringLiteral("Media ingest"),
                            m_subtitleOcr->executionRoute(), QString(),
                            m_subtitleOcr->sourceImportError());
        return workflow;
    }
    QVariantMap workflow = makeWorkflow(QStringLiteral("subtitle-ocr-active"),
        QStringLiteral("subtitle-ocr"), QStringLiteral("Recognizing subtitles"),
        studioRouteForCapability(QStringLiteral("subtitle-ocr")), QStringLiteral("subtitle"),
        m_subtitleOcr->progress(), !m_subtitleOcr->progressAvailable(),
        m_subtitleOcr->phase(), true);
    workflow.insert(QStringLiteral("progressAvailable"), m_subtitleOcr->progressAvailable());
    const bool colab = m_subtitleOcr->executionRoute() == QStringLiteral("colab-direct");
    addExecutionDetails(&workflow, colab ? QStringLiteral("Direct Colab GPU")
                                         : QStringLiteral("Local CPU"),
                        colab ? m_subtitleOcr->colabModelId()
                              : m_subtitleOcr->localEngineId(),
                        colab ? QStringLiteral("fixed") : QString(), m_subtitleOcr->error());
    return workflow;
}

QVariantMap WorkflowActivityManager::llmChatWorkflow() const
{
    if (!m_llmChat || !m_llmChat->generating()) return {};
    QVariantMap workflow = makeWorkflow(QStringLiteral("llm-chat-active"),
        QStringLiteral("llm-chat"), QStringLiteral("Generating chat response"),
        studioRouteForCapability(QStringLiteral("llm-chat")), QStringLiteral("chat"),
        0, true, QStringLiteral("Receiving model response"), true);
    workflow.insert(QStringLiteral("progressAvailable"), false);
    const QString route = m_llmChat->colabActive() ? QStringLiteral("Direct Colab GPU")
        : (m_llmChat->gatewayActive() ? QStringLiteral("API Gateway")
                                       : QStringLiteral("Local CPU"));
    const QString model = m_llmChat->colabActive() ? m_llmChat->colabModel()
        : (m_llmChat->gatewayActive() ? m_llmChat->gatewayModel() : QString());
    addExecutionDetails(&workflow, route, model,
                        m_llmChat->colabActive() ? QStringLiteral("fixed") : QString(),
                        m_llmChat->errorText());
    return workflow;
}

QVariantMap WorkflowActivityManager::makeWorkflow(const QString &id,
                                          const QString &type,
                                          const QString &title,
                                          const QString &routeId,
                                          const QString &iconName,
                                          int progress,
                                          bool progressEstimated,
                                          const QString &stageLabel,
                                          bool cancellable) const
{
    if (!m_startedAtById.contains(id)) {
        m_startedAtById.insert(id, QDateTime::currentDateTimeUtc());
    }

    const QDateTime startedAt = m_startedAtById.value(id);
    QVariantMap workflow;
    workflow.insert(QStringLiteral("id"), id);
    workflow.insert(QStringLiteral("type"), type);
    workflow.insert(QStringLiteral("title"), title);
    workflow.insert(QStringLiteral("routeId"), routeId);
    workflow.insert(QStringLiteral("iconName"), iconName);
    const bool stopping = m_stoppingIds.contains(id);
    workflow.insert(QStringLiteral("status"), stopping ? QStringLiteral("cancelling") : QStringLiteral("running"));
    workflow.insert(QStringLiteral("statusLabel"), statusLabel(stopping));
    workflow.insert(QStringLiteral("progress"), qBound(0, progress, 100));
    workflow.insert(QStringLiteral("progressEstimated"), progressEstimated);
    workflow.insert(QStringLiteral("progressAvailable"), true);
    workflow.insert(QStringLiteral("stageLabel"), stageLabel);
    workflow.insert(QStringLiteral("cancellable"), cancellable);
    workflow.insert(QStringLiteral("pausable"), false);
    workflow.insert(QStringLiteral("startedAt"), startedAt.toString(Qt::ISODate));
    workflow.insert(QStringLiteral("elapsedSeconds"), startedAt.secsTo(QDateTime::currentDateTimeUtc()));
    return workflow;
}

void WorkflowActivityManager::updateActiveStartTimes(const QVariantList &workflows) const
{
    QHash<QString, bool> activeIds;
    for (const QVariant &entry : workflows) {
        const QString id = entry.toMap().value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            activeIds.insert(id, true);
        }
    }

    for (auto it = m_startedAtById.begin(); it != m_startedAtById.end();) {
        if (!activeIds.contains(it.key())) {
            m_stoppingIds.remove(it.key());
            it = m_startedAtById.erase(it);
        } else {
            ++it;
        }
    }
}

QString WorkflowActivityManager::stateLabel(int stateValue)
{
    switch (static_cast<ModelSessionState>(stateValue)) {
    case ModelSessionState::Unconfigured:
        return QStringLiteral("Unconfigured");
    case ModelSessionState::Unloaded:
        return QStringLiteral("Unloaded");
    case ModelSessionState::Loading:
        return QStringLiteral("Loading");
    case ModelSessionState::Ready:
        return QStringLiteral("Ready");
    case ModelSessionState::Processing:
        return QStringLiteral("Processing");
    case ModelSessionState::Unloading:
        return QStringLiteral("Unloading");
    case ModelSessionState::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

QString WorkflowActivityManager::iconForCapability(const QString &capabilityId)
{
    if (capabilityId == QStringLiteral("stt")) {
        return QStringLiteral("waves");
    }
    if (capabilityId == QStringLiteral("voice-cloning")) {
        return QStringLiteral("mic");
    }
    if (capabilityId == QStringLiteral("voice-design")) {
        return QStringLiteral("spark");
    }
    if (capabilityId == QStringLiteral("forced-alignment")) {
        return QStringLiteral("sliders");
    }
    if (capabilityId == QStringLiteral("translation")) {
        return QStringLiteral("translate");
    }
    return QStringLiteral("volume");
}

QString WorkflowActivityManager::fallbackTitleForCapability(const QString &capabilityId)
{
    if (capabilityId == QStringLiteral("stt")) {
        return QStringLiteral("Speech to Text");
    }
    if (capabilityId == QStringLiteral("voice-cloning")) {
        return QStringLiteral("Voice Cloning");
    }
    if (capabilityId == QStringLiteral("voice-design")) {
        return QStringLiteral("Voice Design");
    }
    if (capabilityId == QStringLiteral("forced-alignment")) {
        return QStringLiteral("Alignment");
    }
    if (capabilityId == QStringLiteral("translation")) {
        return QStringLiteral("Translation");
    }
    return QStringLiteral("Text to Speech");
}

QString WorkflowActivityManager::routeForCapability(const QString &capabilityId)
{
    if (capabilityId == QStringLiteral("voice-clone")) {
        return QStringLiteral("voice-cloning");
    }
    if (capabilityId.isEmpty()) {
        return QStringLiteral("tts");
    }
    return capabilityId;
}

QString WorkflowActivityManager::studioRouteForCapability(const QString &capabilityId)
{
    const QString normalized = routeForCapability(capabilityId);
    if (normalized == QStringLiteral("stt")) {
        return QStringLiteral("studio-stt");
    }
    if (normalized == QStringLiteral("voice-cloning")) {
        return QStringLiteral("studio-voice-cloning");
    }
    if (normalized == QStringLiteral("voice-design")) {
        return QStringLiteral("studio-voice-design");
    }
    if (normalized == QStringLiteral("forced-alignment")) {
        return QStringLiteral("studio-alignment");
    }
    if (normalized == QStringLiteral("translation")) {
        return QStringLiteral("studio-translation");
    }
    return QStringLiteral("studio-tts");
}

QString WorkflowActivityManager::statusLabel(bool stopping)
{
    return stopping ? QStringLiteral("Stopping") : QStringLiteral("Running");
}

QString WorkflowActivityManager::ttsTitleForMode(const QString &mode)
{
    if (mode == QStringLiteral("voice-cloning")) {
        return QStringLiteral("Cloning voice");
    }
    if (mode == QStringLiteral("voice-design")) {
        return QStringLiteral("Designing voice");
    }
    return QStringLiteral("Generating speech");
}

} // namespace LAStudio
