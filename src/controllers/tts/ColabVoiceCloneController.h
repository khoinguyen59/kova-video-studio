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
class ColabVoiceCloneRunner;
class HistoryService;
class WaveformProvider;

// Direct Colab voice-cloning controller. It uses only the temporary in-memory
// Colab session and deliberately has no API Gateway dependency.
class ColabVoiceCloneController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool colabActive READ colabActive NOTIFY colabStateChanged)
    Q_PROPERTY(bool colabConnected READ colabConnected NOTIFY colabStateChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressStage READ progressStage NOTIFY progressChanged)
    Q_PROPERTY(QString profileId READ profileId NOTIFY profileChanged)
    Q_PROPERTY(QByteArray lastPcm READ lastPcm NOTIFY outputChanged)
    Q_PROPERTY(QVector<float> lastSamples READ lastSamples NOTIFY outputChanged)
    Q_PROPERTY(QVariantList lastSamplePreview READ lastSamplePreview NOTIFY outputChanged)
    Q_PROPERTY(int lastSampleCount READ lastSampleCount NOTIFY outputChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY outputChanged)

public:
    explicit ColabVoiceCloneController(ColabSession *session, AudioPlayer *player,
                                       WaveformProvider *waveformProvider,
                                       HistoryService *history, QObject *parent = nullptr);
    ~ColabVoiceCloneController() override;

    bool colabActive() const { return m_colabActive; }
    bool colabConnected() const;
    bool processing() const { return m_processing; }
    int progress() const { return m_progress; }
    QString progressStage() const { return m_progressStage; }
    QString profileId() const { return m_profileId; }
    QByteArray lastPcm() const { return m_lastPcm; }
    QVector<float> lastSamples() const { return m_lastSamples; }
    QVariantList lastSamplePreview() const;
    int lastSampleCount() const { return m_lastSamples.size(); }
    int sampleRate() const { return m_sampleRate; }

    Q_INVOKABLE bool connectColab(const QString &workerUrl, const QString &bearerToken);
    Q_INVOKABLE void useColab();
    Q_INVOKABLE void useLocal();
    Q_INVOKABLE void cloneVoice(const QString &text, const QString &referencePath,
                                const QString &referenceText, const QString &language,
                                const QString &profileName, bool consentConfirmed,
                                float speed = 1.0F, int steps = 32);
    Q_INVOKABLE void clearProfile();
    Q_INVOKABLE void deleteColabProfile();
    Q_INVOKABLE void cancelProcessing();
    Q_INVOKABLE void playOutput(qint64 positionMs = 0);
    Q_INVOKABLE void saveWav(const QString &path);

signals:
    void colabStateChanged();
    void processingChanged();
    void progressChanged();
    void profileChanged();
    void outputChanged();
    void synthesisFinished(const QByteArray &pcm16, int sampleRate);
    void errorOccurred(const QString &error);

private slots:
    void onSessionChanged();
    void onRunnerProgress(int percent, const QString &stage);
    void onProfileReady(const QString &profileId);
    void onProfileDeleted();
    void onRunnerFinished(const QByteArray &pcm16, const QVector<float> &samples, int sampleRate);
    void onRunnerFailed(const QString &error);

private:
    QString referenceSignature(const QString &referencePath, const QString &referenceText,
                               const QString &language) const;

    ColabSession *m_session = nullptr;
    AudioPlayer *m_player = nullptr;
    WaveformProvider *m_waveformProvider = nullptr;
    HistoryService *m_history = nullptr;
    ColabVoiceCloneRunner *m_runner = nullptr;
    QThread m_thread;
    std::shared_ptr<std::atomic_bool> m_cancellation;
    bool m_colabActive = false;
    bool m_processing = false;
    int m_progress = 0;
    QString m_progressStage;
    QString m_profileId;
    QString m_profileSignature;
    bool m_profileDeletionPending = false;
    quint64 m_sessionRevision = 0;
    quint64 m_activeSessionRevision = 0;
    QString m_activeText;
    QByteArray m_lastPcm;
    QVector<float> m_lastSamples;
    int m_sampleRate = 0;
};

} // namespace LAStudio
