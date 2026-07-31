#include "subtitles/SubtitleOcrPipeline.h"

#include <QtMath>

namespace LAStudio {

bool SubtitleOcrRoi::isValid() const
{
    return x >= 0 && y >= 0 && width > 0 && height > 0 && x + width <= 1 && y + height <= 1;
}

SubtitleOcrRect SubtitleOcrPipeline::sourceRect(const SubtitleOcrRoi &roi, int sourceWidth, int sourceHeight)
{
    if (!roi.isValid() || sourceWidth <= 0 || sourceHeight <= 0) return {};
    const int x = qBound(0, qRound(roi.x * sourceWidth), sourceWidth - 1);
    const int y = qBound(0, qRound(roi.y * sourceHeight), sourceHeight - 1);
    const int width = qBound(1, qRound(roi.width * sourceWidth), sourceWidth - x);
    const int height = qBound(1, qRound(roi.height * sourceHeight), sourceHeight - y);
    return {x, y, width, height};
}

QStringList SubtitleOcrPipeline::ffmpegCropArguments(const SubtitleOcrRoi &roi, int sourceWidth, int sourceHeight)
{
    const SubtitleOcrRect rect = sourceRect(roi, sourceWidth, sourceHeight);
    if (rect.width <= 0 || rect.height <= 0) return {};
    return {QStringLiteral("-vf"), QStringLiteral("crop=%1:%2:%3:%4")
        .arg(rect.width).arg(rect.height).arg(rect.x).arg(rect.y)};
}

QVector<qint64> SubtitleOcrPipeline::sampleTimes(qint64 durationMs, qint64 intervalMs)
{
    QVector<qint64> result;
    if (durationMs < 0 || intervalMs <= 0) return result;
    for (qint64 value = 0; value <= durationMs; value += intervalMs) result.append(value);
    if (result.isEmpty() || result.constLast() != durationMs) result.append(durationMs);
    return result;
}

QVector<SubtitleOcrSegment> SubtitleOcrPipeline::mergeObservations(
    const QVector<SubtitleOcrObservation> &observations, qint64 intervalMs, double minimumConfidence)
{
    QVector<SubtitleOcrSegment> result;
    for (const auto &observation : observations) {
        const QString text = observation.text.trimmed();
        if (text.isEmpty() || observation.confidence < minimumConfidence || observation.timestampMs < 0) continue;
        if (!result.isEmpty() && result.last().text == text
            && observation.timestampMs <= result.last().endMs + qMax<qint64>(1, intervalMs * 2)) {
            result.last().endMs = qMax(result.last().endMs, observation.timestampMs + intervalMs);
            result.last().confidence = qMin(result.last().confidence, observation.confidence);
            continue;
        }
        result.append({observation.timestampMs, observation.timestampMs + intervalMs, text, observation.confidence});
    }
    return result;
}

static QString timestamp(qint64 value)
{
    const qint64 ms = qMax<qint64>(0, value) % 1000;
    const qint64 seconds = qMax<qint64>(0, value) / 1000;
    return QStringLiteral("%1:%2:%3,%4").arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'))
        .arg(ms, 3, 10, QLatin1Char('0'));
}

QString SubtitleOcrPipeline::toSrt(const QVector<SubtitleOcrSegment> &segments)
{
    QString output;
    int index = 1;
    for (const auto &segment : segments) {
        if (segment.text.trimmed().isEmpty() || segment.endMs <= segment.startMs) continue;
        output += QStringLiteral("%1\n%2 --> %3\n%4\n\n").arg(index++).arg(timestamp(segment.startMs),
            timestamp(segment.endMs), segment.text.trimmed());
    }
    return output;
}

} // namespace LAStudio
