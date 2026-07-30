#include "ColabAlignmentRunner.h"

#include "remote/ColabWorkerClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>

namespace LAStudio {
namespace {

QString timestamp(double seconds, bool webVtt)
{
    const qint64 milliseconds = qMax<qint64>(0, qRound64(seconds * 1000.0));
    const qint64 hours = milliseconds / 3600000;
    const qint64 minutes = (milliseconds / 60000) % 60;
    const qint64 secs = (milliseconds / 1000) % 60;
    const qint64 millis = milliseconds % 1000;
    return QStringLiteral("%1:%2:%3%4%5")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'))
        .arg(webVtt ? QLatin1Char('.') : QLatin1Char(','))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

QString makeOutput(const QVariantList &segments, const QString &format)
{
    if (format == QStringLiteral("srt") || format == QStringLiteral("webvtt")) {
        const bool webVtt = format == QStringLiteral("webvtt");
        QString output = webVtt ? QStringLiteral("WEBVTT\n\n") : QString();
        for (qsizetype index = 0; index < segments.size(); ++index) {
            const QVariantMap segment = segments.at(index).toMap();
            if (!webVtt) output += QString::number(index + 1) + QLatin1Char('\n');
            output += timestamp(segment.value(QStringLiteral("start")).toDouble(), webVtt)
                + QStringLiteral(" --> ")
                + timestamp(segment.value(QStringLiteral("end")).toDouble(), webVtt)
                + QLatin1Char('\n') + segment.value(QStringLiteral("text")).toString() + QStringLiteral("\n\n");
        }
        return output;
    }
    QJsonArray array;
    for (const QVariant &segment : segments) array.append(QJsonObject::fromVariantMap(segment.toMap()));
    return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("segments"), array}})
                                  .toJson(QJsonDocument::Indented));
}

bool parseResponse(const QJsonObject &response, const QString &format,
                   ColabAlignmentResult *result, QString *error)
{
    const QJsonValue durationValue = response.value(QStringLiteral("duration"));
    if (!durationValue.isDouble() || !std::isfinite(durationValue.toDouble()) || durationValue.toDouble() <= 0.0) {
        if (error) *error = QStringLiteral("Colab worker returned an invalid alignment duration");
        return false;
    }
    const double duration = durationValue.toDouble();
    const QJsonValue segmentsValue = response.value(QStringLiteral("segments"));
    if (!segmentsValue.isArray() || segmentsValue.toArray().isEmpty()) {
        if (error) *error = QStringLiteral("Colab worker returned no alignment segments");
        return false;
    }
    QVariantList segments;
    double previousEnd = 0.0;
    constexpr double epsilon = 0.002;
    for (const QJsonValue &value : segmentsValue.toArray()) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("Colab worker returned an invalid alignment segment");
            return false;
        }
        const QJsonObject source = value.toObject();
        const QString text = source.value(QStringLiteral("text")).toString().trimmed();
        const QJsonValue startValue = source.value(QStringLiteral("start"));
        const QJsonValue endValue = source.value(QStringLiteral("end"));
        if (text.isEmpty() || !startValue.isDouble() || !endValue.isDouble()) {
            if (error) *error = QStringLiteral("Colab worker returned an incomplete alignment segment");
            return false;
        }
        const double start = startValue.toDouble();
        const double end = endValue.toDouble();
        if (!std::isfinite(start) || !std::isfinite(end) || start < -epsilon || end + epsilon < start
            || start + epsilon < previousEnd || end > duration + epsilon) {
            if (error) *error = QStringLiteral("Colab worker returned non-monotonic alignment timestamps");
            return false;
        }
        const QJsonValue scoreValue = source.value(QStringLiteral("score"));
        const double score = scoreValue.isDouble() && std::isfinite(scoreValue.toDouble())
            ? qBound(0.0, scoreValue.toDouble(), 1.0) : 0.0;
        QVariantMap segment{{QStringLiteral("text"), text},
                            {QStringLiteral("start"), qMax(0.0, start)},
                            {QStringLiteral("end"), qMin(duration, end)},
                            {QStringLiteral("score"), score},
                            {QStringLiteral("confidence"), score},
                            {QStringLiteral("kind"), source.value(QStringLiteral("kind")).toString(QStringLiteral("token"))}};
        segments.append(segment);
        previousEnd = end;
    }

    QVariantList unaligned;
    const QJsonValue unalignedValue = response.value(QStringLiteral("unaligned_tokens"));
    if (unalignedValue.isArray()) unaligned = unalignedValue.toArray().toVariantList();
    if (result) {
        result->segments = segments;
        result->duration = duration;
        result->unalignedTokens = unaligned;
        result->output = makeOutput(segments, format);
    }
    return true;
}

} // namespace

class ColabAlignmentRunner::Private final
{
public:
    ColabWorkerClient client;
};

ColabAlignmentRunner::ColabAlignmentRunner(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
}

ColabAlignmentRunner::~ColabAlignmentRunner() = default;

void ColabAlignmentRunner::align(const ColabAlignmentRequest &request)
{
    QString error;
    if (!d->client.configure(request.workerUrl, request.bearerToken,
                             request.allowInsecureLocalhost, &error)) {
        emit failed(error);
        return;
    }
    QJsonObject response;
    if (!d->client.alignAudioFile(request.audioPath, request.transcript, request.language, request.model,
                                  request.cancellation.sharedFlag(), &response, &error)) {
        emit failed(request.cancellation.isCancelled() ? QStringLiteral("Colab alignment cancelled") : error);
        return;
    }
    if (request.cancellation.isCancelled()) {
        emit failed(QStringLiteral("Colab alignment cancelled"));
        return;
    }
    ColabAlignmentResult result;
    if (!parseResponse(response, request.outputFormat, &result, &error)) {
        emit failed(error);
        return;
    }
    emit progress(100);
    emit finished(result);
}

void ColabAlignmentRunner::cancel()
{
    d->client.cancel();
}

} // namespace LAStudio
