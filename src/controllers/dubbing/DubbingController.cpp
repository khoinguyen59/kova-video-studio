#include "controllers/dubbing/DubbingController.h"

#include "SttSessionController.h"
#include "tts/TtsEngine.h"
#include "tts/TtsSavedVoiceProfile.h"
#include "controllers/dubbing/DubbingJobRunner.h"
#include "controllers/dubbing/DubbingColabModelRoutes.h"
#include "controllers/dubbing/DubbingTranslationFixService.h"
#include "controllers/subtitles/SubtitleOcrController.h"
#include "controllers/shared/VoiceClonePresetService.h"
#include "dubbing/CapCutDraftExporter.h"
#include "dubbing/DubbingSubtitleService.h"
#include "dubbing/DubbingTranscriptFusionService.h"
#include "dubbing/DubbingTimingService.h"
#include "dubbing/EspeakNgPhonemizer.h"
#include "dubbing/media/ColabMediaDownloadRunner.h"
#include "dubbing/workflow/DubbingWorkflowDefinition.h"
#include "dubbing/workflow/DubbingWorkflowNodes.h"
#include "workflows/WorkflowGraphRunner.h"
#include "core/PathUtils.h"
#include "core/Logger.h"
#include "controllers/app/AppController.h"
#include "controllers/models/ModelSessionRegistry.h"
#include "controllers/models/StudioConfigurationResolver.h"
#include "controllers/models/CapabilitySettingsSchema.h"
#include "controllers/models/DownloadInstallService.h"
#include "core/CapabilityFamilyModel.h"
#include "core/DownloadManager.h"
#include "core/ModelManager.h"
#include "core/RuntimeManager.h"
#include "remote/ExecutionProvider.h"
#include "remote/ColabSession.h"

#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QtMath>
#include <QUrl>
#include <QUuid>
#include <QDir>
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QRegularExpression>

