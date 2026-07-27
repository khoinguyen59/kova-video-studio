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
class ColabVoiceDesignRunner;
class HistoryService;
class WaveformProvider;

// Direct Qwen3 VoiceDesign controller. The temporary Colab session is the
// only remote dependency; this class never reads Gateway configuration.
class ColabVoiceDesignController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool colabActive READ colabActive NOTIFY colabStateChanged)
    Q_PROPERTY(bool colabConnected READ colabConnected NOTIFY colabStateChanged)
    Q_PROPERTY(QString model READ model CONSTANT)
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QByteArray lastPcm READ lastPcm NOTIFY outputChanged)
    Q_PROPERTY(QVector<float> lastSamples READ lastSamples NOTIFY outputChanged)
    Q_PROPERTY(QVariantList lastSamplePreview READ lastSamplePreview NOTIFY outputChanged)
    Q_PROPERTY(int lastSampleCount READ lastSampleCount NOTIFY outputChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY outputChanged)

public:
    explicit ColabVoiceDesignController(ColabSession *session, AudioPlayer *player,
                                        WaveformProvider *waveformProvider,
                                        HistoryService *history, QObject *parent = nullptr);
    ~ColabVoiceDesignController() override;

    bool colabActive() const { return m_colabActive; }
    bool colabConnected() const;
    QString model() const { return QStringLiteral("qwen3-tts-1.7b-voicedesign"); }
    bool processing() const { return m_processing; }
    int progress() const { return m_progress; }
    QByteArray lastPcm() const { return m_lastPcm; }
    QVector<float> lastSamples() const { return m_lastSamples; }
    QVariantList lastSamplePreview() const;
    int lastSampleCount() const { return m_lastSamples.size(); }
    int sampleRate() const { return m_sampleRate; }

    Q_INVOKABLE bool connectColab(const QString &workerUrl, const QString &bearerToken);
    Q_INVOKABLE void useColab();
    Q_INVOKABLE void useLocal();
    Q_INVOKABLE void generate(const QString &text, const QString &voiceDescription,
                              const QString &style, const QString &language,
                              float temperature = 0.9F, qint64 seed = -1);
    Q_INVOKABLE void cancelProcessing();
    Q_INVOKABLE void playOutput(qint64 positionMs = 0);
    Q_INVOKABLE void saveWav(const QString &path);

signals:
    void colabStateChanged();
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
    AudioPlayer *m_player = nullptr;
    WaveformProvider *m_waveformProvider = nullptr;
    HistoryService *m_history = nullptr;
    ColabVoiceDesignRunner *m_runner = nullptr;
    QThread m_thread;
    std::shared_ptr<std::atomic_bool> m_cancellation;
    bool m_colabActive = false;
    bool m_processing = false;
    int m_progress = 0;
    QString m_activeText;
    QString m_activeDescription;
    QByteArray m_lastPcm;
    QVector<float> m_lastSamples;
    int m_sampleRate = 0;
};

} // namespace LAStudio
