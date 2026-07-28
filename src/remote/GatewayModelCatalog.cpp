#include "GatewayModelCatalog.h"

#include "ExecutionProvider.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>

namespace LAStudio {
namespace {

QString gatewayError(const QByteArray &body, int statusCode, const QString &networkError)
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
    if (statusCode >= 400)
        return QStringLiteral("API Gateway returned HTTP %1").arg(statusCode);
    return QStringLiteral("API Gateway request failed: %1").arg(networkError);
}

} // namespace

GatewayModelCatalog::Result GatewayModelCatalog::fetch(const QString &gatewayUrl,
                                                        const QString &apiKey,
                                                        bool allowInsecureLocalhost)
{
    Result result;
    const RemoteEndpointValidation endpoint = validateRemoteEndpoint(
        gatewayUrl, RemoteEndpointKind::ApiGateway, allowInsecureLocalhost);
    const QString key = apiKey.trimmed();
    if (!endpoint.isValid()) {
        result.error = endpoint.error;
        return result;
    }
    if (key.isEmpty()) {
        result.error = QStringLiteral("API Gateway key is required");
        return result;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(endpoint.normalizedUrl, QStringLiteral("models")));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray body = reply->readAll();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();
    if (networkError != QNetworkReply::NoError || statusCode < 200 || statusCode >= 300) {
        result.error = gatewayError(body, statusCode, networkErrorText);
        return result;
    }

    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject() || !document.object().value(QStringLiteral("data")).isArray()) {
        result.error = QStringLiteral("API Gateway returned an invalid model catalog");
        return result;
    }

    QSet<QString> seenModelIds;
    for (const QJsonValue &value : document.object().value(QStringLiteral("data")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject model = value.toObject();
        const QString modelId = model.value(QStringLiteral("id")).toString().trimmed();
        if (modelId.isEmpty() || seenModelIds.contains(modelId)) continue;
        seenModelIds.insert(modelId);
        QVariantMap entry = model.toVariantMap();
        entry.insert(QStringLiteral("id"), QStringLiteral("api-gateway:%1").arg(modelId));
        entry.insert(QStringLiteral("provider"), QStringLiteral("api-gateway"));
        entry.insert(QStringLiteral("modelId"), modelId);
        entry.insert(QStringLiteral("displayName"), model.value(QStringLiteral("name")).toString().trimmed().isEmpty()
                         ? modelId : model.value(QStringLiteral("name")).toString().trimmed());
        entry.insert(QStringLiteral("selectable"), true);
        result.models.append(entry);
    }
    return result;
}

} // namespace LAStudio
