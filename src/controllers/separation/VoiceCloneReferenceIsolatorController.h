#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqml.h>

namespace LAStudio {

class VoiceIsolatorController;
class ColabVoiceIsolatorController;

// Coordinates the existing Isolator controllers for Voice Cloning.  It owns
// no inference backend: Local work stays in VoiceIsolatorController and
// Direct Colab work stays in ColabVoiceIsolatorController.
class VoiceCloneReferenceIsolatorController final : public QObject
{
    Q_OBJECT
    QML_UNCREATABLE("VoiceCloneReferenceIsolatorController is managed by AppController")
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY stateChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath WRITE setSourcePath NOTIFY stateChanged)
    Q_PROPERTY(QString cloneReferencePath READ cloneReferencePath NOTIFY stateChanged)
    Q_PROPERTY(QString selectedRoute READ selectedRoute NOTIFY stateChanged)
    Q_PROPERTY(QString selectedModel READ selectedModel NOTIFY stateChanged)
    Q_PROPERTY(bool routeReady READ routeReady NOTIFY stateChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY stateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(bool resultReady READ resultReady NOTIFY stateChanged)
    Q_PROPERTY(QString vocalsPath READ vocalsPath NOTIFY stateChanged)
    Q_PROPERTY(QString backgroundPath READ backgroundPath NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)

public:
    explicit VoiceCloneReferenceIsolatorController(VoiceIsolatorController *local,
                                                   ColabVoiceIsolatorController *colab,
                                                   QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QString sourcePath() const { return m_sourcePath; }
    void setSourcePath(const QString &path);
    QString cloneReferencePath() const;
    QString selectedRoute() const;
    QString selectedModel() const;
    bool routeReady() const;
    bool processing() const { return m_processing; }
    int progress() const { return m_progress; }
    bool resultReady() const;
    QString vocalsPath() const { return m_vocalsPath; }
    QString backgroundPath() const { return m_backgroundPath; }
    QString lastError() const { return m_lastError; }
    QString statusText() const { return m_statusText; }

    Q_INVOKABLE bool start();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool retry();
    Q_INVOKABLE void clearResult();

signals:
    void stateChanged();

private:
    bool usingColab() const;
    QString sourceFingerprint(QString *error = nullptr) const;
    QString configurationFingerprint() const;
    bool loadCachedResult(const QString &key, const QString &sourceFingerprint,
                          const QString &configurationFingerprint);
    bool persistResult(const QString &key, const QString &vocals, const QString &background);
    bool validStem(const QString &path) const;
    void observeOwnedRun();
    void finishOwnedRun(bool colab);
    void setFailure(const QString &error);

    VoiceIsolatorController *m_local = nullptr;
    ColabVoiceIsolatorController *m_colab = nullptr;
    bool m_enabled = false;
    bool m_processing = false;
    bool m_ownedRun = false;
    bool m_ownedColabRun = false;
    int m_progress = 0;
    QString m_sourcePath;
    QString m_resultKey;
    QString m_resultConfigurationKey;
    QString m_vocalsPath;
    QString m_backgroundPath;
    QString m_lastError;
    QString m_statusText;
};

} // namespace LAStudio