namespace LAStudio {

namespace {

QString automaticDefaultFamilyId(const QString &capabilityId,
                                 const QString &dubbingQuality = QString())
{
    if (capabilityId == QStringLiteral("stt"))
        return QStringLiteral("whisper.cpp");
    if (capabilityId == QStringLiteral("voice-isolation"))
        return QStringLiteral("sherpa-onnx-spleeter-2stems-fp16");
    if (capabilityId == QStringLiteral("translation"))
        return QStringLiteral("hy-mt2-1.8b");
    if (capabilityId == QStringLiteral("llm-chat"))
        return QStringLiteral("qwen3.5-2b");
    if (capabilityId == QStringLiteral("tts"))
        return dubbingQuality == QStringLiteral("fast")
            ? QStringLiteral("vieneu-tts-v2-turbo")
            : QStringLiteral("omnivoice");
    return {};
}

void unloadConflictingDubbingRuntime(ModelSessionRegistry *registry,
                                     const QString &capabilityId)
{
    if (!registry) return;

    QStringList conflictingCapabilities;
    if (capabilityId == QStringLiteral("tts") ||
        capabilityId == QStringLiteral("stt")) {
        conflictingCapabilities.append(QStringLiteral("translation"));
        conflictingCapabilities.append(QStringLiteral("llm-chat"));
    } else if (capabilityId == QStringLiteral("translation")) {
        conflictingCapabilities.append(QStringLiteral("stt"));
        conflictingCapabilities.append(QStringLiteral("tts"));
        conflictingCapabilities.append(QStringLiteral("llm-chat"));
    } else {
        return;
    }

    for (const QString &conflictingCapability : conflictingCapabilities) {
        IModelSession *conflictingSession =
            registry->sessionForCapability(conflictingCapability);
        if (!conflictingSession) continue;

        const QList<SessionConfiguration> loaded =
            conflictingSession->loadedConfigurations();
        if (loaded.isEmpty()) continue;

        Logger::info(
            QStringLiteral("DubbingController"),
            QStringLiteral("Dubbing runtime handoff: unloading %1 before loading %2 "
                           "to avoid incompatible shared DLLs in one process.")
                .arg(conflictingCapability, capabilityId));
        for (const SessionConfiguration &configuration : loaded) {
            conflictingSession->requestUnloadConfiguration(configuration.signature);
        }
    }
}

QString subtitleTimestamp(qint64 milliseconds, bool webVtt)
{
    const qint64 hours = milliseconds / 3600000;
    milliseconds %= 3600000;
    const qint64 minutes = milliseconds / 60000;
    milliseconds %= 60000;
    const qint64 seconds = milliseconds / 1000;
    const qint64 millis = milliseconds % 1000;
    return QStringLiteral("%1:%2:%3%4%5")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(webVtt ? QLatin1Char('.') : QLatin1Char(','))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

bool writeDubbingSubtitles(const QVariantList &segments, const QString &path,
                           bool useTargetText, QString *error)
{
    return DubbingSubtitleService::writeSidecar(segments, path, useTargetText, error);
}

bool replaceCopy(const QString &source, const QString &destination, QString *error)
{
    if (source.isEmpty() || !QFileInfo(source).isFile()) return true;
    if (QFileInfo(source).absoluteFilePath().compare(
            QFileInfo(destination).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (QFileInfo::exists(destination) && !QFile::remove(destination)) {
        if (error) *error = QStringLiteral("Cannot replace package file: %1").arg(destination);
        return false;
    }
    if (!QFile::copy(source, destination)) {
        if (error) *error = QStringLiteral("Cannot copy %1 to %2.").arg(source, destination);
        return false;
    }
    return true;
}

QString activityNodeId(const QString &stage)
{
    const QString normalized = stage.trimmed().toLower();
    if (normalized == QStringLiteral("import")) return QStringLiteral("media-input");
    if (normalized == QStringLiteral("source-separation")) return QStringLiteral("source-separate");
    if (normalized == QStringLiteral("transcription")) return QStringLiteral("transcribe");
    if (normalized == QStringLiteral("translation") || normalized == QStringLiteral("translation-fix"))
        return QStringLiteral("translate");
    if (normalized == QStringLiteral("tts")) return QStringLiteral("synthesize");
    if (normalized == QStringLiteral("timing")) return QStringLiteral("fit-timing");
    return normalized;
}

} // namespace

DubbingController::DubbingController(SttSessionController *sttSession, TtsEngine *tts,
                                     ModelManager *models, RuntimeManager *runtimes, QObject *parent)
    : DubbingController(sttSession, tts, nullptr, models, runtimes, parent)
{
}

DubbingController::~DubbingController() = default;

DubbingController::DubbingController(SttSessionController *sttSession, TtsEngine *tts,
                                     TranslationEngine *translation,
                                     ModelManager *models, RuntimeManager *runtimes, QObject *parent)
    : QObject(parent), m_sttSession(sttSession), m_tts(tts),
      m_models(models), m_runtimes(runtimes)
{
    m_translation = translation;
    m_runner = new DubbingJobRunner(sttSession, tts, translation, models, runtimes, this);
    // Public-media links are intentionally not handled by a local resolver.
    // The runner is wired when the dedicated Colab session is injected below.
    m_colabMediaDownload = new ColabMediaDownloadRunner(nullptr, this);
    connect(m_colabMediaDownload, &ColabMediaDownloadRunner::transferProgress, this,
            [this](qint64 receivedBytes, qint64 totalBytes) {
        const int index = mediaQueueIndex(m_activeMediaQueueDownloadId);
        if (index < 0) return;
        QVariantMap item = m_mediaQueueItems.at(index).toMap();
        item.insert(QStringLiteral("receivedBytes"), receivedBytes);
        item.insert(QStringLiteral("totalBytes"), totalBytes);
        item.insert(QStringLiteral("downloadState"), QStringLiteral("downloading"));
        item.insert(QStringLiteral("status"), totalBytes > 0
                        ? QStringLiteral("Receiving completed media %1 / %2 bytes").arg(receivedBytes).arg(totalBytes)
                        : QStringLiteral("Downloading through Colab (%1 bytes)").arg(receivedBytes));
        replaceMediaQueueItem(index, item);
    });
    connect(m_colabMediaDownload, &ColabMediaDownloadRunner::phaseChanged, this,
            [this](const QString &phase) {
        const int index = mediaQueueIndex(m_activeMediaQueueDownloadId);
        if (index < 0) return;
        QVariantMap item = m_mediaQueueItems.at(index).toMap();
        item.insert(QStringLiteral("status"), phase);
        replaceMediaQueueItem(index, item);
        m_mediaQueueStatus = phase;
        emit mediaQueueChanged();
    });
    connect(m_colabMediaDownload, &ColabMediaDownloadRunner::finished, this,
            &DubbingController::onBatchMediaDownloadFinished);
    m_translationFix = new DubbingTranslationFixService(this);
    connect(m_translationFix, &DubbingTranslationFixService::stateChanged,
            this, [this]() {
        emit translationFixChanged();
        emit processingChanged();
        emit errorChanged();
        emit workflowChanged();
    });
    connect(m_translationFix, &DubbingTranslationFixService::completed,
            this, [this](const QVariantList &segments, int, int) {
        m_project.segments = segments;
        emit segmentsChanged();
        emit translationFixChanged();
        emit workflowChanged();
        persistAfterEdit();
    });
    connect(m_translationFix, &DubbingTranslationFixService::reconciliationCompleted,
            this, [this](const QVariantList &segments, int, int) {
        // Suggestions preserve each segment's conflict evidence and remain
        // pending until an explicit accept/reject/manual review action.
        m_project.segments = segments;
        emit segmentsChanged();
        emit translationFixChanged();
        emit workflowChanged();
        persistAfterEdit();
    });
    connect(m_translationFix, &DubbingTranslationFixService::connectionTested,
            this, &DubbingController::translationFixConnectionTested);
    if (AppController::instance() && AppController::instance()->sessionRegistry()) {
        for (IModelSession *session : AppController::instance()->sessionRegistry()->sessions()) {
            if (!session) continue;
            connect(session, &IModelSession::stateChanged, this, [this]() {
                emit workflowChanged();
                scheduleAutomaticSetupAdvance();
            });
            connect(session, &IModelSession::activeConfigurationChanged, this, [this]() {
                emit workflowChanged();
                scheduleAutomaticSetupAdvance();
            });
        }
    }
    m_workflowRegistry = new NodeRegistry(this);
    registerDubbingWorkflowNodes(*m_workflowRegistry, m_runner);
    loadHistory();
    m_workflowRunner = new WorkflowGraphRunner(m_workflowRegistry, this);
    connect(m_workflowRunner, &WorkflowGraphRunner::stateChanged, this, [this]() {
        emit processingChanged();
        emit errorChanged();
        emit workflowChanged();
    });
    connect(m_workflowRunner, &WorkflowGraphRunner::nodeStarted, this,
            [this](const QString &nodeId) {
        const QString visibleStep = visibleStepForNode(nodeId);
        setCurrentStep(visibleStep);
        if (m_workflowMode == QStringLiteral("automatic")) {
            appendAutomaticEvent(QStringLiteral("Running %1").arg(visibleStep),
                                 QStringLiteral("running"), nodeId);
            setAutomaticStatus(QStringLiteral("Running node: %1").arg(visibleStep));
        }
    });
    connect(m_workflowRunner, &WorkflowGraphRunner::nodeCompleted, this,
            [this](const QString &nodeId, const QVariantMap &) {
        if (m_workflowMode == QStringLiteral("automatic")) {
            appendAutomaticEvent(QStringLiteral("Completed %1").arg(visibleStepForNode(nodeId)),
                                 QStringLiteral("completed"), nodeId);
            if (nodeId == QStringLiteral("transcribe")
                && m_project.transcriptConfiguration.value(QStringLiteral("transcriptSource"),
                                                            QStringLiteral("stt")).toString().trimmed().toLower()
                       != QStringLiteral("ocr")) {
                if (auto *app = AppController::instance(); app && app->sessionRegistry()) {
                    if (IModelSession *stt = app->sessionRegistry()->sessionForCapability(
                            QStringLiteral("stt"))) {
                        const QList<SessionConfiguration> loaded = stt->loadedConfigurations();
                        for (const SessionConfiguration &configuration : loaded)
                            stt->requestUnloadConfiguration(configuration.signature);
                    }
                }
                appendAutomaticEvent(QStringLiteral("Releasing Whisper runtime before translation"),
                                     QStringLiteral("running"), QStringLiteral("transcribe"));
            } else if (nodeId == QStringLiteral("translate")) {
                if (auto *app = AppController::instance(); app && app->sessionRegistry()) {
                    if (IModelSession *translation = app->sessionRegistry()->sessionForCapability(
                            QStringLiteral("translation"))) {
                        const QList<SessionConfiguration> loaded = translation->loadedConfigurations();
                        for (const SessionConfiguration &configuration : loaded)
                            translation->requestUnloadConfiguration(configuration.signature);
                    }
                }
                prepareAutomaticVoiceRuntime();
            }
        }
    });
    connect(m_workflowRunner, &WorkflowGraphRunner::reviewRequested, this, [this](const QVariantMap &request) {
        m_workflowReviewRequest = request;
        m_activeReviewId = request.value(QStringLiteral("reviewId")).toString();
        if (!m_project.projectPath.isEmpty() && !m_activeReviewId.isEmpty()) {
            if (!m_workflowReviewStore) {
                m_workflowReviewStore = std::make_unique<WorkflowReviewStore>(
                    QDir(QFileInfo(m_project.projectPath).absolutePath()).filePath(QStringLiteral(".workflow-artifacts")));
            }
            WorkflowReviewRequest stored;
            stored.reviewId = m_activeReviewId;
            stored.runId = workflowRunId();
            stored.nodeRunId = workflowNodeRunId();
            stored.nodeId = m_workflowRunner->activeNodeId();
            stored.mode = request.value(QStringLiteral("mode")).toString();
            stored.editor = request.value(QStringLiteral("editor")).toString();
            stored.artifact = request.value(QStringLiteral("artifact"));
            stored.createdAt = QDateTime::currentDateTimeUtc();
            QString ignoredError;
            m_workflowReviewStore->save(stored, &ignoredError);
        }
        emit workflowChanged();
    });
    connect(m_workflowRunner, &WorkflowGraphRunner::completed, this, [this](const QVariantMap &) {
        if (m_workflowReviewStore && !m_activeReviewId.isEmpty()) m_workflowReviewStore->remove(m_activeReviewId);
        m_activeReviewId.clear();
        m_workflowReviewRequest.clear();
        setCurrentStep(QStringLiteral("completed"));
        if (m_workflowMode == QStringLiteral("automatic")) {
            setAutomaticStatus(QStringLiteral("Final dubbed media is ready"));
            appendAutomaticEvent(QStringLiteral("Final dubbed media is ready"),
                                 QStringLiteral("completed"), QStringLiteral("export"));
        }
        discoverInterruptedWorkflow();
        emit workflowChanged();
    });
    connect(m_workflowRunner, &WorkflowGraphRunner::failed, this, [this](const QString &) {
        if (m_workflowReviewStore && !m_activeReviewId.isEmpty()) m_workflowReviewStore->remove(m_activeReviewId);
        m_activeReviewId.clear();
        m_workflowReviewRequest.clear();
        setWorkflowMode(QStringLiteral("idle"));
        discoverInterruptedWorkflow();
        emit workflowChanged();
    });
    connect(m_workflowRunner, &WorkflowGraphRunner::cancelled, this, [this]() {
        if (m_workflowReviewStore && !m_activeReviewId.isEmpty()) m_workflowReviewStore->remove(m_activeReviewId);
        m_activeReviewId.clear();
        m_workflowReviewRequest.clear();
        setWorkflowMode(QStringLiteral("idle"));
        discoverInterruptedWorkflow();
        emit workflowChanged();
    });

    if (AppController::instance()) {
        if (auto *downloads = AppController::instance()->downloads()) {
            connect(downloads, &DownloadManager::activeDownloadsChanged,
                    this, &DubbingController::scheduleAutomaticSetupAdvance);
            connect(downloads, &DownloadManager::error, this,
                    [this](const QString &, const QString &, const QString &message) {
                if (m_automaticSetupActive) finishAutomaticSetupFailure(message);
            });
        }
        if (auto *install = AppController::instance()->downloadInstall()) {
            connect(install, &DownloadInstallService::installStatesChanged,
                    this, &DubbingController::scheduleAutomaticSetupAdvance);
        }
        if (auto *registry = AppController::instance()->sessionRegistry()) {
            const auto watchAutomaticLoad = [this, registry](const QString &capabilityId,
                                                              const QString &nodeId) {
                IModelSession *session = registry->sessionForCapability(capabilityId);
                if (!session) return;
                connect(session, &IModelSession::errorOccurred, this,
                        [this, capabilityId, nodeId](const QString &message) {
                    if (!m_automaticSetupActive) return;
                    const QVariantMap configuration =
                        m_workflowNodeConfigurations.value(nodeId).toMap();
                    const QString familyId = configuration.value(
                        QStringLiteral("familyId")).toString();
                    if (!familyId.isEmpty() && m_automaticConfiguredNodes.contains(nodeId)) {
                        appendAutomaticEvent(
                            QStringLiteral("Required default model %1 failed to load")
                                .arg(familyId),
                            QStringLiteral("failed"), nodeId);
                        finishAutomaticSetupFailure(
                            QStringLiteral("Failed to load required default model %1: %2")
                                .arg(familyId, message));
                        return;
                    }
                    finishAutomaticSetupFailure(
                        QStringLiteral("Failed to load %1 model: %2")
                            .arg(capabilityId, message));
                });
            };
            watchAutomaticLoad(QStringLiteral("stt"), QStringLiteral("transcribe"));
            watchAutomaticLoad(QStringLiteral("tts"), QStringLiteral("synthesize"));
        }
    }
    if (m_models)
        connect(m_models, &ModelManager::registryUpdated,
                this, &DubbingController::scheduleAutomaticSetupAdvance);
    if (m_runtimes)
        connect(m_runtimes, &RuntimeManager::registryUpdated,
                this, &DubbingController::scheduleAutomaticSetupAdvance);

    connect(m_runner, &DubbingJobRunner::stateChanged, this, [this]() {
        updateMediaQueueProgressFromRunner();
        emit processingChanged();
        emit errorChanged();
        emit previewChanged();
        emit exportChanged();
        emit workflowChanged();
    });

    connect(m_runner, &DubbingJobRunner::errorOccurred, this, [this](const QString &) {
        m_pendingExportPath.clear();
    });

    connect(m_runner, &DubbingJobRunner::segmentsUpdated, this, [this](const QVariantList &segments) {
        m_project.segments = segments;
        emit segmentsChanged();
        emit workflowChanged();
        persistAfterEdit();
    });

    connect(m_runner, &DubbingJobRunner::segmentUpdated, this, [this](int index, const QVariantMap &patch) {
        if (index >= 0 && index < m_project.segments.size()) {
            m_project.segments[index] = patch;
            emit segmentsChanged();
            emit workflowChanged();
            persistAfterEdit();
        }
    });

    connect(m_runner, &DubbingJobRunner::ingestFinished, this, &DubbingController::onIngestFinished);
    connect(m_runner, &DubbingJobRunner::sourceSeparationFinished, this, [this](const QVariantMap &outputs) {
        m_project.analysisAudioPath = outputs.value(QStringLiteral("vocals"), m_project.masterAudioPath).toString();
        m_project.backgroundAudioPath = outputs.value(QStringLiteral("background"), m_project.masterAudioPath).toString();
        m_runner->setBackgroundAudioPath(m_project.backgroundAudioPath);
        emit projectChanged();
        emit workflowChanged();
        persistAfterEdit();
    });
    connect(m_runner, &DubbingJobRunner::stageCompleted, this,
            [this](const QString &nodeId, const QVariantMap &outputs) {
        m_stepOutputs.insert(nodeId, outputs);
        m_lastCompletedStepId = nodeId;
        if (nodeId == QStringLiteral("mix") && !m_pendingExportPath.isEmpty()) {
            const QString destination = m_pendingExportPath;
            m_pendingExportPath.clear();
            if (!m_runner->startExport(m_project.sourceMediaPath,
                                       outputs.value(QStringLiteral("audio")).toString(),
                                       destination, m_project.segments, subtitleConfiguration())) {
                emit workflowChanged();
                return;
            }
        }
        if (m_workflowMode == QStringLiteral("step")
            && (!m_workflowRunner || !m_workflowRunner->running())) {
            clearError();
            setAutomaticStatus(
                QStringLiteral("Manual node completed: %1").arg(visibleStepForNode(nodeId)));
            advanceManualStep(nodeId);
        }
        emit workflowChanged();
    });
    // Batch work reuses the production runner one item at a time.  Delaying
    // the transition by one event-loop turn lets the normal project/segment
    // signal handlers commit the real result before the next stage begins.
    connect(m_runner, &DubbingJobRunner::stageCompleted, this,
            [this](const QString &nodeId, const QVariantMap &outputs) {
        if (!m_mediaQueueProcessing || m_activeMediaQueueItemId.isEmpty()) return;
        QTimer::singleShot(0, this, [this, nodeId, outputs]() {
            if (!m_mediaQueueProcessing || m_activeMediaQueueItemId.isEmpty()) return;
            const int index = mediaQueueIndex(m_activeMediaQueueItemId);
            if (index < 0) return;
            QVariantMap item = m_mediaQueueItems.at(index).toMap();
            QVariantList completedStages = item.value(QStringLiteral("completedStages")).toList();
            if (!completedStages.contains(nodeId)) completedStages.append(nodeId);
            item.insert(QStringLiteral("completedStages"), completedStages);
            item.insert(QStringLiteral("progress"), 0);
            replaceMediaQueueItem(index, item);

            if (nodeId == QStringLiteral("source-separate")) {
                const QString outputDirectory = mediaQueueOutputDirectory(m_activeMediaQueueItemId);
                QString error;
                const bool wroteVocals = QFileInfo(m_project.analysisAudioPath).isFile()
                    && replaceCopy(m_project.analysisAudioPath,
                                   QDir(outputDirectory).filePath(QStringLiteral("vocals.wav")), &error);
                if (wroteVocals) recordMediaQueueOutput(
                    QStringLiteral("vocalsWav"), QDir(outputDirectory).filePath(QStringLiteral("vocals.wav")));
                const bool wroteBackground = QFileInfo(m_project.backgroundAudioPath).isFile()
                    && replaceCopy(m_project.backgroundAudioPath,
                                   QDir(outputDirectory).filePath(QStringLiteral("background.wav")), &error);
                if (wroteBackground) recordMediaQueueOutput(
                    QStringLiteral("backgroundWav"), QDir(outputDirectory).filePath(QStringLiteral("background.wav")));
                if (!wroteVocals || !wroteBackground) {
                    completeCurrentMediaQueueItem(false, error.isEmpty()
                        ? QStringLiteral("Voice isolation completed without writable vocal and background WAV files.")
                        : error);
                    return;
                }
            }
            if (nodeId == QStringLiteral("transcribe")) {
                if (!writeMediaQueueSubtitles(QStringLiteral("sourceSrt"), false)) return;
            }
            if (nodeId == QStringLiteral("translate")) {
                if (!writeMediaQueueSubtitles(QStringLiteral("translatedSrt"), true)) return;
            }
            if (nodeId == QStringLiteral("mix")) {
                const QString outputPath = QDir(mediaQueueOutputDirectory(m_activeMediaQueueItemId))
                    .filePath(QStringLiteral("voice.wav"));
                QString error;
                if (!QFileInfo(m_runner->previewPath()).isFile()
                    || !replaceCopy(m_runner->previewPath(), outputPath, &error)) {
                    completeCurrentMediaQueueItem(false, error.isEmpty()
                        ? QStringLiteral("Voice synthesis completed without a writable WAV output.") : error);
                    return;
                }
                recordMediaQueueOutput(QStringLiteral("voiceWav"), outputPath);
            }
            if (nodeId == QStringLiteral("export")) {
                const QString outputPath = outputs.value(QStringLiteral("media")).toString();
                if (!QFileInfo(outputPath).isFile()) {
                    completeCurrentMediaQueueItem(false,
                        QStringLiteral("Export completed without a writable media output."));
                    return;
                }
                recordMediaQueueOutput(QStringLiteral("exportedMedia"), outputPath);
            }

            if (m_mediaQueueExecutionMode == QStringLiteral("stage-by-stage")) {
                completeCurrentMediaQueueStage(nodeId);
                return;
            }
            const int completedStageIndex = m_mediaQueueStagePlan.indexOf(nodeId);
            if (completedStageIndex < 0) {
                completeCurrentMediaQueueItem(false,
                    QStringLiteral("The completed batch action was not in the selected plan."));
                return;
            }
            const int nextStageIndex = completedStageIndex + 1;
            if (nextStageIndex < m_mediaQueueStagePlan.size()) {
                startMediaQueueStage(m_mediaQueueStagePlan.at(nextStageIndex));
                return;
            }
            completeCurrentMediaQueueItem(true);
        });
    });
    connect(m_runner, &DubbingJobRunner::errorOccurred, this, [this](const QString &message) {
        if (!m_mediaQueueProcessing || m_activeMediaQueueItemId.isEmpty()) return;
        QTimer::singleShot(0, this, [this, message]() {
            if (!m_mediaQueueProcessing || m_activeMediaQueueItemId.isEmpty()) return;
            completeCurrentMediaQueueItem(false, message);
        });
    });
}

bool DubbingController::processing() const
{
    return m_automaticSetupActive
        || m_mediaQueueProcessing
        || (m_translationFix && m_translationFix->busy())
        || m_runner->processing()
        || (m_workflowRunner && m_workflowRunner->running());
}

QString DubbingController::stage() const
{
    if (m_automaticSetupActive) return QStringLiteral("model-setup");
    if (m_translationFix && m_translationFix->busy())
        return QStringLiteral("translation-fix");
    if (m_workflowRunner && m_workflowRunner->running()
        && !m_workflowRunner->activeNodeId().isEmpty())
        return m_workflowRunner->activeNodeId();
    return m_runner->stage();
}

QVariantMap DubbingController::activityStageInfo() const
{
    QString nodeId;
    if (m_automaticSetupActive) {
        nodeId = m_automaticSetupNodeId;
    } else if (m_translationFix && m_translationFix->busy()) {
        nodeId = QStringLiteral("translate");
    } else if (m_workflowRunner && m_workflowRunner->running()) {
        nodeId = m_workflowRunner->activeNodeId();
    } else {
        nodeId = activityNodeId(m_runner ? m_runner->stage() : QString());
    }

    QVariantMap result;
    result.insert(QStringLiteral("nodeId"), nodeId);
    const QVariantList stages = workflowStages();
    for (int index = 0; index < stages.size(); ++index) {
        const QVariantMap candidate = stages.at(index).toMap();
        const QString actionNodeId = candidate.value(QStringLiteral("actionNodeId")).toString();
        bool matches = nodeId == actionNodeId;
        for (const QVariant &productionNode : candidate.value(
                 QStringLiteral("productionNodeIds")).toList()) {
            matches = matches || nodeId == productionNode.toString();
        }
        if (!matches) continue;

        result.insert(QStringLiteral("stageId"), candidate.value(QStringLiteral("id")));
        result.insert(QStringLiteral("title"), candidate.value(QStringLiteral("title")));
        result.insert(QStringLiteral("index"), index + 1);
        result.insert(QStringLiteral("count"), stages.size());

        const QVariantMap configuration = m_workflowNodeConfigurations.value(actionNodeId).toMap();
        const QVariantMap parameters = configuration.value(QStringLiteral("parameters")).toMap();
        const QString provider = configuration.value(
            QStringLiteral("executionProvider"), parameters.value(
                QStringLiteral("executionProvider"))).toString().trimmed().toLower();
        const QString model = configuration.value(
            QStringLiteral("modelId"), parameters.value(QStringLiteral("modelId"))).toString().trimmed();
        if (provider == QStringLiteral("colab-direct"))
            result.insert(QStringLiteral("route"), QStringLiteral("Direct Colab GPU"));
        else if (provider == QStringLiteral("api-gateway"))
            result.insert(QStringLiteral("route"), QStringLiteral("API Gateway"));
        else if (!provider.isEmpty() || !model.isEmpty()
                 || actionNodeId == QStringLiteral("ingest")
                 || actionNodeId == QStringLiteral("media-input"))
            result.insert(QStringLiteral("route"), QStringLiteral("Local CPU"));
        result.insert(QStringLiteral("model"), model);
        break;
    }

    if (result.value(QStringLiteral("title")).toString().isEmpty()) {
        result.insert(QStringLiteral("title"),
                      m_automaticSetupActive ? QStringLiteral("Preparing workflow")
                                             : QStringLiteral("Dubbing"));
        result.insert(QStringLiteral("count"), stages.size());
    }
    if (m_automaticSetupActive) {
        result.insert(QStringLiteral("status"), m_automaticStatusText);
    } else if (m_runner) {
        const QString status = m_runner->activityStatus().trimmed();
        if (!status.isEmpty()) result.insert(QStringLiteral("status"), status);
        const QVariantMap transfer = m_runner->activityTransferProgress();
        if (!transfer.isEmpty()) result.insert(QStringLiteral("artifactTransfer"), transfer);
    }
    return result;
}

int DubbingController::progress() const
{
    if (m_automaticSetupActive) {
        const QVariantList downloads = automaticSetupDownloads();
        if (downloads.isEmpty()) return 0;
        qint64 received = 0;
        qint64 total = 0;
        for (const QVariant &entry : downloads) {
            const QVariantMap download = entry.toMap();
            received += download.value(QStringLiteral("bytesReceived")).toLongLong();
            total += download.value(QStringLiteral("bytesTotal")).toLongLong();
        }
        // Download byte counts are the only measurable part of automatic
        // setup. Loading/installing a model has no truthful percentage.
        return total > 0 ? qBound(0, int(received * 100 / total), 100) : 0;
    }
    if (m_translationFix && m_translationFix->busy())
        return m_translationFix->progress();
    return m_workflowRunner && m_workflowRunner->running() ? m_workflowRunner->progress() : m_runner->progress();
}

bool DubbingController::progressAvailable() const
{
    if (m_automaticSetupActive) {
        const QVariantList downloads = automaticSetupDownloads();
        qint64 total = 0;
        for (const QVariant &entry : downloads)
            total += entry.toMap().value(QStringLiteral("bytesTotal")).toLongLong();
        return total > 0;
    }
    if (m_translationFix && m_translationFix->busy()) return true;
    if (m_workflowRunner && m_workflowRunner->running())
        return m_workflowRunner->progressAvailable();
    // Individual manual nodes report heterogeneous data. Until their runner
    // exposes a measured unit, show a working state rather than a made-up %.
    return false;
}

bool DubbingController::settingsLocked() const
{
    return m_automaticSetupActive
        || (m_workflowMode == QStringLiteral("automatic") && processing());
}

QString DubbingController::lastError() const
{
    if (m_translationFix && !m_translationFix->lastError().isEmpty())
        return m_translationFix->lastError();
    return (m_workflowRunner && !m_workflowRunner->error().isEmpty()) ? m_workflowRunner->error() : m_runner->lastError();
}

bool DubbingController::translationFixing() const
{
    return m_translationFix && m_translationFix->busy();
}

int DubbingController::translationFixProgress() const
{
    return m_translationFix ? m_translationFix->progress() : 0;
}

QString DubbingController::translationFixStatus() const
{
    return m_translationFix ? m_translationFix->statusText() : QString();
}

QVariantMap DubbingController::translationFixConfiguration() const
{
    return m_translationFix ? m_translationFix->configuration() : QVariantMap();
}

int DubbingController::translationFixCandidateCount() const
{
    return DubbingTranslationFixService::eligibleSegmentCount(
        m_project.segments, m_project.targetLanguage);
}

QString DubbingController::adaptiveProvider() const
{
    return translationFixConfiguration().value(QStringLiteral("provider"),
                                               QStringLiteral("lmstudio")).toString();
}

bool DubbingController::adaptiveReady() const
{
    const QVariantMap config = translationFixConfiguration();
    if (!config.value(QStringLiteral("configured")).toBool()) return false;
    const QString provider = adaptiveProvider();
    if (provider == QStringLiteral("colab-direct")) {
        ColabSession *session = colabSessionForStage(QStringLiteral("adaptive-llm"));
        QString routeError;
        return session && session->hasVerifiedRoute(
            QStringLiteral("llm-chat"),
            config.value(QStringLiteral("model")).toString(), &routeError);
    }
    if (provider == QStringLiteral("local")) {
        StudioConfiguration selection;
        selection.capabilityId = QStringLiteral("llm-chat");
        selection.familyId = config.value(QStringLiteral("model")).toString();
        selection.runtimeId = config.value(QStringLiteral("runtimeId")).toString();
        selection.runtimeVersion = config.value(QStringLiteral("runtimeVersion")).toString();
        selection.selectedFiles = config.value(QStringLiteral("selectedFiles")).toMap();
        return StudioConfigurationResolver::resolve(selection).isValid;
    }
    if (provider == QStringLiteral("cli")) {
        const QString cliAgent = config.value(QStringLiteral("cliAgent"), QStringLiteral("claude")).toString();
        return !DubbingTranslationFixService::cliExecutablePath(cliAgent).isEmpty();
    }
    return !config.value(QStringLiteral("serverUrl")).toString().trimmed().isEmpty()
        && !config.value(QStringLiteral("model")).toString().trimmed().isEmpty();
}

QString DubbingController::adaptiveStatusText() const
{
    if (!adaptiveReady()) return QStringLiteral("LLM setup required");
    const QString provider = adaptiveProvider();
    if (provider == QStringLiteral("local")) {
        return translationFixConfiguration()
            .value(QStringLiteral("model"), QStringLiteral("Local LLM ready")).toString();
    }
    if (provider == QStringLiteral("cli")) {
        const QString cliAgent = translationFixConfiguration().value(QStringLiteral("cliAgent"), QStringLiteral("claude")).toString();
        const QString model = translationFixConfiguration().value(QStringLiteral("model")).toString();
        QString label = QStringLiteral("Claude Code");
        if (cliAgent == QStringLiteral("codex")) label = QStringLiteral("Codex CLI");
        else if (cliAgent == QStringLiteral("antigravity")) label = QStringLiteral("Antigravity");
        return (model.isEmpty() || model == QStringLiteral("default"))
            ? QStringLiteral("%1 (CLI)").arg(label)
            : QStringLiteral("%1 · %2").arg(label, model);
    }
    const QString model = translationFixConfiguration().value(QStringLiteral("model")).toString();
    if (provider == QStringLiteral("colab-direct"))
        return QStringLiteral("Direct Colab GPU · %1").arg(model);
    return provider == QStringLiteral("api")
        ? QStringLiteral("LLM API · %1").arg(model)
        : QStringLiteral("LM Studio · %1").arg(model);
}

QVariantMap DubbingController::firstCustomSetupIssue() const
{
    const QString transcriptSource = m_project.transcriptConfiguration.value(
        QStringLiteral("transcriptSource"), QStringLiteral("stt")).toString().trimmed().toLower();
    const QString persistedOcrRoute = m_project.transcriptConfiguration.value(
        QStringLiteral("ocrExecutionRoute")).toString().trimmed().toLower();
    const bool usesColabOcr = persistedOcrRoute == QStringLiteral("colab-gpu")
        || (persistedOcrRoute.isEmpty() && m_subtitleOcr
            && m_subtitleOcr->executionRoute() == QStringLiteral("colab-gpu"));
    const bool ocrRouteReady = m_subtitleOcr
        && (usesColabOcr ? m_subtitleOcr->colabRouteReady() : m_subtitleOcr->runtimeAvailable());
    if ((transcriptSource == QStringLiteral("ocr") || transcriptSource == QStringLiteral("stt+ocr"))
        && !ocrRouteReady) {
        return {{QStringLiteral("nodeId"), QStringLiteral("transcribe")},
                {QStringLiteral("setupKind"), QStringLiteral("subtitle-ocr-route")},
                {QStringLiteral("message"),
                 usesColabOcr
                    ? QStringLiteral("Connect and check the exact Colab Subtitle OCR worker before using OCR transcript source.")
                    : QStringLiteral("Install the Subtitle OCR runtime before using OCR transcript source.")}};
    }
    QList<QPair<QString, QString>> requiredNodes{
        {QStringLiteral("source-separate"), QStringLiteral("voice-isolation")},
        {QStringLiteral("translate"), QStringLiteral("translation")},
        {QStringLiteral("synthesize"), QStringLiteral("tts")}
    };
    if (transcriptSource != QStringLiteral("ocr"))
        requiredNodes.insert(1, {QStringLiteral("transcribe"), QStringLiteral("stt")});
    for (const auto &required : requiredNodes) {
        const QVariantMap selected = m_workflowNodeConfigurations.value(required.first).toMap();
        if (selected.isEmpty()) {
            return {{QStringLiteral("nodeId"), required.first},
                    {QStringLiteral("setupKind"), QStringLiteral("node-model")},
                    {QStringLiteral("message"),
                     QStringLiteral("Choose a model for the %1 node before running Custom dubbing.")
                         .arg(visibleStepForNode(required.first))}};
        }
        if (required.first == QStringLiteral("source-separate")
            || required.first == QStringLiteral("transcribe")
            || required.first == QStringLiteral("translate")
            || required.first == QStringLiteral("synthesize")) {
            const QVariantMap parameters = selected.value(QStringLiteral("parameters")).toMap();
            ExecutionProvider provider = ExecutionProvider::LocalDev;
            const QString providerId = selected.value(
                QStringLiteral("executionProvider"), parameters.value(QStringLiteral("executionProvider"),
                QStringLiteral("local-dev"))).toString();
            if (executionProviderFromId(providerId, &provider)
                && provider != ExecutionProvider::LocalDev) {
                const QString modelId = selected.value(
                    QStringLiteral("modelId"), parameters.value(QStringLiteral("modelId"))).toString().trimmed();
                if (modelId.isEmpty()) {
                    return {{QStringLiteral("nodeId"), required.first},
                            {QStringLiteral("setupKind"), QStringLiteral("node-model")},
                            {QStringLiteral("message"),
                                 QStringLiteral("Choose a %1 model for the %2 node before running Custom dubbing.")
                                 .arg(executionProviderDisplayName(provider), visibleStepForNode(required.first))}};
                }
                if (provider == ExecutionProvider::ColabDirect
                    && !DubbingColabModelRoutes::supports(required.first, modelId)) {
                    return {{QStringLiteral("nodeId"), required.first},
                            {QStringLiteral("setupKind"), QStringLiteral("node-model")},
                            {QStringLiteral("message"),
                             QStringLiteral("The selected model has no exact Colab notebook for the %1 node.")
                                 .arg(visibleStepForNode(required.first))}};
                }
                continue;
            }
        }
        StudioConfiguration configuration;
        configuration.capabilityId = required.second;
        configuration.familyId = selected.value(QStringLiteral("familyId")).toString();
        configuration.runtimeId = selected.value(QStringLiteral("runtimeId")).toString();
        configuration.runtimeVersion = selected.value(QStringLiteral("runtimeVersion")).toString();
        configuration.selectedFiles = selected.value(QStringLiteral("selectedFiles")).toMap();
        if (!StudioConfigurationResolver::resolve(configuration).isValid) {
            return {{QStringLiteral("nodeId"), required.first},
                    {QStringLiteral("setupKind"), QStringLiteral("node-model")},
                    {QStringLiteral("message"),
                     QStringLiteral("The selected model or runtime for %1 is not installed. Choose an available setup.")
                         .arg(visibleStepForNode(required.first))}};
        }
    }
    const bool rewriteEnabled =
        m_project.durationControl.value(QStringLiteral("enabled"), true).toBool()
        && m_project.durationControl.value(QStringLiteral("autoRewrite"), true).toBool();
    if (rewriteEnabled && !adaptiveReady()) {
        return {{QStringLiteral("nodeId"), QStringLiteral("translate")},
                {QStringLiteral("setupKind"), QStringLiteral("rewrite-model")},
                {QStringLiteral("message"),
                 QStringLiteral("Choose a local, CLI, or API rewrite model for the Translate node, or turn off automatic rewriting.")}};
    }
    return {};
}

bool DubbingController::customReady() const
{
    return firstCustomSetupIssue().isEmpty();
}

QString DubbingController::customStatusText() const
{
    const QVariantMap issue = firstCustomSetupIssue();
    if (!issue.isEmpty()) return issue.value(QStringLiteral("message")).toString();
    if (!m_project.durationControl.value(QStringLiteral("autoRewrite"), true).toBool())
        return QStringLiteral("Custom models ready; long translations require manual review");
    return QStringLiteral("Custom models ready; rewrite: %1").arg(adaptiveStatusText());
}

QString DubbingController::previewPath() const
{
    return m_runner->previewPath();
}

QString DubbingController::dubbedVocalPath() const
{
    return m_runner->dubbedVocalPath();
}

QUrl DubbingController::playbackMediaUrl() const
{
    const QString exported = m_runner ? m_runner->exportPath() : QString();
    const QString suffix = QFileInfo(exported).suffix().toLower();
    if (!exported.isEmpty() && QFileInfo::exists(exported)
        && (suffix == QStringLiteral("mp4") || suffix == QStringLiteral("mkv")
            || suffix == QStringLiteral("mov") || suffix == QStringLiteral("webm")
            || suffix == QStringLiteral("avi")))
        return QUrl::fromLocalFile(exported);
    return sourceMediaUrl();
}

QString DubbingController::exportPath() const
{
    return m_runner->exportPath();
}

void DubbingController::setRemoteServices(Settings *settings, ColabSession *translationSession,
                                           ColabSession *ttsSession, ColabSession *voiceCloneSession,
                                           ColabSession *separationSession,
                                           ColabSession *alignmentSession,
                                           ColabSession *mediaDownloadSession)
{
    m_settings = settings;
    if (m_runner) {
        m_runner->setRemoteServices(settings, translationSession, ttsSession,
                                    voiceCloneSession, separationSession,
                                    alignmentSession,
                                    AppController::instance()
                                        ? AppController::instance()->colabChatSession() : nullptr);
    }
    if (m_translationFix) m_translationFix->setDirectColabSession(
        AppController::instance() ? AppController::instance()->colabChatSession() : nullptr);
    // The sessions remain the sole holders of transient URLs/tokens.  Dubbing
    // observes verification results only to remember which exact model was
    // checked in this process; the snapshot deliberately contains no secret.
    observeColabSession(QStringLiteral("source-separate"), separationSession);
    observeColabSession(QStringLiteral("transcribe"),
                        AppController::instance() ? AppController::instance()->colabSttSession() : nullptr);
    observeColabSession(QStringLiteral("subtitle-ocr"),
                        AppController::instance() ? AppController::instance()->colabSubtitleOcrSession() : nullptr);
    observeColabSession(QStringLiteral("translate"), translationSession);
    observeColabSession(QStringLiteral("synthesize"), ttsSession);
    Q_UNUSED(voiceCloneSession);
    observeColabSession(QStringLiteral("alignment"), alignmentSession);
    m_mediaDownloadSession = mediaDownloadSession;
    if (m_colabMediaDownload) m_colabMediaDownload->setSession(mediaDownloadSession);
    observeColabSession(QStringLiteral("media-download"), mediaDownloadSession);
    observeColabSession(QStringLiteral("adaptive-llm"),
                        AppController::instance() ? AppController::instance()->colabChatSession() : nullptr);
    emit colabSetupChanged();
}

QString DubbingController::colabCapabilityForStage(const QString &stageId)
{
    if (stageId == QStringLiteral("media-download")) return QStringLiteral("media-download");
    if (stageId == QStringLiteral("source-separate")) return QStringLiteral("voice-isolation");
    if (stageId == QStringLiteral("transcribe")) return QStringLiteral("stt");
    if (stageId == QStringLiteral("subtitle-ocr")) return QStringLiteral("subtitle-ocr");
    if (stageId == QStringLiteral("translate")) return QStringLiteral("translation");
    if (stageId == QStringLiteral("synthesize")) return QStringLiteral("tts");
    if (stageId == QStringLiteral("alignment")) return QStringLiteral("forced-alignment");
    if (stageId == QStringLiteral("adaptive-llm")) return QStringLiteral("llm-chat");
    return {};
}

QStringList extractedSharedMediaUrls(const QString &pastedText)
{
    // Copy/share text from Douyin, TikTok and similar services contains a
    // short code and descriptive text around the real public URL.  Only pass
    // the URL to the resolver; share text is neither sent nor persisted.
    // Keep this deliberately ASCII-only.  URL extraction happens before URI
    // parsing and QRegularExpression's Windows PCRE build does not accept the
    // JavaScript-style Unicode escapes that were previously used here.  The
    // copied Douyin form separates its URL with whitespace, while trailing
    // ASCII share punctuation is stripped below.
    static const QRegularExpression urlPattern(
        QStringLiteral(R"((https?://[^\s<>"']+))"),
        QRegularExpression::CaseInsensitiveOption);
    QStringList urls;
    QRegularExpressionMatchIterator matches = urlPattern.globalMatch(pastedText);
    while (matches.hasNext()) {
        QString url = matches.next().captured(1).trimmed();
        while (!url.isEmpty() && QStringLiteral(".,;:!?)]}").contains(url.back()))
            url.chop(1);
        if (!url.isEmpty()) urls.append(url);
    }
    if (!urls.isEmpty()) return urls;

    // Preserve the previous direct-input behavior when no explicit HTTP(S)
    // URL was found, so a concise valid URL still reaches the normal validator.
    return pastedText.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
}

ExecutionProvider configuredSynthesisProvider(const QVariantMap &configuration)
{
    const QVariantMap parameters = configuration.value(QStringLiteral("parameters")).toMap();
    ExecutionProvider provider = ExecutionProvider::LocalDev;
    executionProviderFromId(configuration.value(
        QStringLiteral("executionProvider"),
        parameters.value(QStringLiteral("executionProvider"),
                         QStringLiteral("local-dev"))).toString(), &provider);
    return provider;
}

ColabSession *DubbingController::colabSessionForStage(const QString &stageId) const
{
    if (stageId == QStringLiteral("media-download")) return m_mediaDownloadSession;
    AppController *app = AppController::instance();
    if (!app) return nullptr;
    if (stageId == QStringLiteral("source-separate")) return app->colabSeparationSession();
    if (stageId == QStringLiteral("transcribe")) return app->colabSttSession();
    if (stageId == QStringLiteral("subtitle-ocr")) return app->colabSubtitleOcrSession();
    if (stageId == QStringLiteral("translate")) return app->colabTranslationSession();
    if (stageId == QStringLiteral("synthesize")) return app->colabTtsSession();
    if (stageId == QStringLiteral("alignment")) return app->colabAlignmentSession();
    if (stageId == QStringLiteral("adaptive-llm")) return app->colabChatSession();
    return nullptr;
}

QString DubbingController::selectedColabModelForStage(const QString &stageId) const
{
    if (stageId == QStringLiteral("media-download")) return QStringLiteral("yt-dlp-media-download");
    if (stageId == QStringLiteral("adaptive-llm")) {
        const QString configured = translationFixConfiguration().value(
            QStringLiteral("model")).toString().trimmed().toLower();
        return configured.isEmpty()
            ? DubbingColabModelRoutes::defaultModelForNode(stageId) : configured;
    }
    if (stageId == QStringLiteral("subtitle-ocr")) {
        const QString configured = m_project.transcriptConfiguration.value(
            QStringLiteral("ocrColabModelId")).toString().trimmed().toLower();
        return configured.isEmpty()
            ? DubbingColabModelRoutes::defaultModelForNode(stageId)
            : configured;
    }
    if (stageId == QStringLiteral("transcribe")) {
        const QString persisted = m_project.transcriptConfiguration.value(
            QStringLiteral("sttModelId")).toString().trimmed().toLower();
        if (!persisted.isEmpty()) return persisted;
    }
    const QString nodeId = stageId == QStringLiteral("alignment") ? QStringLiteral("alignment") : stageId;
    const QVariantMap configuration = m_workflowNodeConfigurations.value(
        stageId == QStringLiteral("alignment") ? QStringLiteral("transcribe") : nodeId).toMap();
    const QVariantMap parameters = configuration.value(QStringLiteral("parameters")).toMap();
    QString model;
    if (stageId == QStringLiteral("alignment"))
        model = parameters.value(QStringLiteral("alignmentModelId")).toString();
    else
        model = parameters.value(QStringLiteral("modelId")).toString();
    if (model.trimmed().isEmpty()) model = DubbingColabModelRoutes::defaultModelForNode(nodeId);
    return model.trimmed().toLower();
}

bool DubbingController::stageUsesDirectColab(const QString &stageId) const
{
    if (stageId == QStringLiteral("adaptive-llm")) {
        const bool rewriteRequired = m_project.dubbingQuality == QStringLiteral("adaptive")
            || (m_project.dubbingQuality == QStringLiteral("custom")
                && m_project.durationControl.value(QStringLiteral("enabled"), true).toBool()
                && m_project.durationControl.value(QStringLiteral("autoRewrite"), true).toBool());
        return rewriteRequired && adaptiveProvider() == QStringLiteral("colab-direct");
    }
    if (stageId == QStringLiteral("subtitle-ocr")) {
        const QString source = m_project.transcriptConfiguration.value(
            QStringLiteral("transcriptSource"), QStringLiteral("stt")).toString().trimmed().toLower();
        const QString persistedRoute = m_project.transcriptConfiguration.value(
            QStringLiteral("ocrExecutionRoute")).toString().trimmed().toLower();
        const bool usesOcr = source == QStringLiteral("ocr") || source == QStringLiteral("stt+ocr");
        const bool routeSelected = persistedRoute == QStringLiteral("colab-gpu")
            || (m_subtitleOcr && m_subtitleOcr->executionRoute() == QStringLiteral("colab-gpu"));
        return usesOcr && routeSelected;
    }
    if (stageId == QStringLiteral("transcribe")
        && m_project.transcriptConfiguration.value(QStringLiteral("transcriptSource"),
                                                    QStringLiteral("stt")).toString().trimmed().toLower()
               == QStringLiteral("ocr")) {
        return false;
    }
    const QString configurationNode = stageId == QStringLiteral("alignment") ? QStringLiteral("transcribe") : stageId;
    const QVariantMap configuration = m_workflowNodeConfigurations.value(configurationNode).toMap();
    const QVariantMap parameters = configuration.value(QStringLiteral("parameters")).toMap();
    const QString persistedProvider = stageId == QStringLiteral("transcribe")
        ? m_project.transcriptConfiguration.value(QStringLiteral("sttExecutionProvider"))
              .toString().trimmed().toLower()
        : QString();
    const QString provider = persistedProvider.isEmpty()
        ? configuration.value(QStringLiteral("executionProvider"),
              parameters.value(QStringLiteral("executionProvider"))).toString().trimmed().toLower()
        : persistedProvider;
    if (stageId == QStringLiteral("alignment"))
        return parameters.value(QStringLiteral("refineAlignmentWithColab")).toBool();
    return provider == QStringLiteral("colab-direct");
}

bool DubbingController::snapshotSelectedColabStagesForWorkflow()
{
    // A Direct Colab route may be configured from either the global Dubbing panel
    // or its feature-specific panel. In both cases, capture only the verified
    // model identifier immediately before a workflow begins. URLs and tokens stay
    // exclusively in ColabSession's process-memory state.
    for (const QVariant &entry : colabSetupStages()) {
        const QVariantMap stage = entry.toMap();
        if (!stage.value(QStringLiteral("selectedForDirectColab")).toBool()) continue;

        const QString stageId = stage.value(QStringLiteral("id")).toString();
        const QString capability = stage.value(QStringLiteral("capability")).toString();
        const QString model = stage.value(QStringLiteral("modelId")).toString();
        ColabSession *session = colabSessionForStage(stageId);
        QString routeError;
        if (!session || !session->hasVerifiedRoute(capability, model, &routeError)) {
            m_colabSetupSnapshots.remove(stageId);
            const QString detail = routeError.trimmed().isEmpty()
                ? QStringLiteral("Connect and check its exact model in Colab setup.")
                : routeError;
            setError(QStringLiteral("Direct Colab is not ready for %1: %2")
                         .arg(stage.value(QStringLiteral("title")).toString(), detail));
            emit colabSetupChanged();
            return false;
        }
        m_colabSetupSnapshots.insert(stageId, model);
    }
    emit colabSetupChanged();
    return true;
}

void DubbingController::observeColabSession(const QString &stageId, ColabSession *session)
{
    if (m_colabSetupConnections.contains(stageId)) {
        QObject::disconnect(m_colabSetupConnections.take(stageId));
    }
    if (!session) return;
    m_colabSetupConnections.insert(stageId, connect(
        session, &ColabSession::verificationFinished, this,
        [this, stageId](bool success, const QString &) {
            refreshColabSetupSnapshot(stageId, success);
        }));
}

void DubbingController::refreshColabSetupSnapshot(const QString &stageId, bool verified)
{
    ColabSession *session = colabSessionForStage(stageId);
    const QString capability = colabCapabilityForStage(stageId);
    const QString model = selectedColabModelForStage(stageId);
    QString routeError;
    const bool valid = verified && session
        && session->hasVerifiedRoute(capability, model, &routeError);
    if (valid) {
        m_colabSetupSnapshots.insert(stageId, model);
    } else {
        m_colabSetupSnapshots.remove(stageId);
    }
    m_colabSetupPendingChecks.remove(stageId);
    if (m_colabSetupPendingChecks.isEmpty()) {
        int selected = 0;
        int verifiedCount = 0;
        for (const QVariant &entry : colabSetupStages()) {
            const QVariantMap stage = entry.toMap();
            if (!stage.value(QStringLiteral("selectedForDirectColab")).toBool()) continue;
            ++selected;
            verifiedCount += stage.value(QStringLiteral("verified")).toBool() ? 1 : 0;
        }
        m_colabSetupSummary = selected == 0
            ? QStringLiteral("No workflow stage is currently set to Direct Colab.")
            : QStringLiteral("%1 of %2 selected Direct Colab stage(s) verified.")
                  .arg(verifiedCount).arg(selected);
    }
    emit colabSetupChanged();
    emit workflowChanged();
}

QVariantList DubbingController::colabSetupStages() const
{
    QList<QPair<QString, QString>> definitions{
        {QStringLiteral("source-separate"), QStringLiteral("Isolator (Vocals/Background)")},
        {QStringLiteral("transcribe"), QStringLiteral("Transcribe/STT")},
        {QStringLiteral("subtitle-ocr"), QStringLiteral("Subtitle OCR (Transcribe)")},
        {QStringLiteral("translate"), QStringLiteral("Translation")},
        {QStringLiteral("synthesize"), QStringLiteral("TTS / Text to Speech")},
        {QStringLiteral("alignment"), QStringLiteral("Alignment")},
    };
    if (stageUsesDirectColab(QStringLiteral("adaptive-llm"))) {
        definitions.append({QStringLiteral("adaptive-llm"),
                            QStringLiteral("Translate (Adaptive LLM)")});
    }
    const QString transcriptSource = m_project.transcriptConfiguration.value(
        QStringLiteral("transcriptSource"), QStringLiteral("stt")).toString().trimmed().toLower();
    QVariantList result;
    for (const auto &definition : definitions) {
        const QString stageId = definition.first;
        const QString capability = colabCapabilityForStage(stageId);
        const QString model = selectedColabModelForStage(stageId);
        ColabSession *session = colabSessionForStage(stageId);
        QString diagnostic;
        const bool verified = session && session->hasVerifiedRoute(capability, model, &diagnostic);
        if (diagnostic.isEmpty() && session)
            diagnostic = session->verificationMessage().isEmpty()
                ? session->lastError() : session->verificationMessage();
        if (diagnostic.isEmpty())
            diagnostic = QStringLiteral("Not connected for this exact model.");
        const bool activeForTranscriptSource =
            (stageId != QStringLiteral("transcribe") || transcriptSource != QStringLiteral("ocr"))
            && (stageId != QStringLiteral("subtitle-ocr") || transcriptSource != QStringLiteral("stt"));
        const QString notUsedReason = activeForTranscriptSource ? QString()
            : (stageId == QStringLiteral("transcribe")
                   ? QStringLiteral("Not used: this project is set to OCR only.")
                   : QStringLiteral("Not used: this project is set to STT only."));
        result.append(QVariantMap{
            {QStringLiteral("id"), stageId},
            {QStringLiteral("title"), definition.second},
            {QStringLiteral("capability"), capability},
            {QStringLiteral("modelId"), model},
            // Current exact-model notebooks each expose one immutable GPU
            // configuration.  Make that explicit in every Dubbing surface;
            // it is not a Local CPU model-file variant.
            {QStringLiteral("variant"), session && !session->expectedVariant().isEmpty()
                ? session->expectedVariant() : QStringLiteral("fixed")},
            {QStringLiteral("notebookFile"), DubbingColabModelRoutes::notebookForModel(stageId, model)},
            {QStringLiteral("activeForTranscriptSource"), activeForTranscriptSource},
            {QStringLiteral("notUsedReason"), notUsedReason},
            {QStringLiteral("selectedForDirectColab"), stageUsesDirectColab(stageId)},
            {QStringLiteral("active"), session && session->isActive()},
            {QStringLiteral("checking"), session && session->isChecking()},
            {QStringLiteral("verified"), verified},
            {QStringLiteral("snapshotValid"), m_colabSetupSnapshots.value(stageId) == model && verified},
            {QStringLiteral("diagnostic"), diagnostic}
        });
    }
    return result;
}

QVariantMap DubbingController::mediaDownloadColabSetup() const
{
    const QString stageId = QStringLiteral("media-download");
    const QString modelId = QStringLiteral("yt-dlp-media-download");
    QString diagnostic;
    const bool verified = m_mediaDownloadSession
        && m_mediaDownloadSession->hasVerifiedRoute(stageId, modelId, &diagnostic);
    if (diagnostic.isEmpty() && m_mediaDownloadSession) {
        diagnostic = m_mediaDownloadSession->verificationMessage().isEmpty()
            ? m_mediaDownloadSession->lastError() : m_mediaDownloadSession->verificationMessage();
    }
    return {{QStringLiteral("id"), stageId},
            {QStringLiteral("title"), QStringLiteral("Download public media in Colab")},
            {QStringLiteral("capability"), stageId},
            {QStringLiteral("modelId"), modelId},
            {QStringLiteral("variant"), QStringLiteral("fixed")},
            {QStringLiteral("notebookFile"), DubbingColabModelRoutes::notebookForModel(
                stageId, modelId)},
            {QStringLiteral("active"), m_mediaDownloadSession && m_mediaDownloadSession->isActive()},
            {QStringLiteral("checking"), m_mediaDownloadSession && m_mediaDownloadSession->isChecking()},
            {QStringLiteral("verified"), verified},
            {QStringLiteral("diagnostic"), diagnostic}};
}

bool DubbingController::connectWorkflowColabStage(const QString &stageId, const QString &modelId,
                                                   const QString &workerUrl, const QString &bearerToken)
{
    const QString normalizedStage = stageId.trimmed().toLower();
    const QString transcriptSource = m_project.transcriptConfiguration.value(
        QStringLiteral("transcriptSource"), QStringLiteral("stt")).toString().trimmed().toLower();
    if ((normalizedStage == QStringLiteral("transcribe") && transcriptSource == QStringLiteral("ocr"))
        || (normalizedStage == QStringLiteral("subtitle-ocr") && transcriptSource == QStringLiteral("stt"))) {
        setError(QStringLiteral("This Direct Colab worker is not used by the selected transcript source."));
        return false;
    }
    const QString normalizedModel = modelId.trimmed().toLower();
    const QString capability = colabCapabilityForStage(normalizedStage);
    ColabSession *session = colabSessionForStage(normalizedStage);
    if (capability.isEmpty() || !session || normalizedModel.isEmpty()) {
        setError(QStringLiteral("This Dubbing Colab stage is unavailable."));
        return false;
    }
    if (!selectWorkflowColabModel(normalizedStage, normalizedModel)) return false;
    m_colabSetupSnapshots.remove(normalizedStage);
    if (!session->connectTemporaryWorker(workerUrl, bearerToken, capability, normalizedModel)) {
        setError(session->lastError().isEmpty()
                     ? QStringLiteral("Could not start the Direct Colab verification.")
                     : session->lastError());
        emit colabSetupChanged();
        return false;
    }
    m_colabSetupPendingChecks.insert(normalizedStage);
    m_colabSetupSummary = QStringLiteral("Checking %1 / %2 on Direct Colab.")
        .arg(capability, normalizedModel);
    emit colabSetupChanged();
    return true;
}

bool DubbingController::checkWorkflowColabStage(const QString &stageId)
{
    const QString normalizedStage = stageId.trimmed().toLower();
    ColabSession *session = colabSessionForStage(normalizedStage);
    if (!session || !session->isActive()) {
        setError(QStringLiteral("Connect the %1 Direct Colab worker before checking it.")
                     .arg(colabCapabilityForStage(normalizedStage)));
        return false;
    }
    m_colabSetupSnapshots.remove(normalizedStage);
    if (!session->checkConnection()) {
        setError(session->lastError().isEmpty()
                     ? QStringLiteral("Could not start the Direct Colab connection check.")
                     : session->lastError());
        return false;
    }
    m_colabSetupPendingChecks.insert(normalizedStage);
    m_colabSetupSummary = QStringLiteral("Rechecking %1.").arg(colabCapabilityForStage(normalizedStage));
    emit colabSetupChanged();
    return true;
}

void DubbingController::disconnectWorkflowColabStage(const QString &stageId)
{
    const QString normalizedStage = stageId.trimmed().toLower();
    if (ColabSession *session = colabSessionForStage(normalizedStage))
        session->disconnectTemporaryWorker();
    m_colabSetupSnapshots.remove(normalizedStage);
    m_colabSetupPendingChecks.remove(normalizedStage);
    m_colabSetupSummary = QStringLiteral("Disconnected %1 Direct Colab setup.")
        .arg(colabCapabilityForStage(normalizedStage));
    emit colabSetupChanged();
    emit workflowChanged();
}

bool DubbingController::validateAllWorkflowColabStages()
{
    m_colabSetupPendingChecks.clear();
    QStringList unavailable;
    int requested = 0;
    for (const QVariant &entry : colabSetupStages()) {
        const QVariantMap stage = entry.toMap();
        if (!stage.value(QStringLiteral("selectedForDirectColab")).toBool()) continue;
        const QString stageId = stage.value(QStringLiteral("id")).toString();
        ColabSession *session = colabSessionForStage(stageId);
        ++requested;
        m_colabSetupSnapshots.remove(stageId);
        if (!session || !session->isActive() || !session->checkConnection()) {
            unavailable.append(stage.value(QStringLiteral("title")).toString());
            continue;
        }
        m_colabSetupPendingChecks.insert(stageId);
    }
    if (requested == 0) {
        m_colabSetupSummary = QStringLiteral("No workflow stage is currently set to Direct Colab.");
        emit colabSetupChanged();
        return true;
    }
    if (!unavailable.isEmpty()) {
        m_colabSetupSummary = QStringLiteral("Could not check: %1.").arg(unavailable.join(QStringLiteral(", ")));
        emit colabSetupChanged();
        return false;
    }
    m_colabSetupSummary = QStringLiteral("Checking %1 Direct Colab stage(s).").arg(requested);
    emit colabSetupChanged();
    return true;
}

void DubbingController::setVoiceClonePresetService(VoiceClonePresetService *service)
{
    if (m_voiceClonePresetsService == service) return;
    QObject::disconnect(m_cloneVoicePresetsConnection);
    m_voiceClonePresetsService = service;
    if (m_voiceClonePresetsService) {
        m_cloneVoicePresetsConnection = connect(
            m_voiceClonePresetsService, &VoiceClonePresetService::presetsChanged,
            this, [this](const QString &) { refreshCloneVoicePresets(); });
    }
    refreshCloneVoicePresets();
    emit cloneVoiceSelectionChanged();
    emit workflowChanged();
}

void DubbingController::setSubtitleOcrController(SubtitleOcrController *controller)
{
    m_subtitleOcr = controller;
    applyStoredSubtitleOcrConfiguration();
    if (m_runner) m_runner->setSubtitleOcrController(controller);
}

QVariantMap DubbingController::dubbingOcrRoi() const
{
    if (m_subtitleOcr) {
        return {{QStringLiteral("x"), m_subtitleOcr->roiX()},
                {QStringLiteral("y"), m_subtitleOcr->roiY()},
                {QStringLiteral("width"), m_subtitleOcr->roiWidth()},
                {QStringLiteral("height"), m_subtitleOcr->roiHeight()}};
    }
    return m_project.transcriptConfiguration.value(QStringLiteral("ocrRoi")).toMap();
}

int DubbingController::unresolvedTranscriptConflictCount() const
{
    int count = 0;
    for (const QVariant &value : m_project.segments) {
        const QVariantMap segment = value.toMap();
        if (segment.value(QStringLiteral("fusionNeedsReview")).toBool()
            || segment.value(QStringLiteral("fusionStatus")).toString()
                   == QStringLiteral("conflict")) {
            ++count;
        }
    }
    return count;
}

bool DubbingController::hasUnresolvedTranscriptConflicts() const
{
    return unresolvedTranscriptConflictCount() > 0;
}

bool DubbingController::dubbingOcrRoiVisible() const
{
    const QString source = m_project.transcriptConfiguration.value(
        QStringLiteral("transcriptSource"), QStringLiteral("stt")).toString().trimmed().toLower();
    // This is a mode capability, rather than a video-presence check.  The QML
    // overlay additionally requires a video content rect, while its controls
    // can explain why they are disabled before the user selects media.
    return source == QStringLiteral("ocr") || source == QStringLiteral("stt+ocr");
}

bool DubbingController::setDubbingOcrRoi(const QVariantMap &roi)
{
    if (!m_subtitleOcr || !dubbingOcrRoiVisible()) return false;
    const bool accepted = m_subtitleOcr->setRoi(roi.value(QStringLiteral("x")).toDouble(),
        roi.value(QStringLiteral("y")).toDouble(), roi.value(QStringLiteral("width")).toDouble(),
        roi.value(QStringLiteral("height")).toDouble());
    if (!accepted) return false;
    // The OCR cache key includes this normalized rectangle. Keeping STT
    // segments intact means switching OCR/STT/OCR never destroys STT work.
    m_project.transcriptConfiguration.insert(QStringLiteral("ocrRoi"), dubbingOcrRoi());
    persistAfterEdit();
    emit projectChanged();
    emit workflowChanged();
    return true;
}

bool DubbingController::presetDubbingOcrLowerRegion()
{
    if (!m_subtitleOcr || !dubbingOcrRoiVisible()) return false;
    m_subtitleOcr->setLowerRegionPreset();
    m_project.transcriptConfiguration.insert(QStringLiteral("ocrRoi"), dubbingOcrRoi());
    persistAfterEdit();
    emit projectChanged();
    return true;
}

bool DubbingController::resetDubbingOcrRoi()
{
    if (!m_subtitleOcr || !dubbingOcrRoiVisible()) return false;
    m_subtitleOcr->resetRoi();
    m_project.transcriptConfiguration.insert(QStringLiteral("ocrRoi"), dubbingOcrRoi());
    persistAfterEdit();
    emit projectChanged();
    return true;
}

bool DubbingController::previewDubbingOcrCrop(qint64 positionMs)
{
    return m_subtitleOcr && dubbingOcrRoiVisible()
        && m_subtitleOcr->requestCropPreview(positionMs);
}

void DubbingController::applyStoredSubtitleOcrConfiguration()
{
    if (!m_subtitleOcr) return;
    const QString route = m_project.transcriptConfiguration.value(
        QStringLiteral("ocrExecutionRoute")).toString().trimmed().toLower();
    if (route == QStringLiteral("local-cpu") || route == QStringLiteral("colab-gpu"))
        m_subtitleOcr->setExecutionRoute(route);
    const QString localEngine = m_project.transcriptConfiguration.value(
        QStringLiteral("ocrLocalEngineId")).toString().trimmed().toLower();
    if (!localEngine.isEmpty()) m_subtitleOcr->setLocalEngine(localEngine);
    const QString model = m_project.transcriptConfiguration.value(
        QStringLiteral("ocrColabModelId")).toString().trimmed().toLower();
    if (!model.isEmpty()) m_subtitleOcr->setColabModelId(model);
    const QString language = m_project.transcriptConfiguration.value(
        QStringLiteral("ocrLanguage")).toString().trimmed();
    if (!language.isEmpty()) m_subtitleOcr->setOcrLanguage(language);
    const QVariantMap roi = m_project.transcriptConfiguration.value(QStringLiteral("ocrRoi")).toMap();
    if (!roi.isEmpty()) m_subtitleOcr->setRoi(roi.value(QStringLiteral("x")).toDouble(),
                                               roi.value(QStringLiteral("y")).toDouble(),
                                               roi.value(QStringLiteral("width")).toDouble(),
                                               roi.value(QStringLiteral("height")).toDouble());
    const qint64 interval = m_project.transcriptConfiguration.value(
        QStringLiteral("ocrSampleIntervalMs")).toLongLong();
    if (interval > 0) m_subtitleOcr->setSampleIntervalMs(interval);
    const double confidence = m_project.transcriptConfiguration.value(
        QStringLiteral("ocrMinimumConfidence")).toDouble();
    if (confidence > 0) m_subtitleOcr->setMinimumConfidence(confidence);
}

QVariantMap DubbingController::effectiveTranscriptConfiguration(bool captureOcrSettings)
{
    QVariantMap selected = m_workflowNodeConfigurations.value(QStringLiteral("transcribe")).toMap();
    QVariantMap parameters = selected.value(QStringLiteral("parameters")).toMap();
    if (parameters.isEmpty()) parameters = selected;
    for (auto it = m_project.transcriptConfiguration.cbegin();
         it != m_project.transcriptConfiguration.cend(); ++it) {
        parameters.insert(it.key(), it.value());
    }
    const QString persistedSttProvider = parameters.value(
        QStringLiteral("sttExecutionProvider")).toString().trimmed();
    const QString persistedSttModel = parameters.value(
        QStringLiteral("sttModelId")).toString().trimmed();
    if (!persistedSttProvider.isEmpty()) {
        parameters.insert(QStringLiteral("executionProvider"), persistedSttProvider);
        selected.insert(QStringLiteral("executionProvider"), persistedSttProvider);
    }
    if (!persistedSttModel.isEmpty()) {
        parameters.insert(QStringLiteral("modelId"), persistedSttModel);
        selected.insert(QStringLiteral("modelId"), persistedSttModel);
    }
    QString mode = parameters.value(QStringLiteral("transcriptSource"), QStringLiteral("stt"))
                       .toString().trimmed().toLower();
    if (mode != QStringLiteral("ocr") && mode != QStringLiteral("stt+ocr")) mode = QStringLiteral("stt");
    parameters.insert(QStringLiteral("transcriptSource"), mode);
    parameters.insert(QStringLiteral("fusionPolicy"),
                      DubbingTranscriptFusionService::normalizePolicy(
                          parameters.value(QStringLiteral("fusionPolicy"),
                                           QStringLiteral("ask")).toString()));
    if (captureOcrSettings && m_subtitleOcr) {
        const QVariantMap roi{{QStringLiteral("x"), m_subtitleOcr->roiX()},
                              {QStringLiteral("y"), m_subtitleOcr->roiY()},
                              {QStringLiteral("width"), m_subtitleOcr->roiWidth()},
                              {QStringLiteral("height"), m_subtitleOcr->roiHeight()}};
        parameters.insert(QStringLiteral("ocrLanguage"), m_subtitleOcr->ocrLanguage());
        parameters.insert(QStringLiteral("ocrExecutionRoute"), m_subtitleOcr->executionRoute());
        parameters.insert(QStringLiteral("ocrLocalEngineId"), m_subtitleOcr->localEngineId());
        parameters.insert(QStringLiteral("ocrLocalEngineVersion"), m_subtitleOcr->localEngineVersion());
        parameters.insert(QStringLiteral("ocrColabModelId"), m_subtitleOcr->colabModelId());
        parameters.insert(QStringLiteral("ocrRoi"), roi);
        parameters.insert(QStringLiteral("ocrSampleIntervalMs"), m_subtitleOcr->sampleIntervalMs());
        parameters.insert(QStringLiteral("ocrMinimumConfidence"), m_subtitleOcr->minimumConfidence());
    }
    m_project.transcriptConfiguration = {
        {QStringLiteral("transcriptSource"), parameters.value(QStringLiteral("transcriptSource"))},
        {QStringLiteral("fusionPolicy"), parameters.value(QStringLiteral("fusionPolicy"))},
        {QStringLiteral("sttExecutionProvider"), parameters.value(QStringLiteral("sttExecutionProvider"))},
        {QStringLiteral("sttModelId"), parameters.value(QStringLiteral("sttModelId"))},
        {QStringLiteral("ocrLanguage"), parameters.value(QStringLiteral("ocrLanguage"))},
        {QStringLiteral("ocrExecutionRoute"), parameters.value(QStringLiteral("ocrExecutionRoute"))},
        {QStringLiteral("ocrLocalEngineId"), parameters.value(QStringLiteral("ocrLocalEngineId"))},
        {QStringLiteral("ocrLocalEngineVersion"), parameters.value(QStringLiteral("ocrLocalEngineVersion"))},
        {QStringLiteral("ocrColabModelId"), parameters.value(QStringLiteral("ocrColabModelId"))},
        {QStringLiteral("ocrRoi"), parameters.value(QStringLiteral("ocrRoi"))},
        {QStringLiteral("ocrSampleIntervalMs"), parameters.value(QStringLiteral("ocrSampleIntervalMs"))},
        {QStringLiteral("ocrMinimumConfidence"), parameters.value(QStringLiteral("ocrMinimumConfidence"))}
    };
    selected.insert(QStringLiteral("parameters"), parameters);
    return selected;
}

QString DubbingController::cloneVoicePresetFamily() const
{
    const QVariantMap synthesis = m_workflowNodeConfigurations
        .value(QStringLiteral("synthesize")).toMap();
    const QVariantMap parameters = synthesis.value(QStringLiteral("parameters")).toMap();
    const QString configured = parameters.value(QStringLiteral("voiceCloneModelId"),
        parameters.value(QStringLiteral("modelId"))).toString().trimmed().toLower();
    return configured.isEmpty()
        ? DubbingColabModelRoutes::defaultModelForNode(QStringLiteral("synthesize"))
        : configured;
}

QVariantMap DubbingController::selectedCloneVoicePreset() const
{
    const QString selectedId = m_project.ttsVoiceId.trimmed();
    if (selectedId.startsWith(QStringLiteral("builtin:"))) return {};
    if (selectedId.isEmpty()) return {};
    for (const QVariant &entry : m_cloneVoicePresets) {
        const QVariantMap preset = entry.toMap();
        if (preset.value(QStringLiteral("id")).toString() == selectedId)
            return preset;
    }
    return {};
}

bool DubbingController::cloneVoiceSelectionValid() const
{
    const QString selectedId = m_project.ttsVoiceId.trimmed();
    if (selectedId.startsWith(QStringLiteral("builtin:"))) {
        const QString voice = selectedId.mid(QStringLiteral("builtin:").size()).trimmed();
        return !voice.isEmpty();
    }
    if (!cloneVoiceSelectionRequired()) return false;
    const QVariantMap preset = selectedCloneVoicePreset();
    const QString audioPath = PathUtils::urlToLocalPath(
        preset.value(QStringLiteral("audioPath")).toString());
    const bool validPreset = !preset.value(QStringLiteral("id")).toString().trimmed().isEmpty()
        && !audioPath.isEmpty() && QFileInfo(audioPath).isFile()
        && preset.value(QStringLiteral("valid"), true).toBool()
        && preset.value(QStringLiteral("compatible"), false).toBool();
    if (!validPreset)
        return false;

    const QVariantMap synthesis = m_workflowNodeConfigurations
        .value(QStringLiteral("synthesize")).toMap();
    const ExecutionProvider provider = configuredSynthesisProvider(synthesis);
    if (provider == ExecutionProvider::ApiGateway)
        return false;
    // Do not present a Local run as available when a loaded backend can only
    // clone a reference per segment. An unloaded model is handled separately
    // by the workflow's normal "load a TTS model" readiness state.
    return provider != ExecutionProvider::LocalDev || !m_tts || !m_tts->isModelLoaded()
        || localTtsSupportsSavedVoiceProfile(m_tts->familyConfig());
}

QString DubbingController::cloneVoiceSelectionError() const
{
    if (m_project.ttsVoiceId.trimmed().isEmpty()) {
        return m_cloneVoicePresets.isEmpty()
            ? QStringLiteral("Select a built-in TTS voice before generating dubbing audio. Saved voices can be created in Voice Cloning Studio.")
            : QStringLiteral("Select one TTS voice before generating dubbing audio.");
    }
    if (m_project.ttsVoiceId.startsWith(QStringLiteral("builtin:"))) return {};
    if (!cloneVoiceSelectionRequired())
        return QStringLiteral("Saved Voice Cloning library is unavailable. Select a built-in TTS voice or restore the library.");
    const QVariantMap preset = selectedCloneVoicePreset();
    if (preset.isEmpty())
        return QStringLiteral("The selected clone voice is no longer available. Select another saved voice; LA Studio will not substitute one automatically.");
    if (!preset.value(QStringLiteral("compatible"), false).toBool()) {
        return QStringLiteral("The selected saved voice is incompatible with the current TTS model family. Choose a compatible voice or change the TTS model.");
    }
    const QString validationError = preset.value(QStringLiteral("validationError")).toString().trimmed();
    if (!validationError.isEmpty())
        return QStringLiteral("The selected clone voice cannot be used: %1").arg(validationError);
    if (!QFileInfo(PathUtils::urlToLocalPath(
            preset.value(QStringLiteral("audioPath")).toString())).isFile()) {
        return QStringLiteral("The reference audio for the selected clone voice is missing. Repair or replace that preset before generating dubbing audio.");
    }
    const QVariantMap synthesis = m_workflowNodeConfigurations
        .value(QStringLiteral("synthesize")).toMap();
    const ExecutionProvider provider = configuredSynthesisProvider(synthesis);
    if (provider == ExecutionProvider::ApiGateway) {
        return QStringLiteral("This API Gateway TTS route cannot reuse saved clone voices. Select a built-in voice or Direct Colab; LA Studio will not substitute a voice.");
    }
    if (provider == ExecutionProvider::LocalDev && m_tts && m_tts->isModelLoaded()
        && !localTtsSupportsSavedVoiceProfile(m_tts->familyConfig())) {
        return QStringLiteral("The active local TTS runtime cannot reuse a saved voice profile. Load Qwen3-TTS or choose Direct Colab; LA Studio will not clone the voice again for each segment.");
    }
    return {};
}

void DubbingController::refreshCloneVoicePresets()
{
    QVariantList refreshed;
    if (m_voiceClonePresetsService) {
        const QString activeFamily = cloneVoicePresetFamily();
        for (const QVariant &entry : m_voiceClonePresetsService->allPresets()) {
            QVariantMap preset = entry.toMap();
            const QString id = preset.value(QStringLiteral("id")).toString().trimmed();
            const QString audioPath = PathUtils::urlToLocalPath(
                preset.value(QStringLiteral("audioPath")).toString());
            if (id.isEmpty()) continue;
            preset.insert(QStringLiteral("audioPath"), audioPath);
            preset.insert(QStringLiteral("compatible"),
                          preset.value(QStringLiteral("familyId")).toString() == activeFamily);
            refreshed.append(preset);
        }
    }
    if (m_cloneVoicePresets == refreshed) return;
    m_cloneVoicePresets = refreshed;
    emit cloneVoiceSelectionChanged();
    emit workflowChanged();
}

bool DubbingController::selectCloneVoicePreset(const QString &presetId)
{
    refreshCloneVoicePresets();
    const QString normalized = presetId.trimmed();
    if (normalized.isEmpty()) {
        setError(QStringLiteral("Select a saved clone voice before generating dubbing audio."));
        return false;
    }
    QVariantMap selected;
    for (const QVariant &entry : m_cloneVoicePresets) {
        if (entry.toMap().value(QStringLiteral("id")).toString() == normalized) {
            selected = entry.toMap();
            break;
        }
    }
    if (selected.isEmpty()) {
        setError(QStringLiteral("The selected clone voice is unavailable or its reference audio is missing."));
        return false;
    }
    if (!selected.value(QStringLiteral("compatible"), false).toBool()) {
        setError(QStringLiteral("That saved voice is incompatible with the selected TTS model family. Change the model or choose a compatible voice."));
        return false;
    }
    if (m_project.ttsVoiceId == normalized) return true;
    m_project.ttsVoiceId = normalized;
    m_project.cloneVoicePresetId = normalized;
    emit cloneVoiceSelectionChanged();
    emit projectChanged();
    emit workflowChanged();
    persistAfterEdit();
    return true;
}

bool DubbingController::applySelectedCloneVoiceToSynthesis(QVariantMap *settings)
{
    if (!settings) return false;
    refreshCloneVoicePresets();
    if (!cloneVoiceSelectionValid()) {
        setError(cloneVoiceSelectionError());
        return false;
    }
    const QString selectedId = m_project.ttsVoiceId.trimmed();
    settings->insert(QStringLiteral("ttsVoiceId"), selectedId);
    if (selectedId.startsWith(QStringLiteral("builtin:"))) {
        settings->insert(QStringLiteral("voice"), selectedId.mid(QStringLiteral("builtin:").size()));
    } else {
        // Dubbing only consumes a verified library preset.  It never receives
        // a source-media reference, consent form, or clone-model selection.
        settings->insert(QStringLiteral("savedTtsVoicePreset"), selectedCloneVoicePreset());
    }
    return true;
}

QVariantList DubbingController::ttsVoiceOptions() const
{
    QVariantList result;
    const QVariantMap synthesis = m_workflowNodeConfigurations.value(
        QStringLiteral("synthesize")).toMap();
    const QVariantMap parameters = synthesis.value(QStringLiteral("parameters")).toMap();
    QString voice = parameters.value(QStringLiteral("voice")).toString().trimmed();
    if (voice.isEmpty()) {
        voice = DubbingColabModelRoutes::defaultVoiceForTtsModel(
            parameters.value(QStringLiteral("modelId")).toString());
    }
    if (!voice.isEmpty()) {
        result.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("builtin:") + voice},
                                  {QStringLiteral("name"), QStringLiteral("Built-in TTS · %1").arg(voice)},
                                  {QStringLiteral("group"), QStringLiteral("Built-in TTS voices")},
                                  {QStringLiteral("kind"), QStringLiteral("builtin")},
                                  {QStringLiteral("valid"), true}});
    }
    for (const QVariant &entry : m_cloneVoicePresets) {
        QVariantMap option = entry.toMap();
        option.insert(QStringLiteral("group"), QStringLiteral("Saved Voice Cloning voices"));
        option.insert(QStringLiteral("kind"), QStringLiteral("saved-clone"));
        const QString savedVoiceName = option.value(QStringLiteral("name")).toString();
        const QString family = option.value(QStringLiteral("familyId")).toString();
        const bool compatible = option.value(QStringLiteral("compatible"), false).toBool();
        const bool valid = option.value(QStringLiteral("valid"), false).toBool();
        option.insert(QStringLiteral("name"), QStringLiteral("Saved voice · %1").arg(
            option.value(QStringLiteral("name")).toString()));
        option.insert(QStringLiteral("name"),
                      QStringLiteral("Saved clone [%1] - %2 - %3 - %4").arg(
                          savedVoiceName, family,
                          compatible ? QStringLiteral("Compatible") : QStringLiteral("Incompatible"),
                          valid ? QStringLiteral("Valid") : QStringLiteral("Needs repair")));
        result.append(option);
    }
    return result;
}

bool DubbingController::selectTtsVoice(const QString &voiceId)
{
    refreshCloneVoicePresets();
    const QString normalized = voiceId.trimmed();
    if (normalized.startsWith(QStringLiteral("builtin:"))) {
        const QString voice = normalized.mid(QStringLiteral("builtin:").size()).trimmed();
        if (voice.isEmpty()) {
            setError(QStringLiteral("Choose a valid built-in TTS voice."));
            return false;
        }
        if (m_project.ttsVoiceId == normalized) return true;
        m_project.ttsVoiceId = normalized;
        m_project.cloneVoicePresetId = normalized;
        emit cloneVoiceSelectionChanged();
        emit projectChanged();
        emit workflowChanged();
        persistAfterEdit();
        return true;
    }
    return selectCloneVoicePreset(normalized);
}

QVariantList DubbingController::workflowNodes() const
{
    const bool hasMedia = !m_project.sourceMediaPath.trimmed().isEmpty();
    const bool hasSegments = !m_project.segments.isEmpty();
    bool hasTargets = false;
    bool allTargets = hasSegments;
    bool hasClips = false;
    bool hasConflict = false;
    for (const QVariant &entry : m_project.segments) {
        const QVariantMap segment = entry.toMap();
        hasTargets = hasTargets || !segment.value(QStringLiteral("targetText")).toString().trimmed().isEmpty();
        allTargets = allTargets && !segment.value(QStringLiteral("targetText")).toString().trimmed().isEmpty();
        hasClips = hasClips || (!segment.value(QStringLiteral("clipPath")).toString().isEmpty()
                                && QFileInfo::exists(segment.value(QStringLiteral("clipPath")).toString()));
        hasConflict = hasConflict || segment.value(QStringLiteral("timingConflict")).toBool();
    }
    const QVariantMap synthesisSelection = m_workflowNodeConfigurations
        .value(QStringLiteral("synthesize")).toMap();
    const QVariantMap synthesisParameters = synthesisSelection
        .value(QStringLiteral("parameters")).toMap();
    ExecutionProvider synthesisProvider = ExecutionProvider::LocalDev;
    const QString synthesisProviderId = synthesisSelection.value(
        QStringLiteral("executionProvider"), synthesisParameters.value(
        QStringLiteral("executionProvider"), QStringLiteral("local-dev"))).toString();
    const bool remoteTtsSelected = executionProviderFromId(synthesisProviderId, &synthesisProvider)
        && synthesisProvider != ExecutionProvider::LocalDev;
    const bool ttsReady = remoteTtsSelected || (m_tts && m_tts->isModelLoaded());
    const bool translationReady = !m_project.targetLanguage.trimmed().isEmpty();
    const auto node = [](const QString &id, const QString &title, const QString &state,
                         const QString &detail, const QString &provider = QString()) {
        QVariantMap value{{QStringLiteral("id"), id}, {QStringLiteral("title"), title},
                          {QStringLiteral("state"), state}, {QStringLiteral("detail"), detail},
                          {QStringLiteral("providerName"), provider},
                          {QStringLiteral("providerState"), QStringLiteral("ready")}};
        return QVariant(value);
    };
    QVariantList result;
    const WorkflowGraph graph = DubbingWorkflowDefinition::create();
    for (const WorkflowGraphNode &definition : graph.nodes) {
        QString state = QStringLiteral("blocked");
        QString detail;
        QString provider;
        if (definition.id == QStringLiteral("media-input")) {
            state = hasMedia ? QStringLiteral("ready") : QStringLiteral("missing");
            detail = hasMedia ? QFileInfo(m_project.sourceMediaPath).fileName() : QStringLiteral("Import audio or video");
        } else if (definition.id == QStringLiteral("ingest")) {
            const bool normalized = !m_project.masterAudioPath.trimmed().isEmpty();
            state = normalized ? QStringLiteral("completed") : (hasMedia ? QStringLiteral("ready") : QStringLiteral("missing"));
            detail = normalized ? QStringLiteral("Media normalized") : (hasMedia ? QStringLiteral("Ready to normalize") : QStringLiteral("Import source media"));
        } else if (definition.id == QStringLiteral("source-separate")) {
            const bool separated = hasMedia && !m_project.backgroundAudioPath.trimmed().isEmpty();
            state = hasMedia ? (separated ? QStringLiteral("completed") : QStringLiteral("ready")) : QStringLiteral("missing");
            detail = separated ? QStringLiteral("Vocals and Background stems available")
                               : (hasMedia ? QStringLiteral("Run Isolator to create Vocals and Background stems") : QStringLiteral("Import source media"));
        } else if (definition.id == QStringLiteral("transcribe")) {
            const QString transcriptSource = m_project.transcriptConfiguration.value(
                QStringLiteral("transcriptSource"), QStringLiteral("stt")).toString().trimmed().toLower();
            const bool audioReady = !m_project.analysisAudioPath.trimmed().isEmpty() || !m_project.masterAudioPath.trimmed().isEmpty();
            const bool ocrReady = m_subtitleOcr
                && (m_subtitleOcr->executionRoute() == QStringLiteral("colab-gpu")
                    ? m_subtitleOcr->colabRouteReady() : m_subtitleOcr->localRouteReady());
            const bool sourceReady = transcriptSource == QStringLiteral("ocr") ? hasMedia
                : transcriptSource == QStringLiteral("stt+ocr") ? (hasMedia && audioReady) : audioReady;
            state = hasSegments ? QStringLiteral("completed")
                : (!sourceReady || ((transcriptSource == QStringLiteral("ocr")
                                     || transcriptSource == QStringLiteral("stt+ocr")) && !ocrReady)
                   ? QStringLiteral("blocked") : QStringLiteral("ready"));
            detail = hasSegments ? QStringLiteral("%1 segments").arg(m_project.segments.size())
                : transcriptSource == QStringLiteral("ocr")
                    ? QStringLiteral("Subtitle OCR transcript source")
                    : transcriptSource == QStringLiteral("stt+ocr")
                        ? QStringLiteral("STT + Subtitle OCR transcript sources")
                        : QStringLiteral("Speech-to-text source stage");
        } else if (definition.id == QStringLiteral("review-transcript")) {
            state = hasSegments ? QStringLiteral("completed") : QStringLiteral("blocked");
            detail = hasSegments ? QStringLiteral("Transcript available for review") : QStringLiteral("Transcribe source media first");
        } else if (definition.id == QStringLiteral("translate")) {
            const int unresolved = unresolvedTranscriptConflictCount();
            state = unresolved > 0 ? QStringLiteral("blocked")
                : (!translationReady ? QStringLiteral("blocked")
                    : (hasTargets ? QStringLiteral("completed")
                       : (hasSegments ? QStringLiteral("ready") : QStringLiteral("missing"))));
            detail = unresolved > 0
                ? QStringLiteral("Resolve %1 STT/OCR conflict(s) before Translate.").arg(unresolved)
                : (!translationReady ? QStringLiteral("Choose a target language")
                   : (hasTargets ? QStringLiteral("Target text available")
                                 : QStringLiteral("Translate with CrispASR")));
            provider = QStringLiteral("Local translation runtime");
        } else if (definition.id == QStringLiteral("review-translation")) {
            state = hasTargets ? QStringLiteral("completed") : QStringLiteral("blocked");
            detail = hasTargets ? QStringLiteral("Translated transcript available for review") : QStringLiteral("Translate the transcript first");
        } else if (definition.id == QStringLiteral("assign-voices")) {
            const bool voicesReady = hasTargets && !m_project.speakers.isEmpty();
            state = voicesReady ? QStringLiteral("ready") : QStringLiteral("blocked");
            detail = voicesReady ? QStringLiteral("Speaker assignments are ready") : QStringLiteral("Translated transcript and a speaker are required");
        } else if (definition.id == QStringLiteral("synthesize")) {
            const bool cloneVoiceReady = cloneVoiceSelectionValid();
            state = !cloneVoiceReady ? QStringLiteral("blocked")
                : (!ttsReady ? QStringLiteral("missing")
                   : (hasClips ? QStringLiteral("completed")
                      : (allTargets ? QStringLiteral("ready") : QStringLiteral("blocked"))));
            const QString defaultVoice = automaticDefaultFamilyId(
                QStringLiteral("tts"), m_project.dubbingQuality);
            detail = !cloneVoiceReady ? cloneVoiceSelectionError()
                : (ttsReady ? QStringLiteral("TTS model loaded")
                              : (m_project.dubbingQuality == QStringLiteral("custom")
                                     ? QStringLiteral("Choose a TTS model")
                                     : QStringLiteral("Default: %1").arg(defaultVoice)));
            provider = m_project.dubbingQuality == QStringLiteral("custom")
                ? QStringLiteral("No model configured")
                : (defaultVoice == QStringLiteral("vieneu-tts-v2-turbo")
                       ? QStringLiteral("VieNeu-TTS v2 Turbo (default)")
                       : QStringLiteral("OmniVoice (default)"));
        } else if (definition.id == QStringLiteral("fit-timing")) {
            state = !hasClips ? QStringLiteral("blocked") : (hasConflict ? QStringLiteral("blocked") : QStringLiteral("completed"));
            detail = hasConflict ? QStringLiteral("One or more clips exceed the fit tolerance") : QStringLiteral("Fit generated clips to segment timing");
        } else if (definition.id == QStringLiteral("review-conflicts")) {
            state = hasConflict ? QStringLiteral("blocked") : (hasClips ? QStringLiteral("completed") : QStringLiteral("blocked"));
            detail = hasConflict ? QStringLiteral("Review timing conflicts") : QStringLiteral("No timing conflicts pending");
        } else if (definition.id == QStringLiteral("mix")) {
            state = hasClips && !hasConflict ? QStringLiteral("ready") : QStringLiteral("blocked");
            detail = hasClips ? QStringLiteral("Mix generated clips with background audio") : QStringLiteral("Generate segment audio first");
        } else if (definition.id == QStringLiteral("export")) {
            state = !hasClips ? QStringLiteral("missing") : (!previewPath().isEmpty() ? QStringLiteral("completed") : QStringLiteral("ready"));
            detail = !hasClips ? QStringLiteral("Generate translated audio first") : (!previewPath().isEmpty() ? QStringLiteral("Preview rendered") : QStringLiteral("Render the mixed audio"));
        }
        if (workflowWaitingForInput() && definition.id == m_workflowRunner->activeNodeId()) {
            state = QStringLiteral("waiting_for_input");
            detail = QStringLiteral("Review is waiting for your decision");
        } else if (m_workflowRunner && m_workflowRunner->running()
                   && definition.id == m_workflowRunner->activeNodeId()) {
            state = QStringLiteral("running");
            detail = m_workflowMode != QStringLiteral("automatic")
                    || m_automaticStatusText.isEmpty()
                ? QStringLiteral("Node is running") : m_automaticStatusText;
        }
        const auto stageTitleForNode = [](const QString &id) {
            if (id == QStringLiteral("media-input")) return QStringLiteral("Import/Download");
            if (id == QStringLiteral("ingest")) return QStringLiteral("Normalize");
            if (id == QStringLiteral("source-separate")) return QStringLiteral("Isolator");
            if (id == QStringLiteral("transcribe")) return QStringLiteral("Transcribe/STT");
            if (id == QStringLiteral("review-transcript")) return QStringLiteral("Transcribe/STT");
            if (id == QStringLiteral("translate")) return QStringLiteral("Translate");
            if (id == QStringLiteral("review-translation")) return QStringLiteral("Subtitle");
            if (id == QStringLiteral("assign-voices") || id == QStringLiteral("synthesize")) return QStringLiteral("TTS");
            if (id == QStringLiteral("fit-timing") || id == QStringLiteral("review-conflicts")) return QStringLiteral("Alignment");
            if (id == QStringLiteral("mix")) return QStringLiteral("Export/Output");
            return QStringLiteral("Export/Output");
        };
        const QString displayTitle = stageTitleForNode(definition.id);
        QVariantMap item = node(definition.id, displayTitle, state, detail, provider).toMap();
        item.insert(QStringLiteral("displayStageTitle"), displayTitle);
        item.insert(QStringLiteral("parameters"), definition.parameters);
        item.insert(QStringLiteral("typeId"), definition.typeId);
        item.insert(QStringLiteral("typeVersion"), definition.typeVersion);
        if (definition.id == QStringLiteral("source-separate")) {
            item.insert(QStringLiteral("configurable"), true);
            item.insert(QStringLiteral("capabilityId"), QStringLiteral("voice-isolation"));
        } else if (definition.id == QStringLiteral("transcribe")) {
            item.insert(QStringLiteral("configurable"), true);
            item.insert(QStringLiteral("capabilityId"), QStringLiteral("stt"));
        } else if (definition.id == QStringLiteral("translate")) {
            item.insert(QStringLiteral("configurable"), true);
            item.insert(QStringLiteral("capabilityId"), QStringLiteral("translation"));
        } else if (definition.id == QStringLiteral("synthesize")) {
            item.insert(QStringLiteral("configurable"), true);
            item.insert(QStringLiteral("capabilityId"), QStringLiteral("tts"));
        }
        if (item.value(QStringLiteral("configurable")).toBool()
            && m_project.dubbingQuality != QStringLiteral("custom")) {
            item.insert(
                QStringLiteral("defaultFamilyId"),
                automaticDefaultFamilyId(
                    item.value(QStringLiteral("capabilityId")).toString(),
                    m_project.dubbingQuality));
        }
        const QVariantMap selected = m_workflowNodeConfigurations.value(definition.id).toMap();
        if (!selected.isEmpty()) {
            item.insert(QStringLiteral("providerName"), selected.value(QStringLiteral("modelName")));
            item.insert(QStringLiteral("selectedFamilyId"), selected.value(QStringLiteral("familyId")));
            item.insert(QStringLiteral("selectedRuntimeId"), selected.value(QStringLiteral("runtimeId")));
            item.insert(QStringLiteral("supportsVoiceCloning"),
                        selected.value(QStringLiteral("supportsVoiceCloning")).toBool());
            const QString capabilityId = selected.value(QStringLiteral("capabilityId")).toString();
            IModelSession *session = AppController::instance() && AppController::instance()->sessionRegistry()
                ? AppController::instance()->sessionRegistry()->sessionForCapability(capabilityId) : nullptr;
            item.insert(QStringLiteral("providerState"),
                        session && session->canProcess() ? QStringLiteral("ready") : QStringLiteral("loading"));
            const ModelSessionState modelState = session ? session->state() : ModelSessionState::Unconfigured;
            item.insert(QStringLiteral("modelState"), static_cast<int>(modelState));
            item.insert(QStringLiteral("modelStateText"),
                        modelState == ModelSessionState::Ready ? QStringLiteral("ready")
                        : modelState == ModelSessionState::Loading ? QStringLiteral("loading")
                        : modelState == ModelSessionState::Unloading ? QStringLiteral("unloading")
                        : modelState == ModelSessionState::Processing ? QStringLiteral("processing")
                        : modelState == ModelSessionState::Error ? QStringLiteral("error")
                        : QStringLiteral("unloaded"));
            QVariantMap parameters = item.value(QStringLiteral("parameters")).toMap();
            const QVariantMap customParameters = selected.value(QStringLiteral("parameters")).toMap();
            for (auto it = customParameters.cbegin(); it != customParameters.cend(); ++it)
                parameters.insert(it.key(), it.value());
            item.insert(QStringLiteral("parameters"), parameters);
            const QString providerId = selected.value(
                QStringLiteral("executionProvider"),
                customParameters.value(QStringLiteral("executionProvider"), QStringLiteral("local-dev"))).toString();
            ExecutionProvider executionProvider = ExecutionProvider::LocalDev;
            if (executionProviderFromId(providerId, &executionProvider)
                && executionProvider != ExecutionProvider::LocalDev) {
                const QString modelId = selected.value(
                    QStringLiteral("modelId"), customParameters.value(QStringLiteral("modelId"))).toString();
                item.insert(QStringLiteral("executionProvider"), executionProviderId(executionProvider));
                item.insert(QStringLiteral("providerName"),
                            modelId.isEmpty() ? executionProviderDisplayName(executionProvider)
                                              : QStringLiteral("%1 · %2").arg(executionProviderDisplayName(executionProvider), modelId));
                item.insert(QStringLiteral("providerState"), QStringLiteral("selected"));
            } else {
                item.insert(QStringLiteral("executionProvider"), QStringLiteral("local-dev"));
            }
            QVariantList runtimeSchema;
            if (capabilityId == QStringLiteral("tts") && m_tts) {
                const QString signature = selected.value(QStringLiteral("configurationSignature")).toString();
                if (!signature.isEmpty() && m_tts->instance(signature))
                    runtimeSchema = m_tts->instance(signature)->schemaForCapability(QStringLiteral("tts"));
                else
                    runtimeSchema = m_tts->schemaForCapability(QStringLiteral("tts"));
            }
            const QVariantMap familyConfig = selected.value(QStringLiteral("familyConfig")).toMap();
            const QVariantMap studioConfig = familyConfig.value(QStringLiteral("studio")).toMap()
                .value(capabilityId).toMap();
            const QVariantList parameterSchema = CapabilitySettingsSchema::merge(
                familyConfig, capabilityId, runtimeSchema);
            item.insert(QStringLiteral("parameterSchema"), parameterSchema);
            item.insert(QStringLiteral("studioConfig"), studioConfig);
        }
        const QVariantMap effectiveParameters = item.value(QStringLiteral("parameters")).toMap();
        const QString configuredProvider = item.value(QStringLiteral("executionProvider"),
            effectiveParameters.value(QStringLiteral("executionProvider"),
                                      QStringLiteral("local-dev"))).toString().trimmed().toLower();
        const QString configuredModel = effectiveParameters.value(QStringLiteral("modelId")).toString().trimmed();
        const auto roleForNode = [](const QString &id) {
            if (id == QStringLiteral("media-input")) return QStringLiteral("Choose the original audio or video used by the project.");
            if (id == QStringLiteral("ingest")) return QStringLiteral("Inspect media and create normalized working audio.");
            if (id == QStringLiteral("source-separate")) return QStringLiteral("Create Vocals and Background stems for the mix.");
            if (id == QStringLiteral("transcribe")) return QStringLiteral("Create timed text from speech, subtitles, or both.");
            if (id == QStringLiteral("review-transcript")) return QStringLiteral("Review the transcript before translation.");
            if (id == QStringLiteral("translate")) return QStringLiteral("Translate reviewed timed text into the target language.");
            if (id == QStringLiteral("review-translation")) return QStringLiteral("Review translated text before speech synthesis.");
            if (id == QStringLiteral("assign-voices")) return QStringLiteral("Choose one TTS voice applied to all segments and speakers.");
            if (id == QStringLiteral("synthesize")) return QStringLiteral("Generate timed speech with the selected TTS voice.");
            if (id == QStringLiteral("fit-timing")) return QStringLiteral("Fit generated clips to the reviewed timing.");
            if (id == QStringLiteral("review-conflicts")) return QStringLiteral("Resolve clips whose timing needs a decision.");
            if (id == QStringLiteral("mix")) return QStringLiteral("Mix generated speech with the background audio.");
            return QStringLiteral("Write the verified dub and subtitles to the chosen output.");
        };
        const bool ocrUsesColab = definition.id == QStringLiteral("transcribe")
            && (m_project.transcriptConfiguration.value(QStringLiteral("transcriptSource"))
                    .toString().contains(QStringLiteral("ocr")))
            && m_subtitleOcr && m_subtitleOcr->executionRoute() == QStringLiteral("colab-gpu");
        const bool colabHeavy = configuredProvider == QStringLiteral("colab-direct") || ocrUsesColab;
        item.insert(QStringLiteral("roleDescription"), roleForNode(definition.id));
        item.insert(QStringLiteral("showColabRecommendation"), colabHeavy);
        item.insert(QStringLiteral("resourceText"), colabHeavy ? QStringLiteral("Nên dùng Colab")
            : configuredProvider == QStringLiteral("api-gateway") ? QStringLiteral("API Gateway")
            : QStringLiteral("CPU phù hợp"));
        item.insert(QStringLiteral("resourceReason"), colabHeavy
            ? QStringLiteral("The selected Direct Colab route runs its exact model on the temporary GPU worker.")
            : configuredProvider == QStringLiteral("api-gateway")
                ? QStringLiteral("This node sends requests only to the configured API Gateway.")
                : QStringLiteral("This stage is executed locally without a required GPU worker."));
        if (colabHeavy)
            item.insert(QStringLiteral("notebookFile"), DubbingColabModelRoutes::notebookForModel(
                ocrUsesColab ? QStringLiteral("subtitle-ocr") : definition.id,
                ocrUsesColab ? m_subtitleOcr->colabModelId() : configuredModel));
        result.append(item);
    }
    return result;
}

QVariantList DubbingController::workflowStages() const
{
    // These are intentionally not new graph nodes.  They are a stable
    // presentation contract over the persisted node ids, which preserves
    // existing project journals, artifacts and rerun/resume behaviour.
    const QVariantList nodes = workflowNodes();
    QHash<QString, QVariantMap> byId;
    for (const QVariant &entry : nodes) {
        const QVariantMap node = entry.toMap();
        byId.insert(node.value(QStringLiteral("id")).toString(), node);
    }

    struct StageDefinition {
        const char *id;
        const char *title;
        const char *actionNodeId;
        const char *description;
        QStringList nodeIds;
    };
    const QList<StageDefinition> definitions{
        {"import", "Import/Download", "media-input",
         "Choose a local media file or download/import an approved URL.",
         {QStringLiteral("media-input")}},
        {"normalize", "Normalize", "ingest",
         "Probe media and create the master and analysis audio used downstream.",
         {QStringLiteral("ingest")}},
        {"isolator", "Isolator", "source-separate",
         "Create real Vocals and Background stems for review and mixing.",
         {QStringLiteral("source-separate")}},
        {"transcribe", "Transcribe/STT", "transcribe",
         "Create and review timed source text from STT, Subtitle OCR, or the reviewed STT + OCR mode.",
         {QStringLiteral("transcribe"), QStringLiteral("review-transcript")}},
        {"alignment-subtitle", "Alignment/Subtitle", "fit-timing",
         "Configure timing resolution and subtitle output without exposing internal timing nodes as separate user stages.",
         {QStringLiteral("fit-timing"), QStringLiteral("review-conflicts")}},
        {"translate", "Translate", "translate",
         "Translate and review the timed target-language text.",
         {QStringLiteral("translate"), QStringLiteral("review-translation")}},
        {"tts", "TTS", "synthesize",
         "Assign a voice and synthesize the translated segments.",
         {QStringLiteral("assign-voices"), QStringLiteral("synthesize")}},
        {"export", "Export/Output", "export",
         "Mix/render the verified dub and export media, subtitles, a package, or a CapCut Draft.",
         {QStringLiteral("mix"), QStringLiteral("export")}}
    };

    const QHash<QString, int> priority{
        {QStringLiteral("completed"), 0}, {QStringLiteral("ready"), 1},
        {QStringLiteral("missing"), 2}, {QStringLiteral("blocked"), 3},
        {QStringLiteral("waiting_for_input"), 4}, {QStringLiteral("running"), 5}
    };
    const QString activeNodeId = currentStepId();
    QVariantList result;
    for (const StageDefinition &definition : definitions) {
        QVariantMap stage;
        QVariantList productionNodes;
        QVariantMap actionNode = byId.value(QString::fromLatin1(definition.actionNodeId));
        QString state = QStringLiteral("completed");
        int statePriority = -1;
        QString detail;
        bool active = false;
        for (const QString &nodeId : definition.nodeIds) {
            const QVariantMap node = byId.value(nodeId);
            if (node.isEmpty()) continue;
            productionNodes.append(nodeId);
            const QString candidate = node.value(QStringLiteral("state")).toString();
            const int candidatePriority = priority.value(candidate, 3);
            if (candidatePriority > statePriority) {
                statePriority = candidatePriority;
                state = candidate;
                detail = node.value(QStringLiteral("detail")).toString();
            }
            active = active || activeNodeId == nodeId;
        }
        if (detail.isEmpty()) detail = QString::fromLatin1(definition.description);
        stage.insert(QStringLiteral("id"), QString::fromLatin1(definition.id));
        stage.insert(QStringLiteral("title"), QString::fromLatin1(definition.title));
        stage.insert(QStringLiteral("description"), QString::fromLatin1(definition.description));
        stage.insert(QStringLiteral("actionNodeId"), QString::fromLatin1(definition.actionNodeId));
        stage.insert(QStringLiteral("productionNodeIds"), productionNodes);
        stage.insert(QStringLiteral("state"), state);
        stage.insert(QStringLiteral("detail"), detail);
        stage.insert(QStringLiteral("active"), active);
        stage.insert(QStringLiteral("configurable"), actionNode.value(QStringLiteral("configurable")).toBool());
        stage.insert(QStringLiteral("capabilityId"), actionNode.value(QStringLiteral("capabilityId")));
        stage.insert(QStringLiteral("executionProvider"), actionNode.value(QStringLiteral("executionProvider")));
        stage.insert(QStringLiteral("providerName"), actionNode.value(QStringLiteral("providerName")));
        stage.insert(QStringLiteral("resourceText"), actionNode.value(QStringLiteral("resourceText")));
        stage.insert(QStringLiteral("resourceReason"), actionNode.value(QStringLiteral("resourceReason")));
        stage.insert(QStringLiteral("notebookFile"), actionNode.value(QStringLiteral("notebookFile")));
        result.append(stage);
    }
    return result;
}

bool DubbingController::workflowReady() const
{
    const QVariantMap sttSelection = m_workflowNodeConfigurations
        .value(QStringLiteral("transcribe")).toMap();
    const QVariantMap sttParameters = sttSelection.value(QStringLiteral("parameters")).toMap();
    // The transcript route is independently persisted so a reopened project
    // keeps the exact STT worker selected in the Transcribe/Colab setup UI.
    // Do not fall back to the workflow-template default here: readiness must
    // describe the route that will actually execute the project.
    const QString persistedSttProvider = m_project.transcriptConfiguration.value(
        QStringLiteral("sttExecutionProvider")).toString().trimmed();
    const QString persistedSttModel = m_project.transcriptConfiguration.value(
        QStringLiteral("sttModelId")).toString().trimmed();
    const QString configuredSttProvider = sttSelection.value(
        QStringLiteral("executionProvider"), sttParameters.value(
        QStringLiteral("executionProvider"), QStringLiteral("local-dev"))).toString();
    const QString configuredSttModel = sttSelection.value(
        QStringLiteral("modelId"), sttParameters.value(QStringLiteral("modelId"))).toString().trimmed();
    const QString sttProviderId = persistedSttProvider.isEmpty()
        ? configuredSttProvider : persistedSttProvider;
    const QString sttModelId = persistedSttModel.isEmpty()
        ? configuredSttModel : persistedSttModel;
    ExecutionProvider sttProvider = ExecutionProvider::LocalDev;
    const bool remoteSttSelected = executionProviderFromId(sttProviderId, &sttProvider)
        && sttProvider != ExecutionProvider::LocalDev
        && !sttModelId.isEmpty()
        && (sttProvider != ExecutionProvider::ColabDirect
            || DubbingColabModelRoutes::supports(
                QStringLiteral("transcribe"), sttModelId));
    const bool sttReady = remoteSttSelected || (AppController::instance() && AppController::instance()->sessionRegistry()
        && AppController::instance()->sessionRegistry()->sessionForCapability(QStringLiteral("stt"))
        && AppController::instance()->sessionRegistry()->sessionForCapability(QStringLiteral("stt"))->canProcess());
    const QString transcriptSource = m_project.transcriptConfiguration.value(
        QStringLiteral("transcriptSource"), QStringLiteral("stt")).toString().trimmed().toLower();
    const bool ocrReady = m_subtitleOcr
        && (m_subtitleOcr->executionRoute() == QStringLiteral("colab-gpu")
            ? m_subtitleOcr->colabRouteReady() : m_subtitleOcr->localRouteReady());
    const bool transcriptReady = transcriptSource == QStringLiteral("ocr") ? ocrReady
        : transcriptSource == QStringLiteral("stt+ocr") ? (sttReady && ocrReady) : sttReady;
    const bool translationConfigured = !m_workflowNodeConfigurations.value(QStringLiteral("translate")).toMap().isEmpty();
    bool translatedArtifactReady = !m_project.segments.isEmpty();
    for (const QVariant &entry : m_project.segments) {
        if (entry.toMap().value(QStringLiteral("targetText")).toString().trimmed().isEmpty()) {
            translatedArtifactReady = false;
            break;
        }
    }
    bool configuredTranslationReady = false;
    if (translationConfigured) {
        const QVariantMap selected = m_workflowNodeConfigurations
                                         .value(QStringLiteral("translate")).toMap();
        const QVariantMap parameters = selected.value(QStringLiteral("parameters")).toMap();
        ExecutionProvider provider = ExecutionProvider::LocalDev;
        const QString providerId = selected.value(
            QStringLiteral("executionProvider"), parameters.value(
            QStringLiteral("executionProvider"), QStringLiteral("local-dev"))).toString();
        if (executionProviderFromId(providerId, &provider)
            && provider != ExecutionProvider::LocalDev) {
            const QString remoteModel = selected.value(
                QStringLiteral("modelId"),
                parameters.value(QStringLiteral("modelId"))).toString().trimmed();
            configuredTranslationReady = !remoteModel.isEmpty()
                && (provider != ExecutionProvider::ColabDirect
                    || DubbingColabModelRoutes::supports(
                        QStringLiteral("translate"), remoteModel));
        } else {
            StudioConfiguration configuration;
            configuration.capabilityId = QStringLiteral("translation");
            configuration.familyId = selected.value(QStringLiteral("familyId")).toString();
            configuration.runtimeId = selected.value(QStringLiteral("runtimeId")).toString();
            configuration.runtimeVersion = selected.value(QStringLiteral("runtimeVersion")).toString();
            configuration.selectedFiles = selected.value(QStringLiteral("selectedFiles")).toMap();
            configuredTranslationReady = StudioConfigurationResolver::resolve(configuration).isValid;
        }
    }
    const bool translationReady = !translationConfigured || translatedArtifactReady
        || configuredTranslationReady;
    const QVariantMap synthesisSelection = m_workflowNodeConfigurations
        .value(QStringLiteral("synthesize")).toMap();
    const QVariantMap synthesisParameters = synthesisSelection.value(QStringLiteral("parameters")).toMap();
    ExecutionProvider synthesisProvider = ExecutionProvider::LocalDev;
    const QString synthesisProviderId = synthesisSelection.value(
        QStringLiteral("executionProvider"), synthesisParameters.value(
        QStringLiteral("executionProvider"), QStringLiteral("local-dev"))).toString();
    const bool remoteTtsSelected = executionProviderFromId(synthesisProviderId, &synthesisProvider)
        && synthesisProvider != ExecutionProvider::LocalDev
        && !synthesisSelection.value(QStringLiteral("modelId"), synthesisParameters.value(
            QStringLiteral("modelId"))).toString().trimmed().isEmpty()
        && (synthesisProvider != ExecutionProvider::ColabDirect
            || DubbingColabModelRoutes::supports(
                QStringLiteral("synthesize"),
                synthesisSelection.value(QStringLiteral("modelId"),
                    synthesisParameters.value(QStringLiteral("modelId"))).toString()));
    const bool ttsReady = remoteTtsSelected || (m_tts && m_tts->isModelLoaded());
    return workflowGraphValid()
        && !m_project.sourceMediaPath.isEmpty()
        && !m_project.targetLanguage.trimmed().isEmpty()
        && cloneVoiceSelectionValid()
        && ttsReady
        && transcriptReady
        && !hasUnresolvedTranscriptConflicts()
        && translationReady;
}

bool DubbingController::setWorkflowNodeModel(const QString &nodeId,
                                             const QString &familyId,
                                             const QString &runtimeId,
                                             const QString &runtimeVersion,
                                             const QVariantMap &selectedFiles)
{
    return configureWorkflowNodeModel(nodeId, familyId, runtimeId, runtimeVersion,
                                      selectedFiles, true);
}

bool DubbingController::configureWorkflowNodeModel(const QString &nodeId,
                                                   const QString &familyId,
                                                   const QString &runtimeId,
                                                   const QString &runtimeVersion,
                                                   const QVariantMap &selectedFiles,
                                                   bool loadSession)
{
    QString capabilityId;
    if (nodeId == QStringLiteral("source-separate")) capabilityId = QStringLiteral("voice-isolation");
    else if (nodeId == QStringLiteral("transcribe")) capabilityId = QStringLiteral("stt");
    else if (nodeId == QStringLiteral("translate")) capabilityId = QStringLiteral("translation");
    else if (nodeId == QStringLiteral("synthesize")) capabilityId = QStringLiteral("tts");
    else {
        setError(QStringLiteral("This workflow node does not support model selection."));
        return false;
    }

    AppController *app = AppController::instance();
    if (!app || !app->registry() || !app->sessionRegistry()) return false;
    const QVariantList families = capabilityId == QStringLiteral("stt")
            || capabilityId == QStringLiteral("voice-isolation")
        ? app->registry()->sttFamilies()
        : (capabilityId == QStringLiteral("translation") ? app->registry()->translationFamilies()
                                                           : app->registry()->ttsFamilies());
    QVariantMap family;
    for (const QVariant &entry : families) {
        const QVariantMap candidate = entry.toMap();
        if (candidate.value(QStringLiteral("id")).toString() == familyId) {
            family = candidate;
            break;
        }
    }
    if (family.isEmpty()) {
        setError(QStringLiteral("The selected model family is not available."));
        return false;
    }

    QVariantMap runtime;
    for (const QVariant &entry : family.value(QStringLiteral("runtimes")).toList()) {
        const QVariantMap candidate = entry.toMap();
        if (candidate.value(QStringLiteral("id")).toString() == runtimeId) {
            runtime = candidate;
            break;
        }
    }
    if (runtime.isEmpty()) {
        setError(QStringLiteral("The selected runtime is not compatible with this model."));
        return false;
    }

    StudioConfiguration config;
    config.capabilityId = capabilityId;
    config.familyId = familyId;
    config.runtimeId = runtimeId;
    config.runtimeVersion = runtimeVersion.isEmpty()
        ? runtime.value(QStringLiteral("version")).toString() : runtimeVersion;
    for (const QVariant &entry : family.value(QStringLiteral("requiredFiles")).toList()) {
        const QVariantMap file = entry.toMap();
        const QString role = file.value(QStringLiteral("role")).toString();
        config.selectedFiles.insert(role, selectedFiles.value(role, file.value(QStringLiteral("file"))).toString());
    }
    const auto resolved = StudioConfigurationResolver::resolve(config);
    if (!resolved.isValid) {
        setError(QStringLiteral("The selected model files or runtime are not installed."));
        return false;
    }

    const bool supportsVoiceCloning = family.value(QStringLiteral("supportsCloning")).toBool()
        || family.value(QStringLiteral("capabilities")).toStringList().contains(QStringLiteral("voice-cloning"));
    QVariantMap parameters = m_workflowNodeConfigurations.value(nodeId).toMap()
                                 .value(QStringLiteral("parameters")).toMap();
    // Source-window auto selection was intentionally removed.  Clone voice
    // now always comes from the project-level preset selected by the user.
    parameters.remove(QStringLiteral("autoSelectVoiceReference"));
    parameters.remove(QStringLiteral("autoReferenceSourcePath"));

    const bool isOmniVoice = familyId.contains(QStringLiteral("omnivoice"), Qt::CaseInsensitive);
    if (nodeId == QStringLiteral("synthesize") && isOmniVoice && supportsVoiceCloning) {
        if (!parameters.contains(QStringLiteral("forceSegmentDuration")))
            parameters.insert(QStringLiteral("forceSegmentDuration"), true);
    }
    if (nodeId == QStringLiteral("synthesize")
        && !parameters.contains(QStringLiteral("lang"))) {
        parameters.insert(QStringLiteral("lang"), m_project.targetLanguage);
    }

    QVariantMap selected{{QStringLiteral("executionProvider"), QStringLiteral("local-dev")},
                         {QStringLiteral("modelId"), familyId},
                         {QStringLiteral("familyId"), familyId},
                         {QStringLiteral("runtimeId"), config.runtimeId},
                         {QStringLiteral("runtimeVersion"), config.runtimeVersion},
                          {QStringLiteral("selectedFiles"), config.selectedFiles},
                          {QStringLiteral("modelName"), family.value(QStringLiteral("title"))},
                          {QStringLiteral("capabilityId"), capabilityId},
                          {QStringLiteral("configurationSignature"), resolved.signature},
                          {QStringLiteral("supportsVoiceCloning"), supportsVoiceCloning},
                          {QStringLiteral("parameters"), parameters},
                          {QStringLiteral("familyConfig"), family},
                          {QStringLiteral("parameterDefinitions"), family.value(QStringLiteral("parameterDefinitions"))},
                          {QStringLiteral("studioConfig"),
                           family.value(QStringLiteral("studio")).toMap().value(capabilityId)}};
    m_workflowNodeConfigurations.insert(nodeId, selected);
    // A route/model selected by the user is a durable preflight contract in
    // every quality mode; a template default must not replace it on reopen.
    m_project.workflowNodeConfigurations = m_workflowNodeConfigurations;
    if (loadSession) {
        unloadConflictingDubbingRuntime(app->sessionRegistry(), capabilityId);
        if (IModelSession *session = app->sessionRegistry()->sessionForCapability(capabilityId)) {
            session->requestLoad(capabilityId, config);
        }
    }
    Logger::info(QStringLiteral("DubbingController"),
                 QStringLiteral("Workflow node model changed node=%1 family=%2 runtime=%3")
                     .arg(nodeId, familyId, config.runtimeId));
    persistAfterEdit();
    if (nodeId == QStringLiteral("synthesize")) {
        refreshCloneVoicePresets();
        emit cloneVoiceSelectionChanged();
    }
    emit workflowChanged();
    return true;
}

bool DubbingController::loadWorkflowNodeModel(const QString &nodeId)
{
    const QVariantMap selected = m_workflowNodeConfigurations.value(nodeId).toMap();
    if (selected.isEmpty()) {
        setError(QStringLiteral("Choose a model configuration before loading this node."));
        return false;
    }
    return setWorkflowNodeModel(nodeId, selected.value(QStringLiteral("familyId")).toString(),
                                selected.value(QStringLiteral("runtimeId")).toString(),
                                selected.value(QStringLiteral("runtimeVersion")).toString(),
                                selected.value(QStringLiteral("selectedFiles")).toMap());
}

bool DubbingController::unloadWorkflowNodeModel(const QString &nodeId)
{
    const QVariantMap selected = m_workflowNodeConfigurations.value(nodeId).toMap();
    const QString capabilityId = selected.value(QStringLiteral("capabilityId")).toString();
    auto *app = AppController::instance();
    auto *session = app && app->sessionRegistry()
        ? app->sessionRegistry()->sessionForCapability(capabilityId) : nullptr;
    if (!session || capabilityId.isEmpty()) return false;
    session->requestUnload(capabilityId);
    emit workflowChanged();
    return true;
}

bool DubbingController::reloadWorkflowNodeModel(const QString &nodeId)
{
    const QVariantMap selected = m_workflowNodeConfigurations.value(nodeId).toMap();
    const QString capabilityId = selected.value(QStringLiteral("capabilityId")).toString();
    auto *app = AppController::instance();
    auto *session = app && app->sessionRegistry()
        ? app->sessionRegistry()->sessionForCapability(capabilityId) : nullptr;
    if (!session || capabilityId.isEmpty()) return false;
    session->requestReload(capabilityId);
    emit workflowChanged();
    return true;
}

bool DubbingController::setWorkflowNodeParameters(const QString &nodeId, const QVariantMap &parameters)
{
    if (nodeId.isEmpty()) return false;
    if ((nodeId == QStringLiteral("source-separate") || nodeId == QStringLiteral("transcribe")
         || nodeId == QStringLiteral("translate")
         || nodeId == QStringLiteral("synthesize"))
        && parameters.contains(QStringLiteral("executionProvider"))) {
        ExecutionProvider provider = ExecutionProvider::LocalDev;
        if (!executionProviderFromId(parameters.value(QStringLiteral("executionProvider")).toString(), &provider)) {
            setError(QStringLiteral("Unknown remote execution provider."));
            return false;
        }
        if (nodeId == QStringLiteral("source-separate")
            && provider == ExecutionProvider::ApiGateway) {
            setError(QStringLiteral("Source separation supports Local Dev or Colab GPU, not API Gateway."));
            return false;
        }
    }
    QVariantMap selected = m_workflowNodeConfigurations.value(nodeId).toMap();
    QVariantMap current = selected.value(QStringLiteral("parameters")).toMap();
    const QString previousProviderId = selected.value(
        QStringLiteral("executionProvider"), current.value(
        QStringLiteral("executionProvider"), QStringLiteral("local-dev"))).toString().trimmed().toLower();
    for (auto it = parameters.cbegin(); it != parameters.cend(); ++it)
        current.insert(it.key(), it.value());
    const QString providerId = current.value(QStringLiteral("executionProvider"),
                                             QStringLiteral("local-dev")).toString().trimmed().toLower();
    ExecutionProvider provider = ExecutionProvider::LocalDev;
    if (!executionProviderFromId(providerId, &provider)) {
        setError(QStringLiteral("Unknown remote execution provider."));
        return false;
    }
    const bool routeChanged = parameters.contains(QStringLiteral("executionProvider"))
        && previousProviderId != providerId;
    if (provider != ExecutionProvider::LocalDev) {
        // Route is a contract. Do not retain local runtime/family metadata at
        // either persistence level: legacy projects kept it at the root while
        // newer selections may also have it in the nested parameters map.
        // Keeping either can make an already-remote stage look local after a
        // model reselect or project reload.
        for (const QString &key : {QStringLiteral("familyId"), QStringLiteral("runtimeId"),
                                   QStringLiteral("runtimeVersion"), QStringLiteral("selectedFiles"),
                                   QStringLiteral("modelName"), QStringLiteral("supportsVoiceCloning"),
                                   QStringLiteral("configurationSignature"),
                                   QStringLiteral("familyConfig"),
                                   QStringLiteral("parameterDefinitions"),
                                   QStringLiteral("studioConfig")}) {
            selected.remove(key);
            current.remove(key);
        }
    }
    if (routeChanged && provider != ExecutionProvider::LocalDev) {
        if (AppController *app = AppController::instance(); app && app->sessionRegistry()) {
            QString capability;
            if (nodeId == QStringLiteral("source-separate")) capability = QStringLiteral("voice-isolation");
            else if (nodeId == QStringLiteral("transcribe")) capability = QStringLiteral("stt");
            else if (nodeId == QStringLiteral("translate")) capability = QStringLiteral("translation");
            else if (nodeId == QStringLiteral("synthesize")) capability = QStringLiteral("tts");
            if (IModelSession *session = app->sessionRegistry()->sessionForCapability(capability)) {
                for (const SessionConfiguration &loaded : session->loadedConfigurations())
                    session->requestUnloadConfiguration(loaded.signature);
            }
            m_automaticDownloadsQueued.remove(capability);
        }
    }
    // A canonical root copy prevents an old Local root selection from winning
    // over the newly selected remote provider when legacy projects reopen.
    selected.insert(QStringLiteral("executionProvider"), providerId);
    const QString modelId = current.value(QStringLiteral("modelId")).toString().trimmed();
    if (provider == ExecutionProvider::ColabDirect && !modelId.isEmpty()) {
        if (!DubbingColabModelRoutes::supports(nodeId, modelId)) {
            setError(QStringLiteral("No exact Colab notebook is mapped for model '%1' on the %2 node.")
                         .arg(modelId, visibleStepForNode(nodeId)));
            return false;
        }
    } else if (provider == ExecutionProvider::ApiGateway && !modelId.isEmpty()) {
        RemoteModelCatalogController *catalog = AppController::instance()
            ? AppController::instance()->remoteModels() : nullptr;
        const bool catalogAvailable = catalog && catalog->gatewayAvailable();
        QString capability;
        if (nodeId == QStringLiteral("source-separate")) capability = QStringLiteral("voice-isolation");
        else if (nodeId == QStringLiteral("transcribe")) capability = QStringLiteral("stt");
        else if (nodeId == QStringLiteral("translate")) capability = QStringLiteral("translation");
        else if (nodeId == QStringLiteral("synthesize")) capability = QStringLiteral("tts");
        if (catalogAvailable && !catalog->isModelSelectable(providerId, modelId, capability)) {
            setError(QStringLiteral("The selected %1 model is unavailable for this node. Refresh that provider's model catalog and choose a compatible model.")
                         .arg(executionProviderDisplayName(provider)));
            return false;
        }
    }
    if (nodeId == QStringLiteral("transcribe")) {
        QString mode = current.value(QStringLiteral("transcriptSource"), QStringLiteral("stt"))
                           .toString().trimmed().toLower();
        if (mode != QStringLiteral("ocr") && mode != QStringLiteral("stt+ocr")) mode = QStringLiteral("stt");
        m_project.transcriptConfiguration.insert(QStringLiteral("transcriptSource"), mode);
        if (current.contains(QStringLiteral("executionProvider"))) {
            m_project.transcriptConfiguration.insert(
                QStringLiteral("sttExecutionProvider"),
                current.value(QStringLiteral("executionProvider")));
        }
        if (current.contains(QStringLiteral("modelId"))) {
            m_project.transcriptConfiguration.insert(QStringLiteral("sttModelId"),
                                                     current.value(QStringLiteral("modelId")));
        }
        const QString fusionPolicy = DubbingTranscriptFusionService::normalizePolicy(
            current.value(QStringLiteral("fusionPolicy"),
                          m_project.transcriptConfiguration.value(
                              QStringLiteral("fusionPolicy"), QStringLiteral("ask"))).toString());
        current.insert(QStringLiteral("fusionPolicy"), fusionPolicy);
        m_project.transcriptConfiguration.insert(QStringLiteral("fusionPolicy"), fusionPolicy);
        for (const QString &key : {QStringLiteral("ocrLanguage"), QStringLiteral("ocrExecutionRoute"),
                                   QStringLiteral("ocrLocalEngineId"), QStringLiteral("ocrLocalEngineVersion"),
                                   QStringLiteral("ocrColabModelId"), QStringLiteral("ocrRoi"),
                                   QStringLiteral("ocrSampleIntervalMs"),
                                   QStringLiteral("ocrMinimumConfidence")}) {
            if (current.contains(key))
                m_project.transcriptConfiguration.insert(key, current.value(key));
        }
        applyStoredSubtitleOcrConfiguration();
    }
    selected.insert(QStringLiteral("modelId"), modelId);
    selected.insert(QStringLiteral("parameters"), current);
    m_workflowNodeConfigurations.insert(nodeId, selected);
    // A route/model selected by the user is a durable preflight contract in
    // every quality mode; a template default must not replace it on reopen.
    m_project.workflowNodeConfigurations = m_workflowNodeConfigurations;
    persistAfterEdit();
    if (nodeId == QStringLiteral("synthesize")) {
        refreshCloneVoicePresets();
        emit cloneVoiceSelectionChanged();
    }
    emit workflowChanged();
    return true;
}

QString DubbingController::workflowStatusText() const
{
    if (processing() && m_workflowMode == QStringLiteral("automatic")
        && !m_automaticStatusText.isEmpty()) return m_automaticStatusText;
    if (processing()) {
        return progressAvailable()
            ? QStringLiteral("Running %1 (%2%)").arg(stage()).arg(progress())
            : QStringLiteral("Running %1").arg(stage());
    }
    if (workflowReady()) return QStringLiteral("Workflow configured and ready to run");
    if (m_project.dubbingQuality == QStringLiteral("custom"))
        return customStatusText();
    return QStringLiteral("Configure media, transcript, target text, and a TTS model");
}

QVariantMap DubbingController::automaticPreflight() const
{
    QVariantList issues;
    auto addIssue = [&issues](const QString &id, const QString &message,
                              int page = 1, const QString &focus = QString()) {
        issues.append(QVariantMap{{QStringLiteral("id"), id},
                                  {QStringLiteral("message"), message},
                                  {QStringLiteral("page"), page},
                                  {QStringLiteral("focus"), focus}});
    };

    const bool hasMedia = !m_project.sourceMediaPath.trimmed().isEmpty()
        && QFileInfo(m_project.sourceMediaPath).isFile();
    if (!hasMedia)
        addIssue(QStringLiteral("source-media"),
                 QStringLiteral("Import source media before starting Automatic dubbing."), 0,
                 QStringLiteral("source-media"));
    // sourceLanguage/targetLanguage are the project's single source of truth.
    // The wizard never keeps a parallel, display-only copy of them.
    if (m_project.sourceLanguage.trimmed().isEmpty())
        addIssue(QStringLiteral("source-language"),
                 QStringLiteral("Choose the spoken/source language for Transcribe and Translate."), 0,
                 QStringLiteral("source-language"));
    if (m_project.targetLanguage.trimmed().isEmpty())
        addIssue(QStringLiteral("target-language"),
                 QStringLiteral("Choose the output language for Translate and TTS."), 0,
                 QStringLiteral("target-language"));
    if (!workflowGraphValid())
        addIssue(QStringLiteral("workflow-graph"),
                 QStringLiteral("The default Dubbing workflow definition is invalid."));

    if (!m_project.ttsVoiceId.trimmed().isEmpty() && !cloneVoiceSelectionValid()) {
        addIssue(QStringLiteral("tts-voice"), cloneVoiceSelectionError());
    }

    const bool adaptiveRewriteRequired = m_project.dubbingQuality == QStringLiteral("adaptive")
        || (m_project.dubbingQuality == QStringLiteral("custom")
            && m_project.durationControl.value(QStringLiteral("enabled"), true).toBool()
            && m_project.durationControl.value(QStringLiteral("autoRewrite"), true).toBool());
    if (adaptiveRewriteRequired && !adaptiveReady()) {
        const QString message = adaptiveProvider() == QStringLiteral("colab-direct")
            ? QStringLiteral("Connect and check the exact Direct Colab Adaptive LLM worker for Translate.")
            : QStringLiteral("Configure the Adaptive rewrite LLM for Translate. Automatic will not download or use a local fallback.");
        addIssue(QStringLiteral("adaptive-llm"), message,
                 adaptiveProvider() == QStringLiteral("colab-direct") ? 2 : 1,
                 QStringLiteral("translate"));
    }

    // Sessions are keyed by production node id. The aggregate stage loop
    // below maps each selected worker to one presentation stage exactly once.
    QHash<QString, QVariantMap> directWorkers;
    for (const QVariant &value : colabSetupStages()) {
        const QVariantMap stage = value.toMap();
        if (!stage.value(QStringLiteral("selectedForDirectColab")).toBool()
            || !stage.value(QStringLiteral("activeForTranscriptSource"), true).toBool()) {
            continue;
        }
        directWorkers.insert(stage.value(QStringLiteral("id")).toString(), stage);
    }

    QVariantList aggregateStages;
    QVariantList selectedWorkers;
    for (const QVariant &value : workflowStages()) {
        const QVariantMap stage = value.toMap();
        const QString stageId = stage.value(QStringLiteral("id")).toString();
        const QString nodeId = stage.value(QStringLiteral("actionNodeId")).toString();
        QVariantMap configuration = m_workflowNodeConfigurations.value(nodeId).toMap();
        const QVariantMap parameters = configuration.value(QStringLiteral("parameters")).toMap();
        const QString providerId = configuration.value(
            QStringLiteral("executionProvider"), parameters.value(
            QStringLiteral("executionProvider"))).toString().trimmed().toLower();
        const QString route = providerId == QStringLiteral("colab-direct")
            ? QStringLiteral("Direct Colab")
            : providerId == QStringLiteral("api-gateway")
                ? QStringLiteral("API Gateway")
                : providerId == QStringLiteral("local-dev") ? QStringLiteral("Local")
                : QStringLiteral("Not selected");
        const bool requiresSourceLanguage = nodeId == QStringLiteral("transcribe")
            || nodeId == QStringLiteral("review-transcript")
            || nodeId == QStringLiteral("translate");
        const bool requiresTargetLanguage = nodeId == QStringLiteral("translate")
            || nodeId == QStringLiteral("synthesize");
        QString languageSummary;
        if (nodeId == QStringLiteral("translate"))
            languageSummary = QStringLiteral("%1 -> %2").arg(m_project.sourceLanguage, m_project.targetLanguage);
        else if (requiresSourceLanguage)
            languageSummary = m_project.sourceLanguage;
        else if (requiresTargetLanguage)
            languageSummary = m_project.targetLanguage;
        QString setupAction = QStringLiteral("none");
        QString setupHint = QStringLiteral("No configuration required");
        if (nodeId == QStringLiteral("media-input")) {
            setupAction = QStringLiteral("source");
            setupHint = QStringLiteral("Choose source media on page 1");
        } else if (nodeId == QStringLiteral("source-separate")
                   || nodeId == QStringLiteral("transcribe")
                   || nodeId == QStringLiteral("translate")
                   || nodeId == QStringLiteral("synthesize")) {
            setupAction = QStringLiteral("node-model");
            setupHint = QStringLiteral("Choose route and model");
        } else if (nodeId == QStringLiteral("fit-timing")) {
            setupAction = QStringLiteral("alignment");
            setupHint = QStringLiteral("Configure timing resolution");
        } else if (nodeId == QStringLiteral("export")) {
            setupAction = QStringLiteral("export");
            setupHint = QStringLiteral("Configure output/export options");
        } else if (nodeId == QStringLiteral("ingest")) {
            setupAction = QStringLiteral("normalize");
            setupHint = QStringLiteral("Automatic local preprocessing; no model required");
        }

        QString preflightState = QStringLiteral("ready");
        QString preflightStateLabel = QStringLiteral("Ready");
        if (nodeId == QStringLiteral("media-input") && !hasMedia) {
            preflightState = QStringLiteral("needs-input");
            preflightStateLabel = QStringLiteral("Needs input");
        } else if (stage.value(QStringLiteral("state")).toString() == QStringLiteral("missing")
                   || stage.value(QStringLiteral("state")).toString() == QStringLiteral("blocked")) {
            preflightState = QStringLiteral("blocked-previous");
            preflightStateLabel = QStringLiteral("Blocked by previous stage");
        }
        const bool modelStage = nodeId == QStringLiteral("source-separate")
            || nodeId == QStringLiteral("transcribe") || nodeId == QStringLiteral("translate")
            || nodeId == QStringLiteral("synthesize");
        const QString modelId = configuration.value(
            QStringLiteral("modelId"), parameters.value(QStringLiteral("modelId"))).toString().trimmed();
        if (modelStage && (configuration.isEmpty() || providerId.isEmpty() || modelId.isEmpty())) {
            preflightState = hasMedia ? QStringLiteral("needs-setup")
                                      : QStringLiteral("blocked-previous");
            preflightStateLabel = hasMedia ? QStringLiteral("Needs setup")
                                            : QStringLiteral("Blocked by source media");
            addIssue(stageId, QStringLiteral("Configure %1 with a route and an exact model/runtime.")
                .arg(stage.value(QStringLiteral("title")).toString()));
        } else if (modelStage && providerId == QStringLiteral("colab-direct")) {
            const QVariantMap worker = directWorkers.value(nodeId);
            const bool verified = !worker.isEmpty() && worker.value(QStringLiteral("verified")).toBool();
            if (!DubbingColabModelRoutes::supports(nodeId, modelId) || !verified) {
                preflightState = QStringLiteral("needs-worker");
                preflightStateLabel = QStringLiteral("Direct Colab needs check");
                addIssue(QStringLiteral("colab-") + nodeId,
                         QStringLiteral("Connect and check the exact Direct Colab worker for %1 (%2).")
                             .arg(stage.value(QStringLiteral("title")).toString(), modelId), 2);
            }
            // Saved clone voices use their own exact Direct Colab worker.  Do
            // not validate the ordinary TTS model language contract against a
            // request that will be synthesized by the clone model instead.
            const bool usesSavedCloneVoice = nodeId == QStringLiteral("synthesize")
                && !selectedCloneVoicePreset().isEmpty();
            if (nodeId == QStringLiteral("synthesize") && !usesSavedCloneVoice) {
                const QString ttsLanguage = parameters.value(QStringLiteral("lang"),
                    m_project.targetLanguage).toString().trimmed();
                if (!DubbingColabModelRoutes::supportsTtsLanguage(modelId, ttsLanguage)) {
                    preflightState = QStringLiteral("needs-setup");
                    preflightStateLabel = QStringLiteral("TTS language incompatible");
                    addIssue(QStringLiteral("tts-language"),
                             DubbingColabModelRoutes::ttsLanguageCompatibilityError(
                                 modelId, ttsLanguage), 1, QStringLiteral("synthesize"));
                }
            }
            if (!worker.isEmpty()) {
                QVariantMap workerCard = worker;
                workerCard.insert(QStringLiteral("parentStageId"), stageId);
                workerCard.insert(QStringLiteral("parentStageTitle"), stage.value(QStringLiteral("title")));
                selectedWorkers.append(workerCard);
            }
        } else if (modelStage && providerId == QStringLiteral("api-gateway")) {
            const bool gatewayConfigured = m_settings && !m_settings->gatewayUrl().trimmed().isEmpty()
                && m_settings->gatewayApiKeyConfigured();
            if (!gatewayConfigured) {
                preflightState = QStringLiteral("needs-setup");
                preflightStateLabel = QStringLiteral("API Gateway needs setup");
                addIssue(stageId + QStringLiteral("-gateway"),
                         QStringLiteral("Configure API Gateway credentials before using %1.")
                             .arg(stage.value(QStringLiteral("title")).toString()));
            }
        } else if (modelStage && providerId == QStringLiteral("local-dev")) {
            StudioConfiguration localConfiguration;
            localConfiguration.capabilityId = stage.value(QStringLiteral("capabilityId")).toString();
            localConfiguration.familyId = configuration.value(QStringLiteral("familyId")).toString();
            localConfiguration.runtimeId = configuration.value(QStringLiteral("runtimeId")).toString();
            localConfiguration.runtimeVersion = configuration.value(QStringLiteral("runtimeVersion")).toString();
            localConfiguration.selectedFiles = configuration.value(QStringLiteral("selectedFiles")).toMap();
            if (!StudioConfigurationResolver::resolve(localConfiguration).isValid) {
                preflightState = QStringLiteral("needs-setup");
                preflightStateLabel = QStringLiteral("Local runtime/model needs setup");
                addIssue(stageId + QStringLiteral("-local"),
                         QStringLiteral("Choose an installed local runtime and model for %1.")
                             .arg(stage.value(QStringLiteral("title")).toString()));
            }
        }

        QString configurationSummary = modelStage
            ? (modelId.isEmpty()
                ? QStringLiteral("No route and exact model/runtime have been confirmed.")
                : QStringLiteral("%1 / %2").arg(route, modelId))
            : setupHint;
        if (nodeId == QStringLiteral("ingest")) {
            const QString sourceShape = m_project.sourceSampleRate > 0 && m_project.sourceChannels > 0
                ? QStringLiteral("source %1 Hz / %2 channel(s)")
                      .arg(m_project.sourceSampleRate).arg(m_project.sourceChannels)
                : QStringLiteral("source format will be probed at ingest");
            configurationSummary = QStringLiteral("Automatic local preprocessing; %1; master and analysis WAV outputs; no model required.")
                .arg(sourceShape);
        } else if (nodeId == QStringLiteral("fit-timing")) {
            const QVariantMap timing = timingConfiguration();
            configurationSummary = QStringLiteral("Timing: %1; minimum gap %2 ms.")
                .arg(timing.value(QStringLiteral("mode")).toString())
                .arg(timing.value(QStringLiteral("minimumGapMs")).toInt());
        } else if (stageId == QStringLiteral("export")) {
            configurationSummary = QStringLiteral("Render mix with background; subtitle burn-in: %1.")
                .arg(subtitleConfiguration().value(QStringLiteral("burnIn")).toBool()
                         ? QStringLiteral("enabled") : QStringLiteral("disabled"));
        }
        const bool adaptiveSetupRequired = nodeId == QStringLiteral("translate")
            && adaptiveRewriteRequired && !adaptiveReady();
        if (adaptiveSetupRequired && preflightState == QStringLiteral("ready")) {
            preflightState = adaptiveProvider() == QStringLiteral("colab-direct")
                ? QStringLiteral("needs-worker") : QStringLiteral("needs-setup");
            preflightStateLabel = adaptiveProvider() == QStringLiteral("colab-direct")
                ? QStringLiteral("Adaptive LLM needs check") : QStringLiteral("Adaptive LLM needs setup");
        }
        if (nodeId == QStringLiteral("translate")) {
            configurationSummary += QStringLiteral("\nAdaptive rewrite LLM: %1.")
                .arg(adaptiveReady() ? adaptiveStatusText() : QStringLiteral("not ready"));
        }

        // Direct Colab notebooks currently expose one immutable GPU
        // configuration. Keep that exact variant on the presentation stage
        // as well as on the worker card: otherwise the stage list can say
        // only "model" while verification is bound to model + variant.
        QString variant = parameters.value(QStringLiteral("variant")).toString().trimmed();
        if (providerId == QStringLiteral("colab-direct")) {
            variant = directWorkers.value(nodeId).value(QStringLiteral("variant")).toString().trimmed();
            if (variant.isEmpty()) variant = QStringLiteral("fixed");
        }

        aggregateStages.append(QVariantMap{
            {QStringLiteral("id"), stageId},
            {QStringLiteral("title"), stage.value(QStringLiteral("title"), stageId)},
            {QStringLiteral("actionNodeId"), nodeId},
            {QStringLiteral("productionNodeIds"), stage.value(QStringLiteral("productionNodeIds"))},
            {QStringLiteral("executionProvider"), providerId},
            {QStringLiteral("route"), route},
            {QStringLiteral("modelId"), modelId},
            {QStringLiteral("variant"), variant},
            {QStringLiteral("requiresLanguage"), requiresSourceLanguage || requiresTargetLanguage},
            {QStringLiteral("languageSummary"), languageSummary},
            {QStringLiteral("state"), stage.value(QStringLiteral("state"))},
            {QStringLiteral("detail"), stage.value(QStringLiteral("detail"), configurationSummary)},
            {QStringLiteral("setupAction"), setupAction},
            {QStringLiteral("setupHint"), setupHint},
            {QStringLiteral("configurationSummary"), configurationSummary},
            {QStringLiteral("modelRequired"), modelStage},
            {QStringLiteral("effectiveFormat"), nodeId == QStringLiteral("ingest") ? configurationSummary : QString()},
            {QStringLiteral("preflightState"), preflightState},
            {QStringLiteral("preflightStateLabel"), preflightStateLabel},
            {QStringLiteral("adaptiveSetupRequired"), adaptiveSetupRequired}
        });
    }

    if (directWorkers.contains(QStringLiteral("adaptive-llm"))) {
        QVariantMap adaptiveWorker = directWorkers.value(QStringLiteral("adaptive-llm"));
        adaptiveWorker.insert(QStringLiteral("parentStageId"), QStringLiteral("translate"));
        adaptiveWorker.insert(QStringLiteral("parentStageTitle"), QStringLiteral("Translate"));
        selectedWorkers.append(adaptiveWorker);
    }

    return QVariantMap{
        {QStringLiteral("ready"), issues.isEmpty()},
        {QStringLiteral("issues"), issues},
        {QStringLiteral("stages"), aggregateStages},
        {QStringLiteral("selectedWorkers"), selectedWorkers},
        {QStringLiteral("sourceMediaPath"), m_project.sourceMediaPath},
        {QStringLiteral("sourceLanguage"), m_project.sourceLanguage},
        {QStringLiteral("targetLanguage"), m_project.targetLanguage},
        {QStringLiteral("transcriptSource"), m_project.transcriptConfiguration.value(
            QStringLiteral("transcriptSource"), QStringLiteral("stt"))},
        {QStringLiteral("fingerprint"), automaticPreflightFingerprint()}
    };
}

QString DubbingController::automaticPreflightFingerprint() const
{
    QVariantMap snapshot;
    snapshot.insert(QStringLiteral("sourceMedia"), m_project.sourceMediaPath);
    snapshot.insert(QStringLiteral("sourceLanguage"), m_project.sourceLanguage);
    snapshot.insert(QStringLiteral("targetLanguage"), m_project.targetLanguage);
    snapshot.insert(QStringLiteral("transcript"), m_project.transcriptConfiguration);
    snapshot.insert(QStringLiteral("quality"), m_project.dubbingQuality);
    snapshot.insert(QStringLiteral("adaptiveLlm"), translationFixConfiguration());
    snapshot.insert(QStringLiteral("ttsVoice"), m_project.ttsVoiceId);
    snapshot.insert(QStringLiteral("nodes"), m_workflowNodeConfigurations);

    QVariantList workers;
    for (const QVariant &value : colabSetupStages()) {
        const QVariantMap stage = value.toMap();
        if (!stage.value(QStringLiteral("selectedForDirectColab")).toBool()
            || !stage.value(QStringLiteral("activeForTranscriptSource"), true).toBool()) {
            continue;
        }
        const QString stageId = stage.value(QStringLiteral("id")).toString();
        ColabSession *session = colabSessionForStage(stageId);
        workers.append(QVariantMap{
            {QStringLiteral("id"), stageId},
            {QStringLiteral("capability"), stage.value(QStringLiteral("capability"))},
            {QStringLiteral("model"), stage.value(QStringLiteral("modelId"))},
            {QStringLiteral("verified"), stage.value(QStringLiteral("verified"))},
            {QStringLiteral("workerUrl"), session ? session->workerUrl() : QString()},
            {QStringLiteral("variant"), session ? session->expectedVariant() : QString()},
            {QStringLiteral("verifiedAt"), session ? session->verifiedAt() : QString()}
        });
    }
    snapshot.insert(QStringLiteral("workers"), workers);
    return QString::fromUtf8(QJsonDocument::fromVariant(snapshot).toJson(QJsonDocument::Compact));
}

QSet<QString> DubbingController::activeDownloadKeys() const
{
    QSet<QString> keys;
    const auto *app = AppController::instance();
    const QVariantList downloads = app && app->downloads()
        ? app->downloads()->activeDownloads() : QVariantList();
    for (const QVariant &entry : downloads) {
        const QVariantMap download = entry.toMap();
        const QString identifier = download.value(QStringLiteral("identifier")).toString();
        const QString filename = download.value(QStringLiteral("filename")).toString();
        if (!identifier.isEmpty() && !filename.isEmpty())
            keys.insert(identifier + QStringLiteral("::") + filename);
    }
    return keys;
}

void DubbingController::captureNewAutomaticDownloads(const QSet<QString> &before)
{
    const QSet<QString> current = activeDownloadKeys();
    for (const QString &key : current) {
        if (!before.contains(key))
            m_automaticDownloadKeys.insert(key);
    }
}

QVariantList DubbingController::automaticSetupDownloads() const
{
    QVariantList scoped;
    if (m_automaticDownloadKeys.isEmpty()) return scoped;
    const auto *app = AppController::instance();
    const QVariantList active = app && app->downloads()
        ? app->downloads()->activeDownloads() : QVariantList();
    for (const QVariant &entry : active) {
        const QVariantMap download = entry.toMap();
        const QString key = download.value(QStringLiteral("identifier")).toString()
            + QStringLiteral("::") + download.value(QStringLiteral("filename")).toString();
        if (m_automaticDownloadKeys.contains(key))
            scoped.append(download);
    }
    return scoped;
}

bool DubbingController::approveAutomaticPreflight()
{
    const QVariantMap preflight = automaticPreflight();
    if (!preflight.value(QStringLiteral("ready")).toBool()) {
        const QVariantList issues = preflight.value(QStringLiteral("issues")).toList();
        const QString detail = issues.isEmpty() ? QStringLiteral("Automatic preflight is blocked.")
            : issues.constFirst().toMap().value(QStringLiteral("message")).toString();
        setError(detail);
        return false;
    }
    m_automaticPreflightFingerprint = preflight.value(QStringLiteral("fingerprint")).toString();
    clearError();
    emit workflowChanged();
    return true;
}

QString DubbingController::workflowId() const
{
    return QString::fromLatin1(DubbingWorkflowDefinition::Id);
}

int DubbingController::workflowVersion() const
{
    return DubbingWorkflowDefinition::Version;
}

bool DubbingController::workflowGraphValid() const
{
    if (!m_workflowRegistry) return false;
    return WorkflowGraphRunner(m_workflowRegistry).validate(DubbingWorkflowDefinition::create()).isEmpty();
}

QString DubbingController::workflowRunId() const
{
    if (m_workflowRunner && m_workflowRunner->running()) return m_workflowRunner->runId();
    return m_runner ? m_runner->runId() : QString();
}

QString DubbingController::workflowNodeRunId() const
{
    if (m_workflowRunner && m_workflowRunner->running()) return m_workflowRunner->nodeRunId();
    return m_runner ? m_runner->nodeRunId() : QString();
}

bool DubbingController::workflowWaitingForInput() const
{
    return m_workflowRunner && m_workflowRunner->waitingForInput();
}

QVariantMap DubbingController::workflowReviewRequest() const
{
    return m_workflowReviewRequest;
}

QString DubbingController::currentStepId() const
{
    if (m_workflowRunner && m_workflowRunner->running()) {
        return visibleStepForNode(m_workflowRunner->activeNodeId());
    }
    return m_currentStepId;
}

QVariantMap DubbingController::currentStepOutput() const
{
    return stepOutput(currentStepId());
}

QVariantMap DubbingController::stepOutput(const QString &stepId) const
{
    return m_stepOutputs.value(stepId).toMap();
}

void DubbingController::setWorkflowMode(const QString &mode)
{
    if (m_workflowMode == mode) return;
    m_workflowMode = mode;
    emit workflowChanged();
}

void DubbingController::setCurrentStep(const QString &stepId)
{
    if (m_currentStepId == stepId) return;
    m_currentStepId = stepId;
    emit workflowChanged();
}

void DubbingController::advanceManualStep(const QString &completedStepId)
{
    static const QHash<QString, QString> next{{QStringLiteral("ingest"), QStringLiteral("source-separate")},
                                              {QStringLiteral("source-separate"), QStringLiteral("transcribe")},
                                              {QStringLiteral("transcribe"), QStringLiteral("translate")},
                                              {QStringLiteral("translate"), QStringLiteral("synthesize")},
                                              {QStringLiteral("synthesize"), QStringLiteral("mix")},
                                              {QStringLiteral("mix"), QStringLiteral("export")},
                                              {QStringLiteral("export"), QStringLiteral("completed")}};
    if (next.contains(completedStepId)) setCurrentStep(next.value(completedStepId));
}

void DubbingController::prepareWorkflow()
{
    if (!workflowGraphValid()) {
        setError(QStringLiteral("The default dubbing workflow definition is invalid."));
        return;
    }
    emit workflowChanged();
}

QString DubbingController::defaultWorkflowModelFamily(const QString &nodeId) const
{
    QString capabilityId;
    if (nodeId == QStringLiteral("source-separate"))
        capabilityId = QStringLiteral("voice-isolation");
    else if (nodeId == QStringLiteral("transcribe"))
        capabilityId = QStringLiteral("stt");
    else if (nodeId == QStringLiteral("translate"))
        capabilityId = QStringLiteral("translation");
    else if (nodeId == QStringLiteral("synthesize"))
        capabilityId = QStringLiteral("tts");
    return automaticDefaultFamilyId(capabilityId, m_project.dubbingQuality);
}

void DubbingController::resetStandardWorkflowNodeModels()
{
    if (m_project.dubbingQuality == QStringLiteral("custom")) return;
    m_workflowNodeConfigurations.clear();
    m_project.workflowNodeConfigurations.clear();
    resetStandardTranslationFixConfiguration();
    emit workflowChanged();
}

void DubbingController::resetStandardTranslationFixConfiguration()
{
    if (!m_translationFix || m_translationFix->busy()) return;
    if (m_settings && m_settings->remoteFirstMode()) {
        configureRemoteRewriteFromGateway();
        return;
    }
    m_translationFix->setConfiguration({
        {QStringLiteral("provider"), QStringLiteral("lmstudio")},
        {QStringLiteral("configured"), false},
        {QStringLiteral("serverUrl"), QStringLiteral("http://127.0.0.1:1234")},
        {QStringLiteral("model"), QStringLiteral("qwen3.5-2b")},
        {QStringLiteral("maxAttempts"),
         m_project.durationControl.value(QStringLiteral("maxPreTtsIterations"), 4)},
        {QStringLiteral("temperature"), 0.35}
    });
    if (m_runner)
        m_runner->setTranslationFixConfiguration(m_translationFix->configuration());
}

void DubbingController::configureRemoteRewriteFromGateway()
{
    if (!m_translationFix || m_translationFix->busy() || !m_settings) return;
    const QString model = m_settings->gatewayLlmModel().trimmed();
    const QString url = m_settings->gatewayUrl().trimmed();
    m_translationFix->setConfiguration({
        {QStringLiteral("provider"), QStringLiteral("api")},
        {QStringLiteral("configured"),
         !url.isEmpty() && !model.isEmpty() && m_settings->gatewayApiKeyConfigured()},
        {QStringLiteral("serverUrl"), url},
        {QStringLiteral("apiKey"), m_settings->gatewayApiKey()},
        {QStringLiteral("model"), model},
        {QStringLiteral("maxAttempts"),
         m_project.durationControl.value(QStringLiteral("maxPreTtsIterations"), 4)},
        {QStringLiteral("temperature"), 0.35}
    });
    if (m_runner)
        m_runner->setTranslationFixConfiguration(m_translationFix->configuration());
}

QString DubbingController::visibleStepForNode(const QString &nodeId)
{
    if (nodeId == QStringLiteral("media-input")) return QStringLiteral("import");
    if (nodeId == QStringLiteral("review-transcript")) return QStringLiteral("transcribe");
    if (nodeId == QStringLiteral("review-translation")) return QStringLiteral("translate");
    if (nodeId == QStringLiteral("fit-timing") || nodeId == QStringLiteral("review-conflicts"))
        return QStringLiteral("alignment-subtitle");
    if (nodeId == QStringLiteral("assign-voices")) return QStringLiteral("synthesize");
    if (nodeId == QStringLiteral("mix")) return QStringLiteral("export");
    return nodeId;
}

void DubbingController::setAutomaticStatus(const QString &message)
{
    if (m_automaticStatusText == message) return;
    m_automaticStatusText = message;
    emit workflowChanged();
}

void DubbingController::appendAutomaticEvent(const QString &message,
                                             const QString &state,
                                             const QString &nodeId)
{
    if (message.trimmed().isEmpty()) return;
    if (!m_automaticEvents.isEmpty()) {
        const QVariantMap last = m_automaticEvents.constLast().toMap();
        if (last.value(QStringLiteral("message")).toString() == message
            && last.value(QStringLiteral("state")).toString() == state)
            return;
    }
    m_automaticEvents.append(QVariantMap{
        {QStringLiteral("message"), message},
        {QStringLiteral("state"), state},
        {QStringLiteral("nodeId"), nodeId},
        {QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))}
    });
    while (m_automaticEvents.size() > 40) m_automaticEvents.removeFirst();
    emit workflowChanged();
}

