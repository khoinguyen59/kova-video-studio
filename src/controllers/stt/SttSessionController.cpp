#include "SttSessionController.h"
#include "controllers/app/AppController.h"
#include "stt/SttEngine.h"
#include "audio/AudioRecorder.h"
#include "audio/AudioPlayer.h"
#include "controllers/shared/HistoryService.h"
#include "core/Settings.h"
#include "core/PathUtils.h"
#include "core/Logger.h"
#include "remote/ColabSession.h"
#include "stt/ColabSttRunner.h"
#include "core/RegistryManager.h"
#include "StudioConfigurationResolver.h"
#include <QGuiApplication>
#include <QClipboard>
#include <algorithm>

namespace LAStudio {

SttSessionController::SttSessionController(QObject *parent)
    : QObject(parent)
{
    AppController* app = AppController::instance();
    if (app) {
        m_engine = app->stt();
        m_recorder = app->recorder();
        m_player = app->player();
        m_historyService = app->history();
        m_settings = app->settings();
        m_colabSession = app->colabSession();

        if (app->registry()) {
            m_repository = new StudioSelectionRepository(app->registry()->connectionName(), this);
        }
    }

    m_colabRunner = new ColabSttRunner;
    m_colabRunner->moveToThread(&m_colabThread);
    connect(&m_colabThread, &QThread::finished, m_colabRunner, &QObject::deleteLater);
    connect(m_colabRunner, &ColabSttRunner::progress, this, &SttSessionController::onColabProgress);
    connect(m_colabRunner, &ColabSttRunner::finished, this, &SttSessionController::onColabFinished);
    connect(m_colabRunner, &ColabSttRunner::failed, this, &SttSessionController::onColabFailed);
    m_colabThread.start();
    if (m_colabSession) {
        connect(m_colabSession, &ColabSession::sessionChanged,
                this, &SttSessionController::colabStateChanged);
    }

    if (m_engine) {
        connect(m_engine, &SttEngine::progressChanged, this, &SttSessionController::progressChanged);
        connect(m_engine, &SttEngine::transcriptChanged, this, [this]() {
            m_transcript = m_engine->transcript();
            emit transcriptChanged();
        });
        connect(m_engine, &SttEngine::processingChanged, this, &SttSessionController::processingChanged);
        connect(m_engine, &SttEngine::transcriptionFinished, this, &SttSessionController::onEngineTranscriptionFinished);
        connect(m_engine, &SttEngine::errorOccurred, this, [this](const QString &message) {
            if (!m_activeJob.isValid) return;
            m_activeJob.isValid = false;
            emit transcriptionFailed(message);
        });
    }

    if (m_recorder) {
        connect(m_recorder, &AudioRecorder::recordingChanged, this, &SttSessionController::recordingChanged);
        connect(m_recorder, &AudioRecorder::levelChanged, this, &SttSessionController::recordingLevelChanged);
        connect(m_recorder, &AudioRecorder::finished, this, &SttSessionController::onRecorderFinished);
    }

    if (m_player) {
        connect(m_player, &AudioPlayer::playingChanged, this, &SttSessionController::onPlaybackStateChanged);
        connect(m_player, &AudioPlayer::playbackFinished, this, &SttSessionController::onPlaybackStateChanged);
    }

    if (m_historyService) {
        connect(m_historyService, &HistoryService::sttHistoryChanged, this, &SttSessionController::onHistoryChanged);
    }

    if (m_settings) {
        connect(m_settings, &Settings::sttLanguageChanged, this, &SttSessionController::languageChanged);
        connect(m_settings, &Settings::sttThreadsChanged, this, &SttSessionController::threadsChanged);
        connect(m_settings, &Settings::sttTranslateChanged, this, &SttSessionController::translateChanged);
    }
}

SttSessionController::~SttSessionController()
{
    if (m_colabCancellation) m_colabCancellation->store(true, std::memory_order_relaxed);
    if (m_colabRunner && m_colabThread.isRunning()) {
        QMetaObject::invokeMethod(m_colabRunner, "cancel", Qt::QueuedConnection);
    }
    m_colabThread.quit();
    m_colabThread.wait();
}

QString SttSessionController::transcript() const
{
    return m_transcript;
}

bool SttSessionController::processing() const
{
    return m_colabProcessing || (m_engine && m_engine->isProcessing());
}

int SttSessionController::progress() const
{
    return m_colabProcessing ? m_colabProgress : (m_engine ? m_engine->progress() : 0);
}

bool SttSessionController::recording() const
{
    return m_recorder ? m_recorder->isRecording() : false;
}

double SttSessionController::recordingLevel() const
{
    return m_recorder ? static_cast<double>(m_recorder->level()) : 0.0;
}

QVariantList SttSessionController::history() const
{
    return m_historyService ? m_historyService->sttHistory() : QVariantList();
}

QString SttSessionController::playbackPath() const
{
    return m_playbackPath;
}

bool SttSessionController::colabActive() const
{
    return m_colabSession && m_colabSession->isActive();
}

QString SttSessionController::language() const
{
    return m_settings ? m_settings->sttLanguage() : QStringLiteral("auto");
}

void SttSessionController::setLanguage(const QString &lang)
{
    if (m_settings && m_settings->sttLanguage() != lang) {
        m_settings->setSttLanguage(lang);
    }
}

int SttSessionController::threads() const
{
    return m_settings ? m_settings->sttThreads() : 0;
}

void SttSessionController::setThreads(int count)
{
    count = qBound(0, count, 64);
    if (m_settings && m_settings->sttThreads() != count) {
        m_settings->setSttThreads(count);
    }
}

bool SttSessionController::translate() const
{
    return m_settings ? m_settings->sttTranslate() : false;
}

void SttSessionController::setTranslate(bool val)
{
    if (m_settings && m_settings->sttTranslate() != val) {
        m_settings->setSttTranslate(val);
    }
}

QVariantMap SttSessionController::dynamicSettings() const
{
    return m_dynamicSettings;
}

void SttSessionController::setDynamicSettings(const QVariantMap &settings)
{
    if (m_dynamicSettings == settings) {
        return;
    }
    m_dynamicSettings = settings;
    emit dynamicSettingsChanged();
}

void SttSessionController::selectFileInput(const QString &filePathOrUrl)
{
    clearInput();

    if (filePathOrUrl.isEmpty()) return;

    m_inputPath = PathUtils::urlToLocalPath(filePathOrUrl);
    Logger::info(QStringLiteral("SttSession"),
                 QStringLiteral("Audio decode requested path=%1").arg(m_inputPath));
    m_inputUrl = QUrl::fromLocalFile(m_inputPath);
    emit inputPathChanged();
    emit inputUrlChanged();

    m_inputLoading = true;
    emit inputLoadingChanged();

    m_activeDecoder = new SttAudioDecoder(this);
    connect(m_activeDecoder, &SttAudioDecoder::finished, this, &SttSessionController::onDecoderFinished);
    connect(m_activeDecoder, &SttAudioDecoder::errorOccurred, this, &SttSessionController::onDecoderError);

    m_activeDecoder->startDecode(m_inputPath);
}

void SttSessionController::clearInput()
{
    if (m_activeDecoder) {
        m_activeDecoder->disconnect(this);
        m_activeDecoder->deleteLater();
        m_activeDecoder = nullptr;
    }

    m_inputPath.clear();
    m_inputUrl = QUrl();
    m_inputLoading = false;
    m_inputError.clear();
    m_waveformSamples.clear();
    m_decodedSamples.clear();

    emit inputPathChanged();
    emit inputUrlChanged();
    emit inputLoadingChanged();
    emit inputErrorChanged();
    emit waveformSamplesChanged();
}

void SttSessionController::startRecording(bool systemAudio)
{
    if (m_recorder) {
        clearInput();
        m_recorder->setRecordSystemAudio(systemAudio);
        m_recorder->start();
    }
}

void SttSessionController::stopRecording()
{
    if (m_recorder) {
        m_recorder->stop();
    }
}

void SttSessionController::transcribeInput()
{
    if (m_inputLoading) {
        Logger::debug(QStringLiteral("SttSession"), QStringLiteral("Transcription deferred while audio decode is still running"));
        return;
    }
    if ((!m_engine && !m_colabSession) || m_decodedSamples.isEmpty()) {
        Logger::warning(QStringLiteral("SttSession"),
                        QStringLiteral("Transcription skipped: engine=%1 samples=%2 inputError=%3")
                            .arg(m_engine ? QStringLiteral("available") : QStringLiteral("missing"))
                            .arg(m_decodedSamples.size()).arg(m_inputError));
        return;
    }
    if (!canTranscribe()) {
        const QString message = QStringLiteral("The STT model is not ready. Wait for model loading to finish and try again.");
        Logger::warning(QStringLiteral("SttSession"), message);
        emit transcriptionFailed(message);
        return;
    }

    m_activeJob.samples = m_decodedSamples;
    
    QString modelName = colabActive() ? QStringLiteral("Colab GPU STT") : QStringLiteral("Whisper");
    if (!colabActive() && m_repository) {
        auto selection = m_repository->selectionFor(QStringLiteral("stt"));
        auto resolved = StudioConfigurationResolver::resolve(selection);
        if (resolved.isValid) {
            modelName = resolved.family.value(QStringLiteral("title")).toString();
        }
    }
    m_activeJob.modelName = modelName;
    m_activeJob.inputOrigin = m_inputPath.isEmpty() ? QStringLiteral("Live Recording") : m_inputPath;
    m_activeJob.language = language();
    m_activeJob.threads = threads();
    m_activeJob.translate = translate();
    m_activeJob.isValid = true;

    if (colabActive()) {
        m_colabCancellation = std::make_shared<std::atomic_bool>(false);
        m_colabProcessing = true;
        m_colabProgress = 0;
        emit processingChanged();
        emit progressChanged();
        ColabSttRequest request;
        request.workerUrl = m_colabSession->endpoint();
        request.bearerToken = m_colabSession->bearerTokenForRequest();
        request.samples = m_activeJob.samples;
        request.language = m_activeJob.language;
        request.cancellation = InferenceCancellationToken(m_colabCancellation);
        QMetaObject::invokeMethod(m_colabRunner, "transcribe", Qt::QueuedConnection,
                                  Q_ARG(ColabSttRequest, request));
        return;
    }
    m_engine->transcribeSamples(m_activeJob.samples, m_activeJob.language, m_activeJob.threads, m_activeJob.translate, m_dynamicSettings);
}

bool SttSessionController::canTranscribe() const
{
    return colabActive() || (m_engine && m_engine->state() == SttEngine::Ready);
}

void SttSessionController::cancelProcessing()
{
    if (m_colabProcessing) {
        if (m_colabCancellation) m_colabCancellation->store(true, std::memory_order_relaxed);
        QMetaObject::invokeMethod(m_colabRunner, "cancel", Qt::QueuedConnection);
    } else if (m_engine) {
        m_engine->cancelProcessing();
    }
}

void SttSessionController::clearTranscript()
{
    m_transcript.clear();
    emit transcriptChanged();
}

void SttSessionController::copyTranscript()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        clipboard->setText(transcript());
    }
}

