#include "dubbing/DubbingTranscriptFusionService.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <QtMath>

namespace LAStudio {
namespace {

QString normalizedText(const QString &text)
{
    QString result = text.normalized(QString::NormalizationForm_KC).toLower();
    result.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" "));
    return result.simplified();
}

double textSimilarity(const QString &left, const QString &right)
{
    const QString normalizedLeft = normalizedText(left);
    const QString normalizedRight = normalizedText(right);
    if (normalizedLeft.isEmpty() || normalizedRight.isEmpty()) return 0.0;
    if (normalizedLeft == normalizedRight) return 1.0;
    const QStringList leftWords = normalizedLeft.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QStringList rightWords = normalizedRight.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QSet<QString> leftTokens(leftWords.cbegin(), leftWords.cend());
    const QSet<QString> rightTokens(rightWords.cbegin(), rightWords.cend());
    QSet<QString> intersection = leftTokens;
    intersection.intersect(rightTokens);
    QSet<QString> unionTokens = leftTokens;
    unionTokens.unite(rightTokens);
    return unionTokens.isEmpty() ? 0.0 : double(intersection.size()) / unionTokens.size();
}

double timeOverlap(const QVariantMap &left, const QVariantMap &right)
{
    const qint64 leftStart = left.value(QStringLiteral("startMs")).toLongLong();
    const qint64 leftEnd = left.value(QStringLiteral("endMs")).toLongLong();
    const qint64 rightStart = right.value(QStringLiteral("startMs")).toLongLong();
    const qint64 rightEnd = right.value(QStringLiteral("endMs")).toLongLong();
    const qint64 overlap = qMax<qint64>(0, qMin(leftEnd, rightEnd) - qMax(leftStart, rightStart));
    const qint64 shortest = qMax<qint64>(1, qMin(leftEnd - leftStart, rightEnd - rightStart));
    return double(overlap) / shortest;
}

QVariantMap provenance(const QString &source, const QString &text, double confidence)
{
    return {{QStringLiteral("source"), source},
            {QStringLiteral("text"), text},
            {QStringLiteral("confidence"), confidence}};
}

double confidenceOf(const QVariantMap &segment, const QString &key, double fallback)
{
    bool converted = false;
    const double value = segment.value(key, segment.value(QStringLiteral("confidence"), fallback))
                             .toDouble(&converted);
    return qBound(0.0, converted ? value : fallback, 1.0);
}

} // namespace

QString DubbingTranscriptFusionService::normalizePolicy(const QString &policy)
{
    const QString normalized = policy.trimmed().toLower();
    if (normalized == QStringLiteral("prefer-stt")
        || normalized == QStringLiteral("stt")) {
        return QStringLiteral("prefer-stt");
    }
    if (normalized == QStringLiteral("prefer-ocr")
        || normalized == QStringLiteral("ocr")) {
        return QStringLiteral("prefer-ocr");
    }
    if (normalized == QStringLiteral("ai-suggest")
        || normalized == QStringLiteral("ai")) {
        return QStringLiteral("ai-suggest");
    }
    return QStringLiteral("ask");
}

QVariantList DubbingTranscriptFusionService::normalizeOcrSegments(const QVariantList &ocrSegments)
{
    QVariantList result;
    for (const QVariant &value : ocrSegments) {
        const QVariantMap observation = value.toMap();
        const qint64 startMs = observation.value(QStringLiteral("startMs")).toLongLong();
        const qint64 endMs = observation.value(QStringLiteral("endMs")).toLongLong();
        const QString text = observation.value(QStringLiteral("text"),
                                                observation.value(QStringLiteral("sourceText"))).toString().trimmed();
        if (startMs < 0 || endMs <= startMs || text.isEmpty()) continue;
        const double confidence = confidenceOf(observation, QStringLiteral("ocrConfidence"), 0.5);
        result.append(QVariantMap{{QStringLiteral("id"),
                                   observation.value(QStringLiteral("id"),
                                                     QUuid::createUuid().toString(QUuid::WithoutBraces))},
                                  {QStringLiteral("startMs"), startMs},
                                  {QStringLiteral("endMs"), endMs},
                                  {QStringLiteral("sourceText"), text},
                                  {QStringLiteral("targetText"), QString()},
                                  {QStringLiteral("speakerId"), QStringLiteral("speaker-1")},
                                  {QStringLiteral("timingSource"), QStringLiteral("subtitle-ocr")},
                                  {QStringLiteral("ocrConfidence"), confidence},
                                  {QStringLiteral("transcriptProvenance"),
                                   QVariantList{provenance(QStringLiteral("ocr"), text, confidence)}},
                                  {QStringLiteral("state"), QStringLiteral("transcribed")}});
    }
    return result;
}

