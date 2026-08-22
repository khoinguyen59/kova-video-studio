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
#include "stt/GatewaySttRunner.h"
#include "remote/ExecutionProvider.h"
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

    qRegisterMetaType<ColabSttRequest>("ColabSttRequest");
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
        connect(m_colabSession, &ColabSession::verificationFinished, this,
                [this](bool success, const QString &message) {
            if (!m_activateColabWhenVerified) return;
            m_activateColabWhenVerified = false;
            if (success) {
                selectProvider(ExecutionProvider::ColabDirect);
            } else {
                emit transcriptionFailed(message);
            }
        });
    }
    qRegisterMetaType<GatewaySttRequest>("GatewaySttRequest");
    m_gatewayRunner = new GatewaySttRunner;
    m_gatewayRunner->moveToThread(&m_gatewayThread);
    connect(&m_gatewayThread, &QThread::finished, m_gatewayRunner, &QObject::deleteLater);
    connect(m_gatewayRunner, &GatewaySttRunner::progress, this, &SttSessionController::onGatewayProgress);
    connect(m_gatewayRunner, &GatewaySttRunner::finished, this, &SttSessionController::onGatewayFinished);
    connect(m_gatewayRunner, &GatewaySttRunner::failed, this, &SttSessionController::onGatewayFailed);
    m_gatewayThread.start();
    if (m_settings) {
        connect(m_settings, &Settings::gatewaySttModelChanged,
                this, &SttSessionController::gatewayModelChanged);
    }

    if (m_engine) {
        connect(m_engine, &SttEngine::progressChanged, this, [this]() {
            emit progressChanged();
            emit progressAvailableChanged();
        });
        connect(m_engine, &SttEngine::transcriptChanged, this, [this]() {
            m_transcript = m_engine->transcript();
            emit transcriptChanged();
        });
        connect(m_engine, &SttEngine::processingChanged, this, [this]() {
            emit processingChanged();
            emit progressAvailableChanged();
        });
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
    if (m_gatewayCancellation) m_gatewayCancellation->store(true, std::memory_order_relaxed);
    if (m_gatewayRunner && m_gatewayThread.isRunning()) {
        QMetaObject::invokeMethod(m_gatewayRunner, "cancel", Qt::QueuedConnection);
    }
    m_gatewayThread.quit();
    m_gatewayThread.wait();
}

QString SttSessionController::transcript() const
{
    return m_transcript;
}

bool SttSessionController::processing() const
{
    return m_colabProcessing || m_gatewayProcessing || (m_engine && m_engine->isProcessing());
}

int SttSessionController::progress() const
{
    if (m_colabProcessing) return m_colabProgress;
    if (m_gatewayProcessing) return m_gatewayProgress;
    return m_engine ? m_engine->progress() : 0;
}

bool SttSessionController::progressAvailable() const
{
    if (m_colabProcessing) return m_colabProgressAvailable;
    if (m_gatewayProcessing) return m_gatewayProgressAvailable;
    if (!m_engine || !m_engine->isProcessing()) return false;
    const int localProgress = m_engine->progress();
    return localProgress > 0 && localProgress < 100;
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
    return m_selectedProvider == ExecutionProvider::ColabDirect && colabPaired();
}

bool SttSessionController::colabPaired() const
{
    return m_colabSession
        && m_colabSession->hasVerifiedRoute(QStringLiteral("stt"), m_colabModel);
}

QString SttSessionController::notebookForColabModel(const QString &model) const
{
    const QString normalized = model.trimmed().toLower();
    if (normalized == QStringLiteral("nemotron-3.5-asr-streaming-0.6b"))
        return QStringLiteral("LA_STUDIO_STT_NEMOTRON_3_5_0_6B_GPU.ipynb");
    if (normalized == QStringLiteral("whisper.cpp"))
        return QStringLiteral("LA_STUDIO_STT_WHISPER_GPU.ipynb");
    if (normalized == QStringLiteral("qwen3-asr-0.6b"))
        return QStringLiteral("LA_STUDIO_STT_QWEN3_ASR_0_6B_GPU.ipynb");
    if (normalized == QStringLiteral("qwen3-asr-1.7b"))
        return QStringLiteral("LA_STUDIO_STT_QWEN3_ASR_1_7B_GPU.ipynb");
    return {};
}

QString SttSessionController::colabNotebookFile() const
{
    return notebookForColabModel(m_colabModel);
}

void SttSessionController::setColabModel(const QString &model)
{
    const QString normalized = model.trimmed().toLower();
    if (notebookForColabModel(normalized).isEmpty()) {
        emit transcriptionFailed(
            QStringLiteral("No Colab notebook is mapped for STT model '%1'.").arg(model));
        return;
    }
    if (m_colabModel == normalized) return;
    if (processing() && m_selectedProvider == ExecutionProvider::ColabDirect)
        cancelProcessing();
    if (m_colabSession
        && (m_colabSession->isActive() || m_colabSession->isChecking()))
        m_colabSession->clear();
    m_colabModel = normalized;
    emit colabModelChanged();
}