void SttSessionController::loadHistoryItem(const QString &text, const QString &filePathOrUrl)
{
    m_transcript = text;
    emit transcriptChanged();
    selectFileInput(filePathOrUrl);
}

void SttSessionController::deleteHistoryItem(const QString &id)
{
    if (m_historyService) {
        m_historyService->deleteSttHistoryItem(id);
    }
}

void SttSessionController::clearHistory()
{
    if (m_historyService) {
        m_historyService->clearSttHistory();
    }
}

void SttSessionController::playHistoryFile(const QString &filePath)
{
    if (m_player) {
        m_player->stop();
        m_playbackPath = filePath;
        emit playbackPathChanged();
        m_player->playFile(filePath);
    }
}

void SttSessionController::stopPlayback()
{
    if (m_player) {
        m_player->stop();
    }
    m_playbackPath.clear();
    emit playbackPathChanged();
}

void SttSessionController::onDecoderFinished(const QVector<float> &samples)
{
    Logger::info(QStringLiteral("SttSession"),
                 QStringLiteral("Audio decode finished samples=%1 path=%2").arg(samples.size()).arg(m_inputPath));
    m_decodedSamples = samples;
    m_inputLoading = false;
    m_inputError.clear();

    updateWaveform(samples);

    emit inputLoadingChanged();
    emit inputErrorChanged();
    emit waveformSamplesChanged();

    if (m_activeDecoder) {
        m_activeDecoder->deleteLater();
        m_activeDecoder = nullptr;
    }
}

