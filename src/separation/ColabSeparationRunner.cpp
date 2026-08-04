#include "ColabSeparationRunner.h"

#include "remote/ColabWorkerClient.h"

#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>

namespace LAStudio {

namespace {

QString failureForUser(const QString &detail)
{
    const QString normalized = detail.simplified();
    if (normalized.contains(QStringLiteral("cudnn"), Qt::CaseInsensitive)
        || normalized.contains(QStringLiteral("cudaexecutionprovider"), Qt::CaseInsensitive)) {
        return QStringLiteral(
            "The Direct Colab Spleeter worker failed while initializing CUDA. "
            "No local model was started. Stop this job, reopen the current Spleeter Colab notebook, "
            "wait for its CUDA startup probe to pass, then reconnect and run again.");
    }
    constexpr qsizetype maximumVisibleDetail = 700;
    if (normalized.size() > maximumVisibleDetail) {
        return normalized.left(maximumVisibleDetail)
            + QStringLiteral(" … Full worker detail is in System Logs.");
    }
    return normalized;
}

} // namespace

class ColabSeparationRunner::Private final
{
public:
    ColabWorkerClient client;
    QString activeJobId;
};

ColabSeparationRunner::ColabSeparationRunner(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
}

ColabSeparationRunner::~ColabSeparationRunner() = default;

void ColabSeparationRunner::separate(const ColabSeparationRequest &request)
{
    QString error;
    if (!d->client.configure(request.workerUrl, request.bearerToken,
                             request.allowInsecureLocalhost, &error)) {
        emit failed(error);
        return;
    }
    QJsonObject job;
    if (!d->client.createSeparationJob(request.audioPath, request.model, &job, &error)) {
        emit failed(error);
        return;
    }
    d->activeJobId = job.value(QStringLiteral("job_id")).toString().trimmed();
    if (d->activeJobId.isEmpty()) {
        emit failed(QStringLiteral("Colab worker returned a separation job without an ID"));
        return;
    }
    int lastProgress = -1;
    while (!request.cancellation.isCancelled()) {
        QJsonObject status;
        if (!d->client.separationJobStatus(d->activeJobId, &status, &error)) {
            if (request.cancellation.isCancelled() && !d->activeJobId.isEmpty()) {
                d->client.cancelSeparationJob(d->activeJobId);
                d->activeJobId.clear();
                emit failed(QStringLiteral("Colab separation cancelled"));
                return;
            }
            d->activeJobId.clear();
            emit failed(error);
            return;
        }
        const QString state = status.value(QStringLiteral("status")).toString().toLower();
        // Preserve only the direct worker's phase progress. The desktop must
        // not invent a weighted percentage around a remote separation job.
        // 100 is emitted only after both artifacts are downloaded and safely
        // committed in the local temporary output directory.
        const int reportedProgress = qBound(0, status.value(QStringLiteral("progress")).toInt(), 99);
        if (state != QStringLiteral("ready") && state != QStringLiteral("completed")
            && reportedProgress != lastProgress) {
            lastProgress = reportedProgress;
            emit progress(lastProgress);
        }
        if (state == QStringLiteral("failed") || state == QStringLiteral("cancelled")) {
            d->activeJobId.clear();
            const QString rawDetail = status.value(QStringLiteral("detail")).toString();
            if (!rawDetail.isEmpty()) qWarning().noquote() << "[colab-separation]" << rawDetail;
            emit failed(rawDetail.isEmpty() ? QStringLiteral("Colab separation failed")
                                             : failureForUser(rawDetail));
            return;
        }
        if (state == QStringLiteral("ready") || state == QStringLiteral("completed")) break;
        QThread::msleep(350);
    }
    if (request.cancellation.isCancelled()) {
        if (!d->activeJobId.isEmpty()) d->client.cancelSeparationJob(d->activeJobId);
        d->activeJobId.clear();
        emit failed(QStringLiteral("Colab separation cancelled"));
        return;
    }
    QByteArray vocals, background;
    if (!d->client.downloadSeparationArtifact(d->activeJobId, QStringLiteral("vocals"),
                                             request.cancellation.sharedFlag(), &vocals, &error)
        || !d->client.downloadSeparationArtifact(d->activeJobId, QStringLiteral("background"),
                                                 request.cancellation.sharedFlag(), &background, &error)) {
        if (!d->activeJobId.isEmpty()) d->client.cancelSeparationJob(d->activeJobId);
        d->activeJobId.clear();
        emit failed(request.cancellation.isCancelled() ? QStringLiteral("Colab separation cancelled") : error);
        return;
    }
    if (request.cancellation.isCancelled() || request.outputRoot.trimmed().isEmpty()) {
        if (!d->activeJobId.isEmpty()) d->client.cancelSeparationJob(d->activeJobId);
        d->activeJobId.clear();
        emit failed(QStringLiteral("Colab separation cancelled"));
        return;
    }
    QDir().mkpath(request.outputRoot);
    const QString vocalsPath = QDir(request.outputRoot).filePath(QStringLiteral("vocals.wav"));
    const QString backgroundPath = QDir(request.outputRoot).filePath(QStringLiteral("background.wav"));
    auto saveArtifact = [](const QString &path, const QByteArray &data) {
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
    };
    if (!saveArtifact(vocalsPath, vocals) || !saveArtifact(backgroundPath, background)) {
        if (!d->activeJobId.isEmpty()) d->client.cancelSeparationJob(d->activeJobId);
        d->activeJobId.clear();
        emit failed(QStringLiteral("Failed to save direct Colab separation artifacts"));
        return;
    }
    ColabSeparationResult result{vocalsPath, backgroundPath, d->activeJobId};
    d->activeJobId.clear();
    emit progress(100);
    emit finished(result);
}

void ColabSeparationRunner::cancel()
{
    d->client.cancel();
}

} // namespace LAStudio
