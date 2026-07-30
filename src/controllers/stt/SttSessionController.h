#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QUrl>
#include <QThread>
#include <atomic>
#include <memory>
#include "core/StudioSelectionRepository.h"
#include "remote/ExecutionProvider.h"
#include "SttAudioDecoder.h"

namespace LAStudio {

class SttEngine;
class AudioRecorder;
class AudioPlayer;
class HistoryService;
class Settings;
class ColabSession;
class ColabSttRunner;
class GatewaySttRunner;

struct SttJobSnapshot {
    QVector<float> samples;
    QString modelName;
    QString inputOrigin;
    QString language;
    int threads;
    bool translate;
    bool isValid = false;
};

class SttSessionController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString inputPath READ inputPath NOTIFY inputPathChanged)
    Q_PROPERTY(QUrl inputUrl READ inputUrl NOTIFY inputUrlChanged)
    Q_PROPERTY(bool inputLoading READ inputLoading NOTIFY inputLoadingChanged)
    Q_PROPERTY(QString inputError READ inputError NOTIFY inputErrorChanged)
    Q_PROPERTY(QVariantList waveformSamples READ waveformSamples NOTIFY waveformSamplesChanged)
    Q_PROPERTY(QString transcript READ transcript NOTIFY transcriptChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool progressAvailable READ progressAvailable NOTIFY progressAvailableChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(double recordingLevel READ recordingLevel NOTIFY recordingLevelChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QString playbackPath READ playbackPath NOTIFY playbackPathChanged)
    Q_PROPERTY(bool colabActive READ colabActive NOTIFY colabStateChanged)
    Q_PROPERTY(bool colabPaired READ colabPaired NOTIFY colabStateChanged)
    Q_PROPERTY(QString colabModel READ colabModel WRITE setColabModel NOTIFY colabModelChanged)
    Q_PROPERTY(QString colabNotebookFile READ colabNotebookFile NOTIFY colabModelChanged)
    Q_PROPERTY(bool gatewayActive READ gatewayActive NOTIFY gatewayStateChanged)
    Q_PROPERTY(QString gatewayModel READ gatewayModel WRITE setGatewayModel NOTIFY gatewayModelChanged)

    // Settings
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(int threads READ threads WRITE setThreads NOTIFY threadsChanged)
    Q_PROPERTY(bool translate READ translate WRITE setTranslate NOTIFY translateChanged)
    Q_PROPERTY(QVariantMap dynamicSettings READ dynamicSettings WRITE setDynamicSettings NOTIFY dynamicSettingsChanged)

public:
    explicit SttSessionController(QObject *parent = nullptr);
    ~SttSessionController() override;

    // Property Getters
    QString inputPath() const { return m_inputPath; }
    QUrl inputUrl() const { return m_inputUrl; }
    bool inputLoading() const { return m_inputLoading; }
    QString inputError() const { return m_inputError; }
    QVariantList waveformSamples() const { return m_waveformSamples; }
    QString transcript() const;
    bool processing() const;
    int progress() const;
    bool progressAvailable() const;
    bool canTranscribe() const;
    bool canTranscribeForProvider(ExecutionProvider provider, const QString &model,
                                  QString *error = nullptr) const;
    bool recording() const;
    double recordingLevel() const;
    QVariantList history() const;
    QString playbackPath() const;
    bool colabActive() const;
    bool colabPaired() const;
    QString colabModel() const { return m_colabModel; }
    QString colabNotebookFile() const;
    bool gatewayActive() const { return m_selectedProvider == ExecutionProvider::ApiGateway; }
    QString gatewayModel() const;

    QString language() const;
    void setLanguage(const QString &lang);
    int threads() const;
    void setThreads(int count);
    bool translate() const;
    void setTranslate(bool val);
    QVariantMap dynamicSettings() const;
    void setDynamicSettings(const QVariantMap &settings);
    void setColabModel(const QString &model);
    void setGatewayModel(const QString &model);

    // Commands
    Q_INVOKABLE void selectFileInput(const QString &filePathOrUrl);
    Q_INVOKABLE void clearInput();
    Q_INVOKABLE void startRecording(bool systemAudio = false);
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void transcribeInput();
    // Used by composite workflows.  This is per-request routing: it neither
    // changes the UI's selected route nor falls back to another provider.
    void transcribeInputForProvider(ExecutionProvider provider, const QString &model,
                                    const QString &language, bool translate = false);
    Q_INVOKABLE void cancelProcessing();
    Q_INVOKABLE void clearTranscript();
    Q_INVOKABLE void copyTranscript();
    Q_INVOKABLE void loadHistoryItem(const QString &text, const QString &filePathOrUrl);

    // History playback / actions
    Q_INVOKABLE void deleteHistoryItem(const QString &id);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void playHistoryFile(const QString &filePath);
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE bool connectColab(const QString &workerUrl, const QString &bearerToken);
    Q_INVOKABLE bool selectColabModel(const QString &model);
    Q_INVOKABLE QString notebookForColabModel(const QString &model) const;
    Q_INVOKABLE void disconnectColab();
    Q_INVOKABLE void useColab();
    Q_INVOKABLE void useGateway();
    Q_INVOKABLE void disconnectGateway();
    Q_INVOKABLE void useLocal();

signals:
    void inputPathChanged();
    void inputUrlChanged();
    void inputLoadingChanged();
    void inputErrorChanged();
    void waveformSamplesChanged();
    void transcriptChanged();
    void processingChanged();
    void progressChanged();
    void progressAvailableChanged();
    void recordingChanged();
    void recordingLevelChanged();
    void historyChanged();
    void playbackPathChanged();
    
    void languageChanged();
    void threadsChanged();
    void translateChanged();
    void dynamicSettingsChanged();
    void colabStateChanged();
    void colabModelChanged();
    void gatewayStateChanged();
    void gatewayModelChanged();

    // Forward the timestamped backend result so composite workflows (such as
    // Dubbing) can reuse the shared STT session without duplicating inference.
    void transcriptionFinished(const QString &text, const QVariantList &segments);
    void transcriptionFailed(const QString &message);

private slots:
    void onDecoderFinished(const QVector<float> &samples);
    void onDecoderError(const QString &error);
    
    void onRecorderFinished(const QByteArray &pcmData);
    void onEngineTranscriptionFinished(const QString &text, const QVariantList &segments);
    void onHistoryChanged();
    void onPlaybackStateChanged();
    void onColabProgress(int percent);
    void onColabFinished(const QString &text, const QVariantList &segments);
    void onColabFailed(const QString &error);
    void onGatewayProgress(int percent);
    void onGatewayFinished(const QString &text, const QVariantList &segments);
    void onGatewayFailed(const QString &error);

private:
    void updateWaveform(const QVector<float> &samples);
    void selectProvider(ExecutionProvider provider);

    SttEngine* m_engine = nullptr;
    AudioRecorder* m_recorder = nullptr;
    AudioPlayer* m_player = nullptr;
    HistoryService* m_historyService = nullptr;
    Settings* m_settings = nullptr;
    ColabSession* m_colabSession = nullptr;
    ColabSttRunner* m_colabRunner = nullptr;
    QThread m_colabThread;
    GatewaySttRunner* m_gatewayRunner = nullptr;
    QThread m_gatewayThread;
    StudioSelectionRepository* m_repository = nullptr;

    QString m_inputPath;
    QUrl m_inputUrl;
    bool m_inputLoading = false;
    QString m_inputError;
    QVariantList m_waveformSamples;
    QVector<float> m_decodedSamples;
    QString m_transcript;

    SttJobSnapshot m_activeJob;
    SttAudioDecoder* m_activeDecoder = nullptr;
    QString m_playbackPath;
    QVariantMap m_dynamicSettings;
    QString m_colabModel;
    std::shared_ptr<std::atomic_bool> m_colabCancellation;
    bool m_colabProcessing = false;
    int m_colabProgress = 0;
    bool m_colabProgressAvailable = false;
    std::shared_ptr<std::atomic_bool> m_gatewayCancellation;
    bool m_gatewayProcessing = false;
    int m_gatewayProgress = 0;
    bool m_gatewayProgressAvailable = false;
    bool m_activateColabWhenVerified = false;
    ExecutionProvider m_selectedProvider = ExecutionProvider::LocalDev;
    ExecutionProvider m_activeProvider = ExecutionProvider::LocalDev;
};

} // namespace LAStudio

