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
class GatewayTtsRunner;
class HistoryService;
class Settings;
class WaveformProvider;

// A direct API Gateway route for standard OpenAI-compatible TTS.  It does not
// use Colab sessions, worker URLs, or their temporary tokens.
class GatewayTtsController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool gatewayActive READ gatewayActive NOTIFY gatewayStateChanged)
    Q_PROPERTY(QString gatewayModel READ gatewayModel WRITE setGatewayModel NOTIFY gatewayModelChanged)
    Q_PROPERTY(QString gatewayVoice READ gatewayVoice WRITE setGatewayVoice NOTIFY gatewayVoiceChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QByteArray lastPcm READ lastPcm NOTIFY outputChanged)
    Q_PROPERTY(QVector<float> lastSamples READ lastSamples NOTIFY outputChanged)
    Q_PROPERTY(QVariantList lastSamplePreview READ lastSamplePreview NOTIFY outputChanged)
    Q_PROPERTY(int lastSampleCount READ lastSampleCount NOTIFY outputChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY outputChanged)

public:
    explicit GatewayTtsController(Settings *settings, AudioPlayer *player,
                                  WaveformProvider *waveformProvider,
                                  HistoryService *history, QObject *parent = nullptr);
    ~GatewayTtsController() override;

    bool gatewayActive() const { return m_gatewayActive; }
    QString gatewayModel() const;
    void setGatewayModel(const QString &model);
    QString gatewayVoice() const;
    void setGatewayVoice(const QString &voice);
    bool processing() const { return m_processing; }
    int progress() const { return m_progress; }
    QByteArray lastPcm() const { return m_lastPcm; }
    QVector<float> lastSamples() const { return m_lastSamples; }
    QVariantList lastSamplePreview() const;
    int lastSampleCount() const { return m_lastSamples.size(); }
    int sampleRate() const { return m_sampleRate; }

    Q_INVOKABLE void useGateway();
    Q_INVOKABLE void disconnectGateway();
    Q_INVOKABLE void synthesize(const QString &text, float speed = 1.0F);
    Q_INVOKABLE void cancelProcessing();
    Q_INVOKABLE void playOutput(qint64 positionMs = 0);
    Q_INVOKABLE void saveWav(const QString &path);

signals:
    void gatewayStateChanged();
    void gatewayModelChanged();
    void gatewayVoiceChanged();
    void processingChanged();
    void progressChanged();
    void outputChanged();
    void synthesisFinished(const QByteArray &pcm16, int sampleRate);
    void errorOccurred(const QString &error);

private slots:
    void onRunnerProgress(int percent);
    void onRunnerFinished(const QByteArray &pcm16, const QVector<float> &samples, int sampleRate);
    void onRunnerFailed(const QString &error);

private:
    Settings *m_settings = nullptr;
    AudioPlayer *m_player = nullptr;
    WaveformProvider *m_waveformProvider = nullptr;
    HistoryService *m_history = nullptr;
    GatewayTtsRunner *m_runner = nullptr;
    QThread m_thread;
    std::shared_ptr<std::atomic_bool> m_cancellation;
    bool m_gatewayActive = false;
    bool m_processing = false;
    int m_progress = 0;
    QByteArray m_lastPcm;
    QVector<float> m_lastSamples;
    int m_sampleRate = 0;
    QString m_activeText;
    QString m_activeVoice;
};

} // namespace LAStudio
