#include "ColabVoiceDesignController.h"

#include "audio/AudioPlayer.h"
#include "audio/WaveformProvider.h"
#include "audio/WavIO.h"
#include "controllers/shared/HistoryService.h"
#include "core/PathUtils.h"
#include "core/Settings.h"
#include "remote/ColabSession.h"
#include "tts/ColabVoiceDesignRunner.h"

#include <QMetaObject>

#include <algorithm>

namespace LAStudio {

ColabVoiceDesignController::ColabVoiceDesignController(ColabSession *session, Settings *settings, AudioPlayer *player,
                                                         WaveformProvider *waveformProvider,
                                                         HistoryService *history, QObject *parent)
    : QObject(parent)
    , m_session(session)
    , m_settings(settings)
    , m_player(player)
    , m_waveformProvider(waveformProvider)
    , m_history(history)
{
    qRegisterMetaType<ColabVoiceDesignRequest>("ColabVoiceDesignRequest");
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_runner = new ColabVoiceDesignRunner;
    m_runner->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_runner, &QObject::deleteLater);
    connect(m_runner, &ColabVoiceDesignRunner::progress,
            this, &ColabVoiceDesignController::onRunnerProgress);
    connect(m_runner, &ColabVoiceDesignRunner::finished,
            this, &ColabVoiceDesignController::onRunnerFinished);
    connect(m_runner, &ColabVoiceDesignRunner::failed,
            this, &ColabVoiceDesignController::onRunnerFailed);
    if (m_session) {
        connect(m_session, &ColabSession::sessionChanged,
                this, &ColabVoiceDesignController::onSessionChanged);
    }
    if (m_settings) {
        connect(m_settings, &Settings::remoteFirstModeChanged,
                this, &ColabVoiceDesignController::onRemoteFirstModeChanged);
    }
    m_thread.start();
    onSessionChanged();
}

ColabVoiceDesignController::~ColabVoiceDesignController()
{
    if (m_cancellation) m_cancellation->store(true, std::memory_order_relaxed);
    if (m_runner && m_thread.isRunning()) {
        QMetaObject::invokeMethod(m_runner, "cancel", Qt::QueuedConnection);
    }
    m_thread.quit();
    m_thread.wait();
}

bool ColabVoiceDesignController::colabConnected() const
{
    return m_session && m_session->isActive();
}

QVariantList ColabVoiceDesignController::lastSamplePreview() const
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

bool ColabVoiceDesignController::connectColab(const QString &workerUrl, const QString &bearerToken)
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

void ColabVoiceDesignController::useColab()
{
    if (!colabConnected()) {
        emit errorOccurred(QStringLiteral("Connect a Colab GPU worker before using VoiceDesign"));
        return;
    }
    if (!m_colabActive) {
        m_colabActive = true;
        emit colabStateChanged();
    }
}

void ColabVoiceDesignController::useLocal()
{
    if (m_settings && m_settings->remoteFirstMode()) {
        emit errorOccurred(QStringLiteral("Remote-first mode requires a direct Colab VoiceDesign worker. Disable Remote-first mode before selecting Local Dev VoiceDesign."));
        return;
    }
    if (!m_colabActive) return;
    cancelProcessing();
    m_colabActive = false;
    emit colabStateChanged();
}

