#include "ColabSttRunner.h"

#include "remote/ColabWorkerClient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QThread>

#include <algorithm>
#include <cstring>
#include <memory>

namespace LAStudio {

namespace {

QByteArray makeMono16kWav(const QVector<float> &samples)
{
    const quint32 dataSize = static_cast<quint32>(samples.size() * static_cast<int>(sizeof(qint16)));
    QByteArray wav(44 + static_cast<int>(dataSize), Qt::Uninitialized);
    auto writeBytes = [&wav](int offset, const char *data, int size) {
        std::memcpy(wav.data() + offset, data, size);
    };
    auto write16 = [&wav](int offset, quint16 value) { std::memcpy(wav.data() + offset, &value, sizeof(value)); };
    auto write32 = [&wav](int offset, quint32 value) { std::memcpy(wav.data() + offset, &value, sizeof(value)); };
    writeBytes(0, "RIFF", 4); write32(4, 36 + dataSize); writeBytes(8, "WAVE", 4);
    writeBytes(12, "fmt ", 4); write32(16, 16); write16(20, 1); write16(22, 1);
    write32(24, 16000); write32(28, 32000); write16(32, 2); write16(34, 16);
    writeBytes(36, "data", 4); write32(40, dataSize);
    for (int index = 0; index < samples.size(); ++index) {
        const float sample = std::clamp(samples.at(index), -1.0F, 1.0F);
        const qint16 pcm = static_cast<qint16>(sample * 32767.0F);
        std::memcpy(wav.data() + 44 + index * static_cast<int>(sizeof(qint16)), &pcm, sizeof(pcm));
    }
    return wav;
}

} // namespace

class ColabSttRunner::Private final
{
public:
    ColabWorkerClient client;
    QString activeJobId;
    QTimer *pollTimer = nullptr;
    std::shared_ptr<std::atomic_bool> cancellation;
};

ColabSttRunner::ColabSttRunner(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->pollTimer = new QTimer(this);
    d->pollTimer->setSingleShot(true);
    connect(d->pollTimer, &QTimer::timeout, this, &ColabSttRunner::pollActiveJob);
}

ColabSttRunner::~ColabSttRunner() = default;

void ColabSttRunner::transcribe(const ColabSttRequest &request)
{
    if (!d->activeJobId.isEmpty()) {
        emit failed(QStringLiteral("A Colab transcription job is already running"));
        return;
    }
    QString error;
    if (!d->client.configure(request.workerUrl, request.bearerToken,
                             request.allowInsecureLocalhost, &error)) {
        emit failed(error);
        return;
    }
    const QByteArray wavData = makeMono16kWav(request.samples);
    QJsonObject job;
    if (!d->client.createTranscriptionJob(
            wavData, request.model, request.language, &job, &error)) {
        emit failed(request.cancellation.isCancelled() ? QStringLiteral("Transcription cancelled")
                                                       : error);
        return;
    }
    d->activeJobId = job.value(QStringLiteral("job_id")).toString().trimmed();
    if (d->activeJobId.isEmpty()) {
        emit failed(QStringLiteral("Colab worker returned a transcription job without an ID"));
        return;
    }
    d->cancellation = request.cancellation.sharedFlag();
    d->pollTimer->start(0);
}

void ColabSttRunner::pollActiveJob()
{
    if (d->activeJobId.isEmpty()) return;
    const InferenceCancellationToken cancellation(d->cancellation);
    if (cancellation.isCancelled()) {
        d->client.cancelTranscriptionJob(d->activeJobId);
        d->activeJobId.clear();
        d->cancellation.reset();
        emit failed(QStringLiteral("Transcription cancelled"));
        return;
    }
    QString error;
    QJsonObject status;
    if (!d->client.transcriptionJobStatus(d->activeJobId, &status, &error)) {
        const bool cancelled = cancellation.isCancelled();
        if (cancelled) d->client.cancelTranscriptionJob(d->activeJobId);
        d->activeJobId.clear();
        d->cancellation.reset();
        emit failed(cancelled ? QStringLiteral("Transcription cancelled") : error);
        return;
    }
    const QString state = status.value(QStringLiteral("status")).toString().trimmed().toLower();
    const QJsonValue workerProgressValue = status.value(QStringLiteral("progress"));
    const int workerProgress = qBound(0, workerProgressValue.toInt(0), 100);
    // Only the worker can measure inference progress.  Encoding and upload
    // are separate operations, so converting them into an invented share of
    // one overall percentage would mislead the desktop UI.
    if (workerProgressValue.isDouble() && workerProgress > 0 && workerProgress < 100)
        emit progress(workerProgress);
    if (state != QStringLiteral("succeeded") && state != QStringLiteral("completed")) {
        if (state == QStringLiteral("failed") || state == QStringLiteral("cancelled")) {
            const QString detail = status.value(QStringLiteral("detail")).toString().trimmed();
            d->activeJobId.clear();
            d->cancellation.reset();
            emit failed(detail.isEmpty() ? QStringLiteral("Colab transcription failed") : detail);
            return;
        }
        d->pollTimer->start(250);
        return;
    }
    const QJsonObject response = status.value(QStringLiteral("result")).toObject();
    d->activeJobId.clear();
    d->cancellation.reset();
    QVariantList segments;
    for (const QJsonValue &value : response.value(QStringLiteral("segments")).toArray()) {
        const QJsonObject segment = value.toObject();
        segments.append(QVariantMap{{QStringLiteral("id"), segment.value(QStringLiteral("id")).toVariant()},
                                    {QStringLiteral("start"), segment.value(QStringLiteral("start")).toDouble()},
                                    {QStringLiteral("end"), segment.value(QStringLiteral("end")).toDouble()},
                                    {QStringLiteral("text"), segment.value(QStringLiteral("text")).toString().trimmed()}});
    }
    const QString text = response.value(QStringLiteral("text")).toString().trimmed();
    if (text.isEmpty()) {
        emit failed(QStringLiteral("Colab worker returned an empty transcript"));
        return;
    }
    emit progress(100);
    emit finished(text, segments);
}

void ColabSttRunner::cancel()
{
    d->client.cancel();
    if (d->cancellation) d->cancellation->store(true, std::memory_order_relaxed);
    if (d->pollTimer) d->pollTimer->stop();
    if (!d->activeJobId.isEmpty()) {
        d->client.cancelTranscriptionJob(d->activeJobId);
        d->activeJobId.clear();
        d->cancellation.reset();
        emit failed(QStringLiteral("Transcription cancelled"));
    }
}

} // namespace LAStudio