CapabilityFamilyModel *DubbingController::automaticModel(const QString &capabilityId)
{
    AppController *app = AppController::instance();
    if (!app) return nullptr;
    std::unique_ptr<CapabilityFamilyModel> *holder = nullptr;
    if (capabilityId == QStringLiteral("stt")) holder = &m_automaticSttModel;
    else if (capabilityId == QStringLiteral("voice-isolation"))
        holder = &m_automaticVoiceIsolationModel;
    else if (capabilityId == QStringLiteral("translation")) holder = &m_automaticTranslationModel;
    else if (capabilityId == QStringLiteral("tts")) holder = &m_automaticTtsModel;
    else if (capabilityId == QStringLiteral("llm-chat")) holder = &m_automaticLlmModel;
    if (!holder) return nullptr;
    if (!*holder) {
        *holder = std::make_unique<CapabilityFamilyModel>(
            m_models, m_runtimes, app->registry(), app->settings(), this);
        (*holder)->setCapability(capabilityId);
    }
    (*holder)->refresh();
    return holder->get();
}

bool DubbingController::ensureAutomaticModel(const QString &nodeId,
                                             const QString &capabilityId,
                                             bool loadSession)
{
    AppController *app = AppController::instance();
    if (!app || !app->downloadInstall() || !app->sessionRegistry()) {
        finishAutomaticSetupFailure(
            QStringLiteral("Automatic model setup is unavailable for %1.").arg(capabilityId));
        return false;
    }

    QVariantMap configuration = m_workflowNodeConfigurations.value(nodeId).toMap();
    m_automaticSetupNodeId = nodeId;
    const QVariantMap configuredParameters = configuration.value(QStringLiteral("parameters")).toMap();
    const QString providerId = configuration.value(
        QStringLiteral("executionProvider"), configuredParameters.value(
            QStringLiteral("executionProvider"), QStringLiteral("local-dev")))
        .toString().trimmed().toLower();
    ExecutionProvider provider = ExecutionProvider::LocalDev;
    if (!executionProviderFromId(providerId, &provider)) {
        finishAutomaticSetupFailure(
            QStringLiteral("Unknown execution provider for %1.").arg(visibleStepForNode(nodeId)));
        return false;
    }
    const QString configuredModel = configuration.value(
        QStringLiteral("modelId"), configuredParameters.value(QStringLiteral("modelId")))
        .toString().trimmed().toLower();

    // The Automatic wizard has already captured an explicit provider for each
    // model node.  Do not use a global remote-first preference to decide this
    // boundary: selecting Direct Colab or API Gateway must never enqueue a
    // local model/runtime download as a hidden fallback.
    if (provider == ExecutionProvider::ColabDirect) {
        const QString capability = colabCapabilityForStage(nodeId);
        ColabSession *session = colabSessionForStage(nodeId);
        QString routeError;
        if (configuredModel.isEmpty()
            || !DubbingColabModelRoutes::supports(nodeId, configuredModel)
            || !session || !session->hasVerifiedRoute(capability, configuredModel, &routeError)) {
            const QString detail = routeError.trimmed().isEmpty()
                ? QStringLiteral("Connect and check the exact Direct Colab worker in Automatic setup.")
                : routeError;
            finishAutomaticSetupFailure(
                QStringLiteral("Direct Colab is not ready for %1: %2")
                    .arg(visibleStepForNode(nodeId), detail));
            return false;
        }
        setAutomaticStatus(QStringLiteral("Using verified Direct Colab worker for %1")
                               .arg(visibleStepForNode(nodeId)));
        appendAutomaticEvent(QStringLiteral("Direct Colab worker ready for %1 (%2)")
                                 .arg(visibleStepForNode(nodeId), configuredModel),
                             QStringLiteral("completed"), nodeId);
        return true;
    }
    if (provider == ExecutionProvider::ApiGateway) {
        Settings *settings = m_settings ? m_settings : app->settings();
        if (configuredModel.isEmpty() || !settings || settings->gatewayUrl().trimmed().isEmpty()
            || !settings->gatewayApiKeyConfigured()) {
            finishAutomaticSetupFailure(
                QStringLiteral("API Gateway is not ready for %1. Configure its URL, key, and exact model in Automatic setup.")
                    .arg(visibleStepForNode(nodeId)));
            return false;
        }
        setAutomaticStatus(QStringLiteral("Using configured API Gateway for %1")
                               .arg(visibleStepForNode(nodeId)));
        appendAutomaticEvent(QStringLiteral("API Gateway ready for %1 (%2)")
                                 .arg(visibleStepForNode(nodeId), configuredModel),
                             QStringLiteral("completed"), nodeId);
        return true;
    }

    CapabilityFamilyModel *model = automaticModel(capabilityId);
    if (!model) {
        finishAutomaticSetupFailure(
            QStringLiteral("Automatic model setup is unavailable for %1.").arg(capabilityId));
        return false;
    }
    QVariantMap recommendation;
    if (configuration.isEmpty()) {
        recommendation = model->configurationForFamily(
            automaticDefaultFamilyId(capabilityId, m_project.dubbingQuality));
        if (recommendation.isEmpty()) {
            finishAutomaticSetupFailure(
                QStringLiteral("The required default model %1 is not available for %2.")
                    .arg(automaticDefaultFamilyId(capabilityId, m_project.dubbingQuality),
                         capabilityId));
            return false;
        }
    } else {
        recommendation = {
            {QStringLiteral("familyId"), configuration.value(QStringLiteral("familyId"))},
            {QStringLiteral("runtimeId"), configuration.value(QStringLiteral("runtimeId"))},
            {QStringLiteral("runtimeVersion"), configuration.value(QStringLiteral("runtimeVersion"))},
            {QStringLiteral("selectedFiles"), configuration.value(QStringLiteral("selectedFiles"))}
        };
        model->setInitialSelectedFiles(recommendation.value(QStringLiteral("familyId")).toString(),
                                       recommendation.value(QStringLiteral("selectedFiles")).toMap());
        model->setSelectedFamilyId(recommendation.value(QStringLiteral("familyId")).toString());
        model->refresh();
    }

    const QString familyId = recommendation.value(QStringLiteral("familyId")).toString();
    QVariantMap familyItem = model->itemForFamily(familyId);
    if (familyItem.isEmpty()) {
        appendAutomaticEvent(
            QStringLiteral("Discarded stale %1 model setting: %2").arg(capabilityId, familyId),
            QStringLiteral("warning"), nodeId);
        m_workflowNodeConfigurations.remove(nodeId);
        m_automaticConfiguredNodes.remove(nodeId);
        scheduleAutomaticSetupAdvance();
        return false;
    }

    // A saved workflow may reference a model variant that has since been
    // removed from the compatibility catalog. Replace only those stale roles
    // with the currently supported selection so automatic runs can download
    // and use the compatible file without requiring manual reconfiguration.
    QVariantMap recommendedFiles = recommendation.value(QStringLiteral("selectedFiles")).toMap();
    const QVariantMap supportedFiles = familyItem.value(QStringLiteral("selectedFiles")).toMap();
    for (const QVariant &requirementValue : familyItem.value(QStringLiteral("requiredFiles")).toList()) {
        const QVariantMap requirement = requirementValue.toMap();
        const QString role = requirement.value(QStringLiteral("role")).toString();
        const QString selectedFile = recommendedFiles.value(role).toString();
        const QVariantList candidates = requirement.value(QStringLiteral("candidates")).toList();
        const QString defaultFile = requirement.value(QStringLiteral("file")).toString();
        const bool supported = candidates.isEmpty()
            ? selectedFile == defaultFile : candidates.contains(selectedFile);
        if (selectedFile.isEmpty() || !supported)
            recommendedFiles.insert(role, supportedFiles.value(role, defaultFile));
    }
    recommendation.insert(QStringLiteral("selectedFiles"), recommendedFiles);

    if (!familyItem.value(QStringLiteral("ready")).toBool()) {
        if (!m_automaticDownloadsQueued.contains(capabilityId)) {
            const QSet<QString> downloadsBefore = activeDownloadKeys();
            if (!app->downloadInstall()->enqueueRecommendedSetup(familyItem)) {
                finishAutomaticSetupFailure(
                    QStringLiteral("Could not start the %1 model download.").arg(capabilityId));
                return false;
            }
            captureNewAutomaticDownloads(downloadsBefore);
            m_automaticDownloadsQueued.insert(capabilityId);
            appendAutomaticEvent(
                QStringLiteral("Downloading the default %1 model and runtime").arg(capabilityId),
                QStringLiteral("downloading"), nodeId);
        }
        setAutomaticStatus(
            QStringLiteral("Preparing %1 model: %2").arg(capabilityId,
                familyItem.value(QStringLiteral("displayName"), familyId).toString()));
        return false;
    }

    m_automaticDownloadsQueued.remove(capabilityId);
    const QString runtimeId = recommendation.value(
        QStringLiteral("runtimeId"), familyItem.value(QStringLiteral("selectedRuntimeId"))).toString();
    const QString runtimeVersion = recommendation.value(
        QStringLiteral("runtimeVersion"), familyItem.value(QStringLiteral("selectedRuntimeVersion"))).toString();
    const QVariantMap selectedFiles = recommendation.value(
        QStringLiteral("selectedFiles"), familyItem.value(QStringLiteral("selectedFiles"))).toMap();

    if (!loadSession) {
        StudioConfiguration selected;
        selected.capabilityId = capabilityId;
        selected.familyId = familyId;
        selected.runtimeId = runtimeId;
        selected.runtimeVersion = runtimeVersion;
        selected.selectedFiles = selectedFiles;
        if (!StudioConfigurationResolver::resolve(selected).isValid) return false;
        if (configuration.isEmpty()) {
            if (!configureWorkflowNodeModel(nodeId, familyId, runtimeId,
                                            runtimeVersion, selectedFiles, false))
                return false;
            m_automaticConfiguredNodes.insert(nodeId);
        }
        return true;
    }

    IModelSession *session = app->sessionRegistry()->sessionForCapability(capabilityId);
    if (session && session->canProcess()) return true;
    if (session && (session->state() == ModelSessionState::Loading
                    || session->state() == ModelSessionState::Processing)) {
        setAutomaticStatus(QStringLiteral("Loading %1 model into memory").arg(capabilityId));
        return false;
    }
    setAutomaticStatus(QStringLiteral("Loading %1 model into memory").arg(capabilityId));
    const bool automaticallySelected = configuration.isEmpty();
    if (automaticallySelected) m_automaticConfiguredNodes.insert(nodeId);
    const bool configured = configureWorkflowNodeModel(
        nodeId, familyId, runtimeId, runtimeVersion, selectedFiles, true);
    if (!configured && automaticallySelected) m_automaticConfiguredNodes.remove(nodeId);
    return configured && session && session->canProcess();
}

