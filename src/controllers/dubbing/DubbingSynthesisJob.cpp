#include "controllers/dubbing/DubbingSynthesisJob.h"

#include "audio/WavIO.h"
#include "core/Logger.h"
#include "core/Settings.h"
#include "dubbing/DubbingTimingService.h"
#include "remote/ColabSession.h"
#include "remote/ExecutionProvider.h"
#include "tts/ColabTtsRunner.h"
#include "tts/GatewayTtsRunner.h"
#include "tts/TtsEngine.h"
#include "workflows/WorkflowArtifact.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFutureWatcher>
#include <QUuid>
#include <QtConcurrent>
#include <QtMath>

namespace LAStudio {
namespace {

QVariantList waveformPreview(const QVector<float> &samples, int maximumPoints = 360)
{
    QVariantList preview;
    if (samples.isEmpty() || maximumPoints <= 0) return preview;
    const int pointCount = qMin(samples.size(), maximumPoints);
    preview.reserve(pointCount);
    for (int point = 0; point < pointCount; ++point) {
        const int begin = point * samples.size() / pointCount;
        const int end = qMax(begin + 1, (point + 1) * samples.size() / pointCount);
        float peak = 0.0f;
        for (int sample = begin; sample < end; ++sample) peak = qMax(peak, qAbs(samples.at(sample)));
        preview.append(peak);
    }
    return preview;
}

QString fingerprint(const QVariantMap &segment, const QString &ttsSignature,
                    const QVariantMap &settings)
{
    const QVariantMap value{{QStringLiteral("targetText"), segment.value(QStringLiteral("targetText"))},
                            {QStringLiteral("startMs"), segment.value(QStringLiteral("startMs"))},
                            {QStringLiteral("endMs"), segment.value(QStringLiteral("endMs"))},
                            {QStringLiteral("speakerId"), segment.value(QStringLiteral("speakerId"))},
                            {QStringLiteral("ttsSignature"), ttsSignature},
                            {QStringLiteral("synthesisSettings"), settings}};
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(QJsonObject::fromVariantMap(value)).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());
}

bool needsSynthesis(const QVariantMap &segment, const QString &signature,
                    const QVariantMap &settings)
{
    if (segment.value(QStringLiteral("targetText")).toString().trimmed().isEmpty()) return false;
    return segment.value(QStringLiteral("state")).toString() != QStringLiteral("ready")
        || !QFileInfo::exists(segment.value(QStringLiteral("clipPath")).toString())
        || segment.value(QStringLiteral("cacheFingerprint")).toString() != fingerprint(segment, signature, settings);
}

struct DubbingTimingResult {
    QVariantList segments;
    QString error;
};
}

DubbingSynthesisJob::DubbingSynthesisJob(TtsEngine *tts, QObject *parent)
    : QObject(parent), m_tts(tts), m_executionProvider(ExecutionProvider::LocalDev)
{
    qRegisterMetaType<GatewayTtsRequest>("GatewayTtsRequest");
    qRegisterMetaType<ColabTtsRequest>("ColabTtsRequest");
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_gatewayRunner = new GatewayTtsRunner;
    m_colabRunner = new ColabTtsRunner;
    m_gatewayRunner->moveToThread(&m_remoteThread);
    m_colabRunner->moveToThread(&m_remoteThread);
    connect(&m_remoteThread, &QThread::finished, m_gatewayRunner, &QObject::deleteLater);
    connect(&m_remoteThread, &QThread::finished, m_colabRunner, &QObject::deleteLater);
    m_remoteThread.start();

    if (m_tts) {
        connect(m_tts, &TtsEngine::synthesisFinished,
                this, &DubbingSynthesisJob::onSynthesisFinished);
        connect(m_tts, &TtsEngine::errorOccurred,
                this, &DubbingSynthesisJob::onTtsError);
        connect(m_tts, &TtsEngine::modelLoadedChanged, this, [this]() {
            if (!m_waitingForModel || !m_tts || !m_tts->isModelLoaded()) return;
            const QVariantList segments = m_pendingSegments;
            const QString projectPath = m_pendingProjectPath;
            const QVariantMap settings = m_pendingSettings;
            const QString runId = m_pendingRunId;
            m_waitingForModel = false;
            m_running = false;
            m_pendingSegments.clear();
            m_pendingProjectPath.clear();
            m_pendingSettings.clear();
            m_pendingRunId.clear();
            start(segments, projectPath, settings, runId);
        });
    }
}

