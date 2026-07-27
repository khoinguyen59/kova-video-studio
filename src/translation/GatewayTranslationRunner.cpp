#include "GatewayTranslationRunner.h"

#include "remote/GatewayClient.h"

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

    const QString systemPrompt = QStringLiteral(
        "You are a professional translation engine. Translate the user's text from %1 to %2. "
        "Return only the translated text, without quotation marks, labels, markdown, explanations, or notes. "
        "Preserve line breaks, meaning, names, numbers, and punctuation.")
        .arg(request.sourceLanguage, request.targetLanguage);
    const GatewayClient::ChatOptions options{
        qBound(64, request.maxTokens, 4096), 0.2F, 0.95F};
    QVariantList patches;
    const int total = request.segments.size();
    for (int index = 0; index < total; ++index) {
        if (request.cancellation.isCancelled()) {
            emit failed(QStringLiteral("Translation cancelled"));
            return;
        }
        const QVariantMap segment = request.segments.at(index).toMap();
        const QString id = segment.value(QStringLiteral("id")).toString();
        const QString sourceText = segment.value(QStringLiteral("sourceText")).toString();
        if (id.isEmpty() || sourceText.trimmed().isEmpty()) {
            emit failed(QStringLiteral("Each translated segment requires an id and source text"));
            return;
        }

        QString translatedText;
        if (!d->client.streamChat(
                {QVariantMap{{QStringLiteral("role"), QStringLiteral("system")},
                             {QStringLiteral("content"), systemPrompt}},
                 QVariantMap{{QStringLiteral("role"), QStringLiteral("user")},
                             {QStringLiteral("content"), sourceText}}},
                options, request.cancellation.sharedFlag(), {}, &translatedText, &error)) {
            emit failed(request.cancellation.isCancelled()
                            ? QStringLiteral("Translation cancelled")
                            : (error.isEmpty() ? QStringLiteral("Gateway translation failed") : error));
            return;
        }
        patches.append(QVariantMap{{QStringLiteral("id"), id},
                                   {QStringLiteral("targetText"), translatedText},
                                   {QStringLiteral("state"), QStringLiteral("translated")}});
        emit progress(qRound((index + 1) * 100.0 / total));
    }
    emit finished(patches);
}

void GatewayTranslationRunner::cancel()
{
    d->client.cancel();
}

} // namespace LAStudio
