#pragma once

#include <QObject>
#include <QFutureWatcher>
#include <QAtomicInteger>
#include <QPointer>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <memory>

namespace LAStudio {

class SttSessionController;
class ModelManager;
class RuntimeManager;
class ColabSession;
class ColabAlignmentRunner;
struct ColabAlignmentResult;

class DubbingTranscriptionJob final : public QObject
{
    Q_OBJECT
public:
    DubbingTranscriptionJob(SttSessionController *stt, ModelManager *models,
                            RuntimeManager *runtimes, QObject *parent = nullptr);
    ~DubbingTranscriptionJob() override;

    bool running() const { return m_running; }
    bool start(const QString &language, const QString &audioPath,
               const QString &fallbackAudioPath = QString(),
               const QVariantMap &configuration = QVariantMap());
    void setAlignmentSession(ColabSession *session);
    void cancel();

signals:
    void progressChanged(int progress);
    void completed(const QVariantList &segments);
    void failed(const QString &message);

private slots:
    void onTranscriptionFinished(const QString &text, const QVariantList &segments);
    void onAlignmentFinished();
    void onColabAlignmentFinished(const LAStudio::ColabAlignmentResult &result);
    void onColabAlignmentFailed(const QString &message);

private:
    void beginAlignment(const QVariantList &segments);
    void startColabAlignment(const QVariantList &segments);
    QVariantList applyColabAlignment(const QVariantList &segments,
                                     const QVariantList &alignedWords) const;
    void completeWithoutAlignment(const QVariantList &segments,
                                  const QString &diagnostic);
    void startAudioInput(const QString &audioPath);
    void beginTranscriptionAfterInputReady();
    void fail(const QString &message);

    SttSessionController *m_stt = nullptr;
    ModelManager *m_models = nullptr;
    RuntimeManager *m_runtimes = nullptr;
    QFutureWatcher<QVariantMap> *m_alignmentWatcher = nullptr;
    std::shared_ptr<QAtomicInteger<bool>> m_alignmentCancel;
    QPointer<ColabSession> m_alignmentSession;
    ColabAlignmentRunner *m_colabAlignmentRunner = nullptr;
    QThread m_colabAlignmentThread;
    std::shared_ptr<std::atomic_bool> m_colabAlignmentCancel;
    QVariantList m_pendingAlignmentSegments;
    QString m_audioPath;
    QString m_fallbackAudioPath;
    QString m_language;
    QString m_executionProviderId = QStringLiteral("local-dev");
    QString m_modelId;
    QString m_alignmentModelId = QStringLiteral("mms-forced-aligner-onnx");
    bool m_refineAlignmentWithColab = false;
    bool m_waitingForInput = false;
    bool m_inputLoadStarted = false;
    bool m_running = false;
    bool m_retriedWithFallback = false;
    quint64 m_generation = 0;
};

} // namespace LAStudio
