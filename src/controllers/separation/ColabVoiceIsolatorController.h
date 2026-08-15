#pragma once

#include <QObject>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantList>
#include <QtQml/qqml.h>

#include "separation/ColabSeparationRunner.h"

#include <atomic>
#include <memory>

namespace LAStudio {

class ColabSession;
class Settings;

class ColabVoiceIsolatorController final : public QObject
{
    Q_OBJECT
    QML_UNCREATABLE("ColabVoiceIsolatorController is managed by AppController")
    Q_PROPERTY(bool colabActive READ colabActive NOTIFY colabStateChanged)
    Q_PROPERTY(bool colabConnected READ colabConnected NOTIFY colabStateChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QString colabNotebookFile READ colabNotebookFile NOTIFY modelChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath WRITE setSourcePath NOTIFY stateChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY stateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString vocalsPath READ vocalsPath NOTIFY stateChanged)
    Q_PROPERTY(QString backgroundPath READ backgroundPath NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString warning READ warning NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(QVariantList recentResults READ recentResults NOTIFY stateChanged)
    Q_PROPERTY(QVariantList vocalsSamples READ vocalsSamples NOTIFY vocalsSamplesChanged)
    Q_PROPERTY(QVariantList backgroundSamples READ backgroundSamples NOTIFY backgroundSamplesChanged)

public:
    explicit ColabVoiceIsolatorController(ColabSession *session, Settings *settings, QObject *parent = nullptr);
    ~ColabVoiceIsolatorController() override;

    bool colabActive() const { return m_colabActive; }
    bool colabConnected() const;
    QString model() const { return m_model; }
    void setModel(const QString &model);
    QString colabNotebookFile() const;
    QString sourcePath() const { return m_sourcePath; }
    bool processing() const { return m_processing; }
    int progress() const { return m_progress; }
    QString vocalsPath() const { return m_vocalsPath; }
    QString backgroundPath() const { return m_backgroundPath; }
    QString lastError() const { return m_lastError; }
    QString warning() const { return m_warning; }
    bool ready() const { return m_colabActive && colabConnected(); }
    QVariantList recentResults() const { return m_recentResults; }
    QVariantList vocalsSamples() const { return m_vocalsSamples; }
    QVariantList backgroundSamples() const { return m_backgroundSamples; }

    void setSourcePath(const QString &path);
    Q_INVOKABLE bool connectColab(const QString &workerUrl, const QString &bearerToken);
    Q_INVOKABLE bool selectColabModel(const QString &model);
    Q_INVOKABLE QString notebookForColabModel(const QString &model) const;
    Q_INVOKABLE void useColab();
    Q_INVOKABLE void useLocal();
    Q_INVOKABLE void isolate(bool fast = false);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearResult();
    Q_INVOKABLE bool exportStem(const QString &sourcePath, const QString &destinationPath);
    Q_INVOKABLE void openRecent(const QString &vocalsPath, const QString &backgroundPath);

signals:
    void colabStateChanged();
    void modelChanged();
    void stateChanged();
    void vocalsSamplesChanged();
    void backgroundSamplesChanged();
    void errorOccurred(const QString &error);

private slots:
    void onSessionChanged();
    void onRemoteFirstModeChanged();
    void onRunnerProgress(int percent);
    void onRunnerFinished(const LAStudio::ColabSeparationResult &result);
    void onRunnerFailed(const QString &error);

private:
    // Waveform decoding is deliberately asynchronous.  A large lossless stem
    // must never make the desktop UI look frozen while its preview is built.
    void loadSamples(const QString &path, bool vocals);
    void setError(const QString &error);

    ColabSession *m_session = nullptr;
    Settings *m_settings = nullptr;
    ColabSeparationRunner *m_runner = nullptr;
    QThread m_thread;
    std::shared_ptr<std::atomic_bool> m_cancellation;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    bool m_colabActive = false;
    bool m_activateColabWhenVerified = false;
    QString m_model = QStringLiteral("sherpa-onnx-spleeter-2stems-fp16");
    bool m_processing = false;
    int m_progress = 0;
    QString m_sourcePath;
    QString m_vocalsPath;
    QString m_backgroundPath;
    QString m_lastError;
    QString m_warning;
    QVariantList m_vocalsSamples;
    QVariantList m_backgroundSamples;
    QVariantList m_recentResults;
    quint64 m_sessionRevision = 0;
    quint64 m_activeSessionRevision = 0;
};

} // namespace LAStudio