bool DubbingController::ensureAutomaticAdaptiveModel()
{
    m_automaticSetupNodeId = QStringLiteral("translate");
    if (m_project.dubbingQuality == QStringLiteral("custom")) {
        const bool rewriteEnabled =
            m_project.durationControl.value(QStringLiteral("enabled"), true).toBool()
            && m_project.durationControl.value(QStringLiteral("autoRewrite"), true).toBool();
        if (!rewriteEnabled || adaptiveReady()) return true;
        finishAutomaticSetupFailure(
            QStringLiteral("The Translate node requires a rewrite model for Custom dubbing."));
        return false;
    }
    if (m_project.dubbingQuality != QStringLiteral("adaptive")) return true;
    if (adaptiveReady()) return true;
    finishAutomaticSetupFailure(
        QStringLiteral("The selected Adaptive LLM route is not ready. Configure and verify it in Automatic preflight; no local fallback will be downloaded."));
    return false;
}

void DubbingController::scheduleAutomaticSetupAdvance()
{
    if (!m_automaticSetupActive || m_automaticAdvanceScheduled) return;
    m_automaticAdvanceScheduled = true;
    QTimer::singleShot(100, this, [this]() {
        m_automaticAdvanceScheduled = false;
        advanceAutomaticSetup();
    });
}

