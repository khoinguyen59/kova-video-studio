#include "ColabTranslationRunner.h"

#include "remote/ColabWorkerClient.h"

#include <QJsonArray>

namespace LAStudio {

class ColabTranslationRunner::Private final
{
public:
    ColabWorkerClient client;
};

ColabTranslationRunner::ColabTranslationRunner(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
}

ColabTranslationRunner::~ColabTranslationRunner() = default;

void ColabTranslationRunner::translate(const QUrl &workerUrl, const QString &bearerToken,
                                       const QString &model, const TranslationInferenceRequest &request,
                                       bool allowInsecureLocalhost)
{
    QString error;
    if (!d->client.configure(workerUrl, bearerToken, allowInsecureLocalhost, &error)) {
        emit failed(error);
        return;
    }
    emit progress(5);
    QJsonObject response;
    if (!d->client.translateSegments(request.segments, request.sourceLanguage, request.targetLanguage, model,
                                     request.cancellation.sharedFlag(), &response, &error)) {
        emit failed(request.cancellation.isCancelled() ? QStringLiteral("Colab translation cancelled") : error);
        return;
    }
    if (request.cancellation.isCancelled()) {
        emit failed(QStringLiteral("Colab translation cancelled"));
        return;
    }
    const QJsonValue patches = response.value(QStringLiteral("patches"));
    if (!patches.isArray() || patches.toArray().size() != request.segments.size()) {
        emit failed(QStringLiteral("Colab worker returned incomplete translation patches"));
        return;
    }
    QVariantList output;
    for (const QJsonValue &value : patches.toArray()) {
        const QVariantMap patch = value.toObject().toVariantMap();
        if (patch.value(QStringLiteral("id")).toString().trimmed().isEmpty()
            || patch.value(QStringLiteral("targetText")).toString().trimmed().isEmpty()) {
            emit failed(QStringLiteral("Colab worker returned an invalid translation patch"));
            return;
        }
        output.append(patch);
    }
    emit progress(100);
    emit finished(output);
}

void ColabTranslationRunner::cancel()
{
    d->client.cancel();
}

} // namespace LAStudio