DubbingSynthesisJob::~DubbingSynthesisJob()
{
    cancel();
    if (m_gatewayRunner && m_remoteThread.isRunning())
        QMetaObject::invokeMethod(m_gatewayRunner, "cancel", Qt::QueuedConnection);
    if (m_colabRunner && m_remoteThread.isRunning())
        QMetaObject::invokeMethod(m_colabRunner, "cancel", Qt::QueuedConnection);
    m_remoteThread.quit();
    m_remoteThread.wait();
}

void DubbingSynthesisJob::setRemoteServices(Settings *settings, ColabSession *colabSession)
{
    m_gatewaySettings = settings;
    m_colabSession = colabSession;
}

bool DubbingSynthesisJob::start(const QVariantList &segments, const QString &projectPath,
                                const QVariantMap &settings, const QString &runId)
{
    if (m_running || (m_tts && m_tts->isProcessing())) {
        fail(QStringLiteral("Speech synthesis is already running."));
        return false;
    }
    const QString providerId = settings.value(QStringLiteral("executionProvider"),
                                                QStringLiteral("local-dev"))
                                   .toString().trimmed().toLower();
    if (!executionProviderFromId(providerId, &m_executionProvider)) {
        fail(QStringLiteral("Unknown dubbing TTS provider: %1").arg(providerId));
        return false;
    }
    const bool remote = m_executionProvider != ExecutionProvider::LocalDev;
    if (!remote && !m_tts) {
        fail(QStringLiteral("Load a TTS model before generating dubbing audio."));
        return false;
    }
    if (!remote && !m_tts->isModelLoaded() && m_tts->state() == TtsEngine::Loading) {
        m_running = true;
        m_waitingForModel = true;
        m_pendingSegments = segments;
        m_pendingProjectPath = projectPath;
        m_pendingSettings = settings;
        m_pendingRunId = runId;
        emit progressChanged(0);
        return true;
    }
    if (!remote && !m_tts->isModelLoaded()) {
        fail(QStringLiteral("Load a TTS model before generating dubbing audio."));
        return false;
    }
    ++m_timingRequestId;
    if (m_timingCancelled) m_timingCancelled->storeRelease(true);
    m_timingCancelled.reset();
    m_segments = segments;
    m_projectPath = projectPath;
    m_settings = settings;
    m_cacheSettings = settings;
    m_useVoiceCloning = !remote && settings.value(QStringLiteral("autoSelectVoiceReference")).toBool();
    m_forceSegmentDuration = !remote && settings.value(QStringLiteral("forceSegmentDuration")).toBool()
        && settings.value(QStringLiteral("familyId")).toString()
               .contains(QStringLiteral("omnivoice"), Qt::CaseInsensitive);
    if (remote && settings.value(QStringLiteral("autoSelectVoiceReference")).toBool()) {
        fail(QStringLiteral("Remote Dubbing TTS does not use a local voice reference. Run the dedicated Colab voice-cloning stage first, then choose its prepared voice."));
        return false;
    }
    if (remote) {
        QString model = settings.value(QStringLiteral("modelId")).toString().trimmed();
        QString voice = settings.value(QStringLiteral("voice")).toString().trimmed();
        if (m_executionProvider == ExecutionProvider::ApiGateway && m_gatewaySettings) {
            if (model.isEmpty()) model = m_gatewaySettings->gatewayTtsModel();
            if (voice.isEmpty()) voice = m_gatewaySettings->gatewayTtsVoice();
        } else if (m_executionProvider == ExecutionProvider::ColabDirect) {
            if (model.isEmpty()) model = QStringLiteral("kokoro");
            if (voice.isEmpty()) voice = QStringLiteral("af_heart");
        }
        m_cacheSettings.insert(QStringLiteral("effectiveRemoteModel"), model);
        m_cacheSettings.insert(QStringLiteral("effectiveRemoteVoice"), voice);
        m_synthesisSignature = QStringLiteral("dubbing-tts|%1|%2|%3|%4")
            .arg(executionProviderId(m_executionProvider), model, voice,
                 settings.value(QStringLiteral("lang")).toString());
        m_remoteCancellation = std::make_shared<std::atomic_bool>(false);
    } else {
        m_synthesisSignature = m_tts ? m_tts->activeSignature() : QString();
        m_remoteCancellation.reset();
    }
    m_voiceReference = {};
    if (m_useVoiceCloning) {
        m_voiceReference = DubbingVoiceReferenceSelector::select(
            settings.value(QStringLiteral("autoReferenceSourcePath")).toString(),
            segments, projectPath);
        if (!m_voiceReference.isValid()) {
            fail(m_voiceReference.error);
            return false;
        }
        m_settings.insert(QStringLiteral("ref_text"), m_voiceReference.referenceText);
        m_cacheSettings.insert(QStringLiteral("selectedReferencePath"), m_voiceReference.audioPath);
        m_cacheSettings.insert(QStringLiteral("selectedReferenceText"), m_voiceReference.referenceText);
        m_cacheSettings.insert(QStringLiteral("selectedReferenceQuality"), m_voiceReference.qualityScore);
    }
    m_settings.remove(QStringLiteral("autoSelectVoiceReference"));
    m_settings.remove(QStringLiteral("autoReferenceSourcePath"));
    m_settings.remove(QStringLiteral("forceSegmentDuration"));
    m_settings.remove(QStringLiteral("familyId"));
    m_runId = runId;
    m_generationIndex = -1;
    bool hasTargetText = false;
    for (int i = 0; i < m_segments.size(); ++i) {
        const QVariantMap segment = m_segments.at(i).toMap();
        hasTargetText = hasTargetText || !segment.value(QStringLiteral("targetText")).toString().trimmed().isEmpty();
        if (needsSynthesis(segment, m_synthesisSignature, m_cacheSettings)) {
            m_generationIndex = i;
            break;
        }
    }
    if (m_generationIndex < 0) {
        if (!hasTargetText) { fail(QStringLiteral("Add target text to at least one segment before generating.")); return false; }
        m_running = true;
        emit progressChanged(100);
        fitGeneratedSegments();
        return true;
    }
    m_running = true;
    m_nodeRunId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_chunkIndex = -1;
    emit progressChanged(0);
    startCurrentChunk();
    return m_running;
}

