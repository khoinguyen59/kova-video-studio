#include "GatewayClient.h"

#include "ExecutionProvider.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace LAStudio {

namespace {

QString responseErrorMessage(const QByteArray &body, int statusCode)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject()) {
        const QJsonObject root = document.object();
        const QJsonValue error = root.value(QStringLiteral("error"));
        if (error.isObject()) {
            const QString message = error.toObject().value(QStringLiteral("message")).toString().trimmed();
            if (!message.isEmpty()) return message;
        }
        const QString message = root.value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty()) return message;
    }
    return QStringLiteral("Gateway returned HTTP %1").arg(statusCode);
}

QString contentFromResponse(const QJsonObject &root)
{
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty() || !choices.first().isObject()) return {};
    const QJsonObject choice = choices.first().toObject();
    const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
    if (delta.value(QStringLiteral("content")).isString()) {
        return delta.value(QStringLiteral("content")).toString();
    }
    const QJsonObject message = choice.value(QStringLiteral("message")).toObject();
    return message.value(QStringLiteral("content")).toString();
}

} // namespace

bool GatewayClient::configure(const QString &baseUrl, const QString &apiKey, const QString &model,
                              bool allowInsecureLocalhost, QString *errorMessage)
{
    const RemoteEndpointValidation endpoint = validateRemoteEndpoint(
        baseUrl, RemoteEndpointKind::ApiGateway, allowInsecureLocalhost);
    const QString normalizedKey = apiKey.trimmed();
    const QString normalizedModel = model.trimmed();
    if (!endpoint.isValid()) {
        if (errorMessage) *errorMessage = endpoint.error;
        return false;
    }
    if (normalizedKey.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("API Gateway key is required");
        return false;
    }
    if (normalizedModel.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("API Gateway chat model is required");
        return false;
    }

    m_baseUrl = endpoint.normalizedUrl;
    m_apiKey = normalizedKey;
    m_model = normalizedModel;
    return true;
}

bool GatewayClient::isConfigured() const
{
    return m_baseUrl.isValid() && !m_apiKey.isEmpty() && !m_model.isEmpty();
}

void GatewayClient::clear()
{
    cancel();
    m_baseUrl = {};
    m_apiKey.clear();
    m_model.clear();
}

bool GatewayClient::streamChat(const QList<QVariantMap> &messages, const ChatOptions &options,
                               const std::shared_ptr<std::atomic_bool> &cancelToken,
                               const std::function<void(const QString &)> &tokenHandler,
                               QString *fullText, QString *errorMessage)
{
    if (fullText) fullText->clear();
    if (!isConfigured()) {
        if (errorMessage) *errorMessage = QStringLiteral("API Gateway is not configured");
        return false;
    }

    QJsonArray requestMessages;
    for (const QVariantMap &message : messages) {
        const QString role = message.value(QStringLiteral("role")).toString().trimmed();
        const QString content = message.value(QStringLiteral("content")).toString();
        if (!role.isEmpty()) {
            requestMessages.append(QJsonObject{{QStringLiteral("role"), role},
                                               {QStringLiteral("content"), content}});
        }
    }
    if (requestMessages.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("A chat message is required");
        return false;
    }

    const QJsonObject payload{
        {QStringLiteral("model"), m_model},
        {QStringLiteral("messages"), requestMessages},
        {QStringLiteral("stream"), true},
        {QStringLiteral("max_tokens"), options.maxTokens},
        {QStringLiteral("temperature"), options.temperature},
        {QStringLiteral("top_p"), options.topP}
    };

    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_baseUrl, QStringLiteral("chat/completions")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());
    request.setRawHeader("Accept", "text/event-stream, application/json");

    QNetworkReply *reply = manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_activeReply = reply;
    QByteArray pending;
    QByteArray responseBody;
    QString accumulated;
    QString parseError;
    QEventLoop eventLoop;

    const auto consumePayload = [&](QByteArray line) {
        line = line.trimmed();
        if (line.isEmpty() || line == QByteArrayLiteral("[DONE]")) return;
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject()) {
            parseError = QStringLiteral("API Gateway returned an invalid chat response");
            return;
        }
        const QString token = contentFromResponse(document.object());
        if (!token.isEmpty()) {
            accumulated += token;
            if (tokenHandler) tokenHandler(token);
        }
    };
    const auto consumeAvailable = [&]() {
        const QByteArray bytes = reply->readAll();
        pending += bytes;
        responseBody += bytes;
        while (true) {
            const int lineEnd = pending.indexOf('\n');
            if (lineEnd < 0) break;
            QByteArray line = pending.left(lineEnd);
            pending.remove(0, lineEnd + 1);
            if (line.startsWith("data:")) line.remove(0, 5);
            consumePayload(line);
        }
    };

    QObject::connect(reply, &QNetworkReply::readyRead, &eventLoop, consumeAvailable);
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    consumeAvailable();
    if (!pending.trimmed().isEmpty()) {
        QByteArray finalPayload = pending.trimmed();
        if (finalPayload.startsWith("data:")) finalPayload.remove(0, 5);
        consumePayload(finalPayload);
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    m_activeReply = nullptr;
    reply->deleteLater();

    if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
        if (fullText) *fullText = accumulated;
        return false;
    }
    if (networkError != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = statusCode >= 400
                ? responseErrorMessage(responseBody, statusCode)
                : QStringLiteral("API Gateway request failed: %1").arg(networkErrorText);
        }
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        if (errorMessage) *errorMessage = responseErrorMessage(responseBody, statusCode);
        return false;
    }
    if (!parseError.isEmpty()) {
        if (errorMessage) *errorMessage = parseError;
        return false;
    }

    if (fullText) *fullText = accumulated;
    return true;
}

void GatewayClient::cancel()
{
    if (m_activeReply) m_activeReply->abort();
}

} // namespace LAStudio