void DubbingController::prepareAutomaticVoiceRuntime()
{
    if (m_workflowMode != QStringLiteral("automatic")
        || !m_workflowRunner || !m_workflowRunner->running())
        return;
    const QVariantMap synthesis = m_workflowNodeConfigurations
                                      .value(QStringLiteral("synthesize")).toMap();
    if (configuredSynthesisProvider(synthesis) != ExecutionProvider::LocalDev) {
        const QVariantMap parameters = synthesis.value(QStringLiteral("parameters")).toMap();
        const QString model = synthesis.value(
            QStringLiteral("modelId"), parameters.value(QStringLiteral("modelId"))).toString();
        setAutomaticStatus(QStringLiteral("Using selected remote TTS route%1")
                               .arg(model.trimmed().isEmpty()
                                        ? QString() : QStringLiteral(" (%1)").arg(model)));
        appendAutomaticEvent(QStringLiteral("Remote TTS route remains selected"),
                             QStringLiteral("completed"), QStringLiteral("synthesize"));
        return;
    }
    AppController *app = AppController::instance();
    if (!app || !app->sessionRegistry()) return;
    for (const QString &capabilityId : {QStringLiteral("stt"),
                                        QStringLiteral("translation")}) {
        IModelSession *session = app->sessionRegistry()->sessionForCapability(capabilityId);
        if (session && (session->state() == ModelSessionState::Loading
                        || session->state() == ModelSessionState::Processing
                        || session->state() == ModelSessionState::Unloading
                        || !session->loadedConfigurations().isEmpty())) {
            QTimer::singleShot(100, this, &DubbingController::prepareAutomaticVoiceRuntime);
            return;
        }
    }
    IModelSession *tts = app->sessionRegistry()->sessionForCapability(QStringLiteral("tts"));
    if (tts && (tts->canProcess() || tts->state() == ModelSessionState::Loading)) return;
    setAutomaticStatus(QStringLiteral("Loading the selected model for the Voice node"));
    appendAutomaticEvent(QStringLiteral("Loading the selected voice generation model"),
                         QStringLiteral("loading"), QStringLiteral("synthesize"));
    if (!loadWorkflowNodeModel(QStringLiteral("synthesize"))) {
        setError(QStringLiteral("Could not load the selected model for the Voice node."));
        if (m_workflowRunner->running()) m_workflowRunner->cancel();
    }
}

void DubbingController::finishAutomaticSetupFailure(const QString &message)
{
    if (!m_automaticSetupActive) return;
    m_automaticSetupActive = false;
    m_automaticOutputPath.clear();
    m_automaticDownloadsQueued.clear();
    m_automaticDownloadKeys.clear();
    m_automaticConfiguredNodes.clear();
    m_automaticSetupNodeId.clear();
    setWorkflowMode(QStringLiteral("idle"));
    setError(message);
    setAutomaticStatus(message);
    appendAutomaticEvent(message, QStringLiteral("failed"));
    emit processingChanged();
    emit workflowChanged();
}

void DubbingController::advanceAutomaticSetup()
{
    if (!m_automaticSetupActive) return;
    if (m_automaticSetupNodeId.isEmpty())
        m_automaticSetupNodeId = QStringLiteral("source-separate");
    // Remote-first automatic runs use the graph's explicit per-node routes.
    // They never probe, load, or download a local model as a fallback. A
    // missing Gateway model or Colab worker therefore fails at the selected
    // node with its own provider-specific error.
    if (auto *app = AppController::instance(); app && app->settings()
        && app->settings()->remoteFirstMode()) {
        configureRemoteRewriteFromGateway();
        const QString outputPath = m_automaticOutputPath;
        m_automaticSetupActive = false;
        m_automaticDownloadsQueued.clear();
        m_automaticDownloadKeys.clear();
        m_automaticConfiguredNodes.clear();
        m_automaticSetupNodeId.clear();
        setAutomaticStatus(QStringLiteral("Starting independent remote workflow routes."));
        appendAutomaticEvent(QStringLiteral("Using configured API Gateway and direct Colab routes"),
                             QStringLiteral("completed"));
        emit processingChanged();
        emit workflowChanged();
        if (m_runner) m_runner->setTranslationFixConfiguration(translationFixConfiguration());
        setCurrentStep(QStringLiteral("ingest"));
        if (!runWorkflow(outputPath)) {
            setWorkflowMode(QStringLiteral("idle"));
            setAutomaticStatus(lastError());
            appendAutomaticEvent(lastError(), QStringLiteral("failed"));
        }
        return;
    }
    if (auto *app = AppController::instance(); app && app->sessionRegistry()) {
        bool waitingForRelease = false;
        for (const QString &capabilityId : {QStringLiteral("tts"),
                                            QStringLiteral("translation"),
                                            QStringLiteral("llm-chat")}) {
            IModelSession *session = app->sessionRegistry()->sessionForCapability(capabilityId);
            if (!session) continue;
            const QList<SessionConfiguration> loaded = session->loadedConfigurations();
            for (const SessionConfiguration &configuration : loaded)
                session->requestUnloadConfiguration(configuration.signature);
            waitingForRelease = waitingForRelease || !loaded.isEmpty()
                || session->state() == ModelSessionState::Loading
                || session->state() == ModelSessionState::Processing
                || session->state() == ModelSessionState::Unloading;
        }
        if (waitingForRelease) {
            setAutomaticStatus(QStringLiteral("Releasing previously loaded native runtimes"));
            scheduleAutomaticSetupAdvance();
            return;
        }
    }
    if (!ensureAutomaticModel(QStringLiteral("source-separate"),
                              QStringLiteral("voice-isolation"), false)) return;
    appendAutomaticEvent(QStringLiteral("Voice isolation model is ready"),
                         QStringLiteral("completed"), QStringLiteral("source-separate"));
    const QString transcriptSource = m_project.transcriptConfiguration.value(
        QStringLiteral("transcriptSource"), QStringLiteral("stt")).toString().trimmed().toLower();
    if (transcriptSource != QStringLiteral("ocr")) {
        if (!ensureAutomaticModel(QStringLiteral("transcribe"), QStringLiteral("stt"), true)) return;
        appendAutomaticEvent(QStringLiteral("Speech-to-text model is ready"),
                             QStringLiteral("completed"), QStringLiteral("transcribe"));
    } else {
        appendAutomaticEvent(QStringLiteral("STT setup skipped for OCR-only transcript"),
                             QStringLiteral("completed"), QStringLiteral("transcribe"));
    }
    if (!ensureAutomaticModel(QStringLiteral("translate"), QStringLiteral("translation"), false)) return;
    appendAutomaticEvent(QStringLiteral("Translation model is ready"),
                         QStringLiteral("completed"), QStringLiteral("translate"));
    if (!ensureAutomaticModel(QStringLiteral("synthesize"), QStringLiteral("tts"), false)) return;
    appendAutomaticEvent(QStringLiteral("Voice generation model is configured"),
                         QStringLiteral("completed"), QStringLiteral("synthesize"));
    if (!ensureAutomaticAdaptiveModel()) return;

    const QString outputPath = m_automaticOutputPath;
    m_automaticSetupActive = false;
    m_automaticDownloadsQueued.clear();
    m_automaticDownloadKeys.clear();
    m_automaticConfiguredNodes.clear();
    m_automaticSetupNodeId.clear();
    setAutomaticStatus(QStringLiteral("Models ready. Starting the dubbing workflow."));
    appendAutomaticEvent(QStringLiteral("All required models are ready"),
                         QStringLiteral("completed"));
    emit processingChanged();
    emit workflowChanged();
    if (m_runner) m_runner->setTranslationFixConfiguration(translationFixConfiguration());
    setCurrentStep(QStringLiteral("ingest"));
    if (!runWorkflow(outputPath)) {
        setWorkflowMode(QStringLiteral("idle"));
        setAutomaticStatus(lastError());
        appendAutomaticEvent(lastError(), QStringLiteral("failed"));
    }
}

bool DubbingController::runWorkflow(const QString &outputPath)
{
    if (m_dubbingEntryGateActive) {
        setError(QStringLiteral("Choose an entry mode before running the Dubbing workflow."));
        return false;
    }
    if (!m_workflowRunner || m_workflowRunner->running()) return false;
    if (workflowRecoveryAvailable()) {
        setError(QStringLiteral("Resume or discard the interrupted workflow before starting a new run."));
        return false;
    }
    if (PathUtils::urlToLocalPath(outputPath).trimmed().isEmpty()) {
        setError(QStringLiteral("Choose an output path before running the full dubbing workflow."));
        return false;
    }
    if (!workflowGraphValid() || m_project.sourceMediaPath.isEmpty()) {
        setError(QStringLiteral("Import source media before running the dubbing workflow."));
        return false;
    }
    if (!cloneVoiceSelectionValid()) {
        setError(cloneVoiceSelectionError());
        return false;
    }
    if (!snapshotSelectedColabStagesForWorkflow()) return false;
    const QVariantMap transcriptConfiguration = effectiveTranscriptConfiguration(true);
    persistAfterEdit();
    WorkflowGraph graph = DubbingWorkflowDefinition::create();
    QVariantMap effectiveDurationControl = m_project.durationControl;
    effectiveDurationControl.insert(
        QStringLiteral("autoRewrite"),
        m_project.dubbingQuality != QStringLiteral("fast")
            && m_project.durationControl.value(QStringLiteral("autoRewrite"), true).toBool());
    for (auto &node : graph.nodes) {
        if (node.id == QStringLiteral("translate"))
            node.parameters.insert(QStringLiteral("durationControl"), effectiveDurationControl);
        const QVariantMap modelConfig = m_workflowNodeConfigurations.value(node.id).toMap();
        if (!modelConfig.isEmpty()) {
            node.parameters.insert(QStringLiteral("familyId"), modelConfig.value(QStringLiteral("familyId")));
            node.parameters.insert(QStringLiteral("runtimeId"), modelConfig.value(QStringLiteral("runtimeId")));
            node.parameters.insert(QStringLiteral("runtimeVersion"), modelConfig.value(QStringLiteral("runtimeVersion")));
            node.parameters.insert(QStringLiteral("selectedFiles"), modelConfig.value(QStringLiteral("selectedFiles")));
            const QVariantMap customParameters = modelConfig.value(QStringLiteral("parameters")).toMap();
            for (auto it = customParameters.cbegin(); it != customParameters.cend(); ++it)
                node.parameters.insert(it.key(), it.value());
            node.properties = node.parameters;
        }
        if (node.id == QStringLiteral("media-input")) {
            node.parameters.insert(QStringLiteral("value"), m_project.sourceMediaPath);
            node.properties.insert(QStringLiteral("value"), m_project.sourceMediaPath);
        } else if (node.id == QStringLiteral("transcribe")) {
            const QVariantMap persistedParameters = transcriptConfiguration.value(
                QStringLiteral("parameters")).toMap();
            for (auto it = persistedParameters.cbegin(); it != persistedParameters.cend(); ++it)
                node.parameters.insert(it.key(), it.value());
            node.parameters.insert(QStringLiteral("language"),
                                   m_project.sourceLanguage.trimmed().isEmpty()
                                       ? QStringLiteral("zh") : m_project.sourceLanguage);
            node.parameters.insert(QStringLiteral("ocrSourceMedia"), m_project.sourceMediaPath);
            node.properties = node.parameters;
        } else if (node.id == QStringLiteral("translate")) {
            node.parameters.insert(QStringLiteral("sourceLanguage"), m_project.sourceLanguage);
            node.parameters.insert(QStringLiteral("targetLanguage"), m_project.targetLanguage);
            node.properties = node.parameters;
        } else if (node.typeId == QStringLiteral("core.review-gate")) {
            node.parameters.insert(QStringLiteral("mode"), QStringLiteral("never"));
            node.properties = node.parameters;
        } else if (node.id == QStringLiteral("synthesize")) {
            QVariantMap synthesisSettings = modelConfig.value(QStringLiteral("parameters")).toMap();
            synthesisSettings.insert(QStringLiteral("familyId"),
                                     modelConfig.value(QStringLiteral("familyId")));
            if (!synthesisSettings.contains(QStringLiteral("lang")))
                synthesisSettings.insert(QStringLiteral("lang"), m_project.targetLanguage);
            if (!applySelectedCloneVoiceToSynthesis(&synthesisSettings)) return false;
            node.parameters.insert(QStringLiteral("projectPath"), m_project.projectPath);
            node.parameters.insert(QStringLiteral("synthesisSettings"), synthesisSettings);
            node.properties = node.parameters;
        } else if (node.id == QStringLiteral("fit-timing")) {
            node.parameters.insert(QStringLiteral("projectPath"), m_project.projectPath);
            node.properties = node.parameters;
        } else if (node.id == QStringLiteral("mix")) {
            node.parameters.insert(QStringLiteral("projectPath"), m_project.projectPath);
            node.properties = node.parameters;
        } else if (node.id == QStringLiteral("export")) {
            node.parameters.insert(QStringLiteral("destination"), PathUtils::urlToLocalPath(outputPath));
            node.properties = node.parameters;
        }
    }
    m_workflowJournal = std::make_unique<WorkflowRunJournal>(
        QDir(QFileInfo(m_project.projectPath).absolutePath()).filePath(QStringLiteral(".workflow-artifacts")));
    m_workflowRunner->setJournal(m_workflowJournal.get());
    return m_workflowRunner->run(graph);
}

bool DubbingController::startAutomaticWorkflow(const QString &outputPath)
{
    if (m_dubbingEntryGateActive) {
        setError(QStringLiteral("Choose an entry mode before starting Dubbing."));
        return false;
    }
    if (processing()) return false;
    const QString destination = PathUtils::urlToLocalPath(outputPath).trimmed();
    if (destination.isEmpty()) {
        setError(QStringLiteral("Choose an output path before generating the final dub."));
        return false;
    }
    if (!workflowGraphValid() || m_project.sourceMediaPath.trimmed().isEmpty()) {
        setError(QStringLiteral("Import source media before generating the final dub."));
        return false;
    }
    const QString currentPreflight = automaticPreflightFingerprint();
    if (m_automaticPreflightFingerprint.isEmpty()
        || m_automaticPreflightFingerprint != currentPreflight) {
        m_automaticPreflightFingerprint.clear();
        setError(QStringLiteral(
            "Review Automatic preflight after changing media, route, model, variant, or Colab worker."));
        emit workflowChanged();
        return false;
    }
    // Approval is single-use. A retry returns to the review screen so the
    // operator always sees current worker health before another full run.
    m_automaticPreflightFingerprint.clear();
    if (m_project.dubbingQuality == QStringLiteral("custom")) {
        const QVariantMap issue = firstCustomSetupIssue();
        if (!issue.isEmpty()) {
            const QString message = issue.value(QStringLiteral("message")).toString();
            setError(message);
            emit workflowSetupRequired(
                issue.value(QStringLiteral("nodeId")).toString(),
                issue.value(QStringLiteral("setupKind")).toString(), message);
            emit workflowChanged();
            return false;
        }
    }
    if (m_project.dubbingQuality == QStringLiteral("custom")) {
        if (!cloneVoiceSelectionValid()) {
            setError(cloneVoiceSelectionError());
            return false;
        }
    } else if (m_project.ttsVoiceId.trimmed().isEmpty()) {
        const QVariantMap synthesis = m_workflowNodeConfigurations.value(
            QStringLiteral("synthesize")).toMap();
        const QVariantMap parameters = synthesis.value(QStringLiteral("parameters")).toMap();
        QString modelId = parameters.value(QStringLiteral("modelId")).toString().trimmed();
        if (modelId.isEmpty())
            modelId = automaticDefaultFamilyId(QStringLiteral("tts"), m_project.dubbingQuality);
        const QString builtInVoice = DubbingColabModelRoutes::defaultVoiceForTtsModel(modelId);
        if (builtInVoice.isEmpty()) {
            setError(QStringLiteral("The selected TTS model has no deterministic built-in voice. Select a voice in TTS settings."));
            return false;
        }
        // Automatic quality selects the documented default for its exact TTS
        // family once, then persists it as a normal TTS voice selection.
        // This is not a random fallback and never replaces a saved voice.
        m_project.ttsVoiceId = QStringLiteral("builtin:") + builtInVoice;
        m_project.cloneVoicePresetId = m_project.ttsVoiceId;
        emit cloneVoiceSelectionChanged();
        persistAfterEdit();
    } else if (!cloneVoiceSelectionValid()) {
        setError(cloneVoiceSelectionError());
        return false;
    }
    clearError();
    setWorkflowMode(QStringLiteral("automatic"));
    setCurrentStep(QStringLiteral("import"));
    m_automaticOutputPath = destination;
    m_automaticSetupActive = true;
    m_automaticEvents.clear();
    m_automaticDownloadsQueued.clear();
    m_automaticDownloadKeys.clear();
    m_automaticConfiguredNodes.clear();
    m_automaticSetupNodeId = QStringLiteral("source-separate");
    setAutomaticStatus(QStringLiteral("Checking required models and runtimes"));
    appendAutomaticEvent(QStringLiteral("Checking required models and runtimes"),
                         QStringLiteral("running"));
    emit processingChanged();
    emit workflowChanged();
    scheduleAutomaticSetupAdvance();
    return true;
}

void DubbingController::pauseAutomaticWorkflow()
{
    if (m_workflowMode != QStringLiteral("automatic") || !processing()) return;
    m_automaticSetupActive = false;
    m_automaticOutputPath.clear();
    m_automaticDownloadsQueued.clear();
    m_automaticDownloadKeys.clear();
    m_automaticConfiguredNodes.clear();
    m_automaticSetupNodeId.clear();
    if (m_translationFix) m_translationFix->cancel();
    if (m_workflowRunner && m_workflowRunner->running()) m_workflowRunner->cancel();
    if (m_runner) m_runner->cancel();
    setWorkflowMode(QStringLiteral("paused"));
    setAutomaticStatus(QStringLiteral("Paused. Settings are unlocked; Generate resumes the workflow."));
    appendAutomaticEvent(QStringLiteral("Automatic generation paused"),
                         QStringLiteral("paused"), currentStepId());
    emit processingChanged();
    emit workflowChanged();
}

void DubbingController::startStepByStep()
{
    Logger::info(QStringLiteral("DubbingController"),
                 QStringLiteral("Step-by-step requested current=%1 processing=%2 source=%3 master=%4 background=%5 segments=%6")
                     .arg(m_currentStepId)
                     .arg(processing() ? QStringLiteral("true") : QStringLiteral("false"))
                     .arg(m_project.sourceMediaPath, m_project.masterAudioPath,
                          m_project.backgroundAudioPath)
                     .arg(m_project.segments.size()));
    setWorkflowMode(QStringLiteral("step"));
    if (m_project.sourceMediaPath.isEmpty()) {
        // A new project is allowed into the step-by-step workspace, but it is
        // parked at its first valid action (Import).  It must not synthesize a
        // graph or run a stage before the operator supplies media.
        setCurrentStep(QStringLiteral("media-input"));
        clearError();
        return;
    }
    const QString transcriptSource = m_project.transcriptConfiguration.value(
        QStringLiteral("transcriptSource"), QStringLiteral("stt")).toString().trimmed().toLower();
    if (m_project.masterAudioPath.isEmpty()) setCurrentStep(QStringLiteral("ingest"));
    else if (m_project.backgroundAudioPath.isEmpty() && transcriptSource != QStringLiteral("ocr"))
        setCurrentStep(QStringLiteral("source-separate"));
    else if (m_project.segments.isEmpty()) setCurrentStep(QStringLiteral("transcribe"));
    else {
        bool allTranslated = true;
        bool allGenerated = true;
        for (const QVariant &entry : m_project.segments) {
            const QVariantMap segment = entry.toMap();
            allTranslated = allTranslated && !segment.value(QStringLiteral("targetText")).toString().trimmed().isEmpty();
            allGenerated = allGenerated && QFileInfo::exists(segment.value(QStringLiteral("clipPath")).toString());
        }
        if (!allTranslated) setCurrentStep(QStringLiteral("translate"));
        else if (!allGenerated) setCurrentStep(QStringLiteral("synthesize"));
        else if (previewPath().isEmpty() || !QFileInfo::exists(previewPath())) setCurrentStep(QStringLiteral("mix"));
        else if (exportPath().isEmpty() || !QFileInfo::exists(exportPath())) setCurrentStep(QStringLiteral("export"));
        else setCurrentStep(QStringLiteral("completed"));
    }
    Logger::info(QStringLiteral("DubbingController"),
                 QStringLiteral("Step-by-step resolved next step=%1").arg(m_currentStepId));
}

bool DubbingController::runCurrentStep(const QString &outputPath)
{
    if (m_dubbingEntryGateActive) {
        setError(QStringLiteral("Choose an entry mode before running a Dubbing stage."));
        return false;
    }
    if (m_workflowMode != QStringLiteral("step")) startStepByStep();
    if (processing()) return false;
    const QString step = m_currentStepId;
    Logger::info(QStringLiteral("DubbingController"),
                 QStringLiteral("Run current step step=%1 mode=%2 output=%3 project=%4")
                     .arg(step, m_workflowMode, outputPath, m_project.projectPath));
    if (step == QStringLiteral("ingest")) {
        m_runner->startIngest(m_project.sourceMediaPath);
        return m_runner->processing();
    }
    if (step == QStringLiteral("source-separate")) {
        m_runner->startSourceSeparation(
            m_project.masterAudioPath,
            m_workflowNodeConfigurations.value(QStringLiteral("source-separate")).toMap());
        return m_runner->processing() || !m_project.masterAudioPath.isEmpty();
    }
    if (step == QStringLiteral("transcribe")) {
        transcribeSource();
        return m_runner->processing();
    }
    if (step == QStringLiteral("translate")) {
        translateSource();
        return m_runner->processing();
    }
    if (step == QStringLiteral("synthesize")) {
        generateAudio();
        return m_runner->processing();
    }
    if (step == QStringLiteral("fit-timing")) {
        m_runner->fitTiming(m_project.segments, m_project.projectPath);
        return m_runner->processing();
    }
    if (step == QStringLiteral("mix")) return renderPreview();
    if (step == QStringLiteral("export")) return exportMedia(outputPath);
    return false;
}

