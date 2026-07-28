#include "ColabVoiceCloneController.h"

#include "audio/AudioPlayer.h"
#include "audio/WaveformProvider.h"
#include "audio/WavIO.h"
#include "controllers/shared/HistoryService.h"
#include "core/PathUtils.h"
#include "core/Settings.h"
#include "remote/ColabSession.h"
#include "tts/ColabVoiceCloneRunner.h"

#include <QFileInfo>
#include <QMetaObject>

#include <algorithm>

namespace LAStudio {

ColabVoiceCloneController::ColabVoiceCloneController(ColabSession *session, Settings *settings, AudioPlayer *player,
                                                       WaveformProvider *waveformProvider,
                                                       HistoryService *history, QObject *parent)
    : QObject(parent)
    , m_session(session)
    , m_settings(settings)
    , m_player(player)
    , m_waveformProvider(waveformProvider)
    , m_history(history)
{
    qRegisterMetaType<ColabVoiceCloneRequest>("ColabVoiceCloneRequest");
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_runner = new ColabVoiceCloneRunner;
    m_runner->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_runner, &QObject::deleteLater);
    connect(m_runner, &ColabVoiceCloneRunner::progress,
            this, &ColabVoiceCloneController::onRunnerProgress);
    connect(m_runner, &ColabVoiceCloneRunner::profileReady,
            this, &ColabVoiceCloneController::onProfileReady);
    connect(m_runner, &ColabVoiceCloneRunner::profileDeleted,
            this, &ColabVoiceCloneController::onProfileDeleted);
    connect(m_runner, &ColabVoiceCloneRunner::finished,
            this, &ColabVoiceCloneController::onRunnerFinished);
    connect(m_runner, &ColabVoiceCloneRunner::failed,
            this, &ColabVoiceCloneController::onRunnerFailed);
    if (m_session) {
        connect(m_session, &ColabSession::sessionChanged,
                this, &ColabVoiceCloneController::onSessionChanged);
    }
    if (m_settings) {
        connect(m_settings, &Settings::remoteFirstModeChanged,
                this, &ColabVoiceCloneController::onRemoteFirstModeChanged);
    }
    m_thread.start();
    onSessionChanged();
}

ColabVoiceCloneController::~ColabVoiceCloneController()
{
    if (m_cancellation) m_cancellation->store(true, std::memory_order_relaxed);
    if (m_runner && m_thread.isRunning()) {
        QMetaObject::invokeMethod(m_runner, "cancel", Qt::QueuedConnection);
    }
    m_thread.quit();
    m_thread.wait();
}

bool ColabVoiceCloneController::colabConnected() const
{
    return m_session && m_session->isActive();
}

QVariantList ColabVoiceCloneController::lastSamplePreview() const
{
    QVariantList preview;
    if (m_lastSamples.isEmpty()) return preview;
    const int step = std::max(1, static_cast<int>(m_lastSamples.size() / 1000));
    preview.reserve(m_lastSamples.size() / step + 1);
    for (int index = 0; index < m_lastSamples.size(); index += step) {
        preview.append(m_lastSamples.at(index));
    }
    return preview;
}

bool ColabVoiceCloneController::connectColab(const QString &workerUrl, const QString &bearerToken)
{
    if (!m_session) {
        emit errorOccurred(QStringLiteral("Colab session is unavailable"));
        return false;
    }
    QString error;
    if (!m_session->setSession(workerUrl, bearerToken, &error)) {
        emit errorOccurred(error);
        return false;
    }
    useColab();
    return true;
}

void ColabVoiceCloneController::useColab()
{
    if (!colabConnected()) {
        emit errorOccurred(QStringLiteral("Connect a Colab GPU worker before using voice cloning"));
        return;
    }
    if (!m_colabActive) {
        m_colabActive = true;
        emit colabStateChanged();
    }
}

