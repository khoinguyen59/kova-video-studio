#include "LlmChatController.h"
#include "llm/ColabChatRunner.h"
#include "llm/LlmChatEngine.h"
#include "core/Settings.h"
#include "core/PathUtils.h"
#include "core/Logger.h"
#include "remote/ColabSession.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <utility>

namespace LAStudio {
namespace {
QString storePath() { return QDir(PathUtils::dataDir()).filePath(QStringLiteral("llm-conversations.json")); }
}

LlmChatController::LlmChatController(LlmChatEngine *engine, LlmChatModelSession *session,
                                     Settings *settings, ColabSession *colabSession, QObject *parent)
    : QObject(parent), m_engine(engine), m_session(session), m_settings(settings), m_colabSession(colabSession)
{
    qRegisterMetaType<ColabChatRequest>("ColabChatRequest");
    if (m_engine) {
        connect(m_engine, &LlmChatEngine::tokenGenerated, this, &LlmChatController::onToken);
        connect(m_engine, &LlmChatEngine::generationFinished, this, &LlmChatController::onFinished);
        connect(m_engine, &LlmChatEngine::generationCancelled, this, &LlmChatController::onCancelled);
        connect(m_engine, &LlmChatEngine::errorOccurred, this, &LlmChatController::onEngineError);
        connect(m_engine, &LlmChatEngine::gatewayActiveChanged, this, &LlmChatController::gatewayStateChanged);
        connect(m_engine, &LlmChatEngine::modelLoadedChanged,
                this, &LlmChatController::onEngineModelLoadedChanged);
    }
    if (m_settings) {
        connect(m_settings, &Settings::gatewayLlmModelChanged,
                this, &LlmChatController::gatewayModelChanged);
    }
    m_colabRunner = new ColabChatRunner;
    m_colabRunner->moveToThread(&m_colabThread);
    connect(&m_colabThread, &QThread::finished, m_colabRunner, &QObject::deleteLater);
    connect(m_colabRunner, &ColabChatRunner::tokenGenerated, this, &LlmChatController::onToken);
    connect(m_colabRunner, &ColabChatRunner::finished, this, &LlmChatController::onFinished);
    connect(m_colabRunner, &ColabChatRunner::cancelled, this, &LlmChatController::onCancelled);
    connect(m_colabRunner, &ColabChatRunner::failed, this, &LlmChatController::onColabError);
    m_colabThread.start();
    if (m_colabSession) {
        connect(m_colabSession, &ColabSession::sessionChanged, this, [this] {
            if (m_provider != Provider::Colab) return;
            if (m_generating) stopGeneration();
            if (!m_colabSession->hasVerifiedRoute(
                    QStringLiteral("llm-chat"), m_colabModel)) {
                m_provider = Provider::Local;
            }
            emit colabStateChanged();
            emit gatewayStateChanged();
        });
        connect(m_colabSession, &ColabSession::verificationFinished, this,
                [this](bool success, const QString &message) {
            if (!m_activateColabWhenVerified) return;
            m_activateColabWhenVerified = false;
            if (success) useColab();
            else setError(message);
        });
    }
    load();
    ensureActive();
}
LlmChatController::~LlmChatController()
{
    if (m_colabRunner && m_colabThread.isRunning()) {
        QMetaObject::invokeMethod(m_colabRunner, "cancel", Qt::QueuedConnection);
    }
    m_colabThread.quit();
    m_colabThread.wait();
}
void LlmChatController::setSystemPrompt(const QString &v) { if (m_systemPrompt == v) return; m_systemPrompt = v; emit settingsChanged(); persist(); }
void LlmChatController::setContextTokens(int v) { v = qBound(512, v, 131072); if (m_contextTokens == v) return; m_contextTokens = v; emit settingsChanged(); persist(); }
void LlmChatController::setMaxTokens(int v) { v = qBound(1, v, 32768); if (m_maxTokens == v) return; m_maxTokens = v; emit settingsChanged(); persist(); }
void LlmChatController::setTemperature(double v) { v = qBound(0.01, v, 2.0); if (qFuzzyCompare(m_temperature, v)) return; m_temperature = v; emit settingsChanged(); persist(); }
void LlmChatController::setTopP(double v) { v = qBound(0.01, v, 1.0); if (qFuzzyCompare(m_topP, v)) return; m_topP = v; emit settingsChanged(); persist(); }
void LlmChatController::setTopK(int v) { v = qBound(1, v, 200); if (m_topK == v) return; m_topK = v; emit settingsChanged(); persist(); }
void LlmChatController::setRepeatPenalty(double v) { v = qBound(0.8, v, 2.0); if (qFuzzyCompare(m_repeatPenalty, v)) return; m_repeatPenalty = v; emit settingsChanged(); persist(); }

bool LlmChatController::gatewayActive() const
{
    return m_provider == Provider::Gateway && m_engine && m_engine->isGatewayActive();
}

QString LlmChatController::gatewayModel() const
{
    return m_settings ? m_settings->gatewayLlmModel() : QString();
}

void LlmChatController::setGatewayModel(const QString &value)
{
    if (m_settings) m_settings->setGatewayLlmModel(value);
}
bool LlmChatController::colabActive() const
{
    return m_provider == Provider::Colab && m_colabSession
        && m_colabSession->hasVerifiedRoute(QStringLiteral("llm-chat"), m_colabModel);
}
QString LlmChatController::notebookForColabModel(const QString &model) const
{
    const QString normalized = model.trimmed().toLower();
    if (normalized == QStringLiteral("qwen3.5-2b"))
        return QStringLiteral("LA_STUDIO_LLM_QWEN3_5_2B_GPU.ipynb");
    return {};
}
QString LlmChatController::colabNotebookFile() const
{
    return notebookForColabModel(m_colabModel);
}
void LlmChatController::setColabModel(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (notebookForColabModel(normalized).isEmpty()) {
        setError(QStringLiteral("No Colab notebook is mapped for chat model '%1'.").arg(value));
        return;
    }
    if (normalized == m_colabModel) return;
    if (m_generating) stopGeneration();
    if (m_colabSession
        && (m_colabSession->isActive() || m_colabSession->isChecking()))
        m_colabSession->clear();
    m_colabModel = normalized;
    emit colabModelChanged();
}
bool LlmChatController::selectColabModel(const QString &model)
{
    const QString normalized = model.trimmed().toLower();
    if (notebookForColabModel(normalized).isEmpty()) {
        setError(QStringLiteral("No Colab notebook is mapped for chat model '%1'.").arg(model));
        return false;
    }
    setColabModel(normalized);
    return m_colabModel == normalized;
}
void LlmChatController::selectProvider(Provider provider)
{
    if (m_provider == provider) return;
    const Provider previous = m_provider;
    m_provider = provider;
    if (previous == Provider::Gateway || provider == Provider::Gateway) emit gatewayStateChanged();
    if (previous == Provider::Colab || provider == Provider::Colab) emit colabStateChanged();
}

QString LlmChatController::newId() const { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
void LlmChatController::ensureActive()
{
    if (!m_activeId.isEmpty()) return;
    newConversation();
}
void LlmChatController::newConversation()
{
    if (m_generating) return;
    QVariantMap item{{QStringLiteral("id"), newId()}, {QStringLiteral("title"), QStringLiteral("New chat")},
                     {QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    m_conversations.prepend(item);
    m_activeId = item.value(QStringLiteral("id")).toString();
    m_messages.clear();
    m_streamingMessageIndex = -1;
    emit conversationsChanged(); emit messagesChanged(); emit activeConversationChanged(); persist();
}
void LlmChatController::selectConversation(const QString &id)
{
    if (m_generating || id.isEmpty() || id == m_activeId) return;
    for (const QVariant &value : std::as_const(m_conversations)) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("id")).toString() == id) {
            m_activeId = id; m_messages = item.value(QStringLiteral("messages")).toList(); m_streamingMessageIndex = -1;
            m_systemPrompt = item.value(QStringLiteral("systemPrompt")).toString();
            emit activeConversationChanged(); emit messagesChanged(); emit settingsChanged(); return;
        }
    }
}
void LlmChatController::renameConversation(const QString &id, const QString &title)
{
    for (int i = 0; i < m_conversations.size(); ++i) {
        QVariantMap item = m_conversations.at(i).toMap();
        if (item.value(QStringLiteral("id")).toString() == id) { item.insert(QStringLiteral("title"), title.trimmed().isEmpty() ? QStringLiteral("New chat") : title.trimmed()); m_conversations[i] = item; emit conversationsChanged(); persist(); return; }
    }
}
void LlmChatController::deleteConversation(const QString &id)
{
    if (m_generating) return;
    for (int i = 0; i < m_conversations.size(); ++i) if (m_conversations.at(i).toMap().value(QStringLiteral("id")).toString() == id) { m_conversations.removeAt(i); break; }
    if (m_activeId == id) { m_activeId.clear(); m_messages.clear(); m_streamingMessageIndex = -1; ensureActive(); }
    emit conversationsChanged(); emit messagesChanged(); persist();
}
void LlmChatController::clearConversation() { if (m_generating) return; m_messages.clear(); m_streamingMessageIndex = -1; emit messagesChanged(); persist(); }

void LlmChatController::sendMessage(const QString &text)
{
    const QString content = text.trimmed();
    const bool selectedColab = m_provider == Provider::Colab;
    if (selectedColab) {
        if (!m_colabSession) {
            setError(QStringLiteral("Colab session is unavailable."));
            return;
        }
        QString routeError;
        if (!m_colabSession->hasVerifiedRoute(
                QStringLiteral("llm-chat"), m_colabModel, &routeError)) {
            setError(routeError);
            return;
        }
    }
    const bool directColab = selectedColab;
    if (m_provider == Provider::Local && m_settings && m_settings->remoteFirstMode()) {
        setError(QStringLiteral("Remote-first mode requires API Gateway or a direct Colab chat worker."));
        return;
    }
    if (content.isEmpty() || m_generating || (!directColab && (!m_engine || !m_engine->isModelLoaded()))) {
        if (!directColab && (!m_engine || !m_engine->isModelLoaded())) {
            setError(QStringLiteral("Load an LLM model or connect a Colab GPU worker before sending a message."));
        }
        return;
    }
    QVariantMap user{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), content}, {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    m_messages.append(user);
    QVariantMap assistant{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), QString()}, {QStringLiteral("streaming"), true}};
    m_messages.append(assistant);
    m_streamingMessageIndex = m_messages.size() - 1;
    if (m_messages.size() == 2) { for (int i = 0; i < m_conversations.size(); ++i) { QVariantMap item = m_conversations.at(i).toMap(); if (item.value(QStringLiteral("id")).toString() == m_activeId) { item.insert(QStringLiteral("title"), content.left(48)); m_conversations[i] = item; break; } } emit conversationsChanged(); }
    QVariantList requestMessages;
    if (!m_systemPrompt.trimmed().isEmpty()) requestMessages.append(QVariantMap{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), m_systemPrompt}});
    for (const QVariant &value : m_messages) { const QVariantMap msg = value.toMap(); if (msg.value(QStringLiteral("role")).toString() != QStringLiteral("assistant") || !msg.value(QStringLiteral("streaming")).toBool()) requestMessages.append(msg); }
    QList<QVariantMap> messages; for (const QVariant &value : requestMessages) messages.append(value.toMap());
    m_requestId = newId(); setGenerating(true); m_error.clear(); emit errorTextChanged(); emit messagesChanged(); persist();
    if (directColab) {
        ColabChatRequest request;
        request.workerUrl = m_colabSession->endpoint();
        request.bearerToken = m_colabSession->bearerTokenForRequest();
        request.model = m_colabModel;
        request.messages = messages;
        request.maxTokens = m_maxTokens;
        request.contextTokens = m_contextTokens;
        request.temperature = float(m_temperature);
        request.topP = float(m_topP);
        request.topK = m_topK;
        request.repeatPenalty = float(m_repeatPenalty);
        request.requestId = m_requestId;
        QMetaObject::invokeMethod(m_colabRunner, "generate", Qt::QueuedConnection,
                                  Q_ARG(ColabChatRequest, request));
    } else {
        m_engine->generate(messages, m_contextTokens, m_maxTokens, float(m_temperature), float(m_topP), m_topK, float(m_repeatPenalty), m_requestId);
    }
}
void LlmChatController::stopGeneration()
{
    if (!m_generating) return;
    if (colabActive() && m_colabRunner) QMetaObject::invokeMethod(m_colabRunner, "cancel", Qt::QueuedConnection);
    else if (m_engine) m_engine->cancel();
}
void LlmChatController::useGateway()
{
    if (!m_engine || !m_settings) {
        setError(QStringLiteral("API Gateway configuration is unavailable."));
        return;
    }
    selectProvider(Provider::Gateway);
    m_engine->loadGateway(m_settings->gatewayUrl(), m_settings->gatewayApiKey(),
                          m_settings->gatewayLlmModel());
}
bool LlmChatController::connectColab(const QString &workerUrl, const QString &bearerToken)
{
    if (!m_colabSession) { setError(QStringLiteral("Colab session is unavailable.")); return false; }
    QString error;
    m_activateColabWhenVerified = true;
    if (!m_colabSession->beginVerifiedSession(
            workerUrl, bearerToken, QStringLiteral("llm-chat"), m_colabModel, &error)) {
        m_activateColabWhenVerified = false;
        setError(error);
        return false;
    }
    return true;
}
void LlmChatController::useColab()
{
    if (m_colabModel.trimmed().isEmpty()) { setError(QStringLiteral("Colab chat model is required.")); return; }
    if (!m_colabSession) { setError(QStringLiteral("Colab session is unavailable.")); return; }
    QString routeError;
    if (!m_colabSession->hasVerifiedRoute(
            QStringLiteral("llm-chat"), m_colabModel, &routeError)) {
        setError(routeError);
        return;
    }
    selectProvider(Provider::Colab);
    m_error.clear();
    emit errorTextChanged();
}
void LlmChatController::useLocal()
{
    if (m_settings && m_settings->remoteFirstMode()) {
        setError(QStringLiteral("Remote-first mode requires API Gateway or a direct Colab chat worker. Disable Remote-first mode before selecting Local Dev chat."));
        return;
    }
    if (!m_generating) selectProvider(Provider::Local);
}
void LlmChatController::regenerateLastResponse()
{
    if (m_generating || m_messages.size() < 2) return;
    const QVariantMap last = m_messages.last().toMap(); if (last.value(QStringLiteral("role")).toString() == QStringLiteral("assistant")) m_messages.removeLast();
    const QVariantMap user = m_messages.last().toMap(); if (user.value(QStringLiteral("role")).toString() == QStringLiteral("user")) { m_messages.removeLast(); sendMessage(user.value(QStringLiteral("content")).toString()); }
}
void LlmChatController::copyMessage(const QString &text) { if (QGuiApplication::clipboard()) QGuiApplication::clipboard()->setText(text); }
void LlmChatController::setError(const QString &message) { if (m_error == message) return; m_error = message; emit errorTextChanged(); emit statusChanged(); }
void LlmChatController::setGenerating(bool value) { if (m_generating == value) return; m_generating = value; emit generatingChanged(); emit statusChanged(); }
void LlmChatController::onToken(const QString &id, const QString &token)
{
    if (id != m_requestId || m_streamingMessageIndex < 0 || m_streamingMessageIndex >= m_messages.size()) return;
    QVariantMap item = m_messages.at(m_streamingMessageIndex).toMap();
    if (item.value(QStringLiteral("role")).toString() != QStringLiteral("assistant")) return;
    item.insert(QStringLiteral("content"), item.value(QStringLiteral("content")).toString() + token);
    m_messages[m_streamingMessageIndex] = item;
    emit messagesChanged();
}
void LlmChatController::onFinished(const QString &id, const QString &text)
{
    if (id != m_requestId) return;
    if (m_streamingMessageIndex >= 0 && m_streamingMessageIndex < m_messages.size()) {
        QVariantMap item = m_messages.at(m_streamingMessageIndex).toMap();
        if (item.value(QStringLiteral("role")).toString() == QStringLiteral("assistant")) {
            item.insert(QStringLiteral("content"), text);
            item.remove(QStringLiteral("streaming"));
            m_messages[m_streamingMessageIndex] = item;
        }
    }
    m_streamingMessageIndex = -1;
    setGenerating(false); persist(); emit messagesChanged();
}
void LlmChatController::onCancelled(const QString &id, const QString &text)
{
    if (id != m_requestId) return;
    if (m_streamingMessageIndex >= 0 && m_streamingMessageIndex < m_messages.size()) {
        QVariantMap item = m_messages.at(m_streamingMessageIndex).toMap();
        if (item.value(QStringLiteral("role")).toString() == QStringLiteral("assistant")) {
            item.insert(QStringLiteral("content"), text);
            item.insert(QStringLiteral("finishReason"), QStringLiteral("cancelled"));
            item.remove(QStringLiteral("streaming"));
            m_messages[m_streamingMessageIndex] = item;
        }
    }
    m_streamingMessageIndex = -1;
    setGenerating(false); persist(); emit messagesChanged();
}
void LlmChatController::onEngineError(const QString &message)
{
    setError(message); setGenerating(false);
    if (m_streamingMessageIndex >= 0 && m_streamingMessageIndex < m_messages.size()
        && m_messages.at(m_streamingMessageIndex).toMap().value(QStringLiteral("role")).toString() == QStringLiteral("assistant")) {
        m_messages.removeAt(m_streamingMessageIndex);
    }
    m_streamingMessageIndex = -1;
    emit messagesChanged(); persist();
}
void LlmChatController::onColabError(const QString &requestId, const QString &message)
{
    if (requestId != m_requestId) return;
    onEngineError(message);
}
void LlmChatController::onEngineModelLoadedChanged()
{
    // A local-model reload coming from the model picker is an explicit local
    // selection. It leaves the Colab session in memory but routes new chat
    // messages back to the local engine.
    if (m_provider == Provider::Colab && m_engine && m_engine->isModelLoaded()
        && !m_engine->isGatewayActive()) {
        selectProvider(Provider::Local);
    }
}