void SttSessionController::onDecoderError(const QString &error)
{
    Logger::error(QStringLiteral("SttSession"),
                  QStringLiteral("Audio decode failed path=%1 error=%2").arg(m_inputPath, error));
    m_inputLoading = false;
    m_inputError = error;
    m_decodedSamples.clear();
    m_waveformSamples.clear();

    emit inputLoadingChanged();
    emit inputErrorChanged();
    emit waveformSamplesChanged();

    if (m_activeDecoder) {
        m_activeDecoder->deleteLater();
        m_activeDecoder = nullptr;
    }
}

void SttSessionController::onRecorderFinished(const QByteArray &pcmData)
{
    const auto *raw = reinterpret_cast<const int16_t *>(pcmData.constData());
    int numSamples = pcmData.size() / 2;
    m_decodedSamples.resize(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        m_decodedSamples[i] = static_cast<float>(raw[i]) / 32768.0f;
    }

    updateWaveform(m_decodedSamples);
    emit waveformSamplesChanged();

    transcribeInput();
}


void SttSessionController::onEngineTranscriptionFinished(const QString &text, const QVariantList &segments)
{
    if (m_activeJob.isValid && !text.trimmed().isEmpty() && m_historyService) {
        m_historyService->addSttHistoryItem(text, m_activeJob.modelName, m_activeJob.samples);
    }
    m_activeJob.isValid = false;
    emit transcriptionFinished(text, segments);
}