bool DubbingController::rerunStep(const QString &stepId, const QString &outputPath)
{
    if (m_dubbingEntryGateActive) {
        setError(QStringLiteral("Choose an entry mode before running a Dubbing stage."));
        return false;
    }
    if (processing()) {
        Logger::warning(QStringLiteral("DubbingController"),
                        QStringLiteral("Run request rejected while busy requestedStep=%1 activeStep=%2 runnerStage=%3 progress=%4")
                            .arg(stepId, m_currentStepId, m_runner->stage())
                            .arg(m_runner->progress()));
        return false;
    }

    const QString step = stepId.trimmed();
    const bool supported = step == QStringLiteral("ingest")
        || step == QStringLiteral("source-separate")
        || step == QStringLiteral("transcribe")
        || step == QStringLiteral("translate")
        || step == QStringLiteral("synthesize")
        || step == QStringLiteral("fit-timing")
        || step == QStringLiteral("mix")
        || step == QStringLiteral("export");
    if (!supported) {
        Logger::warning(QStringLiteral("DubbingController"),
                        QStringLiteral("Ignoring rerun request for unsupported step=%1").arg(step));
        return false;
    }
    if (m_project.sourceMediaPath.isEmpty()) {
        setError(QStringLiteral("Import source media before running this step again."));
        return false;
    }

    clearError();
    m_automaticEvents.clear();
    setWorkflowMode(QStringLiteral("step"));
    setCurrentStep(step);
    setAutomaticStatus(QStringLiteral("Running manual node: %1").arg(visibleStepForNode(step)));
    appendAutomaticEvent(QStringLiteral("Running manual node: %1").arg(visibleStepForNode(step)),
                         QStringLiteral("running"), step);
    Logger::info(QStringLiteral("DubbingController"),
                 QStringLiteral("Rerun step step=%1 output=%2 project=%3")
                     .arg(step, outputPath, m_project.projectPath));
    return runCurrentStep(outputPath);
}

bool DubbingController::approveWorkflowReview(const QVariantMap &artifact)
{
    if (!workflowWaitingForInput()) return false;
    return m_workflowRunner->resume(QVariantMap{{QStringLiteral("action"), QStringLiteral("approve")},
                                                 {QStringLiteral("artifact"), artifact.isEmpty()
                                                     ? m_workflowReviewRequest.value(QStringLiteral("artifact")) : QVariant(artifact)}});
}

bool DubbingController::rejectWorkflowReview(const QString &reason)
{
    if (!workflowWaitingForInput()) return false;
    return m_workflowRunner->resume(QVariantMap{{QStringLiteral("action"), QStringLiteral("reject")},
                                                 {QStringLiteral("reason"), reason}});
}

bool DubbingController::resumeInterruptedWorkflow()
{
    const QString runId = m_workflowRecovery.value(QStringLiteral("runId")).toString();
    if (!m_workflowRunner || runId.isEmpty() || m_workflowRunner->running()) return false;
    if (!m_workflowRunner->resumeInterrupted(runId)) {
        setError(m_workflowRunner->error().isEmpty()
                     ? QStringLiteral("The interrupted workflow could not be resumed.")
                     : m_workflowRunner->error());
        return false;
    }
    // Some lightweight nodes finish synchronously. In that case the completed
    // handler has already looked for any older interrupted run, so do not wipe
    // out the next recovery prompt here.
    if (m_workflowRunner->running()) {
        m_workflowRecovery.clear();
        setWorkflowMode(QStringLiteral("automatic"));
    }
    emit workflowChanged();
    return true;
}

QVariantList DubbingController::colabModelOptionsForNode(const QString &nodeId) const
{
    return DubbingColabModelRoutes::optionsForNode(nodeId);
}

QString DubbingController::defaultColabModelForNode(const QString &nodeId) const
{
    return DubbingColabModelRoutes::defaultModelForNode(nodeId);
}

QString DubbingController::colabNotebookForNode(const QString &nodeId,
                                                const QString &modelId) const
{
    return DubbingColabModelRoutes::notebookForModel(nodeId, modelId);
}

bool DubbingController::selectWorkflowColabModel(const QString &nodeId,
                                                 const QString &modelId)
{
    const QString normalized = modelId.trimmed().toLower();
    if (!DubbingColabModelRoutes::supports(nodeId, normalized)) {
        setError(QStringLiteral("No exact Colab notebook is mapped for model '%1' on the %2 node.")
                     .arg(modelId, visibleStepForNode(nodeId)));
        return false;
    }

    AppController *app = AppController::instance();
    bool selected = false;
    if (nodeId == QStringLiteral("media-download"))
        selected = true;
    else if (nodeId == QStringLiteral("source-separate") && app && app->colabVoiceIsolator())
        selected = app->colabVoiceIsolator()->selectColabModel(normalized);
    else if (nodeId == QStringLiteral("transcribe") && app && app->sttSession())
        selected = app->sttSession()->selectColabModel(normalized);
    else if (nodeId == QStringLiteral("subtitle-ocr") && app && app->subtitleOcr())
        selected = app->subtitleOcr()->setColabModelId(normalized);
    else if (nodeId == QStringLiteral("translate") && app && app->translation())
        selected = app->translation()->selectColabModel(normalized);
    else if (nodeId == QStringLiteral("synthesize") && app && app->colabTts())
        selected = app->colabTts()->selectColabModel(normalized);
    else if (nodeId == QStringLiteral("alignment") && app && app->colabAlignment())
        selected = app->colabAlignment()->selectColabModel(normalized);
    else if (nodeId == QStringLiteral("adaptive-llm"))
        // This worker belongs to the Dubbing project, not to the standalone
        // LLM Chat surface.  Selecting it must not clear or mutate the chat
        // controller's temporary worker/session just because both use the
        // llm-chat capability.
        selected = true;

    if (!selected) {
        setError(QStringLiteral("The selected Colab model could not be activated for %1.")
                     .arg(visibleStepForNode(nodeId)));
        return false;
    }
    // A verification is bound to an exact model. Selecting a different model
    // invalidates only that stage's memory-only setup snapshot; it never
    // repurposes a verified worker or silently changes route.
    m_colabSetupSnapshots.remove(nodeId);
    emit colabSetupChanged();
    if (nodeId == QStringLiteral("alignment")) {
        return setWorkflowNodeParameters(
            QStringLiteral("transcribe"),
            {{QStringLiteral("alignmentModelId"), normalized}});
    }
    if (nodeId == QStringLiteral("subtitle-ocr")) {
        m_project.transcriptConfiguration.insert(QStringLiteral("ocrExecutionRoute"),
                                                 QStringLiteral("colab-gpu"));
        m_project.transcriptConfiguration.insert(QStringLiteral("ocrColabModelId"), normalized);
        persistAfterEdit();
        emit projectChanged();
        emit workflowChanged();
        return true;
    }
    if (nodeId == QStringLiteral("adaptive-llm")) {
        QVariantMap configuration = translationFixConfiguration();
        configuration.insert(QStringLiteral("provider"), QStringLiteral("colab-direct"));
        configuration.insert(QStringLiteral("configured"), true);
        configuration.insert(QStringLiteral("model"), normalized);
        configuration.remove(QStringLiteral("runtimeId"));
        configuration.remove(QStringLiteral("runtimeVersion"));
        configuration.remove(QStringLiteral("selectedFiles"));
        setAdaptiveConfiguration(configuration);
        return true;
    }
    return setWorkflowNodeParameters(
        nodeId,
        {{QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
         {QStringLiteral("modelId"), normalized}});
}

bool DubbingController::discardInterruptedWorkflow()
{
    const QString runId = m_workflowRecovery.value(QStringLiteral("runId")).toString();
    if (!m_workflowRunner || runId.isEmpty() || !m_workflowRunner->discardInterrupted(runId)) {
        setError(QStringLiteral("The interrupted workflow could not be discarded."));
        return false;
    }
    discoverInterruptedWorkflow();
    emit workflowChanged();
    return true;
}

void DubbingController::setSourceLanguage(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty() || normalized == m_project.sourceLanguage) return;
    m_project.sourceLanguage = normalized;
    emit projectChanged();
    emit workflowChanged();
    persistAfterEdit();
}

void DubbingController::setTargetLanguage(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty() || normalized == m_project.targetLanguage) return;
    m_project.targetLanguage = normalized;

    QVariantMap synthesis = m_workflowNodeConfigurations
                                .value(QStringLiteral("synthesize")).toMap();
    if (!synthesis.isEmpty()) {
        QVariantMap parameters = synthesis.value(QStringLiteral("parameters")).toMap();
        parameters.insert(QStringLiteral("lang"), normalized);
        synthesis.insert(QStringLiteral("parameters"), parameters);
        m_workflowNodeConfigurations.insert(QStringLiteral("synthesize"), synthesis);
    }

    emit projectChanged();
    emit workflowChanged();
    persistAfterEdit();
}

void DubbingController::setDurationControl(const QVariantMap &value)
{
    m_project.durationControl = value;
    emit projectChanged();
    emit workflowChanged();
    persistAfterEdit();
}

void DubbingController::setDubbingQuality(const QString &value)
{
    const QString requested = value.trimmed().toLower();
    const QString normalized =
        requested == QStringLiteral("adaptive") || requested == QStringLiteral("custom")
        ? requested : QStringLiteral("fast");
    if (normalized == m_project.dubbingQuality) return;
    m_project.dubbingQuality = normalized;
    if (normalized != QStringLiteral("custom")) {
        m_workflowNodeConfigurations.clear();
        m_project.workflowNodeConfigurations.clear();
        resetStandardTranslationFixConfiguration();
    } else if (m_translationFix
               && !m_project.customRewriteConfiguration.isEmpty()) {
        m_translationFix->setConfiguration(m_project.customRewriteConfiguration);
        if (m_runner)
            m_runner->setTranslationFixConfiguration(m_translationFix->configuration());
    }
    emit projectChanged();
    emit workflowChanged();
    persistAfterEdit();
}

bool DubbingController::ensureProject(const QString &path)
{
    if (!path.trimmed().isEmpty()) {
        m_project.projectPath = QFileInfo(PathUtils::urlToLocalPath(path)).absoluteFilePath();
    }
    if (m_project.projectPath.isEmpty()) {
        setError(QStringLiteral("Choose a project file before saving."));
        return false;
    }
    return true;
}

bool DubbingController::newProject(const QString &path)
{
    m_project = DubbingProject();
    m_project.subtitleConfiguration = {{QStringLiteral("source"), QStringLiteral("segments")},
                                       {QStringLiteral("textSource"), QStringLiteral("target")},
                                       {QStringLiteral("burnIn"), false},
                                       {QStringLiteral("style"), DubbingSubtitleService::defaultStyle()}};
    m_project.timingConfiguration = {{QStringLiteral("mode"), QStringLiteral("keep")},
                                     {QStringLiteral("minimumGapMs"), 80}};
    m_timingResolutionPreview.clear();
    m_timingUndoSegments.clear();
    m_workflowNodeConfigurations.clear();
    m_stepOutputs.clear();
    m_lastCompletedStepId.clear();
    setWorkflowMode(QStringLiteral("idle"));
    setCurrentStep(QStringLiteral("import"));
    if (m_workflowRunner) m_workflowRunner->setJournal(nullptr);
    m_workflowJournal.reset();
    m_workflowReviewStore.reset();
    m_activeReviewId.clear();
    m_workflowReviewRequest.clear();
    m_workflowRecovery.clear();
    if (!path.isEmpty()) {
        if (!ensureProject(path)) return false;
    } else {
        m_project.projectPath = PathUtils::dataDir() + QStringLiteral("/dubbing/untitled.ladub.json");
    }
    m_project.speakers.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("speaker-1")},
                                          {QStringLiteral("name"), QStringLiteral("Speaker 1")},
                                          {QStringLiteral("voice"), QVariantMap()} });
    emit projectChanged();
    emit segmentsChanged();
    refreshCloneVoicePresets();
    emit cloneVoiceSelectionChanged();
    emit workflowChanged();
    return saveProject();
}

bool DubbingController::openProject(const QString &path)
{
    DubbingProject loaded;
    QString error;
    if (!DubbingProject::load(PathUtils::urlToLocalPath(path), loaded, &error)) {
        setError(error);
        return false;
    }
    m_project = std::move(loaded);
    applyStoredSubtitleOcrConfiguration();
    if (m_project.subtitleConfiguration.isEmpty()) {
        m_project.subtitleConfiguration = {{QStringLiteral("source"), QStringLiteral("segments")},
                                           {QStringLiteral("textSource"), QStringLiteral("target")},
                                           {QStringLiteral("burnIn"), false},
                                           {QStringLiteral("style"), DubbingSubtitleService::defaultStyle()}};
    }
    if (m_project.timingConfiguration.isEmpty()) {
        m_project.timingConfiguration = {{QStringLiteral("mode"), QStringLiteral("keep")},
                                         {QStringLiteral("minimumGapMs"), 80}};
    }
    m_timingResolutionPreview.clear();
    m_timingUndoSegments.clear();
    m_workflowNodeConfigurations = m_project.workflowNodeConfigurations;
    if (m_translationFix && !m_project.customRewriteConfiguration.isEmpty()) {
        // The field predates Adaptive mode, but it is the versioned project
        // home for the non-secret rewrite-route selection in every mode.
        QVariantMap savedRewrite = m_project.customRewriteConfiguration;
        if (!savedRewrite.contains(QStringLiteral("apiKey"))) {
            savedRewrite.insert(QStringLiteral("apiKey"),
                                m_translationFix->configuration().value(QStringLiteral("apiKey")));
        }
        if (!savedRewrite.contains(QStringLiteral("serverUrl"))) {
            savedRewrite.insert(QStringLiteral("serverUrl"),
                                m_translationFix->configuration().value(QStringLiteral("serverUrl")));
        }
        m_translationFix->setConfiguration(savedRewrite);
        if (m_runner)
            m_runner->setTranslationFixConfiguration(m_translationFix->configuration());
    } else if (m_project.dubbingQuality != QStringLiteral("custom")) {
        resetStandardTranslationFixConfiguration();
    }
    if (m_project.ttsVoiceId.trimmed().isEmpty()) {
        const QVariantMap synthesis = m_workflowNodeConfigurations.value(
            QStringLiteral("synthesize")).toMap();
        const QString modelId = synthesis.value(QStringLiteral("parameters")).toMap()
            .value(QStringLiteral("modelId"), automaticDefaultFamilyId(
                QStringLiteral("tts"), m_project.dubbingQuality)).toString();
        const QString builtInVoice = DubbingColabModelRoutes::defaultVoiceForTtsModel(modelId);
        if (!builtInVoice.isEmpty()) {
            // Migration/default only: persist it on the next project save just
            // like a normal selection, without replacing an existing saved ID.
            m_project.ttsVoiceId = QStringLiteral("builtin:") + builtInVoice;
            m_project.cloneVoicePresetId = m_project.ttsVoiceId;
        }
    }
    m_stepOutputs.clear();
    m_lastCompletedStepId.clear();
    setWorkflowMode(QStringLiteral("idle"));
    setCurrentStep(m_project.sourceMediaPath.isEmpty() ? QStringLiteral("import") : QStringLiteral("ingest"));
    if (m_workflowRunner) m_workflowRunner->setJournal(nullptr);
    m_workflowJournal.reset();
    m_workflowReviewStore.reset();
    m_activeReviewId.clear();
    m_workflowReviewRequest.clear();
    m_workflowJournal = std::make_unique<WorkflowRunJournal>(
        QDir(QFileInfo(m_project.projectPath).absolutePath()).filePath(QStringLiteral(".workflow-artifacts")));
    m_workflowRunner->setJournal(m_workflowJournal.get());
    discoverInterruptedWorkflow();
    
    // Sync paths to runner
    m_runner->setPreviewPath(QFileInfo(m_project.projectPath).absolutePath() + QStringLiteral("/preview.wav"));
    m_runner->setExportPath(QString());
    
    emit projectChanged();
    emit segmentsChanged();
    refreshCloneVoicePresets();
    emit cloneVoiceSelectionChanged();
    emit workflowChanged();
    return true;
}

void DubbingController::discoverInterruptedWorkflow()
{
    m_workflowRecovery.clear();
    if (!m_workflowJournal) return;
    QString error;
    const QList<WorkflowInterruptedRun> runs = m_workflowJournal->interruptedRuns(&error);
    if (!error.isEmpty()) {
        Logger::warning(QStringLiteral("DubbingController"),
                        QStringLiteral("Cannot inspect interrupted workflow runs: %1").arg(error));
        return;
    }
    if (runs.isEmpty()) return;
    const WorkflowInterruptedRun &run = runs.constFirst();
    m_workflowRecovery = {{QStringLiteral("runId"), run.runId},
                          {QStringLiteral("workflowId"), run.workflowId},
                          {QStringLiteral("workflowVersion"), run.workflowVersion},
                          {QStringLiteral("activeNodeId"), run.activeNodeId},
                          {QStringLiteral("lastEvent"), run.lastEventType},
                          {QStringLiteral("lastUpdated"), run.lastUpdated}};
}

bool DubbingController::saveProject()
{
    if (!ensureProject(QString())) return false;
    // Route/model choices are a project contract for every Dubbing quality.
    // Clearing standard-mode selections here was the reason a reopened project
    // could revive an old Local setup instead of the confirmed Colab route.
    m_project.workflowNodeConfigurations = m_workflowNodeConfigurations;
    QVariantMap persistedRewrite = translationFixConfiguration();
    // API credentials belong only to the secure settings store. Direct Colab
    // URL/token never enter this map in the first place.
    persistedRewrite.remove(QStringLiteral("apiKey"));
    persistedRewrite.remove(QStringLiteral("serverUrl"));
    m_project.customRewriteConfiguration = persistedRewrite;
    QString error;
    if (!m_project.save(&error)) {
        setError(error);
        return false;
    }
    recordHistoryEntry();
    return true;
}

QString DubbingController::historyPath() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + QStringLiteral("/history");
    QDir().mkpath(base);
    return base + QStringLiteral("/dubbing_history.json");
}

void DubbingController::loadHistory()
{
    QFile file(historyPath());
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (document.isArray()) {
        m_history = document.array().toVariantList();
        emit historyChanged();
    }
}

void DubbingController::recordHistoryEntry()
{
    if (m_project.projectPath.isEmpty()) return;
    const QFileInfo projectInfo(m_project.projectPath);
    QVariantList updated;
    for (const QVariant &value : std::as_const(m_history)) {
        if (value.toMap().value(QStringLiteral("projectPath")).toString()
            != projectInfo.absoluteFilePath())
            updated.append(value);
    }
    updated.prepend(QVariantMap{
        {QStringLiteral("id"), projectInfo.absoluteFilePath()},
        {QStringLiteral("projectPath"), projectInfo.absoluteFilePath()},
        {QStringLiteral("projectName"), projectInfo.completeBaseName()},
        {QStringLiteral("sourceMediaPath"), m_project.sourceMediaPath},
        {QStringLiteral("sourceName"), QFileInfo(m_project.sourceMediaPath).fileName()},
        {QStringLiteral("sourceLanguage"), m_project.sourceLanguage},
        {QStringLiteral("targetLanguage"), m_project.targetLanguage},
        {QStringLiteral("segmentCount"), m_project.segments.size()},
        {QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))}
    });
    while (updated.size() > 30) updated.removeLast();
    QSaveFile file(historyPath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument::fromVariant(updated).toJson());
        if (file.commit()) {
            m_history = std::move(updated);
            emit historyChanged();
        }
    }
}

bool DubbingController::deleteHistoryItem(const QString &id)
{
    if (id.isEmpty()) return false;
    for (int i = 0; i < m_history.size(); ++i) {
        if (m_history.at(i).toMap().value(QStringLiteral("id")).toString() != id) continue;
        QVariantList updated = m_history;
        updated.removeAt(i);
        QSaveFile file(historyPath());
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.write(QJsonDocument::fromVariant(updated).toJson());
        if (!file.commit()) return false;
        m_history = std::move(updated);
        emit historyChanged();
        return true;
    }
    return false;
}

void DubbingController::clearHistory()
{
    m_history.clear();
    QFile::remove(historyPath());
    emit historyChanged();
}

void DubbingController::closeProject()
{
    m_project = DubbingProject();
    m_workflowNodeConfigurations.clear();
    m_stepOutputs.clear();
    m_lastCompletedStepId.clear();
    setWorkflowMode(QStringLiteral("idle"));
    setCurrentStep(QStringLiteral("import"));
    m_runner->cancel();
    if (m_workflowRunner) m_workflowRunner->setJournal(nullptr);
    m_workflowJournal.reset();
    emit projectChanged();
    emit segmentsChanged();
    refreshCloneVoicePresets();
    emit cloneVoiceSelectionChanged();
    emit workflowChanged();
}

bool DubbingController::importMedia(const QString &pathOrUrl)
{
    const QString input = pathOrUrl.trimmed();
    const QUrl suppliedUrl = QUrl::fromUserInput(input);
    if (suppliedUrl.isValid()
        && (suppliedUrl.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0
            || suppliedUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0)) {
        setError(QStringLiteral(
            "Public links must be downloaded by the dedicated Colab media worker. "
            "Or download the file yourself and add the local file."));
        return false;
    }
    const QString localPath = PathUtils::urlToLocalPath(input).trimmed();
    const QFileInfo fileInfo(localPath);
    const QString path = fileInfo.absoluteFilePath();
    Logger::info(QStringLiteral("DubbingController"),
                 QStringLiteral("Import media requested: input=\"%1\", local=\"%2\", exists=%3, isFile=%4")
                     .arg(input, localPath)
                     .arg(fileInfo.exists() ? QStringLiteral("true") : QStringLiteral("false"))
                     .arg(fileInfo.isFile() ? QStringLiteral("true") : QStringLiteral("false")));
    if (localPath.isEmpty() || !fileInfo.exists() || !fileInfo.isFile()) {
        Logger::error(QStringLiteral("DubbingController"),
                      QStringLiteral("Media import rejected: resolved path does not point to a file: \"%1\"").arg(path));
        setError(QStringLiteral("Media file does not exist: %1").arg(path));
        return false;
    }
    if (m_project.projectPath.isEmpty() && !newProject()) return false;

    // Import is intentionally side-effect free: preview the selected media and
    // reset downstream artifacts. Normalization and source separation only run
    // after the user chooses automatic or step-by-step processing.
    m_project.sourceMediaPath = path;
    m_project.sourceHash.clear();
    m_project.masterAudioPath.clear();
    m_project.analysisAudioPath.clear();
    m_project.backgroundAudioPath.clear();
    m_project.sourceDurationMs = 0;
    m_project.sourceSampleRate = 0;
    m_project.sourceChannels = 0;
    const QString suffix = fileInfo.suffix().toLower();
    m_project.sourceIsVideo = suffix == QStringLiteral("mp4") || suffix == QStringLiteral("mkv")
        || suffix == QStringLiteral("mov") || suffix == QStringLiteral("webm") || suffix == QStringLiteral("avi");
    m_project.segments.clear();
    m_stepOutputs.clear();
    m_lastCompletedStepId.clear();
    m_runner->setBackgroundAudioPath(QString());
    m_runner->setPreviewPath(QString());
    m_runner->setExportPath(QString());
    setWorkflowMode(QStringLiteral("idle"));
    setCurrentStep(QStringLiteral("ingest"));
    emit projectChanged();
    emit segmentsChanged();
    emit workflowChanged();
    persistAfterEdit();
    return true;
}

void DubbingController::beginDubbingEntry()
{
    // This is called whenever the Dubbing route is loaded.  Do not reset a
    // project here: reopening the tab must preserve its work and only require
    // the operator to choose/confirm how it will be operated.
    m_dubbingEntryGateActive = true;
    m_automaticPreflightFingerprint.clear();
    emit workflowChanged();
}

void DubbingController::reopenDubbingEntryGate()
{
    m_dubbingEntryGateActive = true;
    m_automaticPreflightFingerprint.clear();
    emit workflowChanged();
}

bool DubbingController::chooseDubbingEntryMode(const QString &mode)
{
    const QString selected = mode.trimmed().toLower();
    if (selected != QStringLiteral("automatic") && selected != QStringLiteral("step")) {
        setError(QStringLiteral("Choose Automatic or Step-by-step before using Dubbing."));
        return false;
    }

    // Persist only the operator's choice.  No workflow graph, media, segment,
    // subtitle, generated artifact, or node configuration is changed here.
    if (m_project.workflowEntryMode != selected) {
        m_project.workflowEntryMode = selected;
        persistAfterEdit();
        emit projectChanged();
    }
    m_dubbingEntryGateActive = false;
    m_automaticPreflightFingerprint.clear();
    clearError();
    emit workflowChanged();
    return true;
}

QVariantMap DubbingController::subtitleConfiguration() const
{
    QVariantMap configuration = m_project.subtitleConfiguration;
    QVariantMap style;
    QString ignored;
    if (!DubbingSubtitleService::normalizeStyle(configuration.value(QStringLiteral("style")).toMap(),
                                                style, &ignored)) {
        style = DubbingSubtitleService::defaultStyle();
    }
    configuration.insert(QStringLiteral("style"), style);
    if (!configuration.contains(QStringLiteral("source")))
        configuration.insert(QStringLiteral("source"), QStringLiteral("segments"));
    QString textSource = configuration.value(QStringLiteral("textSource"),
                                             QStringLiteral("target")).toString().trimmed().toLower();
    if (textSource != QStringLiteral("source") && textSource != QStringLiteral("target"))
        textSource = QStringLiteral("target");
    configuration.insert(QStringLiteral("textSource"), textSource);
    if (!configuration.contains(QStringLiteral("burnIn")))
        configuration.insert(QStringLiteral("burnIn"), false);
    return configuration;
}

QVariantMap DubbingController::timingConfiguration() const
{
    QVariantMap configuration = m_project.timingConfiguration;
    QString mode = configuration.value(QStringLiteral("mode"), QStringLiteral("keep"))
                       .toString().trimmed().toLower();
    if (mode != QStringLiteral("keep") && mode != QStringLiteral("ripple")
        && mode != QStringLiteral("manual")) {
        mode = QStringLiteral("keep");
    }
    configuration.insert(QStringLiteral("mode"), mode);
    configuration.insert(QStringLiteral("minimumGapMs"),
                         qBound(0, configuration.value(QStringLiteral("minimumGapMs"), 80).toInt(),
                                5000));
    return configuration;
}

QVariantList DubbingController::timingConflicts() const
{
    const QVariantMap configuration = timingConfiguration();
    return DubbingTimingService::analyzeSpeechOverlaps(
               m_project.segments, configuration.value(QStringLiteral("minimumGapMs")).toLongLong())
        .value(QStringLiteral("conflicts")).toList();
}

QVariantMap DubbingController::previewTimingResolution(const QString &mode, int minimumGapMs)
{
    if (processing()) {
        setBusyError(QStringLiteral("Wait for the current Dubbing operation before reviewing speech timing."));
        return {};
    }
    const QString normalizedMode = mode.trimmed().toLower();
    if (normalizedMode != QStringLiteral("keep") && normalizedMode != QStringLiteral("ripple")
        && normalizedMode != QStringLiteral("manual")) {
        setError(QStringLiteral("Choose Keep timing, Ripple forward, or Manual timing."));
        return {};
    }
    if (!hasProject()) {
        setError(QStringLiteral("Open a dubbing project before resolving speech timing."));
        return {};
    }

    const qint64 gapMs = qBound(0, minimumGapMs, 5000);
    QVariantMap report;
    if (normalizedMode == QStringLiteral("ripple")) {
        QString error;
        const QVariantList revised = DubbingTimingService::rippleForward(
            m_project.segments, gapMs, &report, &error);
        if (revised.isEmpty() && !m_project.segments.isEmpty()) {
            setError(error);
            return {};
        }
    } else {
        report = DubbingTimingService::analyzeSpeechOverlaps(m_project.segments, gapMs);
        report.insert(QStringLiteral("mode"), normalizedMode);
        report.insert(QStringLiteral("manualReviewRequired"), normalizedMode == QStringLiteral("manual"));
    }
    m_timingResolutionPreview = report;
    clearError();
    emit timingResolutionChanged();
    return report;
}

bool DubbingController::applyTimingResolution(const QString &mode, int minimumGapMs)
{
    if (processing()) {
        setBusyError(QStringLiteral("Wait for the current Dubbing operation before changing speech timing."));
        return false;
    }
    const QString normalizedMode = mode.trimmed().toLower();
    if (normalizedMode != QStringLiteral("keep") && normalizedMode != QStringLiteral("ripple")
        && normalizedMode != QStringLiteral("manual")) {
        setError(QStringLiteral("Choose Keep timing, Ripple forward, or Manual timing."));
        return false;
    }
    const QVariantMap preview = previewTimingResolution(normalizedMode, minimumGapMs);
    if (preview.isEmpty() && !m_project.segments.isEmpty()) return false;

    QVariantMap configuration = timingConfiguration();
    configuration.insert(QStringLiteral("mode"), normalizedMode);
    configuration.insert(QStringLiteral("minimumGapMs"), qBound(0, minimumGapMs, 5000));

    if (normalizedMode == QStringLiteral("ripple")) {
        QString error;
        QVariantMap report;
        const QVariantList revised = DubbingTimingService::rippleForward(
            m_project.segments, configuration.value(QStringLiteral("minimumGapMs")).toLongLong(),
            &report, &error);
        if (revised.isEmpty() && !m_project.segments.isEmpty()) {
            setError(error);
            return false;
        }
        if (report.value(QStringLiteral("blockingConflictCount")).toInt() != 0) {
            setError(QStringLiteral("Ripple preview still contains blocking speech overlaps."));
            return false;
        }
        m_timingUndoSegments = m_project.segments;
        m_project.segments = revised;
        m_timingResolutionPreview = report;
        invalidateTimingOutputs();
        emit segmentsChanged();
        emit workflowChanged();
    } else {
        // Keep and manual modes deliberately leave all timestamps unchanged.
        // Manual mode is a durable explicit-review decision, not an automatic repair.
        m_timingUndoSegments.clear();
    }

    m_project.timingConfiguration = configuration;
    clearError();
    emit projectChanged();
    emit timingResolutionChanged();
    persistAfterEdit();
    return true;
}

bool DubbingController::undoTimingResolution()
{
    if (processing()) {
        setBusyError(QStringLiteral("Wait for the current Dubbing operation before undoing speech timing."));
        return false;
    }
    if (m_timingUndoSegments.isEmpty()) {
        setError(QStringLiteral("No ripple timing change is available to undo."));
        return false;
    }
    m_project.segments = m_timingUndoSegments;
    m_timingUndoSegments.clear();
    m_timingResolutionPreview = DubbingTimingService::analyzeSpeechOverlaps(
        m_project.segments,
        timingConfiguration().value(QStringLiteral("minimumGapMs")).toLongLong());
    invalidateTimingOutputs();
    clearError();
    emit segmentsChanged();
    emit projectChanged();
    emit workflowChanged();
    emit timingResolutionChanged();
    persistAfterEdit();
    return true;
}

bool DubbingController::setIntentionalTimingOverlap(int segmentIndex, bool enabled)
{
    if (segmentIndex < 0 || segmentIndex >= m_project.segments.size()) {
        setError(QStringLiteral("Choose a valid speech segment before changing overlap intent."));
        return false;
    }
    QVariantMap segment = m_project.segments.at(segmentIndex).toMap();
    segment.insert(QStringLiteral("intentionalOverlap"), enabled);
    m_project.segments[segmentIndex] = segment;
    m_timingResolutionPreview = DubbingTimingService::analyzeSpeechOverlaps(
        m_project.segments,
        timingConfiguration().value(QStringLiteral("minimumGapMs")).toLongLong());
    clearError();
    emit segmentsChanged();
    emit timingResolutionChanged();
    persistAfterEdit();
    return true;
}

int DubbingController::mediaQueueIndex(const QString &itemId) const
{
    for (int index = 0; index < m_mediaQueueItems.size(); ++index) {
        if (m_mediaQueueItems.at(index).toMap().value(QStringLiteral("id")).toString() == itemId)
            return index;
    }
    return -1;
}

void DubbingController::replaceMediaQueueItem(int index, const QVariantMap &item)
{
    if (index < 0 || index >= m_mediaQueueItems.size()) return;
    m_mediaQueueItems[index] = item;
    emit mediaQueueChanged();
}

bool DubbingController::mediaQueueDownloading() const
{
    if ((m_colabMediaDownload && m_colabMediaDownload->active())
        || !m_activeMediaQueueDownloadId.isEmpty()) {
        return true;
    }
    // Keep the queue busy across the event-loop handoff between two links.
    // Otherwise a user could start a processing batch in the tiny gap after
    // item N completes but before item N+1 has been started.
    for (const QVariant &value : m_mediaQueueItems) {
        const QString state = value.toMap().value(QStringLiteral("downloadState")).toString();
        if (state == QStringLiteral("queued") || state == QStringLiteral("downloading")) return true;
    }
    return false;
}

int DubbingController::mediaQueueProgress() const
{
    int queuedJobs = 0;
    int progressTotal = 0;
    for (const QVariant &value : m_mediaQueueItems) {
        const QVariantMap item = value.toMap();
        const QString state = item.value(QStringLiteral("processState")).toString();
        if (state != QStringLiteral("queued") && state != QStringLiteral("running")
            && state != QStringLiteral("completed") && state != QStringLiteral("failed")
            && state != QStringLiteral("cancelled")) {
            continue;
        }
        ++queuedJobs;
        if (state == QStringLiteral("completed") || state == QStringLiteral("failed")
            || state == QStringLiteral("cancelled")) {
            progressTotal += 100;
        } else {
            progressTotal += qBound(0, item.value(QStringLiteral("progress")).toInt(), 99);
        }
    }
    return queuedJobs > 0 ? qRound(static_cast<qreal>(progressTotal) / queuedJobs) : 0;
}

QVariantMap DubbingController::normalizedMediaQueueTasks(const QVariantMap &tasks, QString *error) const
{
    QVariantMap normalized;
    const QString operation = tasks.value(QStringLiteral("operation")).toString().trimmed().toLower();
    if (!operation.isEmpty()) {
        static const QSet<QString> supportedOperations{
            QStringLiteral("import"), QStringLiteral("isolate"),
            QStringLiteral("transcribe"), QStringLiteral("translate"),
            QStringLiteral("voice"), QStringLiteral("export")};
        if (!supportedOperations.contains(operation)) {
            if (error) *error = QStringLiteral("Choose a valid downloaded-media action.");
            return {};
        }
        normalized.insert(QStringLiteral("operation"), operation);
        // A selected action is intentionally one production action across the
        // selected files.  It must not silently enqueue later actions.
        normalized.insert(QStringLiteral("executionMode"), QStringLiteral("per-media"));
        normalized.insert(QStringLiteral("audioFormat"), QStringLiteral("wav"));
        return normalized;
    }
    const bool isolate = tasks.value(QStringLiteral("isolate")).toBool();
    const bool voice = tasks.value(QStringLiteral("voice")).toBool();
    const bool translate = tasks.value(QStringLiteral("translate")).toBool() || voice;
    const bool transcribe = tasks.value(QStringLiteral("transcribe")).toBool() || translate;
    if (!isolate && !transcribe && !translate && !voice) {
        if (error) *error = QStringLiteral("Choose at least one batch task: isolate, STT, translate, or voice.");
        return {};
    }
    normalized.insert(QStringLiteral("isolate"), isolate);
    normalized.insert(QStringLiteral("transcribe"), transcribe);
    normalized.insert(QStringLiteral("translate"), translate);
    normalized.insert(QStringLiteral("voice"), voice);
    const QString executionMode = tasks.value(QStringLiteral("executionMode"),
                                               QStringLiteral("per-media")).toString();
    if (executionMode != QStringLiteral("per-media")
        && executionMode != QStringLiteral("stage-by-stage")) {
        if (error) *error = QStringLiteral("Choose a valid batch execution order.");
        return {};
    }
    normalized.insert(QStringLiteral("executionMode"), executionMode);
    normalized.insert(QStringLiteral("audioFormat"), QStringLiteral("wav"));
    return normalized;
}

QStringList DubbingController::mediaQueueStagePlan() const
{
    const QString operation = m_mediaQueueTasks.value(QStringLiteral("operation")).toString();
    if (operation == QStringLiteral("import")) return {QStringLiteral("ingest")};
    if (operation == QStringLiteral("isolate")) return {QStringLiteral("source-separate")};
    if (operation == QStringLiteral("transcribe")) return {QStringLiteral("transcribe")};
    if (operation == QStringLiteral("translate")) return {QStringLiteral("translate")};
    if (operation == QStringLiteral("voice"))
        return {QStringLiteral("synthesize"), QStringLiteral("mix")};
    if (operation == QStringLiteral("export")) return {QStringLiteral("export")};

    QStringList stages{QStringLiteral("ingest")};
    if (m_mediaQueueTasks.value(QStringLiteral("isolate")).toBool())
        stages.append(QStringLiteral("source-separate"));
    if (m_mediaQueueTasks.value(QStringLiteral("transcribe")).toBool())
        stages.append(QStringLiteral("transcribe"));
    if (m_mediaQueueTasks.value(QStringLiteral("translate")).toBool())
        stages.append(QStringLiteral("translate"));
    if (m_mediaQueueTasks.value(QStringLiteral("voice")).toBool()) {
        stages.append(QStringLiteral("synthesize"));
        stages.append(QStringLiteral("mix"));
    }
    return stages;
}

bool DubbingController::mediaQueueOperationRequiresSavedProject() const
{
    const QString operation = m_mediaQueueTasks.value(QStringLiteral("operation")).toString();
    return !operation.isEmpty() && operation != QStringLiteral("import");
}

bool DubbingController::loadMediaQueueProject(const QVariantMap &item, DubbingProject *project,
                                               QString *error) const
{
    if (!project) return false;
    const QString operation = m_mediaQueueTasks.value(QStringLiteral("operation")).toString();
    const QVariantMap outputs = item.value(QStringLiteral("outputs")).toMap();
    const QString savedProjectPath = outputs.value(QStringLiteral("project")).toString();
    if (savedProjectPath.isEmpty() || !QFileInfo(savedProjectPath).isFile()) {
        if (error) {
            *error = QStringLiteral("Run Import/Normalize for this media before %1. Its saved project is unavailable.")
                         .arg(visibleStepForNode(operation));
        }
        return false;
    }
    if (!DubbingProject::load(savedProjectPath, *project, error)) return false;
    if (project->sourceMediaPath.isEmpty() || !QFileInfo(project->sourceMediaPath).isFile()) {
        if (error) {
            *error = QStringLiteral("The saved project for this media no longer has a usable source file. Run Import/Normalize again.");
        }
        return false;
    }
    // Keep a stable working project outside the public output folder.  The
    // final save is copied atomically back to that folder after this action.
    project->projectPath = newMediaQueueProject(item).projectPath;
    project->workflowNodeConfigurations = m_mediaQueueOriginalNodeConfigurations;
    return true;
}

