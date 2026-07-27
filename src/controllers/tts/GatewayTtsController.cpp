#include "GatewayTtsController.h"

#include "audio/AudioPlayer.h"
#include "audio/WaveformProvider.h"
#include "audio/WavIO.h"
#include "controllers/shared/HistoryService.h"
#include "core/PathUtils.h"
#include "core/Settings.h"
#include "remote/ExecutionProvider.h"
#include "tts/GatewayTtsRunner.h"

#include <QMetaObject>

#include <algorithm>

namespace LAStudio {

GatewayTtsController::GatewayTtsController(Settings *settings, AudioPlayer *player,
                                           WaveformProvider *waveformProvider,
                                           HistoryService *history, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_player(player)
    , m_waveformProvider(waveformProvider)
    , m_history(history)
{
    qRegisterMetaType<GatewayTtsRequest>("GatewayTtsRequest");
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_runner = new GatewayTtsRunner;
    m_runner->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_runner, &QObject::deleteLater);
    connect(m_runner, &GatewayTtsRunner::progress, this, &GatewayTtsController::onRunnerProgress);
    connect(m_runner, &GatewayTtsRunner::finished, this, &GatewayTtsController::onRunnerFinished);
    connect(m_runner, &GatewayTtsRunner::failed, this, &GatewayTtsController::onRunnerFailed);
    m_thread.start();
    if (m_settings) {
        connect(m_settings, &Settings::gatewayTtsModelChanged,
                this, &GatewayTtsController::gatewayModelChanged);
        connect(m_settings, &Settings::gatewayTtsVoiceChanged,
                this, &GatewayTtsController::gatewayVoiceChanged);
    }
}

GatewayTtsController::~GatewayTtsController()
{
    if (m_cancellation) m_cancellation->store(true, std::memory_order_relaxed);
    if (m_runner && m_thread.isRunning()) {
        QMetaObject::invokeMethod(m_runner, "cancel", Qt::QueuedConnection);
    }
    m_thread.quit();
    m_thread.wait();
}

QString GatewayTtsController::gatewayModel() const
{
    return m_settings ? m_settings->gatewayTtsModel() : QString();
}

void GatewayTtsController::setGatewayModel(const QString &model)
{
    if (m_settings) m_settings->setGatewayTtsModel(model);
}

QString GatewayTtsController::gatewayVoice() const
{
    return m_settings ? m_settings->gatewayTtsVoice() : QStringLiteral("alloy");
}

void GatewayTtsController::setGatewayVoice(const QString &voice)
{
    if (m_settings) m_settings->setGatewayTtsVoice(voice);
}

QVariantList GatewayTtsController::lastSamplePreview() const
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

void GatewayTtsController::useGateway()
{
    if (!m_settings) {
        emit errorOccurred(QStringLiteral("API Gateway settings are unavailable"));
        return;
    }
    const RemoteEndpointValidation endpoint = validateRemoteEndpoint(
        m_settings->gatewayUrl(), RemoteEndpointKind::ApiGateway, false);
    if (!endpoint.isValid()) {
        emit errorOccurred(endpoint.error);
        return;
    }
    if (!m_settings->gatewayApiKeyConfigured()) {
        emit errorOccurred(QStringLiteral("API Gateway key is required"));
        return;
    }
    if (gatewayModel().isEmpty()) {
        emit errorOccurred(QStringLiteral("API Gateway TTS model is required"));
        return;
    }
    if (gatewayVoice().isEmpty()) {
        emit errorOccurred(QStringLiteral("API Gateway TTS voice is required"));
        return;
    }
    if (!m_gatewayActive) {
        m_gatewayActive = true;
        emit gatewayStateChanged();
    }
}

void GatewayTtsController::disconnectGateway()
{
    if (!m_gatewayActive) return;
    cancelProcessing();
    m_gatewayActive = false;
    emit gatewayStateChanged();
}

void GatewayTtsController::synthesize(const QString &text, float speed)
{
    if (!m_gatewayActive || m_processing || !m_settings) return;
    const QString normalizedText = text.trimmed();
    if (normalizedText.isEmpty()) {
        emit errorOccurred(QStringLiteral("Text is required for speech synthesis"));
        return;
    }
    m_activeText = normalizedText;
    m_activeVoice = gatewayVoice();
    m_cancellation = std::make_shared<std::atomic_bool>(false);
    m_processing = true;
    m_progress = 0;
    emit processingChanged();
    emit progressChanged();
    GatewayTtsRequest request;
    request.gatewayUrl = m_settings->gatewayUrl();
    request.apiKey = m_settings->gatewayApiKey();
    request.model = gatewayModel();
    request.text = normalizedText;
    request.voice = m_activeVoice;
    request.speed = speed;
    request.cancellation = InferenceCancellationToken(m_cancellation);
    QMetaObject::invokeMethod(m_runner, "synthesize", Qt::QueuedConnection,
                              Q_ARG(GatewayTtsRequest, request));
}

void GatewayTtsController::cancelProcessing()
{
    if (!m_processing) return;
    if (m_cancellation) m_cancellation->store(true, std::memory_order_relaxed);
    QMetaObject::invokeMethod(m_runner, "cancel", Qt::QueuedConnection);
}

void GatewayTtsController::playOutput(qint64 positionMs)
{
    if (m_lastPcm.isEmpty() || !m_player || !m_waveformProvider) {
        emit errorOccurred(QStringLiteral("No API Gateway TTS audio to play"));
        return;
    }
    m_player->playPcm(m_lastPcm, m_sampleRate);
    if (positionMs > 0) m_player->seek(positionMs);
    m_waveformProvider->setSamples(m_lastSamples);
}

void GatewayTtsController::saveWav(const QString &path)
{
    if (m_lastSamples.isEmpty() || m_sampleRate <= 0) {
        emit errorOccurred(QStringLiteral("No API Gateway TTS audio to save"));
        return;
    }
    if (!WavIO::saveFloat(PathUtils::urlToLocalPath(path), m_lastSamples.constData(),
                          m_lastSamples.size(), m_sampleRate)) {
        emit errorOccurred(QStringLiteral("Failed to save WAV file"));
    }
}

void GatewayTtsController::onRunnerProgress(int percent)
{
    const int bounded = qBound(0, percent, 100);
    if (m_progress == bounded) return;
    m_progress = bounded;
    emit progressChanged();
}

void GatewayTtsController::onRunnerFinished(const QByteArray &pcm16, const QVector<float> &samples, int sampleRate)
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
        m_history->addTtsHistorySamples(m_activeText,
                                        QStringLiteral("API Gateway: %1").arg(gatewayModel()),
                                        m_activeVoice, m_lastSamples, m_sampleRate);
    }
    emit synthesisFinished(m_lastPcm, m_sampleRate);
}

void GatewayTtsController::onRunnerFailed(const QString &error)
{
    const bool wasProcessing = m_processing;
    m_processing = false;
    m_progress = 0;
    if (wasProcessing) emit processingChanged();
    emit progressChanged();
    emit errorOccurred(error);
}

} // namespace LAStudio