void SttSessionController::onHistoryChanged()
{
    emit historyChanged();
}

void SttSessionController::onPlaybackStateChanged()
{
    if (m_player && !m_player->isPlaying()) {
        m_playbackPath.clear();
    }
    emit playbackPathChanged();
}

bool SttSessionController::connectColab(const QString &workerUrl, const QString &bearerToken)
{
    if (!m_colabSession) return false;
    QString error;
    if (!m_colabSession->setSession(workerUrl, bearerToken, &error)) {
        emit transcriptionFailed(error);
        return false;
    }
    return true;
}

void SttSessionController::disconnectColab()
{
    if (!m_colabProcessing && m_colabSession) m_colabSession->clear();
}

void SttSessionController::onColabProgress(int percent)
{
    if (!m_colabProcessing) return;
    m_colabProgress = percent;
    emit progressChanged();
}

void SttSessionController::onColabFinished(const QString &text, const QVariantList &segments)
{
    if (!m_colabProcessing) return;
    m_colabProcessing = false;
    m_colabProgress = 100;
    m_colabCancellation.reset();
    m_transcript = text;
    emit transcriptChanged();
    emit processingChanged();
    emit progressChanged();
    onEngineTranscriptionFinished(text, segments);
}

void SttSessionController::onColabFailed(const QString &error)
{
    if (!m_colabProcessing) return;
    const bool cancelled = !m_colabCancellation || m_colabCancellation->load(std::memory_order_relaxed);
    m_colabProcessing = false;
    m_colabProgress = 0;
    m_colabCancellation.reset();
    m_activeJob.isValid = false;
    emit processingChanged();
    emit progressChanged();
    if (!cancelled) emit transcriptionFailed(error);
}


void SttSessionController::updateWaveform(const QVector<float> &samples)
{
    m_waveformSamples.clear();
    if (samples.isEmpty()) return;

    int step = std::max<int>(1, samples.size() / 1000);
    m_waveformSamples.reserve(samples.size() / step + 1);
    for (int i = 0; i < samples.size(); i += step) {
        m_waveformSamples.append(samples[i]);
    }
}

} // namespace LAStudio