void ColabVoiceCloneController::useLocal()
{
    if (m_settings && m_settings->remoteFirstMode()) {
        emit errorOccurred(QStringLiteral("Remote-first mode requires a direct Colab voice-cloning worker. Disable Remote-first mode before selecting Local Dev voice cloning."));
        return;
    }
    if (!m_colabActive) return;
    cancelProcessing();
    m_colabActive = false;
    emit colabStateChanged();
}

void ColabVoiceCloneController::cloneVoice(const QString &text, const QString &referencePath,
                                           const QString &referenceText, const QString &language,
                                           const QString &profileName, bool consentConfirmed,
                                           float speed, int steps)
{
    if (!m_colabActive || m_processing || m_profileDeletionPending || !m_session) return;
    const QString normalizedText = text.trimmed();
    const QString normalizedReferencePath = PathUtils::urlToLocalPath(referencePath);
    const QString normalizedReferenceText = referenceText.trimmed();
    if (normalizedText.isEmpty() || normalizedReferencePath.isEmpty() || normalizedReferenceText.isEmpty()) {
        emit errorOccurred(QStringLiteral("Text, reference audio, and its exact transcript are required"));
        return;
    }
    if (!QFileInfo::exists(normalizedReferencePath)) {
        emit errorOccurred(QStringLiteral("Reference audio file was not found"));
        return;
    }
    if (!consentConfirmed) {
        emit errorOccurred(QStringLiteral("Confirm that you have permission to clone this voice"));
        return;
    }

    const QString normalizedLanguage = language.trimmed().isEmpty() ? QStringLiteral("vi") : language.trimmed();
    const QString signature = referenceSignature(normalizedReferencePath, normalizedReferenceText, normalizedLanguage);
    m_activeText = normalizedText;
    m_cancellation = std::make_shared<std::atomic_bool>(false);
    m_processing = true;
    m_progress = 0;
    m_progressStage.clear();
    emit processingChanged();
    emit progressChanged();

    ColabVoiceCloneRequest request;
    request.workerUrl = m_session->endpoint();
    request.bearerToken = m_session->bearerTokenForRequest();
    request.referencePath = normalizedReferencePath;
    request.referenceName = profileName.trimmed().isEmpty() ? QStringLiteral("LA Studio voice") : profileName.trimmed();
    request.referenceText = normalizedReferenceText;
    request.text = normalizedText;
    request.language = normalizedLanguage;
    request.speed = speed;
    request.steps = steps;
    request.consentConfirmed = consentConfirmed;
    request.existingProfileId = signature == m_profileSignature ? m_profileId : QString();
    request.cancellation = InferenceCancellationToken(m_cancellation);
    m_profileSignature = signature;
    m_activeSessionRevision = m_sessionRevision;
    QMetaObject::invokeMethod(m_runner, "clone", Qt::QueuedConnection,
                              Q_ARG(ColabVoiceCloneRequest, request));
}

void ColabVoiceCloneController::clearProfile()
{
    if (m_profileId.isEmpty() && m_profileSignature.isEmpty()) return;
    m_profileId.clear();
    m_profileSignature.clear();
    emit profileChanged();
}

void ColabVoiceCloneController::deleteColabProfile()
{
    if (m_processing || m_profileDeletionPending || !m_session || m_profileId.isEmpty()) return;
    if (!colabConnected()) {
        emit errorOccurred(QStringLiteral("Connect the Colab worker to delete its voice profile"));
        return;
    }
    m_profileDeletionPending = true;
    ColabVoiceCloneRequest request;
    request.workerUrl = m_session->endpoint();
    request.bearerToken = m_session->bearerTokenForRequest();
    request.existingProfileId = m_profileId;
    QMetaObject::invokeMethod(m_runner, "deleteProfile", Qt::QueuedConnection,
                              Q_ARG(ColabVoiceCloneRequest, request));
}

void ColabVoiceCloneController::cancelProcessing()
{
    if (!m_processing) return;
    if (m_cancellation) m_cancellation->store(true, std::memory_order_relaxed);
    QMetaObject::invokeMethod(m_runner, "cancel", Qt::QueuedConnection);
}