int DubbingController::enqueueMediaLinks(const QString &urls)
{
    QString routeError;
    if (!m_colabMediaDownload || !m_mediaDownloadSession
        || !m_mediaDownloadSession->hasVerifiedRoute(
            QStringLiteral("media-download"), QStringLiteral("yt-dlp-media-download"), &routeError)) {
        setError(routeError.isEmpty()
            ? QStringLiteral("Connect and check the dedicated Colab media downloader before adding links.")
            : routeError);
        return 0;
    }
    int added = 0;
    QStringList rejected;
    const QStringList candidates = extractedSharedMediaUrls(urls);
    for (const QString &candidate : candidates) {
        const QString source = candidate.trimmed();
        if (source.isEmpty()) continue;
        const QUrl url = QUrl::fromUserInput(source);
        const QString scheme = url.scheme().toLower();
        if (!url.isValid() || scheme != QStringLiteral("https") || url.host().isEmpty()
            || !url.userInfo().isEmpty()) {
            rejected.append(source.left(96));
            continue;
        }
        QVariantMap item;
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        item.insert(QStringLiteral("id"), id);
        // sourceUrl is short-lived memory only. It is erased the moment the
        // downloader resolves the staged file and is never saved into a
        // project, settings, history record, or output manifest.
        item.insert(QStringLiteral("sourceUrl"), url.toString());
        item.insert(QStringLiteral("displayName"), url.host().isEmpty()
                    ? QStringLiteral("Queued media") : url.host());
        item.insert(QStringLiteral("localPath"), QString());
        item.insert(QStringLiteral("sourceMode"), QStringLiteral("colab-download"));
        item.insert(QStringLiteral("downloadState"), QStringLiteral("queued"));
        item.insert(QStringLiteral("processState"), QStringLiteral("not-ready"));
        item.insert(QStringLiteral("status"), QStringLiteral("Waiting for dedicated Colab download"));
        item.insert(QStringLiteral("selected"), false);
        item.insert(QStringLiteral("progress"), 0);
        item.insert(QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        m_mediaQueueItems.append(item);
        ++added;
    }
    if (added > 0) {
        m_mediaQueueStatus = QStringLiteral("%1 link(s) queued for serial Colab download").arg(added);
        emit mediaQueueChanged();
        startNextQueuedMediaDownload();
    }
    if (!rejected.isEmpty()) {
        setError(QStringLiteral("Only valid public HTTPS links can be queued. Rejected %1 line(s).").arg(rejected.size()));
    } else if (added > 0) {
        clearError();
    }
    return added;
}

int DubbingController::enqueueMediaFiles(const QVariantList &paths)
{
    int added = 0;
    for (const QVariant &value : paths) {
        const QString localPath = QFileInfo(PathUtils::urlToLocalPath(value.toString())).absoluteFilePath();
        if (!QFileInfo(localPath).isFile()) continue;
        QVariantMap item;
        item.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        item.insert(QStringLiteral("displayName"), QFileInfo(localPath).fileName());
        item.insert(QStringLiteral("localPath"), localPath);
        item.insert(QStringLiteral("sourceMode"), QStringLiteral("manual-upload"));
        item.insert(QStringLiteral("downloadState"), QStringLiteral("downloaded"));
        item.insert(QStringLiteral("processState"), QStringLiteral("ready"));
        item.insert(QStringLiteral("status"), QStringLiteral("Manual file ready for selected batch tasks"));
        item.insert(QStringLiteral("selected"), true);
        item.insert(QStringLiteral("progress"), 0);
        item.insert(QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        m_mediaQueueItems.append(item);
        ++added;
    }
    if (added > 0) {
        m_mediaQueueStatus = QStringLiteral("%1 local media file(s) ready for batch processing").arg(added);
        emit mediaQueueChanged();
        clearError();
    }
    return added;
}

bool DubbingController::setMediaQueueItemSelected(const QString &itemId, bool selected)
{
    const int index = mediaQueueIndex(itemId);
    if (index < 0) return false;
    QVariantMap item = m_mediaQueueItems.at(index).toMap();
    if (item.value(QStringLiteral("downloadState")).toString() != QStringLiteral("downloaded")) return false;
    if (m_mediaQueueProcessing && item.value(QStringLiteral("processState")).toString() == QStringLiteral("running")) return false;
    item.insert(QStringLiteral("selected"), selected);
    replaceMediaQueueItem(index, item);
    return true;
}

bool DubbingController::retryMediaQueueItem(const QString &itemId)
{
    if (mediaQueueDownloading() || mediaQueueProcessing() || processing()) {
        setBusyError(QStringLiteral("Wait for the current download or batch operation to finish before retrying."));
        return false;
    }
    const int index = mediaQueueIndex(itemId);
    if (index < 0) return false;
    QVariantMap item = m_mediaQueueItems.at(index).toMap();
    const QString state = item.value(QStringLiteral("downloadState")).toString();
    if (state != QStringLiteral("failed")) {
        setError(QStringLiteral("Only a failed download that still has its source link can be retried."));
        return false;
    }
    if (item.value(QStringLiteral("sourceUrl")).toString().trimmed().isEmpty()) {
        setError(QStringLiteral("The original link is no longer available. Add the link again to retry it."));
        return false;
    }
    item.insert(QStringLiteral("downloadState"), QStringLiteral("queued"));
    item.insert(QStringLiteral("processState"), QStringLiteral("not-ready"));
    item.insert(QStringLiteral("status"), QStringLiteral("Waiting to retry in the dedicated Colab downloader"));
    item.insert(QStringLiteral("selected"), false);
    item.insert(QStringLiteral("progress"), 0);
    replaceMediaQueueItem(index, item);
    m_mediaQueueStatus = QStringLiteral("Retrying queued media in Colab");
    clearError();
    startNextQueuedMediaDownload();
    return true;
}

bool DubbingController::removeMediaQueueItem(const QString &itemId)
{
    const int index = mediaQueueIndex(itemId);
    if (index < 0) return false;
    const QVariantMap item = m_mediaQueueItems.at(index).toMap();
    if (item.value(QStringLiteral("id")).toString() == m_activeMediaQueueDownloadId
        || item.value(QStringLiteral("id")).toString() == m_activeMediaQueueItemId) {
        setError(QStringLiteral("Cancel the active batch operation before removing that item."));
        return false;
    }
    m_mediaQueueItems.removeAt(index);
    emit mediaQueueChanged();
    return true;
}

void DubbingController::clearCompletedMediaQueue()
{
    if (mediaQueueDownloading() || m_mediaQueueProcessing) {
        setBusyError(QStringLiteral("Cancel or wait for the active batch operation before clearing completed items."));
        return;
    }
    QVariantList retained;
    for (const QVariant &value : std::as_const(m_mediaQueueItems)) {
        const QString state = value.toMap().value(QStringLiteral("processState")).toString();
        const QString downloadState = value.toMap().value(QStringLiteral("downloadState")).toString();
        if (state != QStringLiteral("completed") && state != QStringLiteral("failed")
            && state != QStringLiteral("cancelled")
            ) {
            retained.append(value);
        }
    }
    m_mediaQueueItems = retained;
    m_mediaQueueStatus = m_mediaQueueItems.isEmpty()
        ? QStringLiteral("Queue cleared") : QStringLiteral("Completed batch items cleared");
    emit mediaQueueChanged();
}

void DubbingController::startNextQueuedMediaDownload()
{
    if (m_mediaQueueCancelling) return;
    if (!m_colabMediaDownload || m_colabMediaDownload->active()
        || !m_activeMediaQueueDownloadId.isEmpty()) return;
    for (int index = 0; index < m_mediaQueueItems.size(); ++index) {
        QVariantMap item = m_mediaQueueItems.at(index).toMap();
        if (item.value(QStringLiteral("downloadState")).toString() != QStringLiteral("queued")) continue;
        const QUrl url = QUrl::fromUserInput(item.value(QStringLiteral("sourceUrl")).toString());
        if (!url.isValid() || url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0
            || url.host().isEmpty() || !url.userInfo().isEmpty()) {
            item.insert(QStringLiteral("downloadState"), QStringLiteral("failed"));
            item.insert(QStringLiteral("processState"), QStringLiteral("not-ready"));
            item.insert(QStringLiteral("status"), QStringLiteral("Invalid queued media URL"));
            replaceMediaQueueItem(index, item);
            continue;
        }
        m_activeMediaQueueDownloadId = item.value(QStringLiteral("id")).toString();
        item.insert(QStringLiteral("downloadState"), QStringLiteral("downloading"));
        item.insert(QStringLiteral("status"), QStringLiteral("Submitting to dedicated Colab downloader"));
        replaceMediaQueueItem(index, item);
        m_mediaQueueStatus = QStringLiteral("Downloading queued media %1 in Colab").arg(index + 1);
        emit mediaQueueChanged();
        if (!m_colabMediaDownload->download(url)) {
            onBatchMediaDownloadFinished(false, QString(),
                QStringLiteral("The queued media download could not be started."));
        }
        return;
    }
    if (!m_mediaQueueProcessing) {
        int downloaded = 0;
        int failed = 0;
        for (const QVariant &value : std::as_const(m_mediaQueueItems)) {
            const QString state = value.toMap().value(QStringLiteral("downloadState")).toString();
            if (state == QStringLiteral("downloaded")) ++downloaded;
            else if (state == QStringLiteral("failed")) ++failed;
        }
        m_mediaQueueStatus = failed > 0
            ? QStringLiteral("Download queue finished: %1 downloaded, %2 failed. Only downloaded items can be selected.")
                  .arg(downloaded).arg(failed)
            : QStringLiteral("All queued links have finished downloading");
        emit mediaQueueChanged();
    }
}

void DubbingController::onBatchMediaDownloadFinished(bool success, const QString &localPath,
                                                      const QString &error)
{
    const int index = mediaQueueIndex(m_activeMediaQueueDownloadId);
    const bool cancelled = m_mediaQueueCancelling;
    m_activeMediaQueueDownloadId.clear();
    if (index >= 0) {
        QVariantMap item = m_mediaQueueItems.at(index).toMap();
        item.insert(QStringLiteral("receivedBytes"), 0);
        item.insert(QStringLiteral("totalBytes"), -1);
        if (cancelled) {
            item.remove(QStringLiteral("sourceUrl"));
            item.insert(QStringLiteral("downloadState"), QStringLiteral("cancelled"));
            item.insert(QStringLiteral("processState"), QStringLiteral("cancelled"));
            item.insert(QStringLiteral("status"), QStringLiteral("Download cancelled"));
            item.insert(QStringLiteral("selected"), false);
        } else if (success && QFileInfo(localPath).isFile()) {
            item.remove(QStringLiteral("sourceUrl"));
            item.insert(QStringLiteral("displayName"), QFileInfo(localPath).fileName());
            item.insert(QStringLiteral("localPath"), QFileInfo(localPath).absoluteFilePath());
            item.insert(QStringLiteral("downloadState"), QStringLiteral("downloaded"));
            item.insert(QStringLiteral("processState"), QStringLiteral("ready"));
            item.insert(QStringLiteral("status"), QStringLiteral("Downloaded — select for batch processing"));
            item.insert(QStringLiteral("selected"), true);
        } else {
            const QString safeError = error.trimmed().isEmpty()
                ? QStringLiteral("Media download failed") : error.trimmed();
            item.insert(QStringLiteral("downloadState"), QStringLiteral("failed"));
            item.insert(QStringLiteral("processState"), QStringLiteral("not-ready"));
            item.insert(QStringLiteral("status"), safeError);
            item.insert(QStringLiteral("selected"), false);
        }
        replaceMediaQueueItem(index, item);
    }
    m_mediaQueueStatus = cancelled ? QStringLiteral("Download queue cancelled") : (success
        ? QStringLiteral("Downloaded queued media")
        : QStringLiteral("Queued media download failed"));
    emit mediaQueueChanged();
    if (cancelled) {
        m_mediaQueueCancelling = false;
        emit mediaQueueChanged();
        return;
    }
    QTimer::singleShot(0, this, &DubbingController::startNextQueuedMediaDownload);
}

DubbingProject DubbingController::newMediaQueueProject(const QVariantMap &item) const
{
    DubbingProject project;
    const QString root = QDir(PathUtils::dataDir()).filePath(QStringLiteral("dubbing/batch-projects"));
    const QString base = QFileInfo(item.value(QStringLiteral("localPath")).toString()).completeBaseName()
        .replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("-"));
    const QString safeBase = base.isEmpty() ? QStringLiteral("media") : base.left(80);
    project.projectPath = QDir(root).filePath(
        QStringLiteral("%1-%2.ladub.json").arg(safeBase, item.value(QStringLiteral("id")).toString().left(12)));
    project.sourceLanguage = m_mediaQueueOriginalProject.sourceLanguage;
    project.targetLanguage = m_mediaQueueOriginalProject.targetLanguage;
    project.dubbingQuality = m_mediaQueueOriginalProject.dubbingQuality;
    project.ttsVoiceId = m_mediaQueueOriginalProject.ttsVoiceId;
    project.cloneVoicePresetId = project.ttsVoiceId;
    project.durationControl = m_mediaQueueOriginalProject.durationControl;
    project.workflowNodeConfigurations = m_mediaQueueOriginalNodeConfigurations;
    project.transcriptConfiguration = m_mediaQueueOriginalProject.transcriptConfiguration;
    project.subtitleConfiguration = m_mediaQueueOriginalProject.subtitleConfiguration;
    project.timingConfiguration = m_mediaQueueOriginalProject.timingConfiguration;
    project.customRewriteConfiguration = m_mediaQueueOriginalProject.customRewriteConfiguration;
    project.speakers = m_mediaQueueOriginalProject.speakers;
    project.workflowEntryMode = QStringLiteral("step");
    return project;
}

QString DubbingController::mediaQueueOutputDirectory(const QString &itemId) const
{
    return QDir(PathUtils::dataDir()).filePath(QStringLiteral("dubbing/batch-output/%1").arg(itemId));
}

void DubbingController::recordMediaQueueOutput(const QString &key, const QString &path)
{
    const int index = mediaQueueIndex(m_activeMediaQueueItemId);
    if (index < 0) return;
    QVariantMap item = m_mediaQueueItems.at(index).toMap();
    QVariantMap outputs = item.value(QStringLiteral("outputs")).toMap();
    outputs.insert(key, path);
    item.insert(QStringLiteral("outputs"), outputs);
    item.insert(QStringLiteral("outputDirectory"), mediaQueueOutputDirectory(m_activeMediaQueueItemId));
    replaceMediaQueueItem(index, item);
}

bool DubbingController::writeMediaQueueSubtitles(const QString &key, bool useTargetText)
{
    const QString fileName = useTargetText ? QStringLiteral("translated.srt") : QStringLiteral("source.srt");
    const QString outputPath = QDir(mediaQueueOutputDirectory(m_activeMediaQueueItemId)).filePath(fileName);
    QString error;
    if (!writeDubbingSubtitles(m_project.segments, outputPath, useTargetText, &error)) {
        completeCurrentMediaQueueItem(false, error.isEmpty()
            ? QStringLiteral("Could not write the batch subtitle output.") : error);
        return false;
    }
    recordMediaQueueOutput(key, outputPath);
    return true;
}

bool DubbingController::startMediaQueue(const QVariantMap &tasks)
{
    if (mediaQueueDownloading() || processing()) {
        setBusyError(QStringLiteral("Wait for active import, download, or Dubbing work before starting the media batch."));
        return false;
    }
    QString error;
    const QVariantMap normalizedTasks = normalizedMediaQueueTasks(tasks, &error);
    if (normalizedTasks.isEmpty()) {
        setError(error);
        return false;
    }
    const bool reuseSavedProject = normalizedTasks.contains(QStringLiteral("operation"))
        && normalizedTasks.value(QStringLiteral("operation")).toString() != QStringLiteral("import");
    int selectedReady = 0;
    for (int index = 0; index < m_mediaQueueItems.size(); ++index) {
        QVariantMap item = m_mediaQueueItems.at(index).toMap();
        if (!item.value(QStringLiteral("selected")).toBool()
            || item.value(QStringLiteral("downloadState")).toString() != QStringLiteral("downloaded")
            || !QFileInfo(item.value(QStringLiteral("localPath")).toString()).isFile()) {
            continue;
        }
        item.insert(QStringLiteral("processState"), QStringLiteral("queued"));
        item.insert(QStringLiteral("status"), normalizedTasks.value(QStringLiteral("executionMode")).toString()
                    == QStringLiteral("stage-by-stage")
                    ? QStringLiteral("Waiting for stage-by-stage processing")
                    : QStringLiteral("Waiting for end-to-end processing"));
        item.insert(QStringLiteral("progress"), 0);
        item.insert(QStringLiteral("completedStages"), QVariantList{});
        item.insert(QStringLiteral("nextStageIndex"), 0);
        item.insert(QStringLiteral("executionMode"), normalizedTasks.value(QStringLiteral("executionMode")));
        // A later Translate/TTS/Export selection must keep the exact artifacts
        // and project written by an earlier selected action.  Clearing them
        // here would force the user to repeat work or choose every task up
        // front, which is precisely what the media library avoids.
        if (!reuseSavedProject) item.insert(QStringLiteral("outputs"), QVariantMap{});
        item.remove(QStringLiteral("error"));
        replaceMediaQueueItem(index, item);
        ++selectedReady;
    }
    if (selectedReady == 0) {
        setError(QStringLiteral("Select at least one successfully downloaded media item for the batch."));
        return false;
    }
    m_mediaQueueOriginalProject = m_project;
    m_mediaQueueOriginalNodeConfigurations = m_workflowNodeConfigurations;
    m_mediaQueueOriginalPreviewPath = m_runner->previewPath();
    m_mediaQueueOriginalExportPath = m_runner->exportPath();
    m_mediaQueueOriginalProjectCaptured = true;
    m_mediaQueueTasks = normalizedTasks;
    m_mediaQueueExecutionMode = normalizedTasks.value(QStringLiteral("executionMode")).toString();
    m_mediaQueueStagePlan = mediaQueueStagePlan();
    m_mediaQueueStagePlanIndex = 0;
    m_mediaQueueProjects.clear();
    m_mediaQueueProcessing = true;
    m_mediaQueueCancelling = false;
    m_mediaQueueStatus = QStringLiteral("Preparing %1 selected media item(s)").arg(selectedReady);
    clearError();
    emit mediaQueueChanged();
    emit processingChanged();
    QTimer::singleShot(0, this, &DubbingController::startNextMediaQueueItem);
    return true;
}

void DubbingController::startNextMediaQueueItem()
{
    if (!m_mediaQueueProcessing) return;
    if (m_mediaQueueCancelling) {
        finishMediaQueueRun(QStringLiteral("Batch cancelled"));
        return;
    }
    if (m_mediaQueueExecutionMode == QStringLiteral("stage-by-stage")) {
        startNextMediaQueueStageItem();
        return;
    }
    for (int index = 0; index < m_mediaQueueItems.size(); ++index) {
        QVariantMap item = m_mediaQueueItems.at(index).toMap();
        if (item.value(QStringLiteral("processState")).toString() != QStringLiteral("queued")) continue;
        const QString localPath = item.value(QStringLiteral("localPath")).toString();
        if (!QFileInfo(localPath).isFile()) {
            item.insert(QStringLiteral("processState"), QStringLiteral("failed"));
            item.insert(QStringLiteral("status"), QStringLiteral("Downloaded staging file is no longer available"));
            item.insert(QStringLiteral("error"), item.value(QStringLiteral("status")));
            replaceMediaQueueItem(index, item);
            continue;
        }
        m_activeMediaQueueItemId = item.value(QStringLiteral("id")).toString();
        if (mediaQueueOperationRequiresSavedProject()) {
            DubbingProject restoredProject;
            QString projectError;
            if (!loadMediaQueueProject(item, &restoredProject, &projectError)) {
                item.insert(QStringLiteral("processState"), QStringLiteral("failed"));
                item.insert(QStringLiteral("stage"), QStringLiteral("failed"));
                item.insert(QStringLiteral("status"), projectError);
                item.insert(QStringLiteral("error"), projectError);
                item.insert(QStringLiteral("progress"), 100);
                replaceMediaQueueItem(index, item);
                m_activeMediaQueueItemId.clear();
                continue;
            }
            m_project = std::move(restoredProject);
        } else {
            m_project = newMediaQueueProject(item);
        }
        m_workflowNodeConfigurations = m_project.workflowNodeConfigurations;
        m_stepOutputs.clear();
        m_lastCompletedStepId.clear();
        m_runner->setBackgroundAudioPath(m_project.backgroundAudioPath);
        m_runner->setPreviewPath(QString());
        m_runner->setExportPath(QString());
        item.insert(QStringLiteral("processState"), QStringLiteral("running"));
        item.insert(QStringLiteral("status"), QStringLiteral("Starting import and media validation"));
        item.insert(QStringLiteral("stage"), QStringLiteral("ingest"));
        item.insert(QStringLiteral("progress"), 0);
        item.insert(QStringLiteral("outputDirectory"), mediaQueueOutputDirectory(m_activeMediaQueueItemId));
        replaceMediaQueueItem(index, item);
        if (!QDir().mkpath(mediaQueueOutputDirectory(m_activeMediaQueueItemId))) {
            completeCurrentMediaQueueItem(false, QStringLiteral("Cannot create the batch output directory."));
            return;
        }
        m_mediaQueueStatus = QStringLiteral("Processing %1").arg(item.value(QStringLiteral("displayName")).toString());
        emit projectChanged();
        emit workflowChanged();
        emit processingChanged();
        startMediaQueueStage(QStringLiteral("ingest"));
        return;
    }
    finishMediaQueueRun(QStringLiteral("Selected media batch finished"));
}

void DubbingController::startNextMediaQueueStageItem()
{
    if (!m_mediaQueueProcessing || m_mediaQueueExecutionMode != QStringLiteral("stage-by-stage")) return;
    if (m_mediaQueueCancelling) {
        finishMediaQueueRun(QStringLiteral("Batch cancelled"));
        return;
    }
    if (m_mediaQueueStagePlanIndex >= m_mediaQueueStagePlan.size()) {
        finishMediaQueueRun(QStringLiteral("Selected media batch finished"));
        return;
    }

    const QString stage = m_mediaQueueStagePlan.at(m_mediaQueueStagePlanIndex);
    for (int index = 0; index < m_mediaQueueItems.size(); ++index) {
        QVariantMap item = m_mediaQueueItems.at(index).toMap();
        if (item.value(QStringLiteral("processState")).toString() != QStringLiteral("queued")
            || item.value(QStringLiteral("nextStageIndex")).toInt() != m_mediaQueueStagePlanIndex) {
            continue;
        }
        const QString localPath = item.value(QStringLiteral("localPath")).toString();
        if (!QFileInfo(localPath).isFile()) {
            item.insert(QStringLiteral("processState"), QStringLiteral("failed"));
            item.insert(QStringLiteral("status"), QStringLiteral("Downloaded staging file is no longer available"));
            item.insert(QStringLiteral("error"), item.value(QStringLiteral("status")));
            item.insert(QStringLiteral("progress"), 100);
            replaceMediaQueueItem(index, item);
            continue;
        }

        m_activeMediaQueueItemId = item.value(QStringLiteral("id")).toString();
        if (mediaQueueOperationRequiresSavedProject()) {
            DubbingProject restoredProject;
            QString projectError;
            if (!loadMediaQueueProject(item, &restoredProject, &projectError)) {
                item.insert(QStringLiteral("processState"), QStringLiteral("failed"));
                item.insert(QStringLiteral("stage"), QStringLiteral("failed"));
                item.insert(QStringLiteral("status"), projectError);
                item.insert(QStringLiteral("error"), projectError);
                item.insert(QStringLiteral("progress"), 100);
                replaceMediaQueueItem(index, item);
                m_activeMediaQueueItemId.clear();
                continue;
            }
            m_project = std::move(restoredProject);
        } else {
            m_project = m_mediaQueueProjects.contains(m_activeMediaQueueItemId)
                ? m_mediaQueueProjects.value(m_activeMediaQueueItemId) : newMediaQueueProject(item);
        }
        m_workflowNodeConfigurations = m_project.workflowNodeConfigurations;
        m_stepOutputs.clear();
        m_lastCompletedStepId.clear();
        m_runner->setBackgroundAudioPath(m_project.backgroundAudioPath);
        m_runner->setPreviewPath(QString());
        m_runner->setExportPath(QString());
        item.insert(QStringLiteral("processState"), QStringLiteral("running"));
        item.insert(QStringLiteral("stage"), stage);
        item.insert(QStringLiteral("status"), QStringLiteral("Starting %1 (stage %2/%3)")
                    .arg(visibleStepForNode(stage))
                    .arg(m_mediaQueueStagePlanIndex + 1)
                    .arg(m_mediaQueueStagePlan.size()));
        item.insert(QStringLiteral("progress"), qRound(100.0 * m_mediaQueueStagePlanIndex
                                                         / m_mediaQueueStagePlan.size()));
        item.insert(QStringLiteral("outputDirectory"), mediaQueueOutputDirectory(m_activeMediaQueueItemId));
        replaceMediaQueueItem(index, item);
        if (!QDir().mkpath(mediaQueueOutputDirectory(m_activeMediaQueueItemId))) {
            completeCurrentMediaQueueItem(false, QStringLiteral("Cannot create the batch output directory."));
            return;
        }
        m_mediaQueueStatus = QStringLiteral("Running %1 for %2 (stage %3/%4)")
            .arg(visibleStepForNode(stage), item.value(QStringLiteral("displayName")).toString())
            .arg(m_mediaQueueStagePlanIndex + 1)
            .arg(m_mediaQueueStagePlan.size());
        emit projectChanged();
        emit workflowChanged();
        emit processingChanged();
        startMediaQueueStage(stage);
        return;
    }

    ++m_mediaQueueStagePlanIndex;
    if (m_mediaQueueStagePlanIndex < m_mediaQueueStagePlan.size()) {
        m_mediaQueueStatus = QStringLiteral("Completed %1 for selected media; continuing with %2")
            .arg(visibleStepForNode(stage), visibleStepForNode(m_mediaQueueStagePlan.at(m_mediaQueueStagePlanIndex)));
        emit mediaQueueChanged();
        QTimer::singleShot(0, this, &DubbingController::startNextMediaQueueStageItem);
    } else {
        finishMediaQueueRun(QStringLiteral("Selected media batch finished"));
    }
}

void DubbingController::startMediaQueueStage(const QString &stage)
{
    if (!m_mediaQueueProcessing || m_activeMediaQueueItemId.isEmpty()) return;
    const int index = mediaQueueIndex(m_activeMediaQueueItemId);
    if (index < 0) return;
    QVariantMap item = m_mediaQueueItems.at(index).toMap();
    item.insert(QStringLiteral("stage"), stage);
    item.insert(QStringLiteral("status"), QStringLiteral("Running %1").arg(visibleStepForNode(stage)));
    const int stageIndex = m_mediaQueueStagePlan.indexOf(stage);
    item.insert(QStringLiteral("progress"), m_mediaQueueExecutionMode == QStringLiteral("stage-by-stage")
                && stageIndex >= 0 && !m_mediaQueueStagePlan.isEmpty()
                ? qRound(100.0 * stageIndex / m_mediaQueueStagePlan.size()) : 0);
    replaceMediaQueueItem(index, item);
    m_mediaQueueStage = stage;
    m_runner->clearError();
    emit mediaQueueChanged();
    emit processingChanged();

    if (stage == QStringLiteral("ingest")) {
        m_runner->startIngest(m_project.sourceMediaPath.isEmpty()
            ? item.value(QStringLiteral("localPath")).toString() : m_project.sourceMediaPath);
    } else if (stage == QStringLiteral("source-separate")) {
        const QString audioPath = m_project.masterAudioPath;
        if (audioPath.isEmpty()) {
            completeCurrentMediaQueueItem(false, QStringLiteral("Media validation did not produce a master audio path."));
            return;
        }
        m_runner->startSourceSeparation(audioPath,
            m_workflowNodeConfigurations.value(QStringLiteral("source-separate")).toMap());
    } else if (stage == QStringLiteral("transcribe")) {
        transcribeSource();
    } else if (stage == QStringLiteral("translate")) {
        translateSource();
    } else if (stage == QStringLiteral("synthesize")) {
        generateAudio();
    } else if (stage == QStringLiteral("mix")) {
        if (!m_runner->renderPreview(m_project.segments, m_project.projectPath)) {
            completeCurrentMediaQueueItem(false, QStringLiteral("Could not start WAV mix rendering for this batch item."));
            return;
        }
    } else if (stage == QStringLiteral("export")) {
        const QVariantMap outputs = item.value(QStringLiteral("outputs")).toMap();
        const QString renderedAudio = outputs.value(QStringLiteral("voiceWav")).toString();
        if (!QFileInfo(renderedAudio).isFile()) {
            completeCurrentMediaQueueItem(false,
                QStringLiteral("Run TTS for this media before Export/Output. Its generated voice WAV is unavailable."));
            return;
        }
        const QString suffix = m_project.sourceIsVideo ? QStringLiteral("mp4") : QStringLiteral("wav");
        const QString outputPath = QDir(mediaQueueOutputDirectory(m_activeMediaQueueItemId))
            .filePath(QStringLiteral("dubbed-output.%1").arg(suffix));
        if (!m_runner->startExport(m_project.sourceMediaPath, renderedAudio, outputPath,
                                   m_project.segments, subtitleConfiguration())) {
            completeCurrentMediaQueueItem(false,
                QStringLiteral("Could not start Export/Output for this batch item."));
            return;
        }
    }

    if (stage != QStringLiteral("mix") && stage != QStringLiteral("export") && !m_runner->processing()) {
        const QString message = m_runner->lastError().trimmed().isEmpty()
            ? QStringLiteral("The %1 batch stage did not start.").arg(visibleStepForNode(stage))
            : m_runner->lastError();
        completeCurrentMediaQueueItem(false, message);
    }
}

void DubbingController::updateMediaQueueProgressFromRunner()
{
    if (!m_mediaQueueProcessing || m_activeMediaQueueItemId.isEmpty()) return;
    const int index = mediaQueueIndex(m_activeMediaQueueItemId);
    if (index < 0) return;
    QVariantMap item = m_mediaQueueItems.at(index).toMap();
    const QStringList stages = m_mediaQueueStagePlan.isEmpty() ? mediaQueueStagePlan() : m_mediaQueueStagePlan;
    if (stages.isEmpty()) return;
    const int stageIndex = qMax(0, stages.indexOf(m_mediaQueueStage));
    const int runnerProgress = qBound(0, m_runner->progress(), 100);
    const int progress = qBound(0, qRound((stageIndex * 100.0 + runnerProgress) / stages.size()), 99);
    if (item.value(QStringLiteral("progress")).toInt() != progress) {
        item.insert(QStringLiteral("progress"), progress);
        replaceMediaQueueItem(index, item);
    }
}

void DubbingController::completeCurrentMediaQueueStage(const QString &stage)
{
    const int index = mediaQueueIndex(m_activeMediaQueueItemId);
    if (index < 0) return;
    const int completedStageIndex = m_mediaQueueStagePlan.indexOf(stage);
    if (completedStageIndex < 0 || completedStageIndex != m_mediaQueueStagePlanIndex) {
        completeCurrentMediaQueueItem(false, QStringLiteral("The batch stage order became inconsistent."));
        return;
    }

    m_mediaQueueProjects.insert(m_activeMediaQueueItemId, m_project);
    QVariantMap item = m_mediaQueueItems.at(index).toMap();
    const int nextStageIndex = completedStageIndex + 1;
    if (nextStageIndex >= m_mediaQueueStagePlan.size()) {
        completeCurrentMediaQueueItem(true);
        return;
    }

    item.insert(QStringLiteral("processState"), QStringLiteral("queued"));
    item.insert(QStringLiteral("nextStageIndex"), nextStageIndex);
    item.insert(QStringLiteral("stage"), QStringLiteral("waiting"));
    item.insert(QStringLiteral("progress"), qRound(100.0 * nextStageIndex / m_mediaQueueStagePlan.size()));
    item.insert(QStringLiteral("status"), QStringLiteral("Completed %1; waiting for %2 across the selected queue")
                .arg(visibleStepForNode(stage), visibleStepForNode(m_mediaQueueStagePlan.at(nextStageIndex))));
    replaceMediaQueueItem(index, item);
    m_activeMediaQueueItemId.clear();
    m_mediaQueueStage.clear();
    emit mediaQueueChanged();
    QTimer::singleShot(0, this, &DubbingController::startNextMediaQueueItem);
}

void DubbingController::completeCurrentMediaQueueItem(bool success, const QString &message)
{
    const int index = mediaQueueIndex(m_activeMediaQueueItemId);
    if (index < 0) return;
    QVariantMap item = m_mediaQueueItems.at(index).toMap();
    QString finalMessage = message.trimmed();
    if (success) {
        QString saveError;
        if (!m_project.save(&saveError)) {
            success = false;
            finalMessage = saveError;
        } else {
            const QString projectCopy = QDir(mediaQueueOutputDirectory(m_activeMediaQueueItemId))
                .filePath(QStringLiteral("project.ladub.json"));
            if (!replaceCopy(m_project.projectPath, projectCopy, &saveError)) {
                success = false;
                finalMessage = saveError;
            } else {
                recordMediaQueueOutput(QStringLiteral("project"), projectCopy);
            }
        }
    }
    item = m_mediaQueueItems.at(index).toMap();
    item.insert(QStringLiteral("processState"), success ? QStringLiteral("completed") : QStringLiteral("failed"));
    item.insert(QStringLiteral("progress"), 100);
    item.insert(QStringLiteral("stage"), success ? QStringLiteral("completed") : QStringLiteral("failed"));
    item.insert(QStringLiteral("status"), success ? QStringLiteral("Completed")
                : (finalMessage.isEmpty() ? QStringLiteral("Failed") : finalMessage));
    if (!success) item.insert(QStringLiteral("error"), item.value(QStringLiteral("status")));
    replaceMediaQueueItem(index, item);
    m_mediaQueueStatus = success ? QStringLiteral("Completed %1").arg(item.value(QStringLiteral("displayName")).toString())
                                 : QStringLiteral("Failed %1").arg(item.value(QStringLiteral("displayName")).toString());
    m_activeMediaQueueItemId.clear();
    m_mediaQueueStage.clear();
    m_mediaQueueProjects.remove(item.value(QStringLiteral("id")).toString());
    emit mediaQueueChanged();
    QTimer::singleShot(0, this, &DubbingController::startNextMediaQueueItem);
}

void DubbingController::finishMediaQueueRun(const QString &message)
{
    m_mediaQueueProcessing = false;
    m_mediaQueueCancelling = false;
    m_activeMediaQueueItemId.clear();
    m_mediaQueueStage.clear();
    m_mediaQueueStagePlan.clear();
    m_mediaQueueStagePlanIndex = 0;
    m_mediaQueueProjects.clear();
    if (m_mediaQueueOriginalProjectCaptured) {
        m_project = m_mediaQueueOriginalProject;
        m_workflowNodeConfigurations = m_mediaQueueOriginalNodeConfigurations;
        m_runner->setBackgroundAudioPath(m_project.backgroundAudioPath);
        m_runner->setPreviewPath(m_mediaQueueOriginalPreviewPath);
        m_runner->setExportPath(m_mediaQueueOriginalExportPath);
        m_mediaQueueOriginalProjectCaptured = false;
    }
    m_mediaQueueStatus = message.isEmpty() ? QStringLiteral("Media batch finished") : message;
    emit projectChanged();
    emit segmentsChanged();
    emit workflowChanged();
    emit mediaQueueChanged();
    emit processingChanged();
}

void DubbingController::cancelMediaQueue()
{
    if (mediaQueueDownloading()) {
        m_mediaQueueCancelling = true;
        for (int index = 0; index < m_mediaQueueItems.size(); ++index) {
            QVariantMap item = m_mediaQueueItems.at(index).toMap();
            const QString downloadState = item.value(QStringLiteral("downloadState")).toString();
            if (downloadState == QStringLiteral("queued")) {
                item.remove(QStringLiteral("sourceUrl"));
                item.insert(QStringLiteral("downloadState"), QStringLiteral("cancelled"));
                item.insert(QStringLiteral("processState"), QStringLiteral("cancelled"));
                item.insert(QStringLiteral("status"), QStringLiteral("Cancelled before download"));
                item.insert(QStringLiteral("selected"), false);
                replaceMediaQueueItem(index, item);
            }
        }
        m_mediaQueueStatus = QStringLiteral("Cancelling download queue");
        emit mediaQueueChanged();
        if (m_colabMediaDownload && m_colabMediaDownload->active()) {
            m_colabMediaDownload->cancel();
        } else {
            m_activeMediaQueueDownloadId.clear();
            m_mediaQueueCancelling = false;
            m_mediaQueueStatus = QStringLiteral("Download queue cancelled");
            emit mediaQueueChanged();
        }
        return;
    }
    if (!m_mediaQueueProcessing) return;
    m_mediaQueueCancelling = true;
    for (int index = 0; index < m_mediaQueueItems.size(); ++index) {
        QVariantMap item = m_mediaQueueItems.at(index).toMap();
        if (item.value(QStringLiteral("processState")).toString() == QStringLiteral("queued")) {
            item.insert(QStringLiteral("processState"), QStringLiteral("cancelled"));
            item.insert(QStringLiteral("status"), QStringLiteral("Cancelled before processing"));
            replaceMediaQueueItem(index, item);
        }
    }
    if (m_runner && m_runner->processing()) m_runner->cancel();
    const int activeIndex = mediaQueueIndex(m_activeMediaQueueItemId);
    if (activeIndex >= 0) {
        QVariantMap activeItem = m_mediaQueueItems.at(activeIndex).toMap();
        activeItem.insert(QStringLiteral("processState"), QStringLiteral("cancelled"));
        activeItem.insert(QStringLiteral("status"), QStringLiteral("Cancelled during ")
                          + activeItem.value(QStringLiteral("stage")).toString());
        activeItem.insert(QStringLiteral("progress"), qBound(0, activeItem.value(QStringLiteral("progress")).toInt(), 99));
        replaceMediaQueueItem(activeIndex, activeItem);
    }
    if (!m_runner || !m_runner->processing()) finishMediaQueueRun(QStringLiteral("Batch cancelled"));
}

void DubbingController::onIngestFinished(bool success, const QVariantMap &manifest)
{
    if (!success) return; // Error is already handled and set on runner
    
    m_project.sourceMediaPath = manifest.value(QStringLiteral("sourcePath")).toString();
    m_project.sourceHash = manifest.value(QStringLiteral("sourceHash")).toString();
    m_project.masterAudioPath = manifest.value(QStringLiteral("masterAudioPath")).toString();
    m_project.analysisAudioPath = manifest.value(QStringLiteral("analysisAudioPath")).toString();
    m_project.backgroundAudioPath = manifest.value(QStringLiteral("backgroundAudioPath")).toString();
    m_runner->setBackgroundAudioPath(manifest.value(QStringLiteral("backgroundAudioPath")).toString());
    m_project.sourceDurationMs = manifest.value(QStringLiteral("sourceDurationMs")).toLongLong();
    m_project.sourceSampleRate = manifest.value(QStringLiteral("sourceSampleRate")).toInt();
    m_project.sourceChannels = manifest.value(QStringLiteral("sourceChannels")).toInt();
    m_project.sourceIsVideo = manifest.value(QStringLiteral("sourceIsVideo")).toBool();
    Logger::info(QStringLiteral("DubbingController"),
                 QStringLiteral("Media normalized successfully: source=%1, hash=%2, master=%3, analysis=%4")
                     .arg(m_project.sourceMediaPath, m_project.sourceHash,
                          m_project.masterAudioPath, m_project.analysisAudioPath));
    emit projectChanged();
    emit workflowChanged();
    persistAfterEdit();
}


void DubbingController::transcribeSource()
{
    if (m_project.sourceMediaPath.isEmpty()) {
        setError(QStringLiteral("Import media before starting transcription."));
        return;
    }
    QVariantMap configuration = effectiveTranscriptConfiguration(true);
    QVariantMap parameters = configuration.value(QStringLiteral("parameters")).toMap();
    parameters.insert(QStringLiteral("ocrSourceMedia"), m_project.sourceMediaPath);
    configuration.insert(QStringLiteral("parameters"), parameters);
    const QString mode = parameters.value(QStringLiteral("transcriptSource"), QStringLiteral("stt"))
                             .toString().trimmed().toLower();
    const QString audioPath = !m_project.analysisAudioPath.isEmpty() ? m_project.analysisAudioPath
                                                                     : m_project.masterAudioPath;
    if (mode != QStringLiteral("ocr") && audioPath.isEmpty()) {
        setError(QStringLiteral("Normalize and separate the source audio before transcription."));
        return;
    }
    Logger::info(QStringLiteral("DubbingController"),
                 QStringLiteral("Starting dubbing transcription source=%1 language=%2 audio=%3")
                     .arg(mode, m_project.sourceLanguage, audioPath));
    persistAfterEdit();
    m_runner->startTranscription(m_project.sourceLanguage, audioPath, QString(), configuration);
}

void DubbingController::translateSource()
{
    int emptySourceCount = 0;
    for (const QVariant &value : std::as_const(m_project.segments)) {
        if (value.toMap().value(QStringLiteral("sourceText")).toString().trimmed().isEmpty())
            ++emptySourceCount;
    }
    const QVariantMap configured =
        m_workflowNodeConfigurations.value(QStringLiteral("translate")).toMap();
    Logger::info(QStringLiteral("DubbingController"),
                 QStringLiteral("Translation requested sourceLanguage=%1 targetLanguage=%2 segments=%3 emptySource=%4 family=%5 runtime=%6 runtimeVersion=%7")
                     .arg(m_project.sourceLanguage, m_project.targetLanguage)
                     .arg(m_project.segments.size())
                     .arg(emptySourceCount)
                     .arg(configured.value(QStringLiteral("familyId")).toString(),
                          configured.value(QStringLiteral("runtimeId")).toString(),
                          configured.value(QStringLiteral("runtimeVersion")).toString()));
    if (m_project.sourceMediaPath.isEmpty()) {
        Logger::warning(QStringLiteral("DubbingController"),
                        QStringLiteral("Translation rejected: source media is empty."));
        setError(QStringLiteral("Import media before translating."));
        return;
    }
    if (m_project.segments.isEmpty()) {
        Logger::warning(QStringLiteral("DubbingController"),
                        QStringLiteral("Translation rejected: transcript has no segments."));
        setError(QStringLiteral("Transcribe the source before translating."));
        return;
    }
    const int unresolvedConflicts = unresolvedTranscriptConflictCount();
    if (unresolvedConflicts > 0) {
        Logger::warning(QStringLiteral("DubbingController"),
                        QStringLiteral("Translation rejected: %1 STT/OCR conflict(s) still require review.")
                            .arg(unresolvedConflicts));
        setError(QStringLiteral("Resolve %1 STT/OCR conflict(s) before translating. The original STT and OCR evidence has been retained for review.")
                     .arg(unresolvedConflicts));
        return;
    }
    if (m_project.targetLanguage.trimmed().isEmpty()) {
        Logger::warning(QStringLiteral("DubbingController"),
                        QStringLiteral("Translation rejected: target language is empty."));
        setError(QStringLiteral("Choose a target language before translating."));
        return;
    }
    if (emptySourceCount > 0) {
        Logger::warning(QStringLiteral("DubbingController"),
                        QStringLiteral("Translation request contains %1 segment(s) without source text.")
                            .arg(emptySourceCount));
    }
    QVariantMap translationConfig = configured;
    QVariantMap parameters = translationConfig.value(QStringLiteral("parameters")).toMap();
    QVariantMap effectiveDurationControl = m_project.durationControl;
    effectiveDurationControl.insert(
        QStringLiteral("autoRewrite"),
        m_project.dubbingQuality != QStringLiteral("fast")
            && m_project.durationControl.value(QStringLiteral("autoRewrite"), true).toBool());
    parameters.insert(QStringLiteral("durationControl"), effectiveDurationControl);
    translationConfig.insert(QStringLiteral("parameters"), parameters);
    m_runner->setTranslationFixConfiguration(translationFixConfiguration());
    m_runner->startTranslation(m_project.sourceLanguage, m_project.targetLanguage, m_project.segments,
                               translationConfig);
}

void DubbingController::generateAudio()
{
    const QVariantMap synthesisConfiguration = m_workflowNodeConfigurations
        .value(QStringLiteral("synthesize")).toMap();
    QVariantMap synthesisSettings = synthesisConfiguration
        .value(QStringLiteral("parameters")).toMap();
    synthesisSettings.insert(QStringLiteral("familyId"),
                             synthesisConfiguration.value(QStringLiteral("familyId")));
    if (!synthesisSettings.contains(QStringLiteral("lang")))
        synthesisSettings.insert(QStringLiteral("lang"), m_project.targetLanguage);
    if (!applySelectedCloneVoiceToSynthesis(&synthesisSettings)) return;
    m_runner->startAudioGeneration(m_project.segments, m_project.projectPath, synthesisSettings);
}

void DubbingController::cancelProcessing()
{
    if (m_mediaQueueProcessing) {
        cancelMediaQueue();
        return;
    }
    const bool wasAutomatic = m_workflowMode == QStringLiteral("automatic")
        || m_automaticSetupActive;
    m_automaticSetupActive = false;
    m_automaticOutputPath.clear();
    m_automaticDownloadsQueued.clear();
    m_automaticDownloadKeys.clear();
    m_automaticConfiguredNodes.clear();
    m_automaticSetupNodeId.clear();
    m_pendingExportPath.clear();
    if (m_translationFix) m_translationFix->cancel();
    if (m_workflowRunner && m_workflowRunner->running()) m_workflowRunner->cancel();
    m_runner->cancel();
    if (wasAutomatic) {
        setWorkflowMode(QStringLiteral("idle"));
        setAutomaticStatus(QStringLiteral("Automatic generation stopped"));
        appendAutomaticEvent(QStringLiteral("Automatic generation stopped"),
                             QStringLiteral("stopped"), currentStepId());
    }
    emit processingChanged();
    emit workflowChanged();
}

bool DubbingController::fixTranslations(const QVariantMap &configuration)
{
    if (!m_translationFix || processing()) return false;
    clearError();
    return m_translationFix->start(
        m_project.sourceLanguage, m_project.targetLanguage,
        m_project.segments, configuration);
}

bool DubbingController::fixTranslationSegment(
    int index, const QVariantMap &configuration)
{
    if (!m_translationFix || processing()
        || index < 0 || index >= m_project.segments.size())
        return false;
    clearError();
    return m_translationFix->start(
        m_project.sourceLanguage, m_project.targetLanguage,
        m_project.segments, configuration, index);
}

bool DubbingController::translationSegmentNeedsFix(int index) const
{
    if (index < 0 || index >= m_project.segments.size()) return false;
    return DubbingTranslationFixService::eligibleSegmentCount(
               {m_project.segments.at(index)}, m_project.targetLanguage)
        == 1;
}

void DubbingController::testTranslationFixConnection(
    const QVariantMap &configuration)
{
    if (!m_translationFix || processing()) return;
    m_translationFix->testConnection(configuration);
}

QVariantList DubbingController::translationFixCliModelOptions(
    const QString &cliAgent) const
{
    return DubbingTranslationFixService::cliModelOptions(cliAgent);
}

void DubbingController::cancelTranslationFix()
{
    if (m_translationFix) m_translationFix->cancel();
}

void DubbingController::setAdaptiveConfiguration(const QVariantMap &configuration)
{
    if (!m_translationFix || processing()) return;
    const QString previousProvider = m_translationFix->configuration().value(
        QStringLiteral("provider")).toString();
    QVariantMap next = configuration;
    next.insert(QStringLiteral("configured"), true);
    m_translationFix->setConfiguration(next);
    if (previousProvider == QStringLiteral("local")
        && m_translationFix->configuration().value(QStringLiteral("provider")).toString()
               != QStringLiteral("local")) {
        if (AppController *app = AppController::instance(); app && app->sessionRegistry()) {
            if (IModelSession *session = app->sessionRegistry()->sessionForCapability(
                    QStringLiteral("llm-chat"))) {
                for (const SessionConfiguration &loaded : session->loadedConfigurations())
                    session->requestUnloadConfiguration(loaded.signature);
            }
        }
    }
    if (m_project.dubbingQuality == QStringLiteral("custom"))
        m_project.customRewriteConfiguration = m_translationFix->configuration();
    if (m_runner) m_runner->setTranslationFixConfiguration(
        m_translationFix->configuration());
    persistAfterEdit();
    emit workflowChanged();
}

bool DubbingController::exportMedia(const QString &path)
{
    const QString outputPath = QFileInfo(PathUtils::urlToLocalPath(path)).absoluteFilePath();
    if (outputPath.isEmpty()) {
        setError(QStringLiteral("Choose an output path."));
        return false;
    }
    if (m_project.sourceMediaPath.isEmpty()) {
        setError(QStringLiteral("Import source media before exporting."));
        return false;
    }
    if (previewPath().isEmpty() || !QFileInfo::exists(previewPath())) {
        m_pendingExportPath = outputPath;
        if (!renderPreview()) {
            m_pendingExportPath.clear();
            return false;
        }
        return true;
    }
    return m_runner->startExport(m_project.sourceMediaPath, previewPath(), outputPath,
                                 m_project.segments, subtitleConfiguration());
}

bool DubbingController::exportAudioStem(const QString &stem, const QString &path)
{
    const QString outputPath = QFileInfo(PathUtils::urlToLocalPath(path)).absoluteFilePath();
    if (outputPath.isEmpty()) {
        setError(QStringLiteral("Choose an audio output path."));
        return false;
    }

    QString sourcePath;
    if (stem == QStringLiteral("mix")) sourcePath = previewPath();
    else if (stem == QStringLiteral("vocal")) sourcePath = dubbedVocalPath();
    else if (stem == QStringLiteral("background")) sourcePath = m_project.backgroundAudioPath;
    else {
        setError(QStringLiteral("Unknown audio stem: %1").arg(stem));
        return false;
    }
    if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath)) {
        setError(QStringLiteral("The %1 audio stem is not available yet.").arg(stem));
        return false;
    }
    QString error;
    if (!replaceCopy(sourcePath, outputPath, &error)) {
        setError(error);
        return false;
    }
    clearError();
    return true;
}