void DubbingSynthesisJob::cancel()
{
    if (!m_running) return;
    ++m_timingRequestId;
    ++m_remoteRequestId;
    if (m_timingCancelled) m_timingCancelled->storeRelease(true);
    if (m_executionProvider == ExecutionProvider::LocalDev) {
        if (m_tts && m_tts->isProcessing()) m_tts->cancelProcessing();
    } else {
        if (m_remoteCancellation) m_remoteCancellation->store(true, std::memory_order_relaxed);
        if (m_executionProvider == ExecutionProvider::ApiGateway && m_gatewayRunner)
            QMetaObject::invokeMethod(m_gatewayRunner, "cancel", Qt::QueuedConnection);
        if (m_executionProvider == ExecutionProvider::ColabDirect && m_colabRunner)
            QMetaObject::invokeMethod(m_colabRunner, "cancel", Qt::QueuedConnection);
    }
    m_remoteCancellation.reset();
    m_running = false;
    m_waitingForModel = false;
    m_pendingSegments.clear();
    m_pendingProjectPath.clear();
    m_pendingSettings.clear();
    m_pendingRunId.clear();
    m_generationIndex = -1;
}

void DubbingSynthesisJob::startCurrentChunk()
{
    if (!m_running || m_generationIndex < 0 || m_generationIndex >= m_segments.size()) return;
    if (m_chunkIndex < 0) {
        const QVariantMap segment = m_segments.at(m_generationIndex).toMap();
        m_chunks = m_forceSegmentDuration ? QVariantList()
                                          : segment.value(QStringLiteral("targetChunks")).toList();
        if (m_chunks.isEmpty()) m_chunks.append(QVariantMap{{QStringLiteral("text"), segment.value(QStringLiteral("targetText"))},
                                                             {QStringLiteral("pauseAfterMs"), 0},
                                                             {QStringLiteral("leadingPauseMs"), 0}});
        m_chunkSamples.clear();
        m_chunkSampleRate = 0;
        m_chunkIndex = 0;
    }
    const QString text = m_chunks.at(m_chunkIndex).toMap().value(QStringLiteral("text")).toString().trimmed();
    if (text.isEmpty()) { fail(QStringLiteral("Pause alignment produced an empty TTS chunk.")); return; }
    QVariantMap requestSettings = m_settings;
    if (m_forceSegmentDuration) {
        const QVariantMap segment = m_segments.at(m_generationIndex).toMap();
        const qint64 slotMs = qMax<qint64>(1,
            segment.value(QStringLiteral("endMs")).toLongLong()
                - segment.value(QStringLiteral("startMs")).toLongLong());
        requestSettings.insert(QStringLiteral("duration_sec"), slotMs / 1000.0);
    }
    if (m_executionProvider == ExecutionProvider::LocalDev) {
        if (!m_tts) { fail(QStringLiteral("Local TTS engine is unavailable.")); return; }
        if (m_useVoiceCloning)
            m_tts->cloneVoice(text, m_voiceReference.audioPath, requestSettings);
        else
            m_tts->synthesize(text, 0, 1.0f, requestSettings);
        return;
    }
    startRemoteSynthesis(text, requestSettings);
}

