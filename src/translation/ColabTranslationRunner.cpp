#include "ColabTranslationRunner.h"

#include "remote/ColabWorkerClient.h"

#include <QJsonArray>
#include <QStringList>

namespace LAStudio {

namespace {

bool appendValidatedPatches(const QJsonObject &response, int expectedCount, int completedBefore,
                            int totalCount, QVariantList *output, QString *errorMessage)
{
    const QJsonValue patches = response.value(QStringLiteral("patches"));
    if (!patches.isArray() || patches.toArray().size() != expectedCount) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Colab worker returned incomplete translation patches after %1/%2 segments")
                                .arg(completedBefore)
                                .arg(totalCount);
        }
        return false;
    }
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
            if (errorMessage) {
                *errorMessage = QStringLiteral(
                    "Colab worker returned invalid translation patch %1/%2 (%3 missing). "
                    "Reconnect using the current exact-model Translation notebook and run Check Colab.")
                                    .arg(completedBefore + index + 1)
                                    .arg(totalCount)
                                    .arg(invalidFields.join(QStringLiteral(", ")));
            }
            return false;
        }
        output->append(patch);
    }
    return true;
}

} // namespace

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
    // A synchronous worker response cannot truthfully expose inference progress
    // for a whole batch. Dispatch one source segment at a time instead: every
    // progress update now represents a patch actually returned by the exact
    // selected GPU model. This also confines an invalid model result to its
    // specific source segment rather than discarding an entire dubbing job.
    const int totalSegments = request.segments.size();
    QVariantList output;
    emit progress(0);
    for (int index = 0; index < totalSegments; ++index) {
        if (request.cancellation.isCancelled()) {
            emit failed(QStringLiteral("Colab translation cancelled"));
            return;
        }
        const QVariantList segmentBatch{request.segments.at(index)};
        QJsonObject response;
        if (!d->client.translateSegments(segmentBatch, request.sourceLanguage, request.targetLanguage, model,
                                         request.cancellation.sharedFlag(), &response, &error)) {
            emit failed(request.cancellation.isCancelled() ? QStringLiteral("Colab translation cancelled") : error);
            return;
        }
        if (!appendValidatedPatches(response, segmentBatch.size(), index, totalSegments, &output, &error)) {
            emit failed(error);
            return;
        }
        // Floor gives 1%, 2%, 3% for a 78-segment project after the first,
        // second and third completed segments; no estimated 5% is displayed.
        emit progress(((index + 1) * 100) / totalSegments);
    }
    emit finished(output);
}

void ColabTranslationRunner::cancel()
{
    d->client.cancel();
}

} // namespace LAStudio