bool SttSessionController::selectColabModel(const QString &model)
{
    const QString normalized = model.trimmed().toLower();
    if (notebookForColabModel(normalized).isEmpty()) {
        emit transcriptionFailed(QStringLiteral("No Colab notebook is mapped for STT model '%1'.").arg(model));
        return false;
    }
    setColabModel(normalized);
    if (colabPaired()) selectProvider(ExecutionProvider::ColabDirect);
    return true;
}

QString SttSessionController::gatewayModel() const
{
    return m_settings ? m_settings->gatewaySttModel() : QString();
}

void SttSessionController::setGatewayModel(const QString &model)
{
    if (m_settings) m_settings->setGatewaySttModel(model);
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
    const ExecutionProvider provider = m_selectedProvider;
    transcribeInputForProvider(provider,
                               provider == ExecutionProvider::ApiGateway ? gatewayModel()
                                   : (provider == ExecutionProvider::ColabDirect ? colabModel() : QString()),
                               language(), translate());
}

void SttSessionController::transcribeInputForProvider(ExecutionProvider provider,
                                                       const QString &configuredModel,
                                                       const QString &requestLanguage,
                                                       bool requestTranslate)
{
    if (m_inputLoading) {
        Logger::debug(QStringLiteral("SttSession"), QStringLiteral("Transcription deferred while audio decode is still running"));
        return;
    }
    if (m_decodedSamples.isEmpty()) {
        const QString message = m_inputError.trimmed().isEmpty()
            ? QStringLiteral("No audio data was decoded from the selected input.")
            : QStringLiteral("STT input audio could not be decoded: %1").arg(m_inputError);
        Logger::warning(QStringLiteral("SttSession"), message);
        // A DubbingTranscriptionJob is already waiting for this signal.  A
        // silent return leaves its run active forever and makes the UI look
        // blocked even though no decoder or remote worker is running.
        emit transcriptionFailed(message);
        return;
    }
    QString availabilityError;
    if (!canTranscribeForProvider(provider, configuredModel, &availabilityError)) {
        const QString message = availabilityError.isEmpty()
            ? QStringLiteral("The selected STT provider is not ready.") : availabilityError;
        Logger::warning(QStringLiteral("SttSession"), message);
        emit transcriptionFailed(message);
        return;
    }

    m_activeJob.samples = m_decodedSamples;
    m_activeProvider = provider;
    QString modelName = provider == ExecutionProvider::ApiGateway
        ? QStringLiteral("API Gateway STT: %1").arg(configuredModel)
        : (provider == ExecutionProvider::ColabDirect
               ? QStringLiteral("Colab GPU STT: %1").arg(configuredModel) : QStringLiteral("Whisper"));
    if (provider == ExecutionProvider::LocalDev && m_repository) {
        auto selection = m_repository->selectionFor(QStringLiteral("stt"));
        auto resolved = StudioConfigurationResolver::resolve(selection);
        if (resolved.isValid) {
            modelName = resolved.family.value(QStringLiteral("title")).toString();
        }
    }
    m_activeJob.modelName = modelName;
    m_activeJob.inputOrigin = m_inputPath.isEmpty() ? QStringLiteral("Live Recording") : m_inputPath;
    m_activeJob.language = requestLanguage.trimmed().isEmpty() ? QStringLiteral("auto") : requestLanguage;
    m_activeJob.threads = threads();
    m_activeJob.translate = requestTranslate;
    m_activeJob.isValid = true;

    if (provider == ExecutionProvider::ApiGateway) {
        m_gatewayCancellation = std::make_shared<std::atomic_bool>(false);
        m_gatewayProcessing = true;
        m_gatewayProgress = 0;
        m_gatewayProgressAvailable = false;
        emit processingChanged();
        emit progressChanged();
        emit progressAvailableChanged();
        GatewaySttRequest request;
        request.gatewayUrl = m_settings->gatewayUrl();
        request.apiKey = m_settings->gatewayApiKey();
        request.model = configuredModel;
        request.samples = m_activeJob.samples;
        request.language = m_activeJob.language;
        request.cancellation = InferenceCancellationToken(m_gatewayCancellation);
        QMetaObject::invokeMethod(m_gatewayRunner, "transcribe", Qt::QueuedConnection,
                                  Q_ARG(GatewaySttRequest, request));
        return;
    }
    if (provider == ExecutionProvider::ColabDirect) {
        m_colabCancellation = std::make_shared<std::atomic_bool>(false);
        m_colabProcessing = true;
        m_colabProgress = 0;
        m_colabProgressAvailable = false;
        emit processingChanged();
        emit progressChanged();
        emit progressAvailableChanged();
        ColabSttRequest request;
        request.workerUrl = m_colabSession->endpoint();
        request.bearerToken = m_colabSession->bearerTokenForRequest();
        request.model = configuredModel;
        request.samples = m_activeJob.samples;
        request.language = m_activeJob.language;
        request.cancellation = InferenceCancellationToken(m_colabCancellation);
        request.allowInsecureLocalhost = m_colabSession->allowsInsecureLocalhostForTests();
        QMetaObject::invokeMethod(m_colabRunner, "transcribe", Qt::QueuedConnection,
                                  Q_ARG(ColabSttRequest, request));
        return;
    }
    m_engine->transcribeSamples(m_activeJob.samples, m_activeJob.language, m_activeJob.threads, m_activeJob.translate, m_dynamicSettings);
}

