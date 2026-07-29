#pragma once

#include "dubbing/DubbingDuration.h"
#include "translation/backends/TranslationBackend.h"
#include "translation/TranslationService.h"

#include <QObject>
#include <QPointer>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>

namespace LAStudio {

class ModelManager;
class RuntimeManager;
class TtsEngine;
class TranslationEngine;
class TranslationEngineInstance;
class Settings;
class ColabSession;
class GatewayTranslationRunner;
class ColabTranslationRunner;

class DubbingTranslationJob final : public QObject
{
    Q_OBJECT
public:
    DubbingTranslationJob(TranslationEngine *translation, ModelManager *models,
                          RuntimeManager *runtimes, TtsEngine *tts,
                          QObject *parent = nullptr);
    ~DubbingTranslationJob() override;

    bool running() const { return m_running; }
    bool start(const QString &sourceLanguage, const QString &targetLanguage,
               const QVariantList &segments, const QVariantMap &configuration,
               const QString &runId);
    void cancel();
    // These are intentionally injected separately from local runtimes. A
    // Dubbing node can use either remote provider without passing credentials
    // or traffic through the other provider.
    void setRemoteServices(Settings *settings, ColabSession *colabSession);

signals:
    void progressChanged(int progress);
    void completed(const QVariantList &segments);
    void failed(const QString &message);

private:
    void onTranslationFinished(const QVariantList &patches, quint64 generation);
    void finishDurationTranslation();
    void fail(const QString &message);
    void setProgressForWorker();
    void startRemoteTranslation(const QString &providerId, const QString &modelId);
    void onRemoteProgress(int progress, quint64 generation);
    bool prepareRequest(const QString &sourceLanguage, const QString &targetLanguage,
                        const QVariantMap &configuration, TranslationRequest &request,
                        QString *error) const;

    QPointer<TranslationEngine> m_translation;
    QPointer<ModelManager> m_models;
    QPointer<RuntimeManager> m_runtimes;
    QPointer<TtsEngine> m_tts;
    QPointer<Settings> m_settings;
    QPointer<ColabSession> m_colabSession;
    QPointer<TranslationEngineInstance> m_instance;
    GatewayTranslationRunner *m_gatewayRunner = nullptr;
    ColabTranslationRunner *m_colabRunner = nullptr;
    QThread m_remoteThread;
    QVariantList m_inputSegments;
    QVariantList m_result;
    TranslationInferenceRequest m_pendingRequest;
    std::shared_ptr<std::atomic_bool> m_remoteCancellation;
    DubbingDurationSettings m_durationSettings;
    double m_durationRate = 10.0;
    bool m_durationAware = false;
    bool m_running = false;
    QString m_phase;
    QString m_sourceLanguage;
    QString m_targetLanguage;
    QString m_runId;
    QString m_remoteProviderId;
    quint64 m_generation = 0;
    QMetaObject::Connection m_finishedConnection;
    QMetaObject::Connection m_errorConnection;
    QMetaObject::Connection m_loadedConnection;
    QMetaObject::Connection m_progressConnection;
    QMetaObject::Connection m_remoteProgressConnection;
    QMetaObject::Connection m_remoteFinishedConnection;
    QMetaObject::Connection m_remoteFailedConnection;
    QMetaObject::Connection m_colabSessionConnection;
};

} // namespace LAStudio