bool DubbingController::exportSubtitles(const QString &path, bool useTargetText)
{
    const QString outputPath = QFileInfo(PathUtils::urlToLocalPath(path)).absoluteFilePath();
    if (path.isEmpty() || outputPath.isEmpty()) {
        setError(QStringLiteral("Choose a subtitle output path."));
        return false;
    }
    QString error;
    if (!writeDubbingSubtitles(m_project.segments, outputPath, useTargetText, &error)) {
        setError(error);
        return false;
    }
    clearError();
    return true;
}

bool DubbingController::exportPackage(const QString &directoryPath)
{
    const QString outputDirectory = QFileInfo(PathUtils::urlToLocalPath(directoryPath)).absoluteFilePath();
    if (directoryPath.isEmpty() || outputDirectory.isEmpty()) {
        setError(QStringLiteral("Choose a package output folder."));
        return false;
    }
    if (!hasProject()) {
        setError(QStringLiteral("Save the dubbing project before exporting a package."));
        return false;
    }

    QDir directory(outputDirectory);
    if (!directory.mkpath(QStringLiteral(".")) || !directory.mkpath(QStringLiteral("clips"))) {
        setError(QStringLiteral("Cannot create package folder: %1").arg(outputDirectory));
        return false;
    }

    QString error;
    if (!m_project.save(&error)
        || !replaceCopy(m_project.projectPath, directory.filePath(QStringLiteral("project.ladub.json")), &error)
        || !replaceCopy(previewPath(), directory.filePath(QStringLiteral("dubbed-mix.wav")), &error)
        || !replaceCopy(dubbedVocalPath(), directory.filePath(QStringLiteral("dubbed-vocals.wav")), &error)
        || !replaceCopy(m_project.analysisAudioPath, directory.filePath(QStringLiteral("source-vocals.wav")), &error)
        || !replaceCopy(m_project.backgroundAudioPath, directory.filePath(QStringLiteral("background.wav")), &error)) {
        setError(error);
        return false;
    }

    int exportedClips = 0;
    for (int i = 0; i < m_project.segments.size(); ++i) {
        const QString clipPath = m_project.segments.at(i).toMap().value(QStringLiteral("clipPath")).toString();
        if (clipPath.isEmpty() || !QFileInfo(clipPath).isFile()) continue;
        const QString suffix = QFileInfo(clipPath).suffix().isEmpty()
            ? QStringLiteral("wav") : QFileInfo(clipPath).suffix();
        const QString clipName = QStringLiteral("%1.%2").arg(i + 1, 4, 10, QLatin1Char('0')).arg(suffix);
        if (!replaceCopy(clipPath, directory.filePath(QStringLiteral("clips/") + clipName), &error)) {
            setError(error);
            return false;
        }
        ++exportedClips;
    }

    const QString sourceSubtitlePath = directory.filePath(QStringLiteral("source.srt"));
    const QString dubbedSubtitlePath = directory.filePath(QStringLiteral("dubbed.srt"));
    if (!writeDubbingSubtitles(m_project.segments, sourceSubtitlePath, false, nullptr))
        QFile::remove(sourceSubtitlePath);
    if (!writeDubbingSubtitles(m_project.segments, dubbedSubtitlePath, true, nullptr))
        QFile::remove(dubbedSubtitlePath);

    const QJsonObject manifest{
        {QStringLiteral("format"), QStringLiteral("la-studio-dubbing-package")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("project"), QStringLiteral("project.ladub.json")},
        {QStringLiteral("sourceMediaPath"), m_project.sourceMediaPath},
        {QStringLiteral("segmentCount"), m_project.segments.size()},
        {QStringLiteral("exportedClipCount"), exportedClips}
    };
    QSaveFile manifestFile(directory.filePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::WriteOnly)
        || manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) < 0
        || !manifestFile.commit()) {
        setError(QStringLiteral("Cannot write package manifest: %1").arg(manifestFile.errorString()));
        return false;
    }
    clearError();
    return true;
}

bool DubbingController::importSubtitles(const QString &path, const QString &untimedStrategy)
{
    if (processing()) {
        setBusyError(QStringLiteral("Wait for the active Dubbing operation before importing subtitles."));
        return false;
    }
    if (!hasProject()) {
        setError(QStringLiteral("Open a Dubbing project before importing subtitles."));
        return false;
    }
    const QString localPath = QFileInfo(PathUtils::urlToLocalPath(path)).absoluteFilePath();
    if (path.isEmpty() || !QFileInfo(localPath).isFile()) {
        setError(QStringLiteral("Choose an existing SRT, VTT, ASS/SSA, TXT or Markdown subtitle file."));
        return false;
    }
    QVariantList imported;
    bool hasTiming = false;
    QString format;
    QString error;
    if (!DubbingSubtitleService::importFile(localPath, imported, hasTiming, format, &error)) {
        setError(error);
        return false;
    }
    QVariantList replacement = imported;
    if (!hasTiming) {
        if (untimedStrategy != QStringLiteral("existing-segment")) {
            setError(QStringLiteral("TXT/Markdown has no timestamps. Select line-per-existing-segment or run forced alignment before import; LA Studio will not invent timing."));
            return false;
        }
        if (!DubbingSubtitleService::mapUntimedLines(imported, m_project.segments, replacement, &error)) {
            setError(error);
            return false;
        }
    }
    m_project.segments = replacement;
    QVariantMap configuration = subtitleConfiguration();
    configuration.insert(QStringLiteral("source"), QStringLiteral("imported-") + format);
    configuration.insert(QStringLiteral("hasOriginalTiming"), hasTiming);
    configuration.insert(QStringLiteral("untimedMapping"), hasTiming ? QString() : untimedStrategy);
    configuration.insert(QStringLiteral("importedFileName"), QFileInfo(localPath).fileName());
    m_project.subtitleConfiguration = configuration;
    clearError();
    emit segmentsChanged();
    emit projectChanged();
    emit workflowChanged();
    persistAfterEdit();
    return true;
}

bool DubbingController::setSubtitleStyle(const QVariantMap &style)
{
    QVariantMap normalized;
    QString error;
    if (!DubbingSubtitleService::normalizeStyle(style, normalized, &error)) {
        setError(error);
        return false;
    }
    QVariantMap configuration = subtitleConfiguration();
    configuration.insert(QStringLiteral("style"), normalized);
    m_project.subtitleConfiguration = configuration;
    clearError();
    emit projectChanged();
    persistAfterEdit();
    return true;
}

bool DubbingController::setSubtitleTextSource(const QString &source)
{
    const QString normalized = source.trimmed().toLower();
    if (normalized != QStringLiteral("source") && normalized != QStringLiteral("target")) {
        setError(QStringLiteral("Subtitle text source must be source or target."));
        return false;
    }
    QVariantMap configuration = subtitleConfiguration();
    configuration.insert(QStringLiteral("textSource"), normalized);
    m_project.subtitleConfiguration = configuration;
    clearError();
    emit projectChanged();
    persistAfterEdit();
    return true;
}

bool DubbingController::setSubtitleBurnIn(bool enabled)
{
    QVariantMap configuration = subtitleConfiguration();
    configuration.insert(QStringLiteral("burnIn"), enabled);
    m_project.subtitleConfiguration = configuration;
    clearError();
    emit projectChanged();
    persistAfterEdit();
    return true;
}

bool DubbingController::exportCapCutDraft(const QString &directoryPath)
{
    const QString outputDirectory = QFileInfo(PathUtils::urlToLocalPath(directoryPath)).absoluteFilePath();
    if (directoryPath.isEmpty() || outputDirectory.isEmpty()) {
        setError(QStringLiteral("Choose a parent folder for the CapCut draft."));
        return false;
    }
    if (!hasProject()) {
        setError(QStringLiteral("Save the dubbing project before exporting a CapCut draft."));
        return false;
    }
    QString error;
    if (!m_project.save(&error)) {
        setError(error);
        return false;
    }
    QString draftPath;
    QString warning;
    // analysisAudioPath is the normalized mono analysis input immediately
    // after ingest. It becomes a genuine vocals stem only after the source
    // separation node has produced its companion background stem. Do not
    // label the analysis input as editable source vocals in a CapCut draft.
    const bool hasSeparatedStems = !m_project.analysisAudioPath.trimmed().isEmpty()
        && QFileInfo(m_project.analysisAudioPath).isFile()
        && !m_project.backgroundAudioPath.trimmed().isEmpty()
        && QFileInfo(m_project.backgroundAudioPath).isFile();
    const QString vocalsStemPath = hasSeparatedStems ? m_project.analysisAudioPath : QString();
    if (!CapCutDraftExporter::exportDraft(
            outputDirectory, QFileInfo(m_project.projectPath).completeBaseName(),
            m_project.sourceMediaPath, m_project.masterAudioPath, m_project.backgroundAudioPath,
            previewPath(), m_project.sourceIsVideo, m_project.sourceDurationMs,
            m_project.segments, vocalsStemPath, subtitleConfiguration(),
            timingConfiguration(), &draftPath, &warning, &error)) {
        setError(error);
        return false;
    }
    m_capCutDraftPath = draftPath;
    m_capCutDraftWarning = warning;
    clearError();
    emit exportChanged();
    return true;
}

bool DubbingController::renderPreview(const QString &path)
{
    return m_runner->renderPreview(m_project.segments, m_project.projectPath, path);
}

bool DubbingController::replaceTranscriptSegments(const QVariantList &ocrSegments)
{
    if (processing()) {
        setBusyError(QStringLiteral("Wait for the current Dubbing operation before replacing its transcript."));
        return false;
    }
    if (!hasProject()) {
        setError(QStringLiteral("Open a Dubbing project before importing reviewed Subtitle OCR results."));
        return false;
    }
    if (ocrSegments.isEmpty()) {
        setError(QStringLiteral("Subtitle OCR did not provide any reviewed transcript segments."));
        return false;
    }

    QVariantList replacement;
    replacement.reserve(ocrSegments.size());
    for (const QVariant &entry : ocrSegments) {
        const QVariantMap ocr = entry.toMap();
        const qint64 startMs = ocr.value(QStringLiteral("startMs")).toLongLong();
        const qint64 endMs = ocr.value(QStringLiteral("endMs")).toLongLong();
        const QString sourceText = ocr.value(QStringLiteral("text")).toString().trimmed();
        if (startMs < 0 || endMs <= startMs || sourceText.isEmpty()) {
            setError(QStringLiteral("Subtitle OCR contains an invalid reviewed segment."));
            return false;
        }
        QVariantMap segment;
        segment.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        segment.insert(QStringLiteral("startMs"), startMs);
        segment.insert(QStringLiteral("endMs"), endMs);
        segment.insert(QStringLiteral("sourceText"), sourceText);
        segment.insert(QStringLiteral("targetText"), QString());
        segment.insert(QStringLiteral("speakerId"), QStringLiteral("speaker-1"));
        segment.insert(QStringLiteral("state"), QStringLiteral("draft"));
        segment.insert(QStringLiteral("timingSource"), QStringLiteral("subtitle-ocr"));
        segment.insert(QStringLiteral("alignmentStatus"), QStringLiteral("pending"));
        segment.insert(QStringLiteral("ocrConfidence"), ocr.value(QStringLiteral("confidence")));
        replacement.append(segment);
    }

    m_project.segments = replacement;
    clearError();
    emit segmentsChanged();
    emit workflowChanged();
    persistAfterEdit();
    return true;
}

bool DubbingController::resolveTranscriptConflict(int index, const QString &choice)
{
    if (processing() || index < 0 || index >= m_project.segments.size()) return false;
    const QString normalized = choice.trimmed().toLower();
    if (normalized != QStringLiteral("stt") && normalized != QStringLiteral("ocr")) return false;
    QVariantMap segment = m_project.segments.at(index).toMap();
    if (segment.value(QStringLiteral("fusionStatus")).toString() != QStringLiteral("conflict")) return false;
    const QString text = segment.value(normalized == QStringLiteral("stt")
                                           ? QStringLiteral("fusionSttText")
                                           : QStringLiteral("fusionOcrText")).toString().trimmed();
    if (text.isEmpty()) return false;
    segment.insert(QStringLiteral("sourceText"), text);
    segment.insert(QStringLiteral("fusionChoice"), normalized);
    segment.insert(QStringLiteral("fusionStatus"), QStringLiteral("resolved"));
    segment.insert(QStringLiteral("fusionNeedsReview"), false);
    segment.insert(QStringLiteral("fusionResolutionPolicy"), QStringLiteral("manual"));
    segment.insert(QStringLiteral("state"), QStringLiteral("transcribed"));
    m_project.segments[index] = segment;
    clearError();
    emit segmentsChanged();
    emit workflowChanged();
    persistAfterEdit();
    return true;
}

bool DubbingController::resolveAllTranscriptConflicts(const QString &choice)
{
    if (processing()) return false;
    const QString normalized = choice.trimmed().toLower();
    if (normalized != QStringLiteral("stt") && normalized != QStringLiteral("ocr")) return false;

    int resolved = 0;
    for (int index = 0; index < m_project.segments.size(); ++index) {
        QVariantMap segment = m_project.segments.at(index).toMap();
        if (segment.value(QStringLiteral("fusionStatus")).toString() != QStringLiteral("conflict"))
            continue;
        const QString text = segment.value(normalized == QStringLiteral("stt")
                                               ? QStringLiteral("fusionSttText")
                                               : QStringLiteral("fusionOcrText")).toString().trimmed();
        if (text.isEmpty()) continue;
        segment.insert(QStringLiteral("sourceText"), text);
        segment.insert(QStringLiteral("fusionChoice"), normalized);
        segment.insert(QStringLiteral("fusionStatus"), QStringLiteral("resolved"));
        segment.insert(QStringLiteral("fusionNeedsReview"), false);
        segment.insert(QStringLiteral("fusionResolutionPolicy"), QStringLiteral("bulk-manual"));
        segment.insert(QStringLiteral("state"), QStringLiteral("transcribed"));
        m_project.segments[index] = segment;
        ++resolved;
    }
    if (resolved == 0) return false;
    clearError();
    emit segmentsChanged();
    emit workflowChanged();
    persistAfterEdit();
    return true;
}

bool DubbingController::setTranscriptFusionPolicy(const QString &policy)
{
    if (processing()) return false;
    const QString normalized = DubbingTranscriptFusionService::normalizePolicy(policy);
    m_project.transcriptConfiguration.insert(QStringLiteral("fusionPolicy"), normalized);
    QVariantMap selected = m_workflowNodeConfigurations.value(QStringLiteral("transcribe")).toMap();
    QVariantMap parameters = selected.value(QStringLiteral("parameters")).toMap();
    parameters.insert(QStringLiteral("fusionPolicy"), normalized);
    selected.insert(QStringLiteral("parameters"), parameters);
    m_workflowNodeConfigurations.insert(QStringLiteral("transcribe"), selected);
    m_project.workflowNodeConfigurations = m_workflowNodeConfigurations;
    persistAfterEdit();
    emit projectChanged();
    emit workflowChanged();
    return true;
}

QVariantMap DubbingController::transcriptConflictAiAvailability() const
{
    QString reason;
    const bool available = DubbingTranslationFixService::reconciliationAvailable(
        translationFixConfiguration(), &reason);
    return {{QStringLiteral("available"), available},
            {QStringLiteral("reason"), reason},
            {QStringLiteral("provider"), translationFixConfiguration().value(QStringLiteral("provider"))},
            {QStringLiteral("model"), translationFixConfiguration().value(QStringLiteral("model"))}};
}

bool DubbingController::requestTranscriptConflictAiSuggestion(int index)
{
    if (!m_translationFix || processing()) return false;
    if (index >= m_project.segments.size()) return false;
    if (index >= 0) {
        const QVariantMap segment = m_project.segments.at(index).toMap();
        if (!segment.value(QStringLiteral("fusionNeedsReview")).toBool()) return false;
    } else if (!hasUnresolvedTranscriptConflicts()) {
        return false;
    }
    QString reason;
    const QVariantMap configuration = translationFixConfiguration();
    if (!DubbingTranslationFixService::reconciliationAvailable(configuration, &reason)) {
        setError(reason);
        return false;
    }
    clearError();
    return m_translationFix->startReconciliation(
        m_project.sourceLanguage, m_project.segments, configuration, index);
}

bool DubbingController::acceptTranscriptConflictAiSuggestion(int index)
{
    if (processing() || index < 0 || index >= m_project.segments.size()) return false;
    QVariantMap segment = m_project.segments.at(index).toMap();
    const QString suggestion = segment.value(QStringLiteral("fusionAiSuggestion")).toString().trimmed();
    if (segment.value(QStringLiteral("fusionStatus")).toString() != QStringLiteral("conflict")
        || segment.value(QStringLiteral("fusionAiSuggestionStatus")).toString()
               != QStringLiteral("pending") || suggestion.isEmpty()) {
        return false;
    }
    segment.insert(QStringLiteral("sourceText"), suggestion);
    segment.insert(QStringLiteral("fusionChoice"), QStringLiteral("ai-suggestion"));
    segment.insert(QStringLiteral("fusionStatus"), QStringLiteral("resolved"));
    segment.insert(QStringLiteral("fusionNeedsReview"), false);
    segment.insert(QStringLiteral("fusionResolutionPolicy"), QStringLiteral("ai-suggest"));
    segment.insert(QStringLiteral("fusionAiSuggestionStatus"), QStringLiteral("accepted"));
    segment.insert(QStringLiteral("state"), QStringLiteral("transcribed"));
    m_project.segments[index] = segment;
    clearError();
    emit segmentsChanged();
    emit workflowChanged();
    persistAfterEdit();
    return true;
}

bool DubbingController::rejectTranscriptConflictAiSuggestion(int index)
{
    if (processing() || index < 0 || index >= m_project.segments.size()) return false;
    QVariantMap segment = m_project.segments.at(index).toMap();
    if (segment.value(QStringLiteral("fusionStatus")).toString() != QStringLiteral("conflict")
        || segment.value(QStringLiteral("fusionAiSuggestionStatus")).toString()
               != QStringLiteral("pending")) {
        return false;
    }
    // Retain the rejected suggestion and all source evidence for later audit;
    // rejection deliberately leaves the review gate in place.
    segment.insert(QStringLiteral("fusionAiSuggestionStatus"), QStringLiteral("rejected"));
    segment.insert(QStringLiteral("fusionNeedsReview"), true);
    m_project.segments[index] = segment;
    emit segmentsChanged();
    emit workflowChanged();
    persistAfterEdit();
    return true;
}

void DubbingController::addSegment(qint64 startMs, qint64 endMs, const QString &sourceText)
{
    if (endMs <= startMs) {
        setError(QStringLiteral("Segment end must be after its start."));
        return;
    }
    QVariantMap segment;
    segment.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    segment.insert(QStringLiteral("startMs"), startMs);
    segment.insert(QStringLiteral("endMs"), endMs);
    segment.insert(QStringLiteral("sourceText"), sourceText);
    segment.insert(QStringLiteral("timingSource"), QStringLiteral("manual"));
    segment.insert(QStringLiteral("alignmentStatus"), QStringLiteral("pending"));
    segment.insert(QStringLiteral("targetText"), QString());
    segment.insert(QStringLiteral("speakerId"), QStringLiteral("speaker-1"));
    segment.insert(QStringLiteral("state"), QStringLiteral("draft"));
    m_project.segments.append(segment);
    m_timingResolutionPreview.clear();
    m_timingUndoSegments.clear();
    invalidateTimingOutputs();
    emit segmentsChanged();
    emit timingResolutionChanged();
    emit workflowChanged();
    persistAfterEdit();
}

void DubbingController::updateSegment(int index, const QVariantMap &patch)
{
    if (index < 0 || index >= m_project.segments.size()) return;
    QVariantMap segment = m_project.segments.at(index).toMap();
    const bool sourceTextChanged = patch.contains(QStringLiteral("sourceText"))
        && patch.value(QStringLiteral("sourceText")).toString()
            != segment.value(QStringLiteral("sourceText")).toString();
    const bool targetTextChanged = patch.contains(QStringLiteral("targetText"))
        && patch.value(QStringLiteral("targetText")).toString()
            != segment.value(QStringLiteral("targetText")).toString();
    const bool speakerChanged = patch.contains(QStringLiteral("speakerId"))
        && patch.value(QStringLiteral("speakerId")).toString()
            != segment.value(QStringLiteral("speakerId")).toString();
    const qint64 startMs = patch.value(QStringLiteral("startMs"), segment.value(QStringLiteral("startMs"))).toLongLong();
    const qint64 endMs = patch.value(QStringLiteral("endMs"), segment.value(QStringLiteral("endMs"))).toLongLong();
    const bool timingChanged = startMs != segment.value(QStringLiteral("startMs")).toLongLong()
        || endMs != segment.value(QStringLiteral("endMs")).toLongLong()
        || (patch.contains(QStringLiteral("durationMs"))
            && patch.value(QStringLiteral("durationMs")).toLongLong()
                != segment.value(QStringLiteral("durationMs")).toLongLong());
    if (endMs <= startMs) {
        setError(QStringLiteral("Segment end must be after its start."));
        return;
    }
    for (auto it = patch.cbegin(); it != patch.cend(); ++it) segment.insert(it.key(), it.value());
    segment.insert(QStringLiteral("startMs"), startMs);
    segment.insert(QStringLiteral("endMs"), endMs);
    if (sourceTextChanged) {
        // Word timestamps are derived from the source transcript. Any source edit
        // invalidates the previous alignment and forces the next refinement pass
        // to treat this segment as an ASR/manual-timing fallback.
        segment.remove(QStringLiteral("words"));
        segment.remove(QStringLiteral("alignmentCoverage"));
        segment.remove(QStringLiteral("alignmentMatchScore"));
        segment.remove(QStringLiteral("alignmentModel"));
        segment.remove(QStringLiteral("alignmentRuntime"));
        segment.insert(QStringLiteral("timingSource"), QStringLiteral("asr"));
        segment.insert(QStringLiteral("alignmentStatus"), QStringLiteral("pending"));
        segment.remove(QStringLiteral("durationBudget"));
        segment.remove(QStringLiteral("durationUnits"));
        segment.remove(QStringLiteral("durationStatus"));
        segment.remove(QStringLiteral("phonemeDistance"));
        segment.remove(QStringLiteral("referenceTranslation"));
        segment.remove(QStringLiteral("targetChunks"));
        segment.remove(QStringLiteral("pauseAligned"));
        if (segment.value(QStringLiteral("fusionStatus")).toString()
                == QStringLiteral("conflict")
            || segment.value(QStringLiteral("fusionNeedsReview")).toBool()) {
            // A direct edit is an explicit reviewer decision. Preserve both
            // observations and any AI suggestion, but let this final text move
            // forward instead of leaving a stale, invisible review block.
            segment.insert(QStringLiteral("fusionChoice"), QStringLiteral("manual"));
            segment.insert(QStringLiteral("fusionStatus"), QStringLiteral("resolved"));
            segment.insert(QStringLiteral("fusionNeedsReview"), false);
            segment.insert(QStringLiteral("fusionResolutionPolicy"), QStringLiteral("manual"));
            segment.insert(QStringLiteral("state"), QStringLiteral("transcribed"));
        }
    }
    if (targetTextChanged || speakerChanged) {
        segment.insert(QStringLiteral("state"), QStringLiteral("stale"));
        if (targetTextChanged) {
            const QVariantMap budget = segment.value(QStringLiteral("durationBudget")).toMap();
            const int durationUnits = budget.isEmpty()
                ? -1
                : EspeakNgPhonemizer::count(
                      segment.value(QStringLiteral("targetText")).toString(),
                      m_project.targetLanguage);
            if (durationUnits >= 0) {
                const int minimum = budget.value(QStringLiteral("minUnits")).toInt();
                const int maximum = budget.value(QStringLiteral("maxUnits")).toInt();
                segment.insert(QStringLiteral("durationUnits"), durationUnits);
                segment.insert(QStringLiteral("phonemeDistance"),
                               qAbs(durationUnits
                                    - budget.value(QStringLiteral("targetUnits")).toInt()));
                segment.insert(QStringLiteral("durationStatus"),
                               durationUnits >= minimum && durationUnits <= maximum
                                   ? QStringLiteral("within-budget")
                                   : QStringLiteral("needs-review"));
            } else {
                segment.remove(QStringLiteral("durationUnits"));
                segment.remove(QStringLiteral("durationStatus"));
                segment.remove(QStringLiteral("phonemeDistance"));
            }
            segment.remove(QStringLiteral("durationPrompt"));
            segment.remove(QStringLiteral("targetChunks"));
            segment.remove(QStringLiteral("referenceTranslation"));
            segment.remove(QStringLiteral("pauseAligned"));
            segment.remove(QStringLiteral("candidateSelectionMetric"));
        }
    }
    m_project.segments[index] = segment;
    if (timingChanged) {
        m_timingResolutionPreview.clear();
        m_timingUndoSegments.clear();
        invalidateTimingOutputs();
        emit timingResolutionChanged();
    }
    emit segmentsChanged();
    emit workflowChanged();
    persistAfterEdit();
}

void DubbingController::removeSegment(int index)
{
    if (index < 0 || index >= m_project.segments.size()) return;
    m_project.segments.removeAt(index);
    m_timingResolutionPreview.clear();
    m_timingUndoSegments.clear();
    invalidateTimingOutputs();
    emit segmentsChanged();
    emit timingResolutionChanged();
    emit workflowChanged();
    persistAfterEdit();
}

void DubbingController::addSpeaker(const QString &name)
{
    QVariantMap speaker;
    speaker.insert(QStringLiteral("id"), QStringLiteral("speaker-%1").arg(m_project.speakers.size() + 1));
    speaker.insert(QStringLiteral("name"), name.trimmed().isEmpty()
                   ? QStringLiteral("Speaker %1").arg(m_project.speakers.size() + 1) : name.trimmed());
    speaker.insert(QStringLiteral("voice"), QVariantMap());
    m_project.speakers.append(speaker);
    emit projectChanged();
    emit workflowChanged();
    persistAfterEdit();
}

void DubbingController::setSpeakerVoice(int speakerIndex, const QVariantMap &voice)
{
    if (speakerIndex < 0 || speakerIndex >= m_project.speakers.size()) return;
    QVariantMap speaker = m_project.speakers.at(speakerIndex).toMap();
    speaker.insert(QStringLiteral("voice"), voice);
    m_project.speakers[speakerIndex] = speaker;
    emit projectChanged();
    emit workflowChanged();
    persistAfterEdit();
}

void DubbingController::clearError()
{
    if (m_translationFix) m_translationFix->clearError();
    m_runner->clearError();
}

void DubbingController::setError(const QString &message)
{
    m_runner->setError(message);
}

void DubbingController::setBusyError(const QString &message)
{
    // A rejected user action must never turn a valid in-flight worker into a
    // failed job. Keep the visible diagnostic without changing the stage,
    // progress, or cancellation state of that worker.
    m_runner->setBusyError(message);
}

void DubbingController::persistAfterEdit()
{
    if (!m_project.projectPath.isEmpty()) saveProject();
}

void DubbingController::invalidateTimingOutputs()
{
    // A ripple changes every downstream timestamp.  Keep the existing assets
    // on disk for recovery, but make neither preview nor export appear current.
    m_stepOutputs.remove(QStringLiteral("mix"));
    m_stepOutputs.remove(QStringLiteral("export"));
    m_pendingExportPath.clear();
    if (m_runner) {
        m_runner->setPreviewPath(QString());
        m_runner->setExportPath(QString());
    }
    emit previewChanged();
    emit exportChanged();
}

} // namespace LAStudio
