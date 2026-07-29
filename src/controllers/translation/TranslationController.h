#pragma once

#include "translation/TranslationEngine.h"
#include "translation/TranslationProject.h"

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QtQml/qqml.h>

#include <atomic>
#include <memory>

namespace LAStudio {

class TranslationModelSession;
class Settings;
class ColabSession;
class GatewayTranslationRunner;
class ColabTranslationRunner;

class TranslationController final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("TranslationController is managed by AppController")
    Q_PROPERTY(QVariantList segments READ segments NOTIFY projectChanged)
    Q_PROPERTY(QString sourceLanguage READ sourceLanguage WRITE setSourceLanguage NOTIFY projectChanged)
    Q_PROPERTY(QString targetLanguage READ targetLanguage WRITE setTargetLanguage NOTIFY projectChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    Q_PROPERTY(QString sourceFormat READ sourceFormat NOTIFY projectChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY projectChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
    Q_PROPERTY(QString activeSegmentId READ activeSegmentId NOTIFY processingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY processingChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY processingChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    // These identify the selected execution route, not connection state of the
    // other provider. Selecting one never edits the other's credentials.
    Q_PROPERTY(bool gatewayActive READ gatewayActive NOTIFY gatewayStateChanged)
    Q_PROPERTY(QString gatewayModel READ gatewayModel WRITE setGatewayModel NOTIFY gatewayModelChanged)
    Q_PROPERTY(bool colabActive READ colabActive NOTIFY colabStateChanged)
    Q_PROPERTY(QString colabModel READ colabModel WRITE setColabModel NOTIFY colabModelChanged)
    Q_PROPERTY(QString colabNotebookFile READ colabNotebookFile NOTIFY colabModelChanged)

public:
    TranslationController(TranslationEngine *engine, TranslationModelSession *session,
                          Settings *settings, ColabSession *colabSession, QObject *parent = nullptr);
    ~TranslationController() override;

    QVariantList segments() const { return m_project.segments; }
    QString sourceLanguage() const { return m_project.sourceLanguage; }
    QString targetLanguage() const { return m_project.targetLanguage; }
    QString projectPath() const { return m_project.projectPath; }
    QString sourceFormat() const { return m_project.sourceFormat; }
    bool dirty() const { return m_dirty; }
    bool processing() const { return m_processing; }
    QString activeSegmentId() const { return m_activeSegmentId; }
    int progress() const { return m_progress; }
    QString statusText() const;
    QString errorText() const { return m_error; }
    QVariantList history() const { return m_history; }
    bool gatewayActive() const { return m_provider == Provider::Gateway; }
    QString gatewayModel() const;
    bool colabActive() const;
    QString colabModel() const { return m_colabModel; }
    QString colabNotebookFile() const;

    void setSourceLanguage(const QString &value);
    void setTargetLanguage(const QString &value);
    void setGatewayModel(const QString &value);
    void setColabModel(const QString &value);
    Q_INVOKABLE bool selectColabModel(const QString &model);
    Q_INVOKABLE QString notebookForColabModel(const QString &model) const;

    Q_INVOKABLE void newProject();
    Q_INVOKABLE bool openProject(const QString &path);
    Q_INVOKABLE bool importText(const QString &text);
    Q_INVOKABLE bool importFile(const QString &path);
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool saveProjectAs(const QString &path);
    Q_INVOKABLE bool exportResult(const QString &path);
    Q_INVOKABLE void updateSegment(int index, const QVariantMap &patch);
    Q_INVOKABLE void removeSegment(int index);
    Q_INVOKABLE void addSegment();
    Q_INVOKABLE void swapLanguages();
    Q_INVOKABLE void translateAll();
    Q_INVOKABLE void translateSegment(int index);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool loadHistoryItem(const QString &id);
    Q_INVOKABLE bool deleteHistoryItem(const QString &id);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void useGateway();
    Q_INVOKABLE bool connectColab(const QString &workerUrl, const QString &bearerToken);
    Q_INVOKABLE void useColab();
    Q_INVOKABLE void useLocal();

signals:
    void projectChanged();
    void processingChanged();
    void errorTextChanged();
    void historyChanged();
    void gatewayStateChanged();
    void gatewayModelChanged();
    void colabStateChanged();
    void colabModelChanged();

private:
    enum class Provider { Local, Gateway, Colab };

    void markDirty();
    void setError(const QString &message);
    void startTranslation(const QVariantList &segments, const QString &activeSegmentId = QString());
    void applyPatches(const QVariantList &patches);
    void completeTranslation(const QVariantList &patches);
    void failTranslation(const QString &error);
    void autosave();
    QString historyPath() const;
    void loadHistory();
    void addHistory();

    TranslationEngine *m_engine = nullptr;
    TranslationModelSession *m_session = nullptr;
    Settings *m_settings = nullptr;
    ColabSession *m_colabSession = nullptr;
    GatewayTranslationRunner *m_gatewayWorker = nullptr;
    ColabTranslationRunner *m_colabWorker = nullptr;
    QThread m_gatewayThread;
    QThread m_colabThread;
    TranslationProject m_project;
    QTimer m_autosave;
    std::shared_ptr<std::atomic_bool> m_cancelToken;
    bool m_dirty = false;
    bool m_processing = false;
    Provider m_provider = Provider::Local;
    Provider m_activeProvider = Provider::Local;
    bool m_activateColabWhenVerified = false;
    QString m_colabModel = QStringLiteral("m2m100-418m");
    quint64 m_routeRevision = 0;
    quint64 m_activeRouteRevision = 0;
    QString m_activeSegmentId;
    int m_progress = 0;
    QString m_error;
    QVariantList m_history;
};

} // namespace LAStudio