void DubbingSynthesisJob::onSynthesisFinished(const QByteArray &pcm16, int sampleRate)
{
    Q_UNUSED(pcm16);
    if (!m_running || m_generationIndex < 0 || m_generationIndex >= m_segments.size()) return;
    QVector<float> samples = m_tts ? m_tts->lastSamples() : QVector<float>();
    commitSynthesizedAudio(samples, sampleRate);
}

void DubbingSynthesisJob::startRemoteSynthesis(const QString &text,
                                                const QVariantMap &requestSettings)
{
    const quint64 requestId = ++m_remoteRequestId;
    QObject::disconnect(m_remoteProgressConnection);
    QObject::disconnect(m_remoteFinishedConnection);
    QObject::disconnect(m_remoteFailedConnection);
    const float speed = qBound(0.25F, requestSettings.value(QStringLiteral("speed"), 1.0F).toFloat(), 4.0F);

    if (m_executionProvider == ExecutionProvider::ApiGateway) {
        // This branch receives only Gateway configuration.  It never checks a
        // Colab session and cannot fall through to the Colab runner.
        if (!m_gatewaySettings) { fail(QStringLiteral("API Gateway configuration is unavailable.")); return; }
        if (!m_gatewaySettings->gatewayApiKeyConfigured()) { fail(QStringLiteral("API Gateway key is required.")); return; }
        const QString model = requestSettings.value(QStringLiteral("modelId")).toString().trimmed().isEmpty()
            ? m_gatewaySettings->gatewayTtsModel()
            : requestSettings.value(QStringLiteral("modelId")).toString().trimmed();
        const QString voice = requestSettings.value(QStringLiteral("voice")).toString().trimmed().isEmpty()
            ? m_gatewaySettings->gatewayTtsVoice()
            : requestSettings.value(QStringLiteral("voice")).toString().trimmed();
        if (model.isEmpty()) { fail(QStringLiteral("API Gateway TTS model is required.")); return; }
        if (voice.isEmpty()) { fail(QStringLiteral("API Gateway TTS voice is required.")); return; }
        m_remoteProgressConnection = connect(m_gatewayRunner, &GatewayTtsRunner::progress, this,
                                             [this, requestId](int progress) { onRemoteProgress(progress, requestId); });
        m_remoteFinishedConnection = connect(m_gatewayRunner, &GatewayTtsRunner::finished, this,
                                             [this, requestId](const QByteArray &, const QVector<float> &samples, int rate) {
            if (m_running && requestId == m_remoteRequestId) commitSynthesizedAudio(samples, rate);
        });
        m_remoteFailedConnection = connect(m_gatewayRunner, &GatewayTtsRunner::failed, this,
                                           [this, requestId](const QString &message) {
            if (m_running && requestId == m_remoteRequestId) fail(message);
        });
        GatewayTtsRequest request;
        request.gatewayUrl = m_gatewaySettings->gatewayUrl();
        request.apiKey = m_gatewaySettings->gatewayApiKey();
        request.model = model;
        request.text = text;
        request.voice = voice;
        request.speed = speed;
        request.cancellation = InferenceCancellationToken(m_remoteCancellation);
        QMetaObject::invokeMethod(m_gatewayRunner, "synthesize", Qt::QueuedConnection,
                                  Q_ARG(GatewayTtsRequest, request));
        return;
    }

    if (m_executionProvider == ExecutionProvider::ColabDirect) {
        // This branch receives only the temporary direct Colab session.  It
        // never reads a Gateway URL, key, model or voice setting.
        if (!m_colabSession || !m_colabSession->isActive()) {
            fail(QStringLiteral("Connect a Colab GPU worker before running this TTS node."));
            return;
        }
        QString model = requestSettings.value(QStringLiteral("modelId")).toString().trimmed();
        QString voice = requestSettings.value(QStringLiteral("voice")).toString().trimmed();
        if (model.isEmpty()) model = QStringLiteral("kokoro");
        if (voice.isEmpty()) voice = QStringLiteral("af_heart");
        const QString language = requestSettings.value(QStringLiteral("lang")).toString().trimmed();
        if (model.isEmpty() || voice.isEmpty()) { fail(QStringLiteral("Colab TTS model and voice are required.")); return; }
        m_remoteProgressConnection = connect(m_colabRunner, &ColabTtsRunner::progress, this,
                                             [this, requestId](int progress) { onRemoteProgress(progress, requestId); });
        m_remoteFinishedConnection = connect(m_colabRunner, &ColabTtsRunner::finished, this,
                                             [this, requestId](const QByteArray &, const QVector<float> &samples, int rate) {
            if (m_running && requestId == m_remoteRequestId) commitSynthesizedAudio(samples, rate);
        });
        m_remoteFailedConnection = connect(m_colabRunner, &ColabTtsRunner::failed, this,
                                           [this, requestId](const QString &message) {
            if (m_running && requestId == m_remoteRequestId) fail(message);
        });
        ColabTtsRequest request;
        request.workerUrl = m_colabSession->endpoint();
        request.bearerToken = m_colabSession->bearerTokenForRequest();
        request.model = model;
        request.text = text;
        request.voice = voice;
        request.language = language;
        request.speed = speed;
        request.settings = requestSettings;
        request.settings.remove(QStringLiteral("executionProvider"));
        request.settings.remove(QStringLiteral("modelId"));
        request.settings.remove(QStringLiteral("voice"));
        request.settings.remove(QStringLiteral("lang"));
        request.cancellation = InferenceCancellationToken(m_remoteCancellation);
        QMetaObject::invokeMethod(m_colabRunner, "synthesize", Qt::QueuedConnection,
                                  Q_ARG(ColabTtsRequest, request));
        return;
    }
    fail(QStringLiteral("Dubbing remote TTS provider is unsupported."));
}

