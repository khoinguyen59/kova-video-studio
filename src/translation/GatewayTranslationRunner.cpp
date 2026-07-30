#include "GatewayTranslationRunner.h"

#include "remote/GatewayClient.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtMath>

namespace LAStudio {

class GatewayTranslationRunner::Private final
{
public:
    GatewayClient client;
};

GatewayTranslationRunner::GatewayTranslationRunner(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
}

GatewayTranslationRunner::~GatewayTranslationRunner() = default;

void GatewayTranslationRunner::translate(const QString &gatewayUrl, const QString &apiKey,
                                         const QString &model,
                                         const TranslationInferenceRequest &request,
                                         bool allowInsecureLocalhost)
{
    QString error;
    if (!d->client.configure(gatewayUrl, apiKey, model, allowInsecureLocalhost, &error)) {
        emit failed(error);
        return;
    }
    if (request.segments.isEmpty()) {
        emit finished({});
        return;
    }

    QJsonArray segments;
    QSet<QString> requestedIds;
    for (const QVariant &value : request.segments) {
        const QVariantMap segment = value.toMap();
        const QString id = segment.value(QStringLiteral("id")).toString().trimmed();
        const QString sourceText = segment.value(QStringLiteral("sourceText")).toString();
        if (id.isEmpty() || sourceText.trimmed().isEmpty()) {
            emit failed(QStringLiteral("Each translated segment requires an id and source text"));
            return;
        }
        if (requestedIds.contains(id)) {
            emit failed(QStringLiteral("Translation request contains duplicate segment ids"));
            return;
        }
        requestedIds.insert(id);
        segments.append(QJsonObject{{QStringLiteral("id"), id},
                                    {QStringLiteral("sourceText"), sourceText}});
    }

    const QString systemPrompt = QStringLiteral(
        "You are a professional translation engine. Translate each segment from %1 to %2. "
        "Return exactly one strict JSON object with this schema and no markdown, prose, or code fence: "
        "{\"patches\":[{\"id\":\"original segment id\",\"targetText\":\"translated text\"}]}. "
        "Return one patch for every input segment, preserve every id exactly once, and preserve line breaks, "
        "meaning, names, numbers, and punctuation.")
        .arg(request.sourceLanguage, request.targetLanguage);
    const GatewayClient::ChatOptions options{
        qBound(64, request.maxTokens, 4096), 0.2F, 0.95F};
    const QJsonObject payload{{QStringLiteral("source_language"), request.sourceLanguage},
                              {QStringLiteral("target_language"), request.targetLanguage},
                              {QStringLiteral("segments"), segments}};
    QString responseText;
    if (!d->client.streamChat(
            {QVariantMap{{QStringLiteral("role"), QStringLiteral("system")},
                         {QStringLiteral("content"), systemPrompt}},
             QVariantMap{{QStringLiteral("role"), QStringLiteral("user")},
                         {QStringLiteral("content"),
                          QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))}}},
            options, request.cancellation.sharedFlag(), {}, &responseText, &error)) {
        emit failed(request.cancellation.isCancelled()
                        ? QStringLiteral("Translation cancelled")
                        : (error.isEmpty() ? QStringLiteral("Gateway translation failed") : error));
        return;
    }
    if (request.cancellation.isCancelled()) {
        emit failed(QStringLiteral("Translation cancelled"));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit failed(QStringLiteral("Gateway translation must return the required JSON patches object"));
        return;
    }
    const QJsonArray returnedPatches = document.object().value(QStringLiteral("patches")).toArray();
    if (returnedPatches.size() != request.segments.size()) {
        emit failed(QStringLiteral("Gateway translation returned an incomplete patches list"));
        return;
    }

    QHash<QString, QString> translations;
    for (const QJsonValue &value : returnedPatches) {
        if (!value.isObject()) {
            emit failed(QStringLiteral("Gateway translation returned an invalid patch"));
            return;
        }
        const QJsonObject patch = value.toObject();
        const QString id = patch.value(QStringLiteral("id")).toString().trimmed();
        const QString targetText = patch.value(QStringLiteral("targetText")).toString();
        if (id.isEmpty() || targetText.trimmed().isEmpty() || !requestedIds.contains(id)
            || translations.contains(id)) {
            emit failed(QStringLiteral("Gateway translation returned invalid, duplicate, or unknown segment ids"));
            return;
        }
        translations.insert(id, targetText);
    }

    QVariantList patches;
    for (const QVariant &value : request.segments) {
        const QString id = value.toMap().value(QStringLiteral("id")).toString();
        if (!translations.contains(id)) {
            emit failed(QStringLiteral("Gateway translation did not return every requested segment id"));
            return;
        }
        patches.append(QVariantMap{{QStringLiteral("id"), id},
                                   {QStringLiteral("targetText"), translations.value(id)},
                                   {QStringLiteral("state"), QStringLiteral("translated")}});
    }
    emit progress(100);
    emit finished(patches);
}

void GatewayTranslationRunner::cancel()
{
    d->client.cancel();
}

} // namespace LAStudio
