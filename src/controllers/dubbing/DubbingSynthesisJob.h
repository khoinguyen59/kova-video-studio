#pragma once

#include <QObject>
#include <QAtomicInteger>
#include <QByteArray>
#include <QPointer>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <memory>

#include "dubbing/DubbingVoiceReferenceSelector.h"

namespace LAStudio {

class TtsEngine;
class Settings;
class ColabSession;
class GatewayTtsRunner;
class ColabTtsRunner;
class ColabVoiceCloneRunner;
enum class ExecutionProvider;

class DubbingSynthesisJob final : public QObject
{
    Q_OBJECT
public:
    explicit DubbingSynthesisJob(TtsEngine *tts, QObject *parent = nullptr);
    ~DubbingSynthesisJob() override;

    bool running() const { return m_running; }
    bool start(const QVariantList &segments, const QString &projectPath,
               const QVariantMap &settings, const QString &runId);
    void cancel();
    // Each direct worker keeps its own temporary URL/token. Gateway settings
    // are independent and no route reads credentials from another route.
    void setRemoteServices(Settings *settings, ColabSession *ttsSession,
                           ColabSession *voiceCloneSession);

signals:
    void progressChanged(int progress);
    void segmentUpdated(int index, const QVariantMap &segment);
    void completed(const QVariantList &segments);
    void failed(const QString &message);

private slots:
    void onSynthesisFinished(const QByteArray &pcm16, int sampleRate);
    void onTtsError(const QString &message);

private:
    void startCurrentChunk();
    void startRemoteSynthesis(const QString &text, const QVariantMap &requestSettings);
    void startColabVoiceClone(const QString &text, const QVariantMap &requestSettings,
                              quint64 requestId);
    void commitSynthesizedAudio(const QVector<float> &samples, int sampleRate);
    void onRemoteProgress(int progress, quint64 requestId);
    void fitGeneratedSegments();
    void fail(const QString &message);

    TtsEngine *m_tts = nullptr;
    QPointer<Settings> m_gatewaySettings;
    QPointer<ColabSession> m_colabTtsSession;
    QPointer<ColabSession> m_colabVoiceCloneSession;
    GatewayTtsRunner *m_gatewayRunner = nullptr;
    ColabTtsRunner *m_colabRunner = nullptr;
    ColabVoiceCloneRunner *m_colabVoiceCloneRunner = nullptr;
    QThread m_remoteThread;
    bool m_running = false;
    bool m_waitingForModel = false;
    QVariantList m_pendingSegments;
    QString m_pendingProjectPath;
    QVariantMap m_pendingSettings;
    QString m_pendingRunId;
    QVariantList m_segments;
    QString m_projectPath;
    QVariantMap m_settings;
    QVariantMap m_cacheSettings;
    QString m_synthesisSignature;
    ExecutionProvider m_executionProvider;
    DubbingVoiceReference m_voiceReference;
    QString m_cloneVoicePresetId;
    QString m_cloneVoicePresetName;
    QString m_colabVoiceProfileId;
    QString m_colabVoiceProfileSignature;
    bool m_useVoiceCloning = false;
    bool m_legacyCloneSettings = false;
    bool m_forceSegmentDuration = false;
    QString m_runId;
    QString m_nodeRunId;
    int m_generationIndex = -1;
    int m_synthesisTotal = 0;
    int m_synthesisCompleted = 0;
    QVariantList m_chunks;
    int m_chunkIndex = -1;
    QVector<float> m_chunkSamples;
    int m_chunkSampleRate = 0;
    std::shared_ptr<QAtomicInteger<bool>> m_timingCancelled;
    std::shared_ptr<std::atomic_bool> m_remoteCancellation;
    quint64 m_timingRequestId = 0;
    quint64 m_remoteRequestId = 0;
    QMetaObject::Connection m_remoteProgressConnection;
    QMetaObject::Connection m_remoteFinishedConnection;
    QMetaObject::Connection m_remoteFailedConnection;
    QMetaObject::Connection m_remoteProfileConnection;
    QMetaObject::Connection m_colabTtsSessionConnection;
    QMetaObject::Connection m_colabVoiceCloneSessionConnection;
};

} // namespace LAStudio
