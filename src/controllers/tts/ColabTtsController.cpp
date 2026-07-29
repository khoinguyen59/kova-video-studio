#include "ColabTtsController.h"

#include "audio/AudioPlayer.h"
#include "audio/WaveformProvider.h"
#include "audio/WavIO.h"
#include "controllers/shared/HistoryService.h"
#include "core/PathUtils.h"
#include "core/Settings.h"
#include "remote/ColabSession.h"
#include "tts/ColabTtsRunner.h"

#include <QMetaObject>

#include <algorithm>

namespace LAStudio {

ColabTtsController::ColabTtsController(ColabSession *session, Settings *settings, AudioPlayer *player,
                                       WaveformProvider *waveformProvider,
                                       HistoryService *history, QObject *parent)
    : QObject(parent)
    , m_session(session)
    , m_settings(settings)
    , m_player(player)
    , m_waveformProvider(waveformProvider)
    , m_history(history)
{
    qRegisterMetaType<ColabTtsRequest>("ColabTtsRequest");
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_runner = new ColabTtsRunner;
    m_runner->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_runner, &QObject::deleteLater);
    connect(m_runner, &ColabTtsRunner::progress, this, &ColabTtsController::onRunnerProgress);
    connect(m_runner, &ColabTtsRunner::finished, this, &ColabTtsController::onRunnerFinished);
    connect(m_runner, &ColabTtsRunner::failed, this, &ColabTtsController::onRunnerFailed);
    if (m_session) {
        connect(m_session, &ColabSession::sessionChanged,
                this, &ColabTtsController::onSessionChanged);
        connect(m_session, &ColabSession::verificationFinished, this,
                [this](bool success, const QString &message) {
            if (!m_activateColabWhenVerified) return;
            m_activateColabWhenVerified = false;
            if (success) useColab();
            else emit errorOccurred(message);
        });
    }
    m_thread.start();
}

