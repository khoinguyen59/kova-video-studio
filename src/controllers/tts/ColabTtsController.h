#pragma once

#include <QObject>
#include <QByteArray>
#include <QThread>
#include <QVariantList>
#include <QVector>

#include <atomic>
#include <memory>

namespace LAStudio {

class AudioPlayer;
class ColabSession;
class ColabTtsRunner;
class HistoryService;
class Settings;
class WaveformProvider;

// Direct Colab TTS controller. It uses only the in-memory Colab session and
// never reads API Gateway URL, credentials, or models.
class ColabTtsController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool colabActive READ colabActive NOTIFY colabStateChanged)
    Q_PROPERTY(bool colabConnected READ colabConnected NOTIFY colabStateChanged)
    Q_PROPERTY(QString colabModel READ colabModel WRITE setColabModel NOTIFY colabModelChanged)
    Q_PROPERTY(QString colabNotebookFile READ colabNotebookFile NOTIFY colabModelChanged)
    Q_PROPERTY(QString colabVoice READ colabVoice WRITE setColabVoice NOTIFY colabVoiceChanged)
    Q_PROPERTY(QString colabLanguage READ colabLanguage WRITE setColabLanguage NOTIFY colabLanguageChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QByteArray lastPcm READ lastPcm NOTIFY outputChanged)
    Q_PROPERTY(QVector<float> lastSamples READ lastSamples NOTIFY outputChanged)
    Q_PROPERTY(QVariantList lastSamplePreview READ lastSamplePreview NOTIFY outputChanged)
    Q_PROPERTY(int lastSampleCount READ lastSampleCount NOTIFY outputChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY outputChanged)

public:
    explicit ColabTtsController(ColabSession *session, Settings *settings, AudioPlayer *player,
                                WaveformProvider *waveformProvider,
                                HistoryService *history, QObject *parent = nullptr);
    ~ColabTtsController() override;

    bool colabActive() const { return m_colabActive; }
    bool colabConnected() const;
    QString colabModel() const { return m_colabModel; }
    void setColabModel(const QString &model);
    QString colabNotebookFile() const;
    QString colabVoice() const { return m_colabVoice; }
    void setColabVoice(const QString &voice);
    QString colabLanguage() const { return m_colabLanguage; }
    void setColabLanguage(const QString &language);
    bool processing() const { return m_processing; }
    int progress() const { return m_progress; }
    QByteArray lastPcm() const { return m_lastPcm; }
    QVector<float> lastSamples() const { return m_lastSamples; }
    QVariantList lastSamplePreview() const;
    int lastSampleCount() const { return m_lastSamples.size(); }
    int sampleRate() const { return m_sampleRate; }

    Q_INVOKABLE bool connectColab(const QString &workerUrl, const QString &bearerToken);
    Q_INVOKABLE bool selectColabModel(const QString &model);
    Q_INVOKABLE QString notebookForColabModel(const QString &model) const;
    Q_INVOKABLE void useColab();
    Q_INVOKABLE void useLocal();
    Q_INVOKABLE void deactivateColab();
    Q_INVOKABLE void synthesize(const QString &text, float speed = 1.0F);
    Q_INVOKABLE void cancelProcessing();
    Q_INVOKABLE void playOutput(qint64 positionMs = 0);
    Q_INVOKABLE void saveWav(const QString &path);

signals:
    void colabStateChanged();
    void colabModelChanged();
    void colabVoiceChanged();
    void colabLanguageChanged();
    void processingChanged();
    void progressChanged();
    void outputChanged();
    void synthesisFinished(const QByteArray &pcm16, int sampleRate);
    void errorOccurred(const QString &error);

private slots:
    void onSessionChanged();
    void onRunnerProgress(int percent);
    void onRunnerFinished(const QByteArray &pcm16, const QVector<float> &samples, int sampleRate);
    void onRunnerFailed(const QString &error);

private:
    ColabSession *m_session = nullptr;
    Settings *m_settings = nullptr;
    AudioPlayer *m_player = nullptr;
    WaveformProvider *m_waveformProvider = nullptr;
    HistoryService *m_history = nullptr;
    ColabTtsRunner *m_runner = nullptr;
    QThread m_thread;
    std::shared_ptr<std::atomic_bool> m_cancellation;
    bool m_colabActive = false;
    bool m_activateColabWhenVerified = false;
    QString m_colabModel = QStringLiteral("kokoro");
    QString m_colabVoice = QStringLiteral("af_heart");
    QString m_colabLanguage = QStringLiteral("en");
    bool m_processing = false;
    int m_progress = 0;
    QByteArray m_lastPcm;
    QVector<float> m_lastSamples;
    int m_sampleRate = 0;
    QString m_activeText;
    quint64 m_sessionRevision = 0;
    quint64 m_activeSessionRevision = 0;
};

} // namespace LAStudio