QVariantList DubbingTranscriptFusionService::fuse(const QVariantList &sttSegments,
                                                   const QVariantList &ocrSegments,
                                                   const QString &policy)
{
    const QString normalizedPolicy = normalizePolicy(policy);
    QVariantList result;
    QVector<QVariantMap> ocr;
    ocr.reserve(ocrSegments.size());
    for (const QVariant &value : normalizeOcrSegments(ocrSegments)) ocr.append(value.toMap());
    QSet<int> matchedOcr;

    for (const QVariant &value : sttSegments) {
        QVariantMap segment = value.toMap();
        const QString sttText = segment.value(QStringLiteral("sourceText")).toString().trimmed();
        const qint64 startMs = segment.value(QStringLiteral("startMs")).toLongLong();
        const qint64 endMs = segment.value(QStringLiteral("endMs")).toLongLong();
        if (sttText.isEmpty() || startMs < 0 || endMs <= startMs) continue;

        const double sttConfidence = confidenceOf(segment, QStringLiteral("sttConfidence"), 0.5);
        int bestIndex = -1;
        double bestScore = 0.0;
        for (int index = 0; index < ocr.size(); ++index) {
            if (matchedOcr.contains(index)) continue;
            const double overlap = timeOverlap(segment, ocr.at(index));
            const double similarity = textSimilarity(sttText,
                                                     ocr.at(index).value(QStringLiteral("sourceText")).toString());
            const qint64 sttCenter = (startMs + endMs) / 2;
            const qint64 ocrCenter = (ocr.at(index).value(QStringLiteral("startMs")).toLongLong()
                                     + ocr.at(index).value(QStringLiteral("endMs")).toLongLong()) / 2;
            if (overlap < 0.30 && !(qAbs(sttCenter - ocrCenter) <= 650 && similarity >= 0.40)) continue;
            const double score = 0.70 * overlap + 0.30 * similarity;
            if (score > bestScore) {
                bestScore = score;
                bestIndex = index;
            }
        }

        segment.insert(QStringLiteral("sttConfidence"), sttConfidence);
        segment.insert(QStringLiteral("transcriptSourceMode"), QStringLiteral("reconcile"));
        if (bestIndex < 0) {
            segment.insert(QStringLiteral("fusionStatus"), QStringLiteral("stt-only"));
            segment.insert(QStringLiteral("transcriptProvenance"),
                           QVariantList{provenance(QStringLiteral("stt"), sttText, sttConfidence)});
            result.append(segment);
            continue;
        }

        matchedOcr.insert(bestIndex);
        const QVariantMap ocrSegment = ocr.at(bestIndex);
        const QString ocrText = ocrSegment.value(QStringLiteral("sourceText")).toString();
        const double ocrConfidence = confidenceOf(ocrSegment, QStringLiteral("ocrConfidence"), 0.5);
        const double similarity = textSimilarity(sttText, ocrText);
        const bool conflict = similarity < 0.55;
        const bool preferOcr = !conflict && ocrConfidence > sttConfidence + 0.12;
        segment.insert(QStringLiteral("ocrText"), ocrText);
        segment.insert(QStringLiteral("ocrConfidence"), ocrConfidence);
        segment.insert(QStringLiteral("fusionSimilarity"), similarity);
        segment.insert(QStringLiteral("fusionSttText"), sttText);
        segment.insert(QStringLiteral("fusionOcrText"), ocrText);
        segment.insert(QStringLiteral("fusionPolicy"), normalizedPolicy);
        segment.insert(QStringLiteral("transcriptProvenance"),
                       QVariantList{provenance(QStringLiteral("stt"), sttText, sttConfidence),
                                    provenance(QStringLiteral("ocr"), ocrText, ocrConfidence)});
        if (conflict) {
            // Keep the current sourceText as the STT observation until a
            // chosen policy or a reviewer supplies the final text.  Crucially,
            // `fusionChoice` is pending -- it is not a covert STT decision.
            segment.insert(QStringLiteral("fusionChoice"), QStringLiteral("pending"));
            if (normalizedPolicy == QStringLiteral("prefer-stt")
                || normalizedPolicy == QStringLiteral("prefer-ocr")) {
                const bool useOcr = normalizedPolicy == QStringLiteral("prefer-ocr");
                segment.insert(QStringLiteral("sourceText"), useOcr ? ocrText : sttText);
                segment.insert(QStringLiteral("fusionChoice"), useOcr ? QStringLiteral("ocr")
                                                                         : QStringLiteral("stt"));
                segment.insert(QStringLiteral("fusionStatus"), QStringLiteral("resolved"));
                segment.insert(QStringLiteral("fusionNeedsReview"), false);
                segment.insert(QStringLiteral("fusionResolutionPolicy"), normalizedPolicy);
                segment.insert(QStringLiteral("state"), QStringLiteral("transcribed"));
            } else {
                segment.insert(QStringLiteral("fusionStatus"), QStringLiteral("conflict"));
                segment.insert(QStringLiteral("fusionNeedsReview"), true);
                segment.insert(QStringLiteral("state"), QStringLiteral("needs-review"));
            }
        } else if (preferOcr) {
            segment.insert(QStringLiteral("fusionChoice"), QStringLiteral("ocr"));
            segment.insert(QStringLiteral("fusionStatus"), QStringLiteral("matched"));
            segment.insert(QStringLiteral("fusionNeedsReview"), false);
            segment.insert(QStringLiteral("sourceText"), ocrText);
            segment.insert(QStringLiteral("timingSource"), QStringLiteral("asr-with-ocr-text"));
        } else {
            segment.insert(QStringLiteral("fusionChoice"), QStringLiteral("stt"));
            segment.insert(QStringLiteral("fusionStatus"), QStringLiteral("matched"));
            segment.insert(QStringLiteral("fusionNeedsReview"), false);
        }
        result.append(segment);
    }

    for (int index = 0; index < ocr.size(); ++index) {
        if (matchedOcr.contains(index)) continue;
        QVariantMap segment = ocr.at(index);
        segment.insert(QStringLiteral("transcriptSourceMode"), QStringLiteral("reconcile"));
        segment.insert(QStringLiteral("fusionStatus"), QStringLiteral("ocr-only"));
        result.append(segment);
    }
    return result;
}

} // namespace LAStudio