void LlmChatController::load()
{
    QFile file(storePath()); if (!file.open(QIODevice::ReadOnly)) return;
    QJsonParseError error; const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error); if (error.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonObject root = doc.object(); m_activeId = root.value(QStringLiteral("activeId")).toString(); m_systemPrompt = root.value(QStringLiteral("systemPrompt")).toString(); m_contextTokens = root.value(QStringLiteral("contextTokens")).toInt(m_contextTokens); m_maxTokens = root.value(QStringLiteral("maxTokens")).toInt(m_maxTokens); m_temperature = root.value(QStringLiteral("temperature")).toDouble(m_temperature); m_topP = root.value(QStringLiteral("topP")).toDouble(m_topP); m_topK = root.value(QStringLiteral("topK")).toInt(m_topK); m_repeatPenalty = root.value(QStringLiteral("repeatPenalty")).toDouble(m_repeatPenalty);
    m_conversations = root.value(QStringLiteral("conversations")).toArray().toVariantList();
    for (const QVariant &value : std::as_const(m_conversations)) if (value.toMap().value(QStringLiteral("id")).toString() == m_activeId) { m_messages = value.toMap().value(QStringLiteral("messages")).toList(); break; }
}
void LlmChatController::persist()
{
    for (int i = 0; i < m_conversations.size(); ++i) { QVariantMap item = m_conversations.at(i).toMap(); if (item.value(QStringLiteral("id")).toString() == m_activeId) { item.insert(QStringLiteral("messages"), m_messages); item.insert(QStringLiteral("systemPrompt"), m_systemPrompt); item.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)); m_conversations[i] = item; break; } }
    QJsonObject root; root.insert(QStringLiteral("schemaVersion"), 1); root.insert(QStringLiteral("activeId"), m_activeId); root.insert(QStringLiteral("systemPrompt"), m_systemPrompt); root.insert(QStringLiteral("contextTokens"), m_contextTokens); root.insert(QStringLiteral("maxTokens"), m_maxTokens); root.insert(QStringLiteral("temperature"), m_temperature); root.insert(QStringLiteral("topP"), m_topP); root.insert(QStringLiteral("topK"), m_topK); root.insert(QStringLiteral("repeatPenalty"), m_repeatPenalty); root.insert(QStringLiteral("conversations"), QJsonArray::fromVariantList(m_conversations));
    QSaveFile file(storePath()); if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)); file.commit(); }
}
} // namespace LAStudio
