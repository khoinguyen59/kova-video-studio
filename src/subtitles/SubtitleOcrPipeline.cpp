#include "subtitles/SubtitleOcrPipeline.h"

#include <QHash>
#include <QtMath>

#include <algorithm>

namespace LAStudio {

bool SubtitleOcrRoi::isValid() const
{
    return x >= 0 && y >= 0 && width > 0 && height > 0 && x + width <= 1 && y + height <= 1;
}

SubtitleOcrRect SubtitleOcrPipeline::sourceRect(const SubtitleOcrRoi &roi, int sourceWidth, int sourceHeight)
{
    if (!roi.isValid() || sourceWidth <= 0 || sourceHeight <= 0) return {};
    const int x = qBound(0, qRound(roi.x * sourceWidth), sourceWidth);
    const int y = qBound(0, qRound(roi.y * sourceHeight), sourceHeight);
    // Round the extent independently from its origin.  Rounding both edges
    // loses a pixel for valid bottom/full-width regions such as 0.883/0.105
    // on a 180px frame, while this form still clamps the resulting rectangle
    // strictly inside the decoded source frame.
    const int width = qMin(qMax(0, qRound(roi.width * sourceWidth)), sourceWidth - x);
    const int height = qMin(qMax(0, qRound(roi.height * sourceHeight)), sourceHeight - y);
    // Never turn an almost-empty normalized ROI into a made-up 1px crop.
    // FFmpeg and OCR must receive an actual, positive source-pixel rectangle.
    if (width <= 0 || height <= 0) return {};
    return {x, y, width, height};
}

QStringList SubtitleOcrPipeline::ffmpegCropArguments(const SubtitleOcrRoi &roi, int sourceWidth, int sourceHeight)
{
    const SubtitleOcrRect rect = sourceRect(roi, sourceWidth, sourceHeight);
    if (rect.width <= 0 || rect.height <= 0) return {};
    // exact=1 stops the crop filter silently aligning an odd-height subtitle
    // strip down for chroma subsampling.  The crop coordinates therefore
    // remain exactly the normalized source rectangle.  Hard subtitles are
    // commonly only a few dozen pixels high, however, and Tesseract 5 can
    // return an otherwise-successful empty TSV for that input.  Upscale only
    // the already-cropped strip, then convert it to grayscale; this retains
    // the selected video pixels while giving the OCR engine a usable glyph
    // height and avoids any full-frame/fallback recognition path.
    return {QStringLiteral("-vf"),
            QStringLiteral("crop=%1:%2:%3:%4:exact=1,scale=iw*3:ih*3:flags=lanczos,format=gray")
                .arg(rect.width).arg(rect.height).arg(rect.x).arg(rect.y)};
}

QVector<qint64> SubtitleOcrPipeline::sampleTimes(qint64 durationMs, qint64 intervalMs)
{
    QVector<qint64> result;
    if (durationMs <= 0 || intervalMs <= 0) return result;
    const qint64 finalTimestamp = lastDecodableTimestamp(durationMs);
    // Do not append a safe-end sample behind a later regular sample. The
    // resulting observations are exported as timestamped transcript cues, so
    // their scheduling order must be strictly monotonic as well as decodable.
    for (qint64 value = 0; value <= finalTimestamp; value += intervalMs) result.append(value);
    if (result.isEmpty() || result.constLast() != finalTimestamp) result.append(finalTimestamp);
    return result;
}

qint64 SubtitleOcrPipeline::lastDecodableTimestamp(qint64 durationMs)
{
    // Container duration is not a decoded-frame timestamp. Seeking to
    // duration-1ms can still land after the final video frame for low frame
    // rates/keyframe-only media, where FFmpeg exits 0 without creating an
    // image. Keep the final OCR sample one second inside the stream; short
    // clips safely use their first frame instead.
    constexpr qint64 safeEndMarginMs = 1000;
    return qMax<qint64>(0, durationMs - safeEndMarginMs);
}

QVector<SubtitleOcrSegment> SubtitleOcrPipeline::mergeObservations(
    const QVector<SubtitleOcrObservation> &observations, qint64 intervalMs, double minimumConfidence)
{
    // Bounded local OCR workers complete out of order.  The persisted/exported
    // timeline must still be deterministic and strictly timestamp ordered.
    QVector<SubtitleOcrObservation> ordered = observations;
    std::sort(ordered.begin(), ordered.end(), [](const SubtitleOcrObservation &left,
                                                 const SubtitleOcrObservation &right) {
        return left.timestampMs < right.timestampMs;
    });
    QVector<SubtitleOcrSegment> result;
    for (const auto &observation : ordered) {
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

SubtitleOcrObservation SubtitleOcrPipeline::parseTesseractTsv(const QByteArray &tsv,
                                                               qint64 timestampMs)
{
    struct Line {
        QStringList words;
        double confidenceTotal = 0.0;
        int confidenceCount = 0;
    };

    QVector<Line> lines;
    QHash<QString, int> lineIndexes;
    const QList<QByteArray> rows = tsv.split('\n');
    for (const QByteArray &rawRow : rows) {
        const QStringList columns = QString::fromUtf8(rawRow).trimmed().split(QLatin1Char('\t'));
        // Tesseract TSV has 12 columns. Its first row is the header and the
        // final text field can contain a tab, hence rejoin columns after it.
        if (columns.size() < 12 || columns.constFirst() == QStringLiteral("level")) continue;
        bool confidenceOk = false;
        const double confidence = columns.at(10).toDouble(&confidenceOk);
        const QString word = columns.mid(11).join(QLatin1Char('\t')).trimmed();
        if (!confidenceOk || confidence < 0.0 || word.isEmpty()) continue;

        const QString key = columns.at(2) + QLatin1Char(':') + columns.at(3)
            + QLatin1Char(':') + columns.at(4);
        int index = lineIndexes.value(key, -1);
        if (index < 0) {
            index = lines.size();
            lineIndexes.insert(key, index);
            lines.append(Line{});
        }
        lines[index].words.append(word);
        lines[index].confidenceTotal += confidence / 100.0;
        ++lines[index].confidenceCount;
    }

    QStringList textLines;
    double confidenceTotal = 0.0;
    int confidenceCount = 0;
    for (const Line &line : lines) {
        if (line.words.isEmpty() || line.confidenceCount <= 0) continue;
        textLines.append(line.words.join(QLatin1Char(' ')));
        confidenceTotal += line.confidenceTotal / line.confidenceCount;
        ++confidenceCount;
    }
    return {timestampMs, textLines.join(QLatin1Char('\n')),
            confidenceCount > 0 ? confidenceTotal / confidenceCount : 0.0};
}

bool SubtitleOcrPipeline::containsHanText(const QString &text)
{
    for (const uint codePoint : text.toUcs4()) {
        if ((codePoint >= 0x3400 && codePoint <= 0x4DBF)
            || (codePoint >= 0x4E00 && codePoint <= 0x9FFF)
            || (codePoint >= 0xF900 && codePoint <= 0xFAFF)
            || (codePoint >= 0x20000 && codePoint <= 0x2EBEF)) {
            return true;
        }
    }
    return false;
}

bool SubtitleOcrPipeline::validatePublishableSegments(const QVector<SubtitleOcrSegment> &segments,
                                                       bool requireHanText,
                                                       QString *errorMessage)
{
    if (segments.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("No OCR observations passed the text and confidence filters.");
        return false;
    }

    qint64 previousStartMs = -1;
    bool hasHan = false;
    for (const SubtitleOcrSegment &segment : segments) {
        if (segment.startMs < 0 || segment.endMs <= segment.startMs
            || segment.startMs <= previousStartMs || segment.text.trimmed().isEmpty()) {
            if (errorMessage) *errorMessage = QStringLiteral("OCR produced an invalid or non-increasing timestamped segment.");
            return false;
        }
        previousStartMs = segment.startMs;
        hasHan = hasHan || containsHanText(segment.text);
    }
    if (requireHanText && !hasHan) {
        if (errorMessage) *errorMessage = QStringLiteral("The chi_sim transcript does not contain a Unicode Han character.");
        return false;
    }
    return true;
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