void ColabVoiceDesignController::generate(const QString &text, const QString &voiceDescription,
                                          const QString &style, const QString &language,
                                          float temperature, qint64 seed)
{
    if (!m_colabActive || m_processing || !m_session) return;
    const QString normalizedText = text.trimmed();
    const QString normalizedDescription = voiceDescription.trimmed();
    if (normalizedText.isEmpty() || normalizedDescription.isEmpty()) {
        emit errorOccurred(QStringLiteral("Text and a voice description are required for VoiceDesign"));
        return;
    }
    m_activeText = normalizedText;
    m_activeDescription = normalizedDescription;
    m_cancellation = std::make_shared<std::atomic_bool>(false);
    m_processing = true;
    m_progress = 0;
    emit processingChanged();
    emit progressChanged();

    ColabVoiceDesignRequest request;
    request.workerUrl = m_session->endpoint();
    request.bearerToken = m_session->bearerTokenForRequest();
    request.model = model();
    request.text = normalizedText;
    request.voiceDescription = normalizedDescription;
    request.style = style.trimmed();
    request.language = language.trimmed().isEmpty() ? QStringLiteral("en") : language.trimmed();
    request.temperature = temperature;
    request.seed = seed;
    request.cancellation = InferenceCancellationToken(m_cancellation);
    QMetaObject::invokeMethod(m_runner, "generate", Qt::QueuedConnection,
                              Q_ARG(ColabVoiceDesignRequest, request));
}

void ColabVoiceDesignController::cancelProcessing()
{
    if (!m_processing) return;
    if (m_cancellation) m_cancellation->store(true, std::memory_order_relaxed);
    QMetaObject::invokeMethod(m_runner, "cancel", Qt::QueuedConnection);
}

void ColabVoiceDesignController::playOutput(qint64 positionMs)
{
    if (m_lastPcm.isEmpty() || !m_player || !m_waveformProvider) {
        emit errorOccurred(QStringLiteral("No Colab VoiceDesign audio to play"));
        return;
    }
    m_player->playPcm(m_lastPcm, m_sampleRate);
    if (positionMs > 0) m_player->seek(positionMs);
    m_waveformProvider->setSamples(m_lastSamples);
}

void ColabVoiceDesignController::saveWav(const QString &path)
{
    if (m_lastSamples.isEmpty() || m_sampleRate <= 0) {
        emit errorOccurred(QStringLiteral("No Colab VoiceDesign audio to save"));
        return;
    }
    if (!WavIO::saveFloat(PathUtils::urlToLocalPath(path), m_lastSamples.constData(),
                          m_lastSamples.size(), m_sampleRate)) {
        emit errorOccurred(QStringLiteral("Failed to save WAV file"));
    }
}

void ColabVoiceDesignController::onSessionChanged()
{
    if (m_processing) cancelProcessing();
    if (m_settings && m_settings->remoteFirstMode() && colabConnected()) {
        m_colabActive = true;
    } else if (!colabConnected()) {
        m_colabActive = false;
    }
    emit colabStateChanged();
}

void ColabVoiceDesignController::onRemoteFirstModeChanged()
{
    if (m_settings && m_settings->remoteFirstMode() && colabConnected()) {
        m_colabActive = true;
    }
    emit colabStateChanged();
}

void ColabVoiceDesignController::onRunnerProgress(int percent)
{
    const int bounded = qBound(0, percent, 100);
    if (m_progress == bounded) return;
    m_progress = bounded;
    emit progressChanged();
}

void ColabVoiceDesignController::onRunnerFinished(const QByteArray &pcm16, const QVector<float> &samples,
                                                   int sampleRate)
{
    if (!m_processing) return;
    m_processing = false;
    m_progress = 100;
    m_lastPcm = pcm16;
    m_lastSamples = samples;
    m_sampleRate = sampleRate;
    emit processingChanged();
    emit progressChanged();
    emit outputChanged();
    if (m_history) {
        m_history->addVoiceDesignHistorySamples(m_activeText, m_activeDescription, QString(),
                                                QStringLiteral("colab-qwen3-voicedesign"),
                                                QStringLiteral("Colab GPU: Qwen3 VoiceDesign"),
                                                m_lastSamples, m_sampleRate);
    }
    emit synthesisFinished(m_lastPcm, m_sampleRate);
}

void ColabVoiceDesignController::onRunnerFailed(const QString &error)
{
    const bool wasProcessing = m_processing;
    m_processing = false;
    m_progress = 0;
    if (wasProcessing) emit processingChanged();
    emit progressChanged();
    emit errorOccurred(error);
}

} // namespace LAStudio
