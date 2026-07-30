#include "ColabTranslationRunner.h"

#include "remote/ColabWorkerClient.h"

#include <QJsonArray>
#include <QStringList>

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
    const QJsonArray patchArray = patches.toArray();
    for (int index = 0; index < patchArray.size(); ++index) {
        const QJsonValue &value = patchArray.at(index);
        const QVariantMap patch = value.toObject().toVariantMap();
        const QString id = patch.value(QStringLiteral("id")).toString().trimmed();
        const QString targetText = patch.value(QStringLiteral("targetText")).toString().trimmed();
        if (id.isEmpty() || targetText.isEmpty()) {
            QStringList invalidFields;
            if (id.isEmpty()) invalidFields.append(QStringLiteral("id"));
            if (targetText.isEmpty()) invalidFields.append(QStringLiteral("targetText"));
            emit failed(QStringLiteral(
                "Colab worker returned invalid translation patch %1/%2 (%3 missing). "
                "Reconnect using the current exact-model Translation notebook and run Check Colab.")
                            .arg(index + 1)
                            .arg(patchArray.size())
                            .arg(invalidFields.join(QStringLiteral(", "))));
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
