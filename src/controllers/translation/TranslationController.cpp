#include "TranslationController.h"
#include "TranslationModelSession.h"
#include "core/Settings.h"
#include "remote/ColabSession.h"
#include "remote/ExecutionProvider.h"
#include "translation/ColabTranslationRunner.h"
#include "translation/GatewayTranslationRunner.h"
#include "translation/TranslationService.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

namespace LAStudio {
TranslationController::TranslationController(TranslationEngine *engine, TranslationModelSession *session,
                                             Settings *settings, ColabSession *colabSession, QObject *parent)
    : QObject(parent), m_engine(engine), m_session(session), m_settings(settings), m_colabSession(colabSession)
{
    qRegisterMetaType<TranslationInferenceRequest>("TranslationInferenceRequest");
    m_gatewayWorker = new GatewayTranslationRunner;
    m_colabWorker = new ColabTranslationRunner;
    m_gatewayWorker->moveToThread(&m_gatewayThread);
    m_colabWorker->moveToThread(&m_colabThread);
    connect(&m_gatewayThread, &QThread::finished, m_gatewayWorker, &QObject::deleteLater);
    connect(&m_colabThread, &QThread::finished, m_colabWorker, &QObject::deleteLater);
    connect(m_gatewayWorker, &GatewayTranslationRunner::progress, this, [this](int percent) {
        if (!m_processing || m_provider != Provider::Gateway) return;
        m_progress = percent;
        emit processingChanged();
    });
    connect(m_gatewayWorker, &GatewayTranslationRunner::finished,
            this, &TranslationController::completeTranslation);
    connect(m_gatewayWorker, &GatewayTranslationRunner::failed,
            this, &TranslationController::failTranslation);
    connect(m_colabWorker, &ColabTranslationRunner::progress, this, [this](int percent) {
        if (!m_processing || m_provider != Provider::Colab) return;
        m_progress = percent;
        emit processingChanged();
    });
    connect(m_colabWorker, &ColabTranslationRunner::finished,
            this, &TranslationController::completeTranslation);
    connect(m_colabWorker, &ColabTranslationRunner::failed,
            this, &TranslationController::failTranslation);
    m_gatewayThread.start();
    m_colabThread.start();
    m_autosave.setSingleShot(true);
    m_autosave.setInterval(750);
    connect(&m_autosave, &QTimer::timeout, this, &TranslationController::autosave);
    if (m_engine) {
        connect(m_engine, &TranslationEngine::progressChanged, this, [this]() {
            if (!m_processing) return;
            m_progress = m_engine->progress();
            emit processingChanged();
        });
        connect(m_engine, &TranslationEngine::translationFinished,
                this, &TranslationController::completeTranslation);
        connect(m_engine, &TranslationEngine::errorOccurred,
                this, &TranslationController::failTranslation);
    }
    if (m_session) {
        connect(m_session, &IModelSession::activeConfigurationChanged, this, [this] {
            if (m_provider != Provider::Gateway) return;
            ++m_routeRevision;
            m_provider = Provider::Local;
            emit gatewayStateChanged();
        });
    }
    if (m_colabSession) {
        connect(m_colabSession, &ColabSession::sessionChanged, this, [this] {
            ++m_routeRevision;
            if (m_provider != Provider::Colab) return;
            if (m_processing) cancel();
            if (!m_colabSession->hasVerifiedRoute(
                    QStringLiteral("translation"), m_colabModel)) {
                m_provider = Provider::Local;
            }
            emit colabStateChanged();
        });
        connect(m_colabSession, &ColabSession::verificationFinished, this,
                [this](bool success, const QString &message) {
            if (!m_activateColabWhenVerified) return;
            m_activateColabWhenVerified = false;
            if (success) useColab();
            else setError(message);
        });
    }
    if (m_settings) {
        connect(m_settings, &Settings::gatewayTranslationModelChanged,
                this, &TranslationController::gatewayModelChanged);
        connect(m_settings, &Settings::gatewayLlmModelChanged,
                this, &TranslationController::gatewayModelChanged);
    }
    loadHistory();
}
TranslationController::~TranslationController()
{
    if (m_cancelToken) m_cancelToken->store(true, std::memory_order_relaxed);
    if (m_gatewayWorker && m_gatewayThread.isRunning()) {
        QMetaObject::invokeMethod(m_gatewayWorker, "cancel", Qt::QueuedConnection);
    }
    if (m_colabWorker && m_colabThread.isRunning()) {
        QMetaObject::invokeMethod(m_colabWorker, "cancel", Qt::QueuedConnection);
    }
    m_gatewayThread.quit();
    m_colabThread.quit();
    m_gatewayThread.wait();
    m_colabThread.wait();
}
QString TranslationController::statusText() const { return m_processing ? QStringLiteral("Translating %1%").arg(m_progress) : (m_dirty ? QStringLiteral("Unsaved changes") : QStringLiteral("Ready")); }
void TranslationController::setSourceLanguage(const QString &value) { if (m_project.sourceLanguage == value) return; m_project.sourceLanguage = value; markDirty(); }
void TranslationController::setTargetLanguage(const QString &value) { if (m_project.targetLanguage == value) return; m_project.targetLanguage = value; markDirty(); }
QString TranslationController::gatewayModel() const { return m_settings ? (m_settings->gatewayTranslationModel().isEmpty() ? m_settings->gatewayLlmModel() : m_settings->gatewayTranslationModel()) : QString(); }
void TranslationController::setGatewayModel(const QString &value) { if (m_settings) m_settings->setGatewayTranslationModel(value); }
bool TranslationController::colabActive() const
{
    return m_provider == Provider::Colab && m_colabSession
        && m_colabSession->hasVerifiedRoute(QStringLiteral("translation"), m_colabModel);
}
QString TranslationController::notebookForColabModel(const QString &model) const
{
    const QString normalized = model.trimmed().toLower();
    if (normalized == QStringLiteral("m2m100-418m"))
        return QStringLiteral("LA_STUDIO_TRANSLATION_M2M100_418M_GPU.ipynb");
    if (normalized == QStringLiteral("madlad400-3b-mt"))
        return QStringLiteral("LA_STUDIO_TRANSLATION_MADLAD400_3B_GPU.ipynb");
    if (normalized == QStringLiteral("hy-mt2-1.8b"))
        return QStringLiteral("LA_STUDIO_TRANSLATION_HY_MT2_1_8B_GPU.ipynb");
    return {};
}
QString TranslationController::colabNotebookFile() const
{
    return notebookForColabModel(m_colabModel);
}
void TranslationController::setColabModel(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (notebookForColabModel(normalized).isEmpty()) {
        setError(QStringLiteral("No Colab notebook is mapped for translation model '%1'.").arg(value));
        return;
    }
    if (normalized == m_colabModel) return;
    if (m_processing && m_provider == Provider::Colab) cancel();
    ++m_routeRevision;
    if (m_colabSession
        && (m_colabSession->isActive() || m_colabSession->isChecking()))
        m_colabSession->clear();
    m_colabModel = normalized;
    emit colabModelChanged();
}
bool TranslationController::selectColabModel(const QString &model)
{
    const QString normalized = model.trimmed().toLower();
    if (notebookForColabModel(normalized).isEmpty()) {
        setError(QStringLiteral("No Colab notebook is mapped for translation model '%1'.").arg(model));
        return false;
    }
    setColabModel(normalized);
    return m_colabModel == normalized;
}
void TranslationController::newProject() { if (m_processing) return; m_project = TranslationProject(); m_dirty = false; m_error.clear(); emit projectChanged(); emit errorTextChanged(); }
bool TranslationController::openProject(const QString &path) { if (m_processing) return false; TranslationProject project; QString error; if (!TranslationProject::load(path, project, &error)) { setError(error); return false; } m_project = std::move(project); m_dirty = false; emit projectChanged(); return true; }
bool TranslationController::importText(const QString &text) { if (m_processing) return false; QString error; if (!TranslationProject::importText(text, m_project, &error)) { setError(error); return false; } m_project.sourcePath.clear(); markDirty(); return true; }
bool TranslationController::importFile(const QString &path) { if (m_processing) return false; QFile file(path); if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { setError(file.errorString()); return false; } const QString extension = QFileInfo(path).suffix().toLower(); QString error; const QString content = QString::fromUtf8(file.readAll()); const bool ok = extension == QStringLiteral("srt") || extension == QStringLiteral("vtt") ? TranslationProject::importSubtitle(content, extension, m_project, &error) : TranslationProject::importText(content, m_project, &error); if (!ok) { setError(error); return false; } m_project.sourcePath = QFileInfo(path).absoluteFilePath(); markDirty(); return true; }
bool TranslationController::saveProject() { QString error; if (!m_project.save(&error)) { setError(error); return false; } m_dirty = false; emit projectChanged(); return true; }
bool TranslationController::saveProjectAs(const QString &path) { if (path.isEmpty()) return false; m_project.projectPath = path; return saveProject(); }
bool TranslationController::exportResult(const QString &path) { if (path.isEmpty()) return false; QString error; const QString extension = QFileInfo(path).suffix().toLower(); QString output; if (extension == QStringLiteral("srt") || extension == QStringLiteral("vtt")) output = m_project.exportSubtitle(&error); else if (extension == QStringLiteral("json")) { QVariantMap root{{QStringLiteral("sourceLanguage"), m_project.sourceLanguage}, {QStringLiteral("targetLanguage"), m_project.targetLanguage}, {QStringLiteral("segments"), m_project.segments}}; output = QString::fromUtf8(QJsonDocument::fromVariant(root).toJson()); } else output = m_project.exportText(); if (!error.isEmpty()) { setError(error); return false; } QSaveFile file(path); if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { setError(file.errorString()); return false; } file.write(output.toUtf8()); if (!file.commit()) { setError(file.errorString()); return false; } return true; }
void TranslationController::updateSegment(int index, const QVariantMap &patch) { if (index < 0 || index >= m_project.segments.size() || m_processing) return; QVariantMap segment = m_project.segments.at(index).toMap(); for (auto it = patch.cbegin(); it != patch.cend(); ++it) segment.insert(it.key(), it.value()); m_project.segments[index] = segment; markDirty(); }
void TranslationController::removeSegment(int index) { if (index < 0 || index >= m_project.segments.size() || m_processing) return; m_project.segments.removeAt(index); markDirty(); }
void TranslationController::addSegment() { if (m_processing) return; m_project.segments.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-%1").arg(m_project.segments.size() + 1)}, {QStringLiteral("sourceText"), QString()}, {QStringLiteral("targetText"), QString()}, {QStringLiteral("state"), QStringLiteral("ready")}}); markDirty(); }
void TranslationController::swapLanguages() { const QString source = m_project.sourceLanguage; m_project.sourceLanguage = m_project.targetLanguage; m_project.targetLanguage = source; markDirty(); }
void TranslationController::translateAll() { startTranslation(m_project.segments); }
void TranslationController::translateSegment(int index) { if (index >= 0 && index < m_project.segments.size()) { const QVariantMap segment = m_project.segments.at(index).toMap(); startTranslation({segment}, segment.value(QStringLiteral("id")).toString()); } }
void TranslationController::cancel()
{
    if (!m_processing) return;
    if (m_cancelToken) m_cancelToken->store(true, std::memory_order_relaxed);
    if (m_provider == Provider::Gateway && m_gatewayWorker) {
        QMetaObject::invokeMethod(m_gatewayWorker, "cancel", Qt::QueuedConnection);
    } else if (m_provider == Provider::Colab && m_colabWorker) {
        QMetaObject::invokeMethod(m_colabWorker, "cancel", Qt::QueuedConnection);
    } else if (m_engine) {
        m_engine->cancelProcessing();
    }
}
void TranslationController::useGateway()
{
    if (!m_settings) { setError(QStringLiteral("API Gateway configuration is unavailable.")); return; }
    const RemoteEndpointValidation endpoint = validateRemoteEndpoint(m_settings->gatewayUrl(), RemoteEndpointKind::ApiGateway);
    if (!endpoint.isValid()) { setError(endpoint.error); return; }
    if (!m_settings->gatewayApiKeyConfigured()) { setError(QStringLiteral("API Gateway key is required.")); return; }
    if (gatewayModel().isEmpty()) { setError(QStringLiteral("API Gateway translation model is required.")); return; }
    if (m_provider != Provider::Gateway) {
        const bool colabWasSelected = m_provider == Provider::Colab;
        ++m_routeRevision;
        m_provider = Provider::Gateway;
        emit gatewayStateChanged();
        if (colabWasSelected) emit colabStateChanged();
    }
    m_error.clear();
    emit errorTextChanged();
}
bool TranslationController::connectColab(const QString &workerUrl, const QString &bearerToken)
{
    if (!m_colabSession) {
        setError(QStringLiteral("Colab session is unavailable."));
        return false;
    }
    QString error;
    m_activateColabWhenVerified = true;
    if (!m_colabSession->beginVerifiedSession(
            workerUrl, bearerToken, QStringLiteral("translation"), m_colabModel, &error)) {
        m_activateColabWhenVerified = false;
        setError(error);
        return false;
    }
    return true;
}
void TranslationController::useColab()
{
    if (m_colabModel.trimmed().isEmpty()) {
        setError(QStringLiteral("Colab translation model is required."));
        return;
    }
    if (!m_colabSession) {
        setError(QStringLiteral("Colab session is unavailable."));
        return;
    }
    QString routeError;
    if (!m_colabSession->hasVerifiedRoute(
            QStringLiteral("translation"), m_colabModel, &routeError)) {
        setError(routeError);
        return;
    }
    if (m_provider != Provider::Colab) {
        const bool gatewayWasSelected = m_provider == Provider::Gateway;
        ++m_routeRevision;
        m_provider = Provider::Colab;
        emit colabStateChanged();
        if (gatewayWasSelected) emit gatewayStateChanged();
    }
    m_error.clear();
    emit errorTextChanged();
}
void TranslationController::useLocal()
{
    if (m_settings && m_settings->remoteFirstMode()) {
        setError(QStringLiteral("Remote-first mode requires API Gateway or a direct Colab translation worker. Disable Remote-first mode before selecting Local Dev translation."));
        return;
    }
    if (m_processing || m_provider == Provider::Local) return;
    const Provider previous = m_provider;
    ++m_routeRevision;
    m_provider = Provider::Local;
    if (previous == Provider::Gateway) emit gatewayStateChanged();
    if (previous == Provider::Colab) emit colabStateChanged();
}
bool TranslationController::loadHistoryItem(const QString &id) { if (id.isEmpty() || m_processing) return false; for (const QVariant &historyValue : std::as_const(m_history)) { const QVariantMap item = historyValue.toMap(); if (item.value(QStringLiteral("id")).toString() != id) continue; TranslationProject project; project.sourceLanguage = item.value(QStringLiteral("sourceLanguage"), QStringLiteral("en")).toString(); project.targetLanguage = item.value(QStringLiteral("targetLanguage"), QStringLiteral("vi")).toString(); project.sourceFormat = item.value(QStringLiteral("sourceFormat"), QStringLiteral("text")).toString(); project.segments = item.value(QStringLiteral("segments")).toList(); if (project.segments.isEmpty()) return false; m_project = std::move(project); m_dirty = true; m_error.clear(); emit projectChanged(); emit errorTextChanged(); return true; } return false; }
bool TranslationController::deleteHistoryItem(const QString &id) { if (id.isEmpty()) return false; for (int index = 0; index < m_history.size(); ++index) { if (m_history.at(index).toMap().value(QStringLiteral("id")).toString() != id) continue; QVariantList updatedHistory = m_history; updatedHistory.removeAt(index); QSaveFile file(historyPath()); if (!file.open(QIODevice::WriteOnly)) return false; file.write(QJsonDocument::fromVariant(updatedHistory).toJson()); if (!file.commit()) return false; m_history = std::move(updatedHistory); emit historyChanged(); return true; } return false; }
void TranslationController::clearHistory() { m_history.clear(); QFile::remove(historyPath()); emit historyChanged(); }
void TranslationController::markDirty() { m_dirty = true; m_error.clear(); if (!m_project.projectPath.isEmpty()) m_autosave.start(); emit projectChanged(); emit errorTextChanged(); }
void TranslationController::setError(const QString &message) { m_error = message; if (m_session) m_session->setError(message); emit errorTextChanged(); }
void TranslationController::startTranslation(const QVariantList &segments, const QString &activeSegmentId)
{
    if (m_provider == Provider::Local && m_settings && m_settings->remoteFirstMode()) {
        setError(QStringLiteral("Remote-first mode requires API Gateway or a direct Colab translation worker."));
        return;
    }
    if (m_processing) { setError(QStringLiteral("A translation request is already running.")); return; }
    if (m_provider == Provider::Colab) {
        if (!m_colabSession) {
            setError(QStringLiteral("Colab session is unavailable."));
            return;
        }
        QString routeError;
        if (!m_colabSession->hasVerifiedRoute(
                QStringLiteral("translation"), m_colabModel, &routeError)) {
            setError(routeError);
            return;
        }
    }
    if (m_provider == Provider::Local && (!m_engine || !m_session || !m_session->activeConfiguration())) {
        setError(QStringLiteral("Select and load a Translation model and runtime first."));
        return;
    }
    int maxTokens = 4096;
    if (m_provider == Provider::Local) {
        TranslationRequest prepared;
        QString error;
        if (!TranslationService::prepareConfiguration(*m_session->activeConfiguration(),
                                                       m_project.sourceLanguage,
                                                       m_project.targetLanguage,
                                                       prepared, &error)) {
            setError(error);
            return;
        }
        maxTokens = prepared.maxTokens;
    }
    m_cancelToken = std::make_shared<std::atomic_bool>(false);
    TranslationInferenceRequest request;
    request.segments = segments;
    request.sourceLanguage = m_project.sourceLanguage;
    request.targetLanguage = m_project.targetLanguage;
    request.maxTokens = maxTokens;
    request.cancellation = InferenceCancellationToken(m_cancelToken);
    m_error.clear();
    m_processing = true;
    m_activeProvider = m_provider;
    m_activeRouteRevision = m_routeRevision;
    m_activeSegmentId = activeSegmentId;
    m_progress = 0;
    if (m_session) m_session->clearError();
    emit errorTextChanged();
    emit processingChanged();
    if (m_provider == Provider::Gateway) {
        QMetaObject::invokeMethod(m_gatewayWorker, "translate", Qt::QueuedConnection,
                                  Q_ARG(QString, m_settings->gatewayUrl()),
                                  Q_ARG(QString, m_settings->gatewayApiKey()),
                                  Q_ARG(QString, gatewayModel()),
                                  Q_ARG(TranslationInferenceRequest, request), Q_ARG(bool, false));
    } else if (m_provider == Provider::Colab) {
        QMetaObject::invokeMethod(m_colabWorker, "translate", Qt::QueuedConnection,
                                  Q_ARG(QUrl, m_colabSession->endpoint()),
                                  Q_ARG(QString, m_colabSession->bearerTokenForRequest()),
                                  Q_ARG(QString, m_colabModel),
                                  Q_ARG(TranslationInferenceRequest, request), Q_ARG(bool, false));
    } else {
        m_engine->translate(request);
    }
}
void TranslationController::completeTranslation(const QVariantList &patches)
{
    if (!m_processing) return;
    if (m_activeProvider != m_provider || m_activeRouteRevision != m_routeRevision) {
        m_processing = false;
        m_activeSegmentId.clear();
        m_progress = 0;
        m_cancelToken.reset();
        emit processingChanged();
        return;
    }
    m_processing = false;
    m_activeSegmentId.clear();
    m_progress = 100;
    m_cancelToken.reset();
    if (m_session) m_session->clearError();
    applyPatches(patches);
    addHistory();
    emit processingChanged();
}
void TranslationController::failTranslation(const QString &error)
{
    if (!m_processing) return;
    if (m_activeProvider != m_provider || m_activeRouteRevision != m_routeRevision) {
        m_processing = false;
        m_activeSegmentId.clear();
        m_progress = 0;
        m_cancelToken.reset();
        emit processingChanged();
        return;
    }
    const bool cancelled = !m_cancelToken || m_cancelToken->load(std::memory_order_relaxed);
    m_processing = false;
    m_activeSegmentId.clear();
    m_progress = 0;
    m_cancelToken.reset();
    if (!cancelled) setError(error);
    emit processingChanged();
}
void TranslationController::applyPatches(const QVariantList &patches) { for (const QVariant &patchValue : patches) { const QVariantMap patch = patchValue.toMap(); const QString id = patch.value(QStringLiteral("id")).toString(); for (int i = 0; i < m_project.segments.size(); ++i) { QVariantMap segment = m_project.segments.at(i).toMap(); if (segment.value(QStringLiteral("id")).toString() != id) continue; for (auto it = patch.cbegin(); it != patch.cend(); ++it) segment.insert(it.key(), it.value()); m_project.segments[i] = segment; break; } } markDirty(); }
void TranslationController::autosave() { if (m_dirty && !m_project.projectPath.isEmpty()) saveProject(); }
QString TranslationController::historyPath() const { const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/history"); QDir().mkpath(base); return base + QStringLiteral("/translation_history.json"); }
void TranslationController::loadHistory() { QFile file(historyPath()); if (!file.open(QIODevice::ReadOnly)) return; const QJsonDocument document = QJsonDocument::fromJson(file.readAll()); if (document.isArray()) m_history = document.array().toVariantList(); emit historyChanged(); }
void TranslationController::addHistory() { if (m_project.segments.isEmpty()) return; const QVariantMap first = m_project.segments.first().toMap(); const QVariantMap last = m_project.segments.last().toMap(); QVariantMap item{{QStringLiteral("id"), QString::number(QDateTime::currentMSecsSinceEpoch())}, {QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))}, {QStringLiteral("sourceLanguage"), m_project.sourceLanguage}, {QStringLiteral("targetLanguage"), m_project.targetLanguage}, {QStringLiteral("sourceFormat"), m_project.sourceFormat}, {QStringLiteral("sourcePreview"), first.value(QStringLiteral("sourceText")).toString()}, {QStringLiteral("targetPreview"), last.value(QStringLiteral("targetText")).toString()}, {QStringLiteral("segments"), m_project.segments}}; m_history.prepend(item); while (m_history.size() > 30) m_history.removeLast(); QSaveFile file(historyPath()); if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument::fromVariant(m_history).toJson()); file.commit(); } emit historyChanged(); }
} // namespace LAStudio
