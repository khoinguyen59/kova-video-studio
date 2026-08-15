#include "controllers/dubbing/DubbingJobRunner.h"
#include "controllers/dubbing/DubbingColabModelRoutes.h"
#include "dubbing/AudioTimelineMixer.h"
#include "controllers/dubbing/SourceSeparationConfigurationResolver.h"
#include "controllers/dubbing/DubbingTranscriptionJob.h"
#include "controllers/dubbing/DubbingSynthesisJob.h"
#include "controllers/dubbing/DubbingExportJob.h"
#include "controllers/dubbing/DubbingTranslationJob.h"
#include "controllers/dubbing/DubbingTranslationFixService.h"
#include "controllers/subtitles/SubtitleOcrController.h"
#include "audio/WavIO.h"
#include "translation/TranslationEngine.h"
#include "dubbing/DubbingTimingService.h"
#include "dubbing/DubbingTranscriptFusionService.h"

#include "SttSessionController.h"
#include "tts/TtsEngine.h"
#include "dubbing/media/MediaIngestService.h"
#include "separation/SourceSeparationService.h"
#include "separation/ColabSeparationRunner.h"
#include "separation/SeparationTypes.h"
#include "core/PathUtils.h"
#include "core/Logger.h"
#include "core/ModelManager.h"
#include "core/RuntimeManager.h"
#include "remote/ColabSession.h"
#include "remote/ExecutionProvider.h"

#include <QFileInfo>
#include <QDir>
#include <QMetaObject>
#include <QtConcurrent>
#include <QRegularExpression>