void ColabVoiceCloneController::playOutput(qint64 positionMs)
{
    if (m_lastPcm.isEmpty() || !m_player || !m_waveformProvider) {
        emit errorOccurred(QStringLiteral("No Colab voice clone audio to play"));
        return;
    }
    m_player->playPcm(m_lastPcm, m_sampleRate);
    if (positionMs > 0) m_player->seek(positionMs);
    m_waveformProvider->setSamples(m_lastSamples);
}

void ColabVoiceCloneController::saveWav(const QString &path)
{
    if (m_lastSamples.isEmpty() || m_sampleRate <= 0) {
        emit errorOccurred(QStringLiteral("No Colab voice clone audio to save"));
        return;
    }
    if (!WavIO::saveFloat(PathUtils::urlToLocalPath(path), m_lastSamples.constData(),
                          m_lastSamples.size(), m_sampleRate)) {
        emit errorOccurred(QStringLiteral("Failed to save WAV file"));
    }
}

void ColabVoiceCloneController::onSessionChanged()
{
    // A Colab restart or reconnect invalidates remote profile IDs, even when
    // the replacement session is already active.
    ++m_sessionRevision;
    if (m_processing) cancelProcessing();
    clearProfile();
    if (m_settings && m_settings->remoteFirstMode() && colabConnected()) {
        m_colabActive = true;
    } else if (!colabConnected()) {
        m_colabActive = false;
    }
    emit colabStateChanged();
}

void ColabVoiceCloneController::onRemoteFirstModeChanged()
{
    if (m_settings && m_settings->remoteFirstMode() && colabConnected()) {
        m_colabActive = true;
    }
    emit colabStateChanged();
}

void ColabVoiceCloneController::onRunnerProgress(int percent, const QString &stage)
{
    const int bounded = qBound(0, percent, 100);
    if (m_progress == bounded && m_progressStage == stage) return;
    m_progress = bounded;
    m_progressStage = stage;
    emit progressChanged();
}

void ColabVoiceCloneController::onProfileReady(const QString &profileId)
{
    if (m_activeSessionRevision != m_sessionRevision || profileId.isEmpty() || m_profileId == profileId) return;
    m_profileId = profileId;
    emit profileChanged();
}

void ColabVoiceCloneController::onProfileDeleted()
{
    m_profileDeletionPending = false;
    clearProfile();
}

void ColabVoiceCloneController::onRunnerFinished(const QByteArray &pcm16, const QVector<float> &samples,
                                                  int sampleRate)
{
    if (!m_processing) return;
    m_processing = false;
    m_progress = 100;
    m_progressStage = QStringLiteral("complete");
    m_lastPcm = pcm16;
    m_lastSamples = samples;
    m_sampleRate = sampleRate;
    emit processingChanged();
    emit progressChanged();
    emit outputChanged();
    if (m_history) {
        m_history->addTtsHistorySamples(m_activeText, QStringLiteral("Colab GPU voice clone"),
                                        QStringLiteral("Clone"), m_lastSamples, m_sampleRate);
    }
    emit synthesisFinished(m_lastPcm, m_sampleRate);
}

void ColabVoiceCloneController::onRunnerFailed(const QString &error)
{
    m_profileDeletionPending = false;
    const bool wasProcessing = m_processing;
    m_processing = false;
    m_progress = 0;
    m_progressStage.clear();
    if (wasProcessing) emit processingChanged();
    emit progressChanged();
    emit errorOccurred(error);
}

QString ColabVoiceCloneController::referenceSignature(const QString &referencePath,
                                                       const QString &referenceText,
                                                       const QString &language) const
{
    const QFileInfo info(referencePath);
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(info.absoluteFilePath(), QString::number(info.size()),
             QString::number(info.lastModified().toMSecsSinceEpoch()),
             referenceText, language);
}

} // namespace LAStudio
