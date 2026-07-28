#include "GatewayModelCatalog.h"

#include "ExecutionProvider.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPair>
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

bool fetchModels(QNetworkAccessManager *manager, const QUrl &baseUrl,
                 const QString &path, const QByteArray &apiKey,
                 QJsonArray *models, QString *error, int transferTimeoutMs)
{
    if (!manager || !models) return false;
    QNetworkRequest request(appendRemotePath(baseUrl, path));
    request.setTransferTimeout(qMax(1, transferTimeoutMs));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey);
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = manager->get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray body = reply->readAll();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();
    if (networkError != QNetworkReply::NoError || statusCode < 200 || statusCode >= 300) {
        if (error) *error = gatewayError(body, statusCode, networkErrorText);
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject() || !document.object().value(QStringLiteral("data")).isArray()) {
        if (error) *error = QStringLiteral("API Gateway returned an invalid model catalog");
        return false;
    }
    *models = document.object().value(QStringLiteral("data")).toArray();
    return true;
}

} // namespace

GatewayModelCatalog::Result GatewayModelCatalog::fetch(const QString &gatewayUrl,
                                                        const QString &apiKey,
                                                        bool allowInsecureLocalhost,
                                                        int transferTimeoutMs)
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
    const QList<QPair<QString, QString>> catalogs = {
        {QStringLiteral("models"), QStringLiteral("llm")},
        {QStringLiteral("models/stt"), QStringLiteral("stt")},
        {QStringLiteral("models/tts"), QStringLiteral("tts")},
    };
    QSet<QString> seenModelKeys;
    for (const auto &catalog : catalogs) {
        QJsonArray models;
        QString error;
        if (!fetchModels(&manager, endpoint.normalizedUrl, catalog.first, key.toUtf8(), &models,
                         &error, transferTimeoutMs)) {
            result.error = QStringLiteral("API Gateway %1 catalog: %2").arg(catalog.second, error);
            result.models.clear();
            return result;
        }
        for (const QJsonValue &value : models) {
            if (!value.isObject()) continue;
            const QJsonObject model = value.toObject();
            const QString modelId = model.value(QStringLiteral("id")).toString().trimmed();
            const QString modelKey = catalog.second + QLatin1Char('\x1f') + modelId;
            if (modelId.isEmpty() || seenModelKeys.contains(modelKey)) continue;
            seenModelKeys.insert(modelKey);
            QVariantMap entry = model.toVariantMap();
            entry.insert(QStringLiteral("id"), QStringLiteral("api-gateway:%1:%2").arg(catalog.second, modelId));
            entry.insert(QStringLiteral("provider"), QStringLiteral("api-gateway"));
            entry.insert(QStringLiteral("capability"), catalog.second);
            entry.insert(QStringLiteral("modelId"), modelId);
            entry.insert(QStringLiteral("displayName"), model.value(QStringLiteral("name")).toString().trimmed().isEmpty()
                             ? modelId : model.value(QStringLiteral("name")).toString().trimmed());
            entry.insert(QStringLiteral("selectable"), true);
            result.models.append(entry);
        }
    }
    return result;
}

} // namespace LAStudio