namespace LAStudio {

DubbingJobRunner::DubbingJobRunner(SttSessionController *sttSession, TtsEngine *tts,
                                   ModelManager *models, RuntimeManager *runtimes, QObject *parent)
    : DubbingJobRunner(sttSession, tts, nullptr, models, runtimes, parent)
{
}

namespace {
QString protectedTokensFor(const QString &text)
{
    QStringList tokens;
    const QRegularExpression re(QStringLiteral("(?:https?://\\S+|\\b\\d[\\d.,/%-]*|\\b[A-Z]{2,}\\b)"));
    auto match = re.globalMatch(text);
    while (match.hasNext()) tokens.append(match.next().captured(0));
    tokens.removeDuplicates();
    return tokens.join(QStringLiteral(", "));
}

QVariantMap transcriptParametersFor(const QVariantMap &configuration)
{
    const QVariantMap nested = configuration.value(QStringLiteral("parameters")).toMap();
    return nested.isEmpty() ? configuration : nested;
}

QString transcriptSourceModeFor(const QVariantMap &parameters)
{
    const QString mode = parameters.value(QStringLiteral("transcriptSource"),
                                           QStringLiteral("stt")).toString().trimmed().toLower();
    // `stt+ocr` was the old coupled mode. Keep old projects readable, but do
    // not start two workers from one click: reconciliation is now explicit
    // after each independently produced source transcript is available.
    if (mode == QStringLiteral("ocr")) return QStringLiteral("ocr");
    if (mode == QStringLiteral("reconcile") || mode == QStringLiteral("stt+ocr"))
        return QStringLiteral("reconcile");
    return QStringLiteral("stt");
}

QString readableTransferSize(qint64 bytes)
{
    if (bytes < 0) return QStringLiteral("unknown size");
    return QStringLiteral("%1 MiB").arg(QString::number(
        static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1));
}

QString artifactLabel(const QString &artifact)
{
    return artifact == QStringLiteral("background") ? QStringLiteral("Background")
                                                     : QStringLiteral("Vocals");
}

}

DubbingJobRunner::DubbingJobRunner(SttSessionController *sttSession, TtsEngine *tts,
                                   TranslationEngine *translation,
                                   ModelManager *models, RuntimeManager *runtimes, QObject *parent)
    : QObject(parent), m_sttSession(sttSession), m_tts(tts), m_translation(translation), m_models(models), m_runtimes(runtimes)
{
    m_timingWatcher = new QFutureWatcher<QVariantList>(this);
    connect(m_timingWatcher, &QFutureWatcher<QVariantList>::finished,
            this, &DubbingJobRunner::onTimingFinished);
    m_mediaIngest = new MediaIngestService(this);
    connect(m_mediaIngest, &MediaIngestService::finished,
            this, &DubbingJobRunner::onIngestFinished);
    connect(m_mediaIngest, &MediaIngestService::progress, this, [this](int percent) {
        if (m_run.processing() && m_run.stageId() == DubbingStage::Import) {
            m_run.setProgress(percent);
            emit stateChanged();
        }
    });
    m_sourceSeparation = new SourceSeparationService(this);
    connect(m_sourceSeparation, &SourceSeparationService::finished, this, &DubbingJobRunner::onSourceSeparationFinished);
    qRegisterMetaType<ColabSeparationRequest>("ColabSeparationRequest");
    qRegisterMetaType<ColabSeparationResult>("ColabSeparationResult");
    m_colabSeparationRunner = new ColabSeparationRunner;
    m_colabSeparationRunner->moveToThread(&m_colabSeparationThread);
    connect(&m_colabSeparationThread, &QThread::finished,
            m_colabSeparationRunner, &QObject::deleteLater);
    connect(m_colabSeparationRunner, &ColabSeparationRunner::progress, this,
            [this](int progress) {
        if (m_run.processing() && m_run.stageId() == DubbingStage::SourceSeparation) {
            m_run.setProgress(qBound(0, progress, 99));
            emit stateChanged();
        }
    });
    connect(m_colabSeparationRunner, &ColabSeparationRunner::phaseChanged, this,
            [this](const QString &phase) {
        if (!m_run.processing() || m_run.stageId() != DubbingStage::SourceSeparation) return;
        m_colabSeparationActivityStatus = phase;
        if (!phase.startsWith(QStringLiteral("Requesting "))
            && !phase.startsWith(QStringLiteral("Colab created both "))) {
            m_colabSeparationTransferActive = false;
        }
        emit stateChanged();
    });
    connect(m_colabSeparationRunner, &ColabSeparationRunner::artifactTransferProgress, this,
            [this](const QString &artifact, qint64 receivedBytes, qint64 totalBytes) {
        if (!m_run.processing() || m_run.stageId() != DubbingStage::SourceSeparation) return;
        m_colabSeparationArtifact = artifact;
        m_colabSeparationReceivedBytes = qMax<qint64>(0, receivedBytes);
        m_colabSeparationTotalBytes = totalBytes;
        m_colabSeparationTransferActive = true;
        const QString received = readableTransferSize(m_colabSeparationReceivedBytes);
        m_colabSeparationActivityStatus = totalBytes > 0
            ? QStringLiteral("Downloading %1: %2 / %3 from Direct Colab")
                  .arg(artifactLabel(artifact), received, readableTransferSize(totalBytes))
            : QStringLiteral("Downloading %1: %2 received from Direct Colab")
                  .arg(artifactLabel(artifact), received);
        emit stateChanged();
    });
    connect(m_colabSeparationRunner, &ColabSeparationRunner::finished, this,
            [this](const ColabSeparationResult &result) {
        if (!m_run.processing() || m_run.stageId() != DubbingStage::SourceSeparation) return;
        m_colabSeparationCancellation.reset();
        m_pendingSourceAudioPath.clear();
        m_colabSeparationActivityStatus.clear();
        m_colabSeparationTransferActive = false;
        if (result.vocalsPath.trimmed().isEmpty() || result.backgroundPath.trimmed().isEmpty()
            || !QFileInfo(result.vocalsPath).isFile() || !QFileInfo(result.backgroundPath).isFile()) {
            setError(QStringLiteral("Colab voice separation returned an incomplete stem set. The original audio was not used as a substitute."));
            return;
        }
        setProcessing(false, QStringLiteral("separated"), 100);
        const QVariantMap outputs{{QStringLiteral("vocals"), result.vocalsPath},
                                  {QStringLiteral("background"), result.backgroundPath},
                                  {QStringLiteral("sourceSeparation"), QStringLiteral("colab-direct")},
                                  {QStringLiteral("remoteJobId"), result.jobId}};
        emit sourceSeparationFinished(outputs);
        emit stageCompleted(QStringLiteral("source-separate"), outputs);
    });
    connect(m_colabSeparationRunner, &ColabSeparationRunner::failed, this,
            [this](const QString &message) {
        if (!m_run.processing() || m_run.stageId() != DubbingStage::SourceSeparation) return;
        m_colabSeparationCancellation.reset();
        m_pendingSourceAudioPath.clear();
        m_colabSeparationActivityStatus.clear();
        m_colabSeparationTransferActive = false;
        setError(message);
    });
    m_colabSeparationThread.start();

    m_transcriptionJob = new DubbingTranscriptionJob(m_sttSession, m_models, m_runtimes, this);
    connect(m_transcriptionJob, &DubbingTranscriptionJob::progressChanged, this, [this](int progress) {
        if (!m_run.processing() || m_run.stageId() != DubbingStage::Transcription) return;
        m_run.setProgress(progress);
        emit stateChanged();
    });
    connect(m_transcriptionJob, &DubbingTranscriptionJob::failed, this,
            [this](const QString &message) {
        if (!m_run.processing() || m_run.stageId() != DubbingStage::Transcription) return;
        m_sttTranscriptActive = false;
        failTranscriptSource(QStringLiteral("STT"), message);
    });
    connect(m_transcriptionJob, &DubbingTranscriptionJob::completed, this,
            [this](const QVariantList &segments) {
        if (!m_run.processing() || m_run.stageId() != DubbingStage::Transcription) return;
        m_sttTranscriptActive = false;
        if (segments.isEmpty()) {
            failTranscriptSource(QStringLiteral("STT"),
                                 QStringLiteral("STT completed without usable transcript segments."));
            return;
        }
        finishTranscript(segments);
    });

    m_synthesisJob = new DubbingSynthesisJob(m_tts, this);
    connect(m_synthesisJob, &DubbingSynthesisJob::progressChanged, this, [this](int progress) {
        if (!m_run.processing() || m_run.stageId() != DubbingStage::Tts) return;
        m_run.setProgress(progress);
        emit stateChanged();
    });
    connect(m_synthesisJob, &DubbingSynthesisJob::segmentUpdated,
            this, &DubbingJobRunner::segmentUpdated);
    connect(m_synthesisJob, &DubbingSynthesisJob::failed,
            this, &DubbingJobRunner::setError);
    connect(m_synthesisJob, &DubbingSynthesisJob::completed, this,
            [this](const QVariantList &segments) {
        if (!m_run.processing() || m_run.stageId() != DubbingStage::Tts) return;
        m_activeSegments = segments;
        setProcessing(false, QStringLiteral("ready"), 100);
        emit segmentsUpdated(m_activeSegments);
        emit stageCompleted(QStringLiteral("synthesize"), {{QStringLiteral("timeline"), m_activeSegments}});
    });

    m_exportJob = new DubbingExportJob(this);
    connect(m_exportJob, &DubbingExportJob::progressChanged, this,
            [this](const QString &stage, int progress) {
        if (!m_run.processing() || (stage == QStringLiteral("mix") && m_run.stageId() != DubbingStage::Mix)
            || (stage == QStringLiteral("export") && m_run.stageId() != DubbingStage::Export)) return;
        m_run.setProgress(progress);
        emit stateChanged();
    });
    connect(m_exportJob, &DubbingExportJob::failed,
            this, &DubbingJobRunner::setError);
    connect(m_exportJob, &DubbingExportJob::previewReady, this, [this](const QString &path) {
        m_previewPath = path;
        m_dubbedVocalPath = AudioTimelineMixer::vocalStemPath(path);
        setProcessing(false, QStringLiteral("mixed"), 100);
        emit stageCompleted(QStringLiteral("mix"),
                            {{QStringLiteral("audio"), path},
                             {QStringLiteral("vocals"), m_dubbedVocalPath}});
    });
    connect(m_exportJob, &DubbingExportJob::exported, this, [this](const QString &path) {
        m_exportPath = path;
        setProcessing(false, QStringLiteral("exported"), 100);
        emit stageCompleted(QStringLiteral("export"), {{QStringLiteral("media"), path}});
    });

    m_translationJob = new DubbingTranslationJob(m_translation, m_models, m_runtimes, m_tts, this);
    m_autoTranslationFix = new DubbingTranslationFixService(this);
    connect(m_autoTranslationFix, &DubbingTranslationFixService::stateChanged, this, [this]() {
        if (!m_run.processing() || m_run.stageId() != DubbingStage::Translation
            || !m_autoTranslationFix->busy()) return;
        m_run.setProgress(70 + qRound(m_autoTranslationFix->progress() * 0.29));
        emit stateChanged();
    });
    connect(m_autoTranslationFix, &DubbingTranslationFixService::completed, this,
            [this](const QVariantList &segments, int, int) { finishTranslation(segments); });
    connect(m_autoTranslationFix, &DubbingTranslationFixService::failed,
            this, [this](const QString &message) {
        if (m_run.processing() && m_run.stageId() == DubbingStage::Translation) {
            Logger::warning(
                QStringLiteral("DubbingPipeline"),
                QStringLiteral("[translation] LLM shortening unavailable; keeping faithful translations for review: %1")
                    .arg(message));
            finishTranslation(m_activeSegments);
            return;
        }
        setError(message);
    });
    connect(m_translationJob, &DubbingTranslationJob::progressChanged, this, [this](int progress) {
        if (!m_run.processing() || m_run.stageId() != DubbingStage::Translation) return;
        m_run.setProgress(progress);
        if (progress == 0 || progress == 100 || progress % 10 == 0) {
            Logger::debug(QStringLiteral("DubbingPipeline"),
                          QStringLiteral("[translation] progress run=%1 value=%2")
                              .arg(m_run.runId()).arg(progress));
        }
        emit stateChanged();
    });
    connect(m_translationJob, &DubbingTranslationJob::failed,
            this, &DubbingJobRunner::setError);
    connect(m_translationJob, &DubbingTranslationJob::completed, this,
            [this](const QVariantList &segments) {
        if (!m_run.processing() || m_run.stageId() != DubbingStage::Translation) return;
        int translated = 0;
        for (const QVariant &value : segments) {
            if (!value.toMap().value(QStringLiteral("targetText")).toString().trimmed().isEmpty())
                ++translated;
        }
        Logger::info(QStringLiteral("DubbingPipeline"),
                     QStringLiteral("[translation] completed run=%1 segments=%2 translated=%3 elapsedMs=%4")
                         .arg(m_run.runId()).arg(segments.size()).arg(translated)
                         .arg(m_run.elapsedMs()));
        m_activeSegments = segments;
        QVariantMap parameters = m_translationConfiguration.value(QStringLiteral("parameters")).toMap();
        if (parameters.isEmpty()) parameters = m_translationConfiguration;
        const QVariantMap durationControl = parameters.value(QStringLiteral("durationControl")).toMap();
        const bool autoRewrite = durationControl.value(QStringLiteral("autoRewrite"), true).toBool();
        const int candidates = DubbingTranslationFixService::eligibleSegmentCount(
            segments, m_translationTargetLanguage);
        QVariantMap fixConfiguration = m_translationFixConfiguration.isEmpty()
            ? (m_autoTranslationFix ? m_autoTranslationFix->configuration() : QVariantMap())
            : m_translationFixConfiguration;
        if (autoRewrite && candidates > 0 && m_autoTranslationFix
            && fixConfiguration.value(QStringLiteral("provider")).toString()
                   != QStringLiteral("local")) {
            Logger::info(QStringLiteral("DubbingPipeline"),
                         QStringLiteral("[translation] automatically shortening %1 overlong segment(s) with the configured LLM")
                             .arg(candidates));
            m_run.setProgress(70);
            emit stateChanged();
            fixConfiguration.insert(QStringLiteral("maxAttempts"),
                                    durationControl.value(QStringLiteral("maxPreTtsIterations"), 4));
            if (m_autoTranslationFix->start(m_translationSourceLanguage,
                                            m_translationTargetLanguage,
                                            segments, fixConfiguration))
                return;
        }
        finishTranslation(segments);
    });
}

void DubbingJobRunner::setTranslationFixConfiguration(const QVariantMap &configuration)
{
    m_translationFixConfiguration = configuration;
}

void DubbingJobRunner::setSubtitleOcrController(SubtitleOcrController *controller)
{
    for (const QMetaObject::Connection &connection : std::as_const(m_subtitleOcrConnections))
        QObject::disconnect(connection);
    m_subtitleOcrConnections.clear();
    m_subtitleOcr = controller;
    if (!m_subtitleOcr) return;

    m_subtitleOcrConnections = {
        connect(m_subtitleOcr, &SubtitleOcrController::sourceChanged,
                this, &DubbingJobRunner::onSubtitleOcrSourceChanged),
        connect(m_subtitleOcr, &SubtitleOcrController::segmentsChanged,
                this, &DubbingJobRunner::onSubtitleOcrSegmentsChanged),
        connect(m_subtitleOcr, &SubtitleOcrController::errorChanged,
                this, &DubbingJobRunner::onSubtitleOcrErrorChanged),
        connect(m_subtitleOcr, &SubtitleOcrController::progressChanged,
                this, &DubbingJobRunner::onSubtitleOcrProgressChanged)
    };
}

void DubbingJobRunner::finishTranscript(const QVariantList &segments)
{
    if (!m_run.processing() || m_run.stageId() != DubbingStage::Transcription) return;
    m_activeSegments = segments;
    setProcessing(false, QStringLiteral("transcribed"), 100);
    emit segmentsUpdated(segments);
    emit stageCompleted(QStringLiteral("transcribe"),
                        {{QStringLiteral("transcript"), segments},
                         {QStringLiteral("transcriptSource"), m_transcriptSourceMode}});
}

void DubbingJobRunner::failTranscriptSource(const QString &source, const QString &message)
{
    setError(QStringLiteral("%1 transcript failed: %2").arg(source, message));
}

void DubbingJobRunner::startOcrTranscript(const QVariantMap &parameters)
{
    if (!m_subtitleOcr) {
        failTranscriptSource(QStringLiteral("OCR"),
                             QStringLiteral("Subtitle OCR controller is unavailable."));
        return;
    }
    m_ocrTranscriptSourcePath = parameters.value(QStringLiteral("ocrSourceMedia")).toString().trimmed();
    if (m_ocrTranscriptSourcePath.isEmpty()) {
        failTranscriptSource(QStringLiteral("OCR"),
                             QStringLiteral("Import source video before running the OCR transcript route."));
        return;
    }
    if (parameters.contains(QStringLiteral("ocrLanguage"))
        && !m_subtitleOcr->setOcrLanguage(parameters.value(QStringLiteral("ocrLanguage")).toString())) {
        failTranscriptSource(QStringLiteral("OCR"), m_subtitleOcr->error());
        return;
    }
    if (parameters.contains(QStringLiteral("ocrLocalEngineId"))
        && !m_subtitleOcr->setLocalEngine(
            parameters.value(QStringLiteral("ocrLocalEngineId")).toString())) {
        failTranscriptSource(QStringLiteral("OCR"), m_subtitleOcr->error());
        return;
    }
    if (parameters.contains(QStringLiteral("ocrExecutionRoute"))
        && !m_subtitleOcr->setExecutionRoute(
            parameters.value(QStringLiteral("ocrExecutionRoute")).toString())) {
        failTranscriptSource(QStringLiteral("OCR"), m_subtitleOcr->error());
        return;
    }
    if (parameters.contains(QStringLiteral("ocrColabModelId"))
        && !m_subtitleOcr->setColabModelId(
            parameters.value(QStringLiteral("ocrColabModelId")).toString())) {
        failTranscriptSource(QStringLiteral("OCR"), m_subtitleOcr->error());
        return;
    }
    if (parameters.contains(QStringLiteral("ocrSampleIntervalMs"))
        && !m_subtitleOcr->setSampleIntervalMs(parameters.value(QStringLiteral("ocrSampleIntervalMs")).toLongLong())) {
        failTranscriptSource(QStringLiteral("OCR"), m_subtitleOcr->error());
        return;
    }
    if (parameters.contains(QStringLiteral("ocrMinimumConfidence"))
        && !m_subtitleOcr->setMinimumConfidence(parameters.value(QStringLiteral("ocrMinimumConfidence")).toDouble())) {
        failTranscriptSource(QStringLiteral("OCR"), m_subtitleOcr->error());
        return;
    }
    const QVariantMap roi = parameters.value(QStringLiteral("ocrRoi")).toMap();
    if (!roi.isEmpty() && !m_subtitleOcr->setRoi(roi.value(QStringLiteral("x")).toDouble(),
                                                 roi.value(QStringLiteral("y")).toDouble(),
                                                 roi.value(QStringLiteral("width")).toDouble(),
                                                 roi.value(QStringLiteral("height")).toDouble())) {
        failTranscriptSource(QStringLiteral("OCR"), m_subtitleOcr->error());
        return;
    }
    m_ocrTranscriptActive = true;
    m_ocrTranscriptLoadingSource = true;
    if (!m_subtitleOcr->loadSource(m_ocrTranscriptSourcePath)) {
        m_ocrTranscriptActive = false;
        m_ocrTranscriptLoadingSource = false;
        failTranscriptSource(QStringLiteral("OCR"), m_subtitleOcr->error());
    }
}

void DubbingJobRunner::onSubtitleOcrSourceChanged()
{
    if (!m_ocrTranscriptActive || !m_ocrTranscriptLoadingSource || !m_subtitleOcr
        || !m_run.processing() || m_run.stageId() != DubbingStage::Transcription) return;
    if (QFileInfo(m_subtitleOcr->sourcePath()).absoluteFilePath()
        != QFileInfo(m_ocrTranscriptSourcePath).absoluteFilePath()) return;
    m_ocrTranscriptLoadingSource = false;
    if (!m_subtitleOcr->run()) {
        m_ocrTranscriptActive = false;
        failTranscriptSource(QStringLiteral("OCR"), m_subtitleOcr->error());
    }
}

void DubbingJobRunner::onSubtitleOcrSegmentsChanged()
{
    if (!m_ocrTranscriptActive || !m_subtitleOcr || m_subtitleOcr->processing()
        || m_subtitleOcr->phase() != QStringLiteral("completed")
        || !m_run.processing() || m_run.stageId() != DubbingStage::Transcription) return;
    m_ocrTranscriptActive = false;
    m_ocrTranscriptSegments = DubbingTranscriptFusionService::normalizeOcrSegments(m_subtitleOcr->segments());
    if (m_ocrTranscriptSegments.isEmpty()) {
        failTranscriptSource(QStringLiteral("OCR"),
                             QStringLiteral("OCR completed without usable subtitle segments."));
        return;
    }
    finishTranscript(m_ocrTranscriptSegments);
}

void DubbingJobRunner::onSubtitleOcrErrorChanged()
{
    if (!m_ocrTranscriptActive || !m_subtitleOcr || m_subtitleOcr->error().trimmed().isEmpty()
        || !m_run.processing() || m_run.stageId() != DubbingStage::Transcription) return;
    m_ocrTranscriptActive = false;
    m_ocrTranscriptLoadingSource = false;
    failTranscriptSource(QStringLiteral("OCR"), m_subtitleOcr->error());
}

void DubbingJobRunner::onSubtitleOcrProgressChanged()
{
    if (!m_ocrTranscriptActive || !m_subtitleOcr || !m_run.processing()
        || m_run.stageId() != DubbingStage::Transcription || !m_subtitleOcr->progressAvailable()) return;
    // This is a worker-reported percentage, not a synthetic workflow weight.
    m_run.setProgress(m_subtitleOcr->progress());
    emit stateChanged();
}

void DubbingJobRunner::setRemoteServices(Settings *settings, ColabSession *translationSession,
                                         ColabSession *ttsSession, ColabSession *voiceCloneSession,
                                         ColabSession *separationSession,
                                         ColabSession *alignmentSession,
                                         ColabSession *chatSession)
{
    if (m_translationJob) m_translationJob->setRemoteServices(settings, translationSession);
    if (m_synthesisJob) m_synthesisJob->setRemoteServices(settings, ttsSession, voiceCloneSession);
    if (m_transcriptionJob) m_transcriptionJob->setAlignmentSession(alignmentSession);
    if (m_autoTranslationFix) m_autoTranslationFix->setDirectColabSession(chatSession);
    QObject::disconnect(m_colabSeparationSessionConnection);
    m_colabSeparationSession = separationSession;
    if (m_colabSeparationSession) {
        m_colabSeparationSessionConnection = connect(
            m_colabSeparationSession, &ColabSession::sessionChanged, this, [this]() {
            if (!m_run.processing() || m_run.stageId() != DubbingStage::SourceSeparation) return;
            if (m_colabSeparationCancellation)
                m_colabSeparationCancellation->store(true, std::memory_order_relaxed);
            if (m_colabSeparationRunner)
                QMetaObject::invokeMethod(m_colabSeparationRunner, "cancel", Qt::QueuedConnection);
            setError(QStringLiteral("Colab Voice Isolation worker session changed during dubbing. Pair the selected model again, then rerun the Separate node."));
        });
    }
}

void DubbingJobRunner::finishTranslation(const QVariantList &segments)
{
    if (!m_run.processing() || m_run.stageId() != DubbingStage::Translation) return;
    m_activeSegments = segments;
    setProcessing(false, QStringLiteral("translated"), 100);
    emit segmentsUpdated(segments);
    emit stageCompleted(QStringLiteral("translate"), {{QStringLiteral("transcript"), segments}});
}

DubbingJobRunner::~DubbingJobRunner()
{
    cancel();
    if (m_colabSeparationCancellation)
        m_colabSeparationCancellation->store(true, std::memory_order_relaxed);
    if (m_colabSeparationRunner && m_colabSeparationThread.isRunning())
        QMetaObject::invokeMethod(m_colabSeparationRunner, "cancel", Qt::QueuedConnection);
    m_colabSeparationThread.quit();
    m_colabSeparationThread.wait();
    if (m_timingWatcher) {
        if (m_timingCancel) m_timingCancel->storeRelease(true);
        m_timingWatcher->cancel();
        m_timingWatcher->waitForFinished();
    }
}

void DubbingJobRunner::startIngest(const QString &path)
{
    if (m_run.processing()) {
        setBusyError(QStringLiteral("A dubbing operation is already running."));
        return;
    }
    m_run.ensureRun();
    m_run.beginNode();
    Logger::info(QStringLiteral("DubbingPipeline"),
                 QStringLiteral("[ingest] start run=%1 node=%2 input=%3")
                     .arg(m_run.runId(), m_run.nodeRunId(), path));
    setProcessing(true, QStringLiteral("import"), 0);
    m_mediaIngest->ingest(path);
}

void DubbingJobRunner::startSourceSeparation(const QString &audioPath,
                                             const QVariantMap &modelConfiguration)
{
    if (m_run.processing()) {
        setBusyError(QStringLiteral("A dubbing operation is already running."));
        return;
    }
    m_colabSeparationActivityStatus.clear();
    m_colabSeparationArtifact.clear();
    m_colabSeparationReceivedBytes = 0;
    m_colabSeparationTotalBytes = -1;
    m_colabSeparationTransferActive = false;
    if (audioPath.isEmpty() || !QFileInfo::exists(audioPath)) {
        setError(QStringLiteral("Normalize the source media before separating speech."));
        return;
    }

    Logger::info(QStringLiteral("DubbingPipeline"),
                 QStringLiteral("[source-separate] requested audio=%1 size=%2 bytes")
                     .arg(audioPath).arg(QFileInfo(audioPath).size()));

    const QVariantMap parameters = modelConfiguration.value(QStringLiteral("parameters")).toMap();
    const QString providerId = modelConfiguration.value(
        QStringLiteral("executionProvider"), parameters.value(
        QStringLiteral("executionProvider"), QStringLiteral("local-dev"))).toString().trimmed().toLower();
    ExecutionProvider provider = ExecutionProvider::LocalDev;
    if (!executionProviderFromId(providerId, &provider)) {
        setError(QStringLiteral("Unknown source-separation provider: %1").arg(providerId));
        return;
    }
    if (provider == ExecutionProvider::ApiGateway) {
        setError(QStringLiteral("Source separation is not available through API Gateway. Select Local Dev or Colab GPU."));
        return;
    }
    if (provider == ExecutionProvider::ColabDirect) {
        if (!m_colabSeparationRunner) {
            setError(QStringLiteral("Colab source-separation runner is unavailable."));
            return;
        }
        if (!m_colabSeparationSession) {
            setError(QStringLiteral("Connect a Colab GPU worker before running this Voice Isolation node."));
            return;
        }
        const QString model = modelConfiguration.value(
            QStringLiteral("modelId"), parameters.value(
            QStringLiteral("modelId"))).toString().trimmed().toLower();
        if (!DubbingColabModelRoutes::supports(QStringLiteral("source-separate"), model)) {
            setError(QStringLiteral("Select an exact Colab voice-isolation model before running this node."));
            return;
        }
        QString routeError;
        if (!m_colabSeparationSession->hasVerifiedRoute(
                QStringLiteral("voice-isolation"), model, &routeError)) {
            setError(routeError);
            return;
        }
        m_run.ensureRun();
        m_run.beginNode();
        m_pendingSourceAudioPath = audioPath;
        m_colabSeparationActivityStatus = QStringLiteral("Uploading source audio to Direct Colab");
        setProcessing(true, QStringLiteral("source-separation"), 0);
        m_colabSeparationCancellation = std::make_shared<std::atomic_bool>(false);
        ColabSeparationRequest request;
        request.workerUrl = m_colabSeparationSession->endpoint();
        request.bearerToken = m_colabSeparationSession->bearerTokenForRequest();
        request.audioPath = audioPath;
        request.outputRoot = QDir(PathUtils::cacheDir()).filePath(
            QStringLiteral("dubbing/colab-separation/%1/%2")
                .arg(m_run.runId(), m_run.nodeRunId()));
        request.model = model;
        request.artifactFormat = parameters.value(QStringLiteral("artifactTransferFormat"),
                                                   QStringLiteral("flac")).toString();
        request.cancellation = InferenceCancellationToken(m_colabSeparationCancellation);
        Logger::info(QStringLiteral("DubbingPipeline"),
                     QStringLiteral("[source-separate] direct Colab run=%1 node=%2 model=%3 transfer=%4")
                         .arg(m_run.runId(), m_run.nodeRunId(), request.model, request.artifactFormat));
        QMetaObject::invokeMethod(m_colabSeparationRunner, "separate", Qt::QueuedConnection,
                                  Q_ARG(ColabSeparationRequest, request));
        return;
    }

    const SourceSeparationConfigurationResult resolved =
        SourceSeparationConfigurationResolver(m_models, m_runtimes).resolve(modelConfiguration);
    if (!resolved.error.isEmpty()) {
        setError(resolved.error);
        return;
    }
    if (!resolved.available) {
        setError(QStringLiteral("Voice isolation runtime or model is unavailable. Install/configure it or connect an exact Colab GPU worker; normalized source audio will not be used as a substitute."));
        return;
    }
    const SeparationConfiguration config = resolved.configuration;

    m_run.ensureRun();
    m_run.beginNode();
    m_pendingSourceAudioPath = audioPath;
    Logger::info(QStringLiteral("DubbingPipeline"),
                 QStringLiteral("[source-separate] runtime=%1 family=%2")
                     .arg(config.runtimePath, config.familyId));
    setProcessing(true, QStringLiteral("source-separation"), 0);

    SeparationRequest request;
    request.sourcePath = audioPath;
    request.outputRoot = PathUtils::cacheDir() + QStringLiteral("/dubbing/source-separation/");
    request.configuration = config;
    request.numThreads = 4;
    QString error;
    if (!m_sourceSeparation->isolate(request, &error)) {
        m_pendingSourceAudioPath.clear();
        setError(error);
        Logger::error(QStringLiteral("DubbingPipeline"),
                      QStringLiteral("[source-separate] failed to start: %1").arg(error));
    }
}

void DubbingJobRunner::startTranscription(const QString &sourceLanguage,
                                          const QString &sourceMediaPath,
                                          const QString &fallbackAudioPath,
                                          const QVariantMap &modelConfiguration)
{
    const QVariantMap parameters = transcriptParametersFor(modelConfiguration);
    const QString sourceMode = transcriptSourceModeFor(parameters);
    if (m_run.processing() || (m_transcriptionJob && m_transcriptionJob->running())
        || m_ocrTranscriptActive) {
        setBusyError(QStringLiteral("Speech transcription is already running."));
        return;
    }
    if (sourceMode == QStringLiteral("reconcile")) {
        setError(QStringLiteral("Reconcile STT + OCR is a local review step. Run STT and OCR independently, then reconcile their completed results."));
        return;
    }
    if (sourceMode == QStringLiteral("stt")
        && sourceMediaPath.trimmed().isEmpty()) {
        setError(QStringLiteral("STT transcript failed: normalize the source audio before running STT."));
        return;
    }
    m_run.ensureRun();
    m_run.beginNode();
    m_transcriptSourceMode = sourceMode;
    m_transcriptParameters = parameters;
    m_sttTranscriptSegments.clear();
    m_ocrTranscriptSegments.clear();
    m_sttTranscriptActive = sourceMode != QStringLiteral("ocr");
    m_ocrTranscriptActive = false;
    m_ocrTranscriptLoadingSource = false;
    Logger::info(QStringLiteral("DubbingPipeline"),
                 QStringLiteral("[transcription] start run=%1 node=%2 source=%3 provider=%4 language=%5 audio=%6 size=%7 bytes")
                     .arg(m_run.runId(), m_run.nodeRunId())
                     .arg(sourceMode,
                          parameters.value(QStringLiteral("executionProvider"),
                                                    QStringLiteral("local-dev")).toString(),
                          sourceLanguage, sourceMediaPath)
                     .arg(QFileInfo(sourceMediaPath).size()));
    setProcessing(true, QStringLiteral("transcription"), 0);
    if (sourceMode != QStringLiteral("ocr")) {
        if (!m_transcriptionJob || !m_transcriptionJob->start(sourceLanguage, sourceMediaPath,
                                                              fallbackAudioPath, modelConfiguration)) {
            // DubbingTranscriptionJob can emit its precise readiness error
            // synchronously before returning false. Do not overwrite that
            // diagnostic with a generic orchestration error.
            if (m_run.processing()) {
                m_sttTranscriptActive = false;
                failTranscriptSource(QStringLiteral("STT"),
                                     QStringLiteral("The STT route could not be started."));
            }
            return;
        }
    }
    if (sourceMode == QStringLiteral("ocr")) startOcrTranscript(parameters);
}

void DubbingJobRunner::startTranslation(const QString &sourceLanguage, const QString &targetLanguage, const QVariantList &segments,
                                         const QVariantMap &modelConfiguration)
{
    if (m_run.processing() || (m_translationJob && m_translationJob->running())) {
        Logger::warning(QStringLiteral("DubbingPipeline"),
                        QStringLiteral("[translation] start rejected: already running stage=%1 progress=%2")
                            .arg(m_run.stageName()).arg(m_run.progress()));
        setBusyError(QStringLiteral("A translation request is already running."));
        return;
    }
    m_run.ensureRun();
    m_run.beginNode();
    m_translationSourceLanguage = sourceLanguage;
    m_translationTargetLanguage = targetLanguage;
    m_translationConfiguration = modelConfiguration;
    Logger::info(QStringLiteral("DubbingPipeline"),
                 QStringLiteral("[translation] start run=%1 node=%2 source=%3 target=%4 segments=%5 family=%6 runtime=%7")
                     .arg(m_run.runId(), m_run.nodeRunId(), sourceLanguage, targetLanguage)
                     .arg(segments.size())
                     .arg(modelConfiguration.value(QStringLiteral("familyId")).toString(),
                          modelConfiguration.value(QStringLiteral("runtimeId")).toString()));
    setProcessing(true, QStringLiteral("translation"), 0);
    if (!m_translationJob) {
        setError(QStringLiteral("Translation job is unavailable."));
        return;
    }
    if (!m_translationJob->start(sourceLanguage, targetLanguage,
                                 segments, modelConfiguration, m_run.runId())) {
        Logger::error(QStringLiteral("DubbingPipeline"),
                      QStringLiteral("[translation] job rejected start run=%1").arg(m_run.runId()));
    }
}
void DubbingJobRunner::startAudioGeneration(const QVariantList &segments, const QString &projectPath,
                                             const QVariantMap &synthesisSettings)
{
    if (m_run.processing() || (m_synthesisJob && m_synthesisJob->running())) {
        setBusyError(QStringLiteral("Speech synthesis is already running."));
        return;
    }
    // A new synthesis run replaces a previously hand-uploaded full voice bed;
    // it must not accidentally be mixed into the new segment clips.
    m_dubbedVocalPath.clear();
    m_activeSegments = segments;
    m_projectPath = projectPath;
    m_run.ensureRun();
    m_run.beginNode();
    Logger::info(QStringLiteral("DubbingPipeline"),
                 QStringLiteral("[tts] start run=%1 segments=%2 provider=%3 voice=%4 project=%5")
                     .arg(m_run.runId()).arg(m_activeSegments.size())
                     .arg(synthesisSettings.value(QStringLiteral("executionProvider"), QStringLiteral("local-dev")).toString(),
                          synthesisSettings.value(QStringLiteral("voice")).toString(), projectPath));
    setProcessing(true, QStringLiteral("tts"), 0);
    if (!m_synthesisJob || !m_synthesisJob->start(segments, projectPath, synthesisSettings, m_run.runId())) return;
}

void DubbingJobRunner::fitTiming(const QVariantList &segments, const QString &projectPath)
{
    if (segments.isEmpty()) return;
    if (m_run.processing() || !m_timingWatcher || m_timingWatcher->isRunning()) {
        setBusyError(QStringLiteral("Another dubbing operation is already running."));
        return;
    }
    m_activeSegments = segments;
    m_projectPath = projectPath;
    m_timingCancel = std::make_shared<QAtomicInteger<bool>>(false);
    const auto cancel = m_timingCancel;
    const QVariantList input = segments;
    setProcessing(true, QStringLiteral("fit-timing"), 0);
    m_timingWatcher->setFuture(QtConcurrent::run([input, cancel]() {
        QString error;
        const QVariantList result = DubbingTimingService::fitSegments(input, cancel.get(), &error);
        Q_UNUSED(error);
        return result;
    }));
}

void DubbingJobRunner::onTimingFinished()
{
    if (!m_timingWatcher || m_run.stageId() != DubbingStage::FitTiming) return;
    const QVariantList fitted = m_timingWatcher->result();
    m_timingCancel.reset();
    if (fitted.isEmpty() && !m_activeSegments.isEmpty()) {
        setError(QStringLiteral("Timing fit failed or was cancelled."));
        return;
    }
    for (int i = 0; i < fitted.size(); ++i)
        emit segmentUpdated(i, fitted.at(i).toMap());
    m_activeSegments = fitted;
    setProcessing(false, QStringLiteral("fitted"), 100);
    emit segmentsUpdated(m_activeSegments);
    emit stageCompleted(QStringLiteral("fit-timing"),
                       {{QStringLiteral("timeline"), m_activeSegments}});
}

void DubbingJobRunner::cancel()
{
    if (!m_run.processing()) {
        return;
    }
    if (m_run.stageId() == DubbingStage::Tts && m_synthesisJob) m_synthesisJob->cancel();
    if (m_run.stageId() == DubbingStage::Transcription) {
        if (m_sttTranscriptActive && m_transcriptionJob) m_transcriptionJob->cancel();
        if (m_ocrTranscriptActive && m_subtitleOcr) m_subtitleOcr->cancel();
        m_sttTranscriptActive = false;
        m_ocrTranscriptActive = false;
        m_ocrTranscriptLoadingSource = false;
    }
    if (m_run.stageId() == DubbingStage::FitTiming && m_timingCancel)
        m_timingCancel->storeRelease(true);
    if ((m_run.stageId() == DubbingStage::Mix || m_run.stageId() == DubbingStage::Export) && m_exportJob)
        m_exportJob->cancel();
    if (m_run.stageId() == DubbingStage::Import && m_mediaIngest) m_mediaIngest->cancel();
    if (m_run.stageId() == DubbingStage::SourceSeparation) {
        m_colabSeparationActivityStatus.clear();
        m_colabSeparationTransferActive = false;
        if (m_colabSeparationCancellation) {
            m_colabSeparationCancellation->store(true, std::memory_order_relaxed);
            if (m_colabSeparationRunner)
                QMetaObject::invokeMethod(m_colabSeparationRunner, "cancel", Qt::QueuedConnection);
        } else if (m_sourceSeparation) {
            m_sourceSeparation->cancel();
        }
    }
    if (m_run.stageId() == DubbingStage::Translation && m_translationJob)
        m_translationJob->cancel();
    if (m_autoTranslationFix && m_autoTranslationFix->busy())
        m_autoTranslationFix->cancel();
    Logger::warning(QStringLiteral("DubbingPipeline"),
                    QStringLiteral("[%1] cancelled at %2%%").arg(m_run.stageName()).arg(m_run.progress()));
    setProcessing(false, QStringLiteral("cancelled"), m_run.progress());
}

bool DubbingJobRunner::renderPreview(const QVariantList &segments, const QString &projectPath, const QString &path)
{
    if (m_run.processing()) {
        setBusyError(QStringLiteral("Finish the active dubbing operation before rendering a preview."));
        return false;
    }
    m_run.ensureRun();
    m_run.beginNode();
    QVariantList mixSegments = segments;
    bool hasSegmentClip = false;
    for (const QVariant &entry : segments) {
        const QString clipPath = entry.toMap().value(QStringLiteral("clipPath")).toString();
        if (!clipPath.isEmpty() && QFileInfo::exists(clipPath)) {
            hasSegmentClip = true;
            break;
        }
    }
    // Manual TTS/alignment handoff is a complete timed voice bed, not a
    // fabricated per-segment bundle. Convert it to one explicit timeline clip
    // only for the real mixer, so the next Export/Output task can continue.
    const QFileInfo uploadedVoiceInfo(m_dubbedVocalPath);
    if (!hasSegmentClip && uploadedVoiceInfo.isFile()) {
        const WavIO::WavData voice = WavIO::loadAsFloat(m_dubbedVocalPath);
        const int channels = qMax(1, voice.channels);
        const qint64 durationMs = voice.sampleRate > 0
            ? (static_cast<qint64>(voice.samples.size() / channels) * 1000 / voice.sampleRate)
            : 0;
        if (voice.samples.isEmpty() || durationMs <= 0) {
            setError(QStringLiteral("The uploaded timed voice WAV cannot be decoded for mixing."));
            return false;
        }
        mixSegments = QVariantList{QVariantMap{{QStringLiteral("clipPath"), m_dubbedVocalPath},
                                                {QStringLiteral("startMs"), 0},
                                                {QStringLiteral("endMs"), durationMs}}};
    }
    Logger::info(QStringLiteral("DubbingPipeline"),
                 QStringLiteral("[mix] start run=%1 node=%2 segments=%3 background=%4")
                     .arg(m_run.runId()).arg(m_run.nodeRunId()).arg(mixSegments.size())
                     .arg(m_backgroundAudioPath));
    setProcessing(true, QStringLiteral("mix"), 0);
    return m_exportJob && m_exportJob->renderPreview(mixSegments, projectPath, m_backgroundAudioPath, path);
}

bool DubbingJobRunner::startExport(const QString &sourceMediaPath, const QString &outputPath)
{
    return startExport(sourceMediaPath, m_previewPath, outputPath, m_activeSegments);
}

bool DubbingJobRunner::startExport(const QString &sourceMediaPath,
                                   const QString &audioPath,
                                   const QString &outputPath,
                                   const QVariantList &segments,
                                   const QVariantMap &subtitleConfiguration)
{
    if (m_run.processing()) {
        setBusyError(QStringLiteral("Finish the active dubbing operation before exporting."));
        return false;
    }
    m_run.ensureRun();
    m_run.beginNode();
    Logger::info(QStringLiteral("DubbingPipeline"),
                 QStringLiteral("[export] start run=%1 node=%2 source=%3 preview=%4 output=%5")
                     .arg(m_run.runId(), m_run.nodeRunId(), sourceMediaPath, audioPath, outputPath));
    m_exportPath.clear();
    emit stateChanged();
    setProcessing(true, QStringLiteral("export"), 0);
    return m_exportJob && m_exportJob->startExport(sourceMediaPath, audioPath, outputPath,
                                                    segments.isEmpty() ? m_activeSegments : segments,
                                                    subtitleConfiguration);
}

void DubbingJobRunner::setPreviewPath(const QString &path)
{
    m_previewPath = path;
    const QString vocalPath = AudioTimelineMixer::vocalStemPath(path);
    m_dubbedVocalPath = QFileInfo::exists(vocalPath) ? vocalPath : QString();
    emit stateChanged();
}

void DubbingJobRunner::setExportPath(const QString &path)
{
    m_exportPath = path;
    emit stateChanged();
}

void DubbingJobRunner::clearError()
{
    if (m_run.lastError().isEmpty()) return;
    m_run.clearError();
    emit stateChanged();
}

void DubbingJobRunner::setProcessingState(bool value, const QString &stageValue, int progressValue)
{
    setProcessing(value, stageValue, progressValue);
}

void DubbingJobRunner::onIngestFinished(bool success, const QVariantMap &manifest, const QString &error)
{
    if (m_run.stageId() != DubbingStage::Import) return;
    Logger::info(QStringLiteral("DubbingPipeline"),
                 QStringLiteral("[ingest] finished success=%1 manifestKeys=%2 elapsedMs=%3")
                     .arg(success ? QStringLiteral("true") : QStringLiteral("false"))
                     .arg(manifest.keys().join(QLatin1Char(',')))
                      .arg(m_run.elapsedMs()));
    if (!success) {
        setError(error.isEmpty() ? QStringLiteral("Media import failed.") : error);
        emit ingestFinished(false, {});
        return;
    }
    setProcessing(false, QStringLiteral("imported"), 100);
    emit ingestFinished(true, manifest);
    emit stageCompleted(QStringLiteral("ingest"), manifest);
}

void DubbingJobRunner::onSourceSeparationFinished(const SeparationResult &result)
{
    if (m_run.stageId() != DubbingStage::SourceSeparation) return;
    Logger::info(QStringLiteral("DubbingPipeline"),
                 QStringLiteral("[source-separate] finished success=%1 stems=%2 elapsedMs=%3 error=%4")
                     .arg(result.success ? QStringLiteral("true") : QStringLiteral("false"))
                     .arg(result.stems.size())
                      .arg(m_run.elapsedMs())
                     .arg(result.error));
    m_pendingSourceAudioPath.clear();
    if (!result.success) {
        setError(result.error.isEmpty()
                     ? QStringLiteral("Source separation failed before producing clean speech and background stems.")
                     : result.error);
        return;
    }
    QString vocalsPath;
    QString bgPath;
    for (const auto &stem : result.stems) {
        if (stem.id == QStringLiteral("vocals")) {
            vocalsPath = stem.path;
        } else if (stem.id == QStringLiteral("background")) {
            bgPath = stem.path;
        }
    }
    if (!QFileInfo(vocalsPath).isFile() || !QFileInfo(bgPath).isFile()) {
        setError(QStringLiteral("Source separation completed without both required vocals and background stems. The original audio was not used as a substitute."));
        return;
    }
    const QVariantMap outputs{{QStringLiteral("vocals"), vocalsPath},
                              {QStringLiteral("background"), bgPath},
                              {QStringLiteral("sourceSeparation"), QStringLiteral("uvr")}};
    setProcessing(false, QStringLiteral("separated"), 100);
    emit sourceSeparationFinished(outputs);
    emit stageCompleted(QStringLiteral("source-separate"), outputs);
}

void DubbingJobRunner::setError(const QString &message)
{
    Logger::error(QStringLiteral("DubbingPipeline"),
                  QStringLiteral("[%1] error at %2%%: %3").arg(m_run.stageName()).arg(m_run.progress()).arg(message));
    m_run.setError(message);
    setProcessing(false, QStringLiteral("error"), 0);
    emit errorOccurred(message);
}

void DubbingJobRunner::setBusyError(const QString &message)
{
    m_run.setLastError(message);
    emit stateChanged();
    emit errorOccurred(message);
}

QVariantMap DubbingJobRunner::activityTransferProgress() const
{
    if (!m_colabSeparationTransferActive) return {};
    QVariantMap result{{QStringLiteral("artifact"), m_colabSeparationArtifact},
                       {QStringLiteral("receivedBytes"), m_colabSeparationReceivedBytes},
                       {QStringLiteral("totalBytes"), m_colabSeparationTotalBytes}};
    if (m_colabSeparationTotalBytes > 0) {
        result.insert(QStringLiteral("percent"), qBound(0, int(
            m_colabSeparationReceivedBytes * 100 / m_colabSeparationTotalBytes), 100));
        result.insert(QStringLiteral("available"), true);
    } else {
        result.insert(QStringLiteral("available"), false);
    }
    return result;
}

void DubbingJobRunner::setProcessing(bool value, const QString &stageValue, int progressValue)
{
    if (value && (!m_run.processing() || m_run.stageName() != stageValue)) {
        Logger::debug(QStringLiteral("DubbingPipeline"),
                      QStringLiteral("stage entered: %1 progress=%2").arg(stageValue).arg(progressValue));
    } else if (!value && m_run.processing()) {
        Logger::debug(QStringLiteral("DubbingPipeline"),
                      QStringLiteral("stage leaving: %1 progress=%2 elapsedMs=%3")
                          .arg(m_run.stageName()).arg(progressValue).arg(m_run.elapsedMs()));
    }
    m_run.setState(value, stageValue, progressValue);
    emit stateChanged();
}

} // namespace LAStudio