ColabTtsController::~ColabTtsController()
{
    if (m_cancellation) m_cancellation->store(true, std::memory_order_relaxed);
    if (m_runner && m_thread.isRunning()) QMetaObject::invokeMethod(m_runner, "cancel", Qt::QueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

bool ColabTtsController::colabConnected() const
{
    return m_session && m_session->hasVerifiedRoute(QStringLiteral("tts"), m_colabModel);
}

void ColabTtsController::setColabModel(const QString &model)
{
    const QString normalized = model.trimmed().toLower();
    if (notebookForColabModel(normalized).isEmpty()) {
        emit errorOccurred(QStringLiteral("No Colab notebook is mapped for TTS model '%1'.").arg(model));
        return;
    }
    if (normalized == m_colabModel) return;
    if (m_processing) cancelProcessing();
    if (m_session && (m_session->isActive() || m_session->isChecking())) {
        m_colabActive = false;
        m_session->clear();
        emit colabStateChanged();
    }
    m_colabModel = normalized;
    emit colabModelChanged();
    if (normalized == QStringLiteral("kokoro")) {
        setColabVoice(QStringLiteral("af_heart"));
        setColabLanguage(QStringLiteral("en"));
    } else if (normalized == QStringLiteral("kokoro-vietnamese")) {
        setColabVoice(QStringLiteral("diem_trinh"));
        setColabLanguage(QStringLiteral("vi"));
    } else if (normalized == QStringLiteral("qwen3-tts-1.7b-customvoice")) {
        setColabVoice(QStringLiteral("Aiden"));
        setColabLanguage(QStringLiteral("en"));
    } else if (normalized == QStringLiteral("vibevoice")) {
        setColabVoice(QStringLiteral("carter"));
        setColabLanguage(QStringLiteral("en"));
    } else if (normalized.startsWith(QStringLiteral("vieneu-tts-"))) {
        setColabVoice(QStringLiteral("Phạm Tuyên"));
        setColabLanguage(QStringLiteral("vi"));
    } else {
        setColabVoice(QStringLiteral("auto"));
        setColabLanguage(QStringLiteral("auto"));
    }
}

QString ColabTtsController::notebookForColabModel(const QString &model) const
{
    const QString normalized = model.trimmed().toLower();
    if (normalized == QStringLiteral("kokoro"))
        return QStringLiteral("LA_STUDIO_TTS_KOKORO_GPU.ipynb");
    if (normalized == QStringLiteral("kokoro-vietnamese"))
        return QStringLiteral("LA_STUDIO_TTS_KOKORO_VIETNAMESE_GPU.ipynb");
    if (normalized == QStringLiteral("omnivoice"))
        return QStringLiteral("LA_STUDIO_TTS_OMNIVOICE_GPU.ipynb");
    if (normalized == QStringLiteral("qwen3-tts-1.7b-customvoice"))
        return QStringLiteral("LA_STUDIO_TTS_QWEN3_CUSTOMVOICE_1_7B_GPU.ipynb");
    if (normalized == QStringLiteral("vibevoice"))
        return QStringLiteral("LA_STUDIO_TTS_VIBEVOICE_0_5B_GPU.ipynb");
    if (normalized == QStringLiteral("vieneu-tts-v2-turbo"))
        return QStringLiteral("LA_STUDIO_TTS_VIENEU_V2_TURBO_GPU.ipynb");
    if (normalized == QStringLiteral("vieneu-tts-v3-turbo"))
        return QStringLiteral("LA_STUDIO_TTS_VIENEU_V3_TURBO_GPU.ipynb");
    if (normalized == QStringLiteral("voxcpm2"))
        return QStringLiteral("LA_STUDIO_TTS_VOXCPM2_GPU.ipynb");
    return {};
}

QString ColabTtsController::colabNotebookFile() const
{
    return notebookForColabModel(m_colabModel);
}

bool ColabTtsController::selectColabModel(const QString &model)
{
    const QString normalized = model.trimmed().toLower();
    if (notebookForColabModel(normalized).isEmpty()) {
        emit errorOccurred(QStringLiteral("No Colab notebook is mapped for TTS model '%1'.").arg(model));
        return false;
    }
    setColabModel(normalized);
    return m_colabModel == normalized;
}

void ColabTtsController::setColabVoice(const QString &voice)
{
    const QString normalized = voice.trimmed();
    if (normalized == m_colabVoice) return;
    m_colabVoice = normalized;
    emit colabVoiceChanged();
}

void ColabTtsController::setColabLanguage(const QString &language)
{
    const QString normalized = language.trimmed();
    if (normalized == m_colabLanguage) return;
    m_colabLanguage = normalized;
    emit colabLanguageChanged();
}

QVariantList ColabTtsController::lastSamplePreview() const
{
    QVariantList preview;
    if (m_lastSamples.isEmpty()) return preview;
    const int step = std::max(1, static_cast<int>(m_lastSamples.size() / 1000));
    preview.reserve(m_lastSamples.size() / step + 1);
    for (int index = 0; index < m_lastSamples.size(); index += step) preview.append(m_lastSamples.at(index));
    return preview;
}

bool ColabTtsController::connectColab(const QString &workerUrl, const QString &bearerToken)
{
    if (!m_session) {
        emit errorOccurred(QStringLiteral("Colab session is unavailable"));
        return false;
    }
    QString error;
    m_activateColabWhenVerified = true;
    if (!m_session->beginVerifiedSession(
            workerUrl, bearerToken, QStringLiteral("tts"), m_colabModel, &error)) {
        m_activateColabWhenVerified = false;
        emit errorOccurred(error);
        return false;
    }
    return true;
}

void ColabTtsController::useColab()
{
    if (!colabConnected()) {
        emit errorOccurred(QStringLiteral("Connect a Colab GPU worker before using Colab TTS"));
        return;
    }
    if (!m_colabActive) {
        m_colabActive = true;
        emit colabStateChanged();
    }
}

void ColabTtsController::useLocal()
{
    if (m_settings && m_settings->remoteFirstMode()) {
        emit errorOccurred(QStringLiteral("Remote-first mode requires API Gateway or a direct Colab TTS worker. Disable Remote-first mode before selecting Local Dev TTS."));
        return;
    }
    deactivateColab();
}

void ColabTtsController::deactivateColab()
{
    if (!m_colabActive) return;
    cancelProcessing();
    m_colabActive = false;
    emit colabStateChanged();
}

void ColabTtsController::synthesize(const QString &text, float speed)
{
    if (!m_colabActive || m_processing || !m_session) return;
    const QString normalizedText = text.trimmed();
    if (normalizedText.isEmpty()) {
        emit errorOccurred(QStringLiteral("Text is required for speech synthesis"));
        return;
    }
    if (m_colabModel.isEmpty() || m_colabVoice.isEmpty()) {
        emit errorOccurred(QStringLiteral("Colab TTS model and voice are required"));
        return;
    }
    QString routeError;
    if (!m_session->hasVerifiedRoute(QStringLiteral("tts"), m_colabModel, &routeError)) {
        emit errorOccurred(routeError);
        return;
    }
    m_activeText = normalizedText;
    m_cancellation = std::make_shared<std::atomic_bool>(false);
    m_processing = true;
    m_progress = 0;
    emit processingChanged();
    emit progressChanged();
    ColabTtsRequest request;
    request.workerUrl = m_session->endpoint();
    request.bearerToken = m_session->bearerTokenForRequest();
    request.model = m_colabModel;
    request.text = normalizedText;
    request.voice = m_colabVoice;
    request.language = m_colabLanguage;
    request.speed = speed;
    request.cancellation = InferenceCancellationToken(m_cancellation);
    m_activeSessionRevision = m_sessionRevision;
    QMetaObject::invokeMethod(m_runner, "synthesize", Qt::QueuedConnection,
                              Q_ARG(ColabTtsRequest, request));
}

void ColabTtsController::cancelProcessing()
{
    if (!m_processing) return;
    if (m_cancellation) m_cancellation->store(true, std::memory_order_relaxed);
    QMetaObject::invokeMethod(m_runner, "cancel", Qt::QueuedConnection);
}

void ColabTtsController::playOutput(qint64 positionMs)
{
    if (m_lastPcm.isEmpty() || !m_player || !m_waveformProvider) {
        emit errorOccurred(QStringLiteral("No Colab TTS audio to play"));
        return;
    }
    m_player->playPcm(m_lastPcm, m_sampleRate);
    if (positionMs > 0) m_player->seek(positionMs);
    m_waveformProvider->setSamples(m_lastSamples);
}

void ColabTtsController::saveWav(const QString &path)
{
    if (m_lastSamples.isEmpty() || m_sampleRate <= 0) {
        emit errorOccurred(QStringLiteral("No Colab TTS audio to save"));
        return;
    }
    if (!WavIO::saveFloat(PathUtils::urlToLocalPath(path), m_lastSamples.constData(),
                          m_lastSamples.size(), m_sampleRate)) {
        emit errorOccurred(QStringLiteral("Failed to save WAV file"));
    }
}

void ColabTtsController::onSessionChanged()
{
    ++m_sessionRevision;
    if (m_processing) cancelProcessing();
    if (!colabConnected() && m_colabActive) m_colabActive = false;
    emit colabStateChanged();
}

void ColabTtsController::onRunnerProgress(int percent)
{
    const int bounded = qBound(0, percent, 100);
    if (m_progress == bounded) return;
    m_progress = bounded;
    emit progressChanged();
}

void ColabTtsController::onRunnerFinished(const QByteArray &pcm16, const QVector<float> &samples, int sampleRate)
{
    if (!m_processing) return;
    if (m_activeSessionRevision != m_sessionRevision) {
        m_processing = false;
        m_progress = 0;
        emit processingChanged();
        emit progressChanged();
        return;
    }
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
                                        QStringLiteral("Colab GPU: %1").arg(m_colabModel),
                                        m_colabVoice, m_lastSamples, m_sampleRate);
    }
    emit synthesisFinished(m_lastPcm, m_sampleRate);
}

void ColabTtsController::onRunnerFailed(const QString &error)
{
    const bool wasProcessing = m_processing;
    m_processing = false;
    m_progress = 0;
    if (wasProcessing) emit processingChanged();
    emit progressChanged();
    if (m_activeSessionRevision != m_sessionRevision) return;
    emit errorOccurred(error);
}

} // namespace LAStudio
