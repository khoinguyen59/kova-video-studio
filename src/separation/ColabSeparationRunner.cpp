#include "ColabSeparationRunner.h"

#include "remote/ColabWorkerClient.h"

#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>

namespace LAStudio {

namespace {

constexpr int kDefaultFinalizeTimeoutMs = 5 * 60 * 1000;
constexpr int kDefaultStatusPollIntervalMs = 350;

QString normalizedArtifactFormat(const QString &value)
{
    return value.trimmed().compare(QStringLiteral("wav"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("wav") : QStringLiteral("flac");
}

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
    QString lastPhase;
    const auto reportPhase = [this, &lastPhase](const QString &phase) {
        const QString normalized = phase.simplified();
        if (normalized.isEmpty() || normalized == lastPhase) return;
        lastPhase = normalized;
        qInfo().noquote() << "[colab-separation]" << normalized;
        emit phaseChanged(normalized);
    };
    reportPhase(QStringLiteral("Uploading source audio to Direct Colab"));
    if (!d->client.configure(request.workerUrl, request.bearerToken,
                             request.allowInsecureLocalhost, &error)) {
        emit failed(error);
        return;
    }
    QJsonObject job;
    QString artifactFormat = normalizedArtifactFormat(request.artifactFormat);
    if (!d->client.createSeparationJob(request.audioPath, request.model, artifactFormat, &job, &error)) {
        emit failed(error);
        return;
    }
    d->activeJobId = job.value(QStringLiteral("job_id")).toString().trimmed();
    if (d->activeJobId.isEmpty()) {
        emit failed(QStringLiteral("Colab worker returned a separation job without an ID"));
        return;
    }
    // A current worker echoes the negotiated format.  Older notebooks omit it
    // and are treated as WAV so a stale notebook fails safely neither during
    // transfer nor by silently relabelling a binary artifact.
    if (job.contains(QStringLiteral("artifact_format"))) {
        artifactFormat = normalizedArtifactFormat(job.value(QStringLiteral("artifact_format")).toString());
    } else {
        artifactFormat = QStringLiteral("wav");
    }
    reportPhase(QStringLiteral("Waiting for the Direct Colab CUDA worker"));
    int lastProgress = -1;
    QElapsedTimer finalizingTimer;
    const int finalizeTimeoutMs = request.finalizeTimeoutMs > 0
        ? request.finalizeTimeoutMs : kDefaultFinalizeTimeoutMs;
    const int statusPollIntervalMs = request.statusPollIntervalMs > 0
        ? request.statusPollIntervalMs : kDefaultStatusPollIntervalMs;
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
        if (state == QStringLiteral("ready") || state == QStringLiteral("completed")) {
            const QString reportedFormat = status.value(QStringLiteral("artifact_format")).toString();
            if (!reportedFormat.trimmed().isEmpty())
                artifactFormat = normalizedArtifactFormat(reportedFormat);
            const bool artifactsReady = status.value(QStringLiteral("artifacts_ready")).toBool(true);
            if (!artifactsReady) {
                d->activeJobId.clear();
                emit failed(QStringLiteral("Colab reported completion before both separated stems were available."));
                return;
            }
            reportPhase(QStringLiteral("Colab created both %1 stems; downloading them now")
                            .arg(artifactFormat.toUpper()));
            break;
        }
        const QString detail = status.value(QStringLiteral("detail")).toString().simplified();
        reportPhase(detail.isEmpty()
            ? QStringLiteral("Direct Colab worker is processing (%1%)").arg(reportedProgress)
            : detail);
        if (reportedProgress >= 90) {
            if (!finalizingTimer.isValid()) finalizingTimer.start();
            if (finalizingTimer.elapsed() >= finalizeTimeoutMs) {
                const QString message = QStringLiteral(
                    "The Direct Colab worker stayed at 90% while finalizing separated stems for %1 seconds. "
                    "It did not become ready, so this remote job was cancelled. No local model was started.")
                    .arg(finalizeTimeoutMs / 1000);
                qWarning().noquote() << "[colab-separation]" << message;
                d->client.cancelSeparationJob(d->activeJobId);
                d->activeJobId.clear();
                emit failed(message);
                return;
            }
        } else {
            finalizingTimer.invalidate();
        }
        QThread::msleep(statusPollIntervalMs);
    }
    if (request.cancellation.isCancelled()) {
        if (!d->activeJobId.isEmpty()) d->client.cancelSeparationJob(d->activeJobId);
        d->activeJobId.clear();
        emit failed(QStringLiteral("Colab separation cancelled"));
        return;
    }
    QByteArray vocals, background;
    const auto downloadArtifact = [this, &request, &artifactFormat, &error, &reportPhase](const QString &artifact,
                                                                           QByteArray *data) {
        reportPhase(QStringLiteral("Requesting %1 stem from Direct Colab").arg(artifact));
        return d->client.downloadSeparationArtifact(
            d->activeJobId, artifact, artifactFormat, request.cancellation.sharedFlag(), data, &error,
            [this, artifact](qint64 received, qint64 total) {
                emit artifactTransferProgress(artifact, received, total);
            });
    };
    if (!downloadArtifact(QStringLiteral("vocals"), &vocals)
        || !downloadArtifact(QStringLiteral("background"), &background)) {
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
    reportPhase(QStringLiteral("Saving separated stems locally"));
    QDir().mkpath(request.outputRoot);
    const QString vocalsPath = QDir(request.outputRoot).filePath(QStringLiteral("vocals.") + artifactFormat);
    const QString backgroundPath = QDir(request.outputRoot).filePath(QStringLiteral("background.") + artifactFormat);
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
