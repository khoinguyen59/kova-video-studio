#include "ColabCapabilityCatalog.h"

#include "ExecutionProvider.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QStringList>

namespace LAStudio {
namespace {

constexpr int kSupportedColabContractVersion = 1;

QString colabError(const QByteArray &body, int statusCode, const QString &networkError)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject()) {
        const QJsonObject root = document.object();
        const QString detail = root.value(QStringLiteral("detail")).toString().trimmed();
        if (!detail.isEmpty()) return detail;
        const QJsonValue error = root.value(QStringLiteral("error"));
        if (error.isObject()) {
            const QString message = error.toObject().value(QStringLiteral("message")).toString().trimmed();
            if (!message.isEmpty()) return message;
        }
    }
    if (statusCode >= 400)
        return QStringLiteral("Colab worker returned HTTP %1").arg(statusCode);
    return QStringLiteral("Colab worker request failed: %1").arg(networkError);
}

double numberValue(const QJsonObject &object, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isDouble()) return value.toDouble();
    }
    return 0.0;
}

QString modelDevice(const QJsonObject &model, const QJsonObject &capability,
                    const QJsonObject &root)
{
    QString device = model.value(QStringLiteral("device")).toString().trimmed();
    if (device.isEmpty()) device = capability.value(QStringLiteral("device")).toString().trimmed();
    if (device.isEmpty()) device = root.value(QStringLiteral("device")).toString().trimmed();
    return device;
}

void appendModel(QVariantList *models, QSet<QString> *seen, const QString &capabilityId,
                 const QJsonObject &model, const QJsonObject &capability,
                 const QJsonObject &root)
{
    if (!models || !seen || capabilityId.trimmed().isEmpty()) return;
    const QString modelId = model.value(QStringLiteral("id")).toString().trimmed();
    if (modelId.isEmpty()) return;
    const QString uniqueId = QStringLiteral("colab-direct:%1:%2").arg(capabilityId, modelId);
    if (seen->contains(uniqueId)) return;
    seen->insert(uniqueId);

    const QString device = modelDevice(model, capability, root);
    const bool loaded = !model.contains(QStringLiteral("loaded")) || model.value(QStringLiteral("loaded")).toBool();
    const double requiredVram = numberValue(model, {QStringLiteral("required_vram_gb"),
                                                     QStringLiteral("requiredVramGb"),
                                                     QStringLiteral("vram_gb")});
    const double availableVram = numberValue(root, {QStringLiteral("available_vram_gb"),
                                                     QStringLiteral("availableVramGb"),
                                                     QStringLiteral("vram_gb")});
    const bool hasCuda = device.startsWith(QStringLiteral("cuda"), Qt::CaseInsensitive);
    const bool enoughVram = requiredVram <= 0.0 || availableVram <= 0.0 || availableVram >= requiredVram;

    QVariantMap entry = model.toVariantMap();
    entry.insert(QStringLiteral("id"), uniqueId);
    entry.insert(QStringLiteral("provider"), QStringLiteral("colab-direct"));
    entry.insert(QStringLiteral("capability"), capabilityId);
    entry.insert(QStringLiteral("modelId"), modelId);
    // Exact-model notebooks currently expose one immutable GPU configuration.
    // Make that explicit in catalog consumers instead of leaking Local CPU
    // quantization/file choices into the Direct Colab route.
    entry.insert(QStringLiteral("variant"),
                 model.value(QStringLiteral("variant")).toString().trimmed().isEmpty()
                     ? QStringLiteral("fixed")
                     : model.value(QStringLiteral("variant")).toString().trimmed().toLower());
    entry.insert(QStringLiteral("displayName"), model.value(QStringLiteral("name")).toString().trimmed().isEmpty()
                     ? modelId : model.value(QStringLiteral("name")).toString().trimmed());
    entry.insert(QStringLiteral("device"), device);
    entry.insert(QStringLiteral("loaded"), loaded);
    entry.insert(QStringLiteral("requiredVramGb"), requiredVram);
    entry.insert(QStringLiteral("availableVramGb"), availableVram);
    entry.insert(QStringLiteral("selectable"), loaded && hasCuda && enoughVram);
    models->append(entry);
}

} // namespace

ColabCapabilityCatalog::Result ColabCapabilityCatalog::fetch(const QUrl &workerUrl,
                                                               const QString &bearerToken,
                                                               bool allowInsecureLocalhost,
                                                               int transferTimeoutMs)
{
    Result result;
    const RemoteEndpointValidation endpoint = validateRemoteEndpoint(
        workerUrl.toString(), RemoteEndpointKind::ColabWorker, allowInsecureLocalhost);
    const QString token = bearerToken.trimmed();
    if (!endpoint.isValid()) {
        result.error = endpoint.error;
        return result;
    }
    if (token.isEmpty()) {
        result.error = QStringLiteral("Colab worker bearer token is required");
        return result;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(endpoint.normalizedUrl, QStringLiteral("v1/capabilities")));
    request.setTransferTimeout(qMax(1, transferTimeoutMs));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8());
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
        result.error = colabError(body, statusCode, networkErrorText);
        return result;
    }

    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        result.error = QStringLiteral("Colab worker returned an invalid capability catalog");
        return result;
    }

    const QJsonObject root = document.object();
    const QJsonValue contractVersion = root.value(QStringLiteral("contract_version"));
    if (!contractVersion.isDouble() || contractVersion.toInt() != kSupportedColabContractVersion) {
        result.error = QStringLiteral("Colab worker contract_version must be %1")
                           .arg(kSupportedColabContractVersion);
        return result;
    }
    QSet<QString> seen;
    const QJsonArray capabilities = root.value(QStringLiteral("capabilities")).toArray();
    for (const QJsonValue &value : capabilities) {
        if (!value.isObject()) continue;
        const QJsonObject capability = value.toObject();
        const QString capabilityId = capability.value(QStringLiteral("id")).toString().trimmed();
        for (const QJsonValue &modelValue : capability.value(QStringLiteral("models")).toArray()) {
            if (modelValue.isObject()) appendModel(&result.models, &seen, capabilityId,
                                                   modelValue.toObject(), capability, root);
        }
    }

    // The language notebook predates the standard capabilities envelope. Keep
    // it readable without treating an unloaded model as remotely available.
    const QList<QPair<QString, QString>> legacyKeys{
        {QStringLiteral("translation"), QStringLiteral("translation")},
        {QStringLiteral("chat"), QStringLiteral("llm-chat")}
    };
    for (const auto &entry : legacyKeys) {
        const QJsonArray models = root.value(entry.first).toArray();
        for (const QJsonValue &modelValue : models) {
            if (modelValue.isObject()) appendModel(&result.models, &seen, entry.second,
                                                   modelValue.toObject(), {}, root);
        }
    }
    return result;
}

} // namespace LAStudio