void DubbingSynthesisJob::onRemoteProgress(int progress, quint64 requestId)
{
    if (!m_running || requestId != m_remoteRequestId) return;
    emit progressChanged(qBound(0, progress, 94));
}

void DubbingSynthesisJob::commitSynthesizedAudio(const QVector<float> &inputSamples, int sampleRate)
{
    if (!m_running || m_generationIndex < 0 || m_generationIndex >= m_segments.size()) return;
    QVector<float> samples = inputSamples;
    if (samples.isEmpty() || sampleRate <= 0) { fail(QStringLiteral("TTS returned empty audio for segment %1.").arg(m_generationIndex + 1)); return; }
    if (m_chunkIndex >= 0 && m_chunkIndex < m_chunks.size()) {
        const QVariantMap chunk = m_chunks.at(m_chunkIndex).toMap();
        if (m_chunkSampleRate == 0) m_chunkSampleRate = sampleRate;
        if (sampleRate != m_chunkSampleRate) { fail(QStringLiteral("TTS changed sample rate between pause-aligned chunks.")); return; }
        if (m_chunkIndex == 0) m_chunkSamples.append(QVector<float>(qMax<qint64>(0, chunk.value(QStringLiteral("leadingPauseMs")).toLongLong() * sampleRate / 1000), 0.0f));
        m_chunkSamples.append(samples);
        m_chunkSamples.append(QVector<float>(qMax<qint64>(0, chunk.value(QStringLiteral("pauseAfterMs")).toLongLong() * sampleRate / 1000), 0.0f));
        if (m_chunkIndex + 1 < m_chunks.size()) {
            ++m_chunkIndex;
            QMetaObject::invokeMethod(this, [this]() { startCurrentChunk(); }, Qt::QueuedConnection);
            return;
        }
        samples = m_chunkSamples;
        sampleRate = m_chunkSampleRate;
        m_chunkIndex = -1;
        m_chunks.clear();
        m_chunkSamples.clear();
        m_chunkSampleRate = 0;
    }

    const QVariantMap segment = m_segments.at(m_generationIndex).toMap();
    const qint64 slotMs = qMax<qint64>(1, segment.value(QStringLiteral("endMs")).toLongLong() - segment.value(QStringLiteral("startMs")).toLongLong());
    const qint64 sourceDurationMs = qMax<qint64>(1, qRound64(samples.size() * 1000.0 / sampleRate));
    const double fitFactor = static_cast<double>(sourceDurationMs) / slotMs;
    const bool conflict = fitFactor < 0.85 || fitFactor > 1.20;
    WorkflowArtifactStore store(QDir(QFileInfo(m_projectPath).absolutePath()).filePath(QStringLiteral(".workflow-artifacts")));
    QString error;
    const QString staging = store.createStagingDirectory(m_runId, m_nodeRunId, &error);
    if (staging.isEmpty()) { fail(error); return; }
    const QString stagedClip = QDir(staging).filePath(QStringLiteral("clip.wav"));
    if (!WavIO::saveFloat(stagedClip, samples.constData(), samples.size(), sampleRate)) { fail(QStringLiteral("Failed to stage generated clip: %1").arg(stagedClip)); return; }
    WorkflowArtifactReference artifact;
    if (!store.commitFile(stagedClip, QStringLiteral("audio.clip@1"), m_runId, m_nodeRunId, artifact, &error)) { fail(error); return; }
    const QString clipPath = store.resolve(artifact);
    if (clipPath.isEmpty()) { fail(QStringLiteral("Committed clip artifact cannot be resolved.")); return; }

    QVariantMap updated = segment;
    updated.insert(QStringLiteral("clipPath"), clipPath);
    updated.insert(QStringLiteral("clipArtifact"), artifact.toJson().toVariantMap());
    updated.insert(QStringLiteral("cacheFingerprint"), fingerprint(segment, m_synthesisSignature, m_cacheSettings));
    if (m_useVoiceCloning) {
        updated.insert(QStringLiteral("voiceReferencePath"), m_voiceReference.audioPath);
        updated.insert(QStringLiteral("voiceReferenceText"), m_voiceReference.referenceText);
        updated.insert(QStringLiteral("voiceReferenceStartMs"), m_voiceReference.startMs);
        updated.insert(QStringLiteral("voiceReferenceEndMs"), m_voiceReference.endMs);
        updated.insert(QStringLiteral("voiceReferenceQuality"), m_voiceReference.qualityScore);
    }
    updated.insert(QStringLiteral("sampleRate"), sampleRate);
    updated.insert(QStringLiteral("sampleCount"), samples.size());
    updated.insert(QStringLiteral("waveformSamples"), waveformPreview(samples));
    updated.insert(QStringLiteral("sourceDurationMs"), sourceDurationMs);
    updated.insert(QStringLiteral("durationMs"), sourceDurationMs);
    updated.insert(QStringLiteral("fitFactor"), fitFactor);
    updated.insert(QStringLiteral("timingConflict"), conflict);
    updated.insert(QStringLiteral("fitMethod"), QStringLiteral("pending"));
    updated.insert(QStringLiteral("state"), conflict ? QStringLiteral("conflict") : QStringLiteral("natural"));
    m_segments[m_generationIndex] = updated;
    emit segmentUpdated(m_generationIndex, updated);

    int next = m_generationIndex + 1;
    while (next < m_segments.size() && !needsSynthesis(m_segments.at(next).toMap(), m_synthesisSignature, m_cacheSettings)) ++next;
    if (next >= m_segments.size()) { m_generationIndex = -1; fitGeneratedSegments(); return; }
    m_generationIndex = next;
    m_nodeRunId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    emit progressChanged(qRound((100.0 * next) / m_segments.size()));
    QMetaObject::invokeMethod(this, [this]() { startCurrentChunk(); }, Qt::QueuedConnection);
}