bool SttSessionController::canTranscribe() const
{
    const ExecutionProvider provider = m_selectedProvider;
    return canTranscribeForProvider(provider,
                                    provider == ExecutionProvider::ApiGateway ? gatewayModel()
                                        : (provider == ExecutionProvider::ColabDirect ? colabModel() : QString()));
}

bool SttSessionController::canTranscribeForProvider(ExecutionProvider provider,
                                                     const QString &model, QString *error) const
{
    if (error) error->clear();
    if (provider == ExecutionProvider::ApiGateway) {
        if (!m_settings) {
            if (error) *error = QStringLiteral("API Gateway configuration is unavailable.");
            return false;
        }
        const RemoteEndpointValidation endpoint = validateRemoteEndpoint(
            m_settings->gatewayUrl(), RemoteEndpointKind::ApiGateway);
        if (!endpoint.isValid()) {
            if (error) *error = endpoint.error;
            return false;
        }
        if (!m_settings->gatewayApiKeyConfigured()) {
            if (error) *error = QStringLiteral("API Gateway key is required.");
            return false;
        }
        if (model.trimmed().isEmpty()) {
            if (error) *error = QStringLiteral("API Gateway STT model is required.");
            return false;
        }
        return true;
    }
    if (provider == ExecutionProvider::ColabDirect) {
        if (model.trimmed().isEmpty()) {
            if (error) *error = QStringLiteral("Select one of the four STT models for Colab GPU first.");
            return false;
        }
        if (notebookForColabModel(model).isEmpty()) {
            if (error) *error = QStringLiteral("The selected Colab STT model is not supported by this build.");
            return false;
        }
        if (!m_colabSession) {
            if (error) *error = QStringLiteral("Connect a Colab GPU worker before running this STT node.");
            return false;
        }
        QString routeError;
        if (!m_colabSession->hasVerifiedRoute(QStringLiteral("stt"), model, &routeError)) {
            if (error) *error = routeError;
            return false;
        }
        return true;
    }
    if (m_settings && m_settings->remoteFirstMode()) {
        if (error) *error = QStringLiteral("Remote-first mode requires API Gateway or a direct Colab STT worker.");
        return false;
    }
    if (!m_engine || m_engine->state() != SttEngine::Ready) {
        if (error) *error = QStringLiteral("The STT model is not ready. Wait for model loading to finish and try again.");
        return false;
    }
    return true;
}

