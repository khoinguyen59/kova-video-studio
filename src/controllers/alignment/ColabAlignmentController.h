#pragma once

#include <QObject>
#include <QThread>
#include <QVariantList>
#include <QtQml/qqml.h>

#include <atomic>
#include <memory>

namespace LAStudio {

class ColabSession;
class ColabAlignmentRunner;
class Settings;
struct ColabAlignmentResult;

// Direct Colab alignment controller. Gateway credentials never enter this
// class; Settings only controls the explicit Remote-first execution policy.
class ColabAlignmentController final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("ColabAlignmentController is managed by AppController")

    Q_PROPERTY(bool colabActive READ colabActive NOTIFY colabStateChanged)
    Q_PROPERTY(bool colabConnected READ colabConnected NOTIFY colabStateChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QString colabNotebookFile READ colabNotebookFile NOTIFY modelChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY stateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString output READ output NOTIFY resultChanged)
    Q_PROPERTY(QVariantList segments READ segments NOTIFY resultChanged)
    Q_PROPERTY(double duration READ duration NOTIFY resultChanged)
    Q_PROPERTY(QVariantList diagnostics READ diagnostics NOTIFY resultChanged)
    Q_PROPERTY(double averageConfidence READ averageConfidence NOTIFY resultChanged)
    Q_PROPERTY(QVariantList karaokeLines READ karaokeLines NOTIFY resultChanged)

public:
    explicit ColabAlignmentController(ColabSession *session, Settings *settings, QObject *parent = nullptr);
    ~ColabAlignmentController() override;

    bool colabActive() const { return m_colabActive; }
    bool colabConnected() const;
    QString model() const { return m_model; }
    void setModel(const QString &model);
    QString colabNotebookFile() const;
    bool processing() const { return m_processing; }
    int progress() const { return m_progress; }
    QString statusText() const { return m_statusText; }
    QString errorMessage() const { return m_errorMessage; }
    QString output() const { return m_output; }
    QVariantList segments() const { return m_segments; }
    double duration() const { return m_duration; }
    QVariantList diagnostics() const { return m_diagnostics; }
    double averageConfidence() const;
    QVariantList karaokeLines() const;

    Q_INVOKABLE bool connectColab(const QString &workerUrl, const QString &bearerToken);
    Q_INVOKABLE bool selectColabModel(const QString &model);
    Q_INVOKABLE QString notebookForColabModel(const QString &model) const;
    Q_INVOKABLE void useColab();
    Q_INVOKABLE void useLocal();
    Q_INVOKABLE bool runAlignment(const QString &audioPath, const QString &transcript,
                                  const QString &language, const QString &outputFormat);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearResult();
    Q_INVOKABLE int segmentIndexAt(double seconds) const;
    Q_INVOKABLE int karaokeLineIndexAt(double seconds) const;

signals:
    void colabStateChanged();
    void modelChanged();
    void stateChanged();
    void resultChanged();
    void completed();
    void failed(const QString &message);

private slots:
    void onSessionChanged();
    void onRemoteFirstModeChanged();
    void onRunnerProgress(int percent);
    void onRunnerFinished(const LAStudio::ColabAlignmentResult &result);
    void onRunnerFailed(const QString &error);

private:
    void setError(const QString &message);

    ColabSession *m_session = nullptr;
    Settings *m_settings = nullptr;
    ColabAlignmentRunner *m_runner = nullptr;
    QThread m_thread;
    std::shared_ptr<std::atomic_bool> m_cancellation;
    bool m_colabActive = false;
    bool m_activateColabWhenVerified = false;
    QString m_model = QStringLiteral("mms-forced-aligner-onnx");
    bool m_processing = false;
    int m_progress = 0;
    QString m_statusText = QStringLiteral("Colab worker not connected");
    QString m_errorMessage;
    QString m_output;
    QVariantList m_segments;
    QVariantList m_diagnostics;
    double m_duration = 0.0;
    quint64 m_sessionRevision = 0;
    quint64 m_activeSessionRevision = 0;
};

} // namespace LAStudio