void DubbingSynthesisJob::fitGeneratedSegments()
{
    if (!m_running) return;
    const quint64 requestId = ++m_timingRequestId;
    const auto cancelled = std::make_shared<QAtomicInteger<bool>>(false);
    m_timingCancelled = cancelled;
    const QVariantList segments = m_segments;
    emit progressChanged(95);

    auto *watcher = new QFutureWatcher<DubbingTimingResult>(this);
    connect(watcher, &QFutureWatcher<DubbingTimingResult>::finished, this,
            [this, watcher, requestId]() {
        const DubbingTimingResult result = watcher->result();
        watcher->deleteLater();
        if (requestId != m_timingRequestId || !m_running) return;
        if (!result.error.isEmpty()) { fail(result.error); return; }
        m_segments = result.segments;
        m_running = false;
        m_remoteCancellation.reset();
        emit progressChanged(100);
        emit completed(m_segments);
    });
    watcher->setFuture(QtConcurrent::run([segments, cancelled]() {
        DubbingTimingResult result;
        result.segments = DubbingTimingService::fitSegments(segments, cancelled.get(), &result.error);
        return result;
    }));
}

void DubbingSynthesisJob::onTtsError(const QString &message)
{
    if (m_running && (m_waitingForModel || m_generationIndex >= 0)) fail(message);
}

void DubbingSynthesisJob::fail(const QString &message)
{
    m_running = false;
    m_waitingForModel = false;
    m_generationIndex = -1;
    m_remoteCancellation.reset();
    emit failed(message);
}

} // namespace LAStudio