void SttSessionController::cancelProcessing()
{
    if (m_gatewayProcessing) {
        if (m_gatewayCancellation) m_gatewayCancellation->store(true, std::memory_order_relaxed);
        QMetaObject::invokeMethod(m_gatewayRunner, "cancel", Qt::QueuedConnection);
    } else if (m_colabProcessing) {
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
    if (m_colabModel.isEmpty()) {
        emit transcriptionFailed(QStringLiteral(
            "Select one of the four STT models for Colab GPU first."));
        return false;
    }
    QString error;
    m_activateColabWhenVerified = true;
    if (!m_colabSession->beginVerifiedSession(
            workerUrl, bearerToken, QStringLiteral("stt"), m_colabModel, &error)) {
        m_activateColabWhenVerified = false;
        emit transcriptionFailed(error);
        return false;
    }
    return true;
}

void SttSessionController::disconnectColab()
{
    if (m_colabProcessing || !m_colabSession) return;
    const bool wasSelected = m_selectedProvider == ExecutionProvider::ColabDirect;
    m_colabSession->clear();
    if (wasSelected) selectProvider(ExecutionProvider::LocalDev);
}

void SttSessionController::useColab()
{
    if (m_colabModel.isEmpty()) {
        emit transcriptionFailed(QStringLiteral("Select one of the four STT models for Colab GPU first."));
        return;
    }
    if (!colabPaired()) {
        emit transcriptionFailed(QStringLiteral("Connect a Colab GPU worker before selecting Colab STT."));
        return;
    }
    selectProvider(ExecutionProvider::ColabDirect);
}

void SttSessionController::useGateway()
{
    if (!m_settings) { emit transcriptionFailed(QStringLiteral("API Gateway configuration is unavailable.")); return; }
    const RemoteEndpointValidation endpoint = validateRemoteEndpoint(m_settings->gatewayUrl(), RemoteEndpointKind::ApiGateway);
    if (!endpoint.isValid()) { emit transcriptionFailed(endpoint.error); return; }
    if (!m_settings->gatewayApiKeyConfigured()) { emit transcriptionFailed(QStringLiteral("API Gateway key is required.")); return; }
    if (gatewayModel().isEmpty()) { emit transcriptionFailed(QStringLiteral("API Gateway STT model is required.")); return; }
    // Selecting Gateway does not disconnect or overwrite the direct Colab session.
    selectProvider(ExecutionProvider::ApiGateway);
}

void SttSessionController::disconnectGateway()
{
    if (!m_gatewayProcessing && m_selectedProvider == ExecutionProvider::ApiGateway)
        selectProvider(ExecutionProvider::LocalDev);
}

void SttSessionController::useLocal()
{
    if (m_settings && m_settings->remoteFirstMode()) {
        emit transcriptionFailed(QStringLiteral("Remote-first mode requires API Gateway or a direct Colab STT worker. Disable Remote-first mode before selecting Local Dev STT."));
        return;
    }
    selectProvider(ExecutionProvider::LocalDev);
}

void SttSessionController::selectProvider(ExecutionProvider provider)
{
    if (m_selectedProvider == provider) return;
    const ExecutionProvider previous = m_selectedProvider;
    m_selectedProvider = provider;
    if (previous == ExecutionProvider::ColabDirect || provider == ExecutionProvider::ColabDirect)
        emit colabStateChanged();
    if (previous == ExecutionProvider::ApiGateway || provider == ExecutionProvider::ApiGateway)
        emit gatewayStateChanged();
}

void SttSessionController::onColabProgress(int percent)
{
    if (!m_colabProcessing) return;
    m_colabProgress = qBound(0, percent, 100);
    m_colabProgressAvailable = m_colabProgress > 0 && m_colabProgress < 100;
    emit progressChanged();
    emit progressAvailableChanged();
}

void SttSessionController::onColabFinished(const QString &text, const QVariantList &segments)
{
    if (!m_colabProcessing) return;
    m_colabProcessing = false;
    m_colabProgress = 100;
    m_colabProgressAvailable = false;
    m_colabCancellation.reset();
    m_transcript = text;
    emit transcriptChanged();
    emit processingChanged();
    emit progressChanged();
    emit progressAvailableChanged();
    onEngineTranscriptionFinished(text, segments);
}

void SttSessionController::onColabFailed(const QString &error)
{
    if (!m_colabProcessing) return;
    const bool cancelled = !m_colabCancellation || m_colabCancellation->load(std::memory_order_relaxed);
    m_colabProcessing = false;
    m_colabProgress = 0;
    m_colabProgressAvailable = false;
    m_colabCancellation.reset();
    m_activeJob.isValid = false;
    emit processingChanged();
    emit progressChanged();
    emit progressAvailableChanged();
    if (!cancelled) emit transcriptionFailed(error);
}

void SttSessionController::onGatewayProgress(int percent)
{
    if (!m_gatewayProcessing) return;
    m_gatewayProgress = qBound(0, percent, 100);
    m_gatewayProgressAvailable = m_gatewayProgress > 0 && m_gatewayProgress < 100;
    emit progressChanged();
    emit progressAvailableChanged();
}

void SttSessionController::onGatewayFinished(const QString &text, const QVariantList &segments)
{
    if (!m_gatewayProcessing) return;
    m_gatewayProcessing = false;
    m_gatewayProgress = 100;
    m_gatewayProgressAvailable = false;
    m_gatewayCancellation.reset();
    m_transcript = text;
    emit transcriptChanged();
    emit processingChanged();
    emit progressChanged();
    emit progressAvailableChanged();
    onEngineTranscriptionFinished(text, segments);
}

void SttSessionController::onGatewayFailed(const QString &error)
{
    if (!m_gatewayProcessing) return;
    const bool cancelled = !m_gatewayCancellation || m_gatewayCancellation->load(std::memory_order_relaxed);
    m_gatewayProcessing = false;
    m_gatewayProgress = 0;
    m_gatewayProgressAvailable = false;
    m_gatewayCancellation.reset();
    m_activeJob.isValid = false;
    emit processingChanged();
    emit progressChanged();
    emit progressAvailableChanged();
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
