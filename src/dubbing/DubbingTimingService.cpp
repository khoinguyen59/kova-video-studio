#include "dubbing/DubbingTimingService.h"

#include "audio/AudioTimelineRenderer.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QVector>
#include <QtMath>

#include <algorithm>

namespace LAStudio {

namespace {

struct SpeechInterval
{
    int index = -1;
    QString id;
    qint64 startMs = 0;
    qint64 subtitleEndMs = 0;
    qint64 voiceDurationMs = 0;
    qint64 voiceEndMs = 0;
    bool hasMeasuredVoiceDuration = false;
    bool intentional = false;
};

qint64 measuredVoiceDuration(const QVariantMap &segment)
{
    const qint64 measured = segment.value(QStringLiteral("durationMs"),
                                           segment.value(QStringLiteral("sourceDurationMs"))).toLongLong();
    return qMax<qint64>(0, measured);
}

QVector<SpeechInterval> sortedIntervals(const QVariantList &segments)
{
    QVector<SpeechInterval> intervals;
    intervals.reserve(segments.size());
    for (int index = 0; index < segments.size(); ++index) {
        const QVariantMap segment = segments.at(index).toMap();
        const qint64 start = segment.value(QStringLiteral("startMs")).toLongLong();
        const qint64 end = segment.value(QStringLiteral("endMs")).toLongLong();
        if (end <= start) continue;
        const qint64 duration = measuredVoiceDuration(segment);
        // Before synthesis, retain the cue for an honest "not ready" report,
        // but never pretend its subtitle length is a measured speech duration.
        const qint64 analysisDuration = duration > 0 ? duration : end - start;
        intervals.append({index, segment.value(QStringLiteral("id")).toString(), start, end, analysisDuration,
                          start + analysisDuration, duration > 0,
                          segment.value(QStringLiteral("intentionalOverlap")).toBool()});
    }
    std::stable_sort(intervals.begin(), intervals.end(), [](const SpeechInterval &left,
                                                             const SpeechInterval &right) {
        return left.startMs == right.startMs ? left.index < right.index : left.startMs < right.startMs;
    });
    return intervals;
}

void shiftWords(QVariantMap &segment, qint64 offset)
{
    if (offset == 0) return;
    QVariantList words = segment.value(QStringLiteral("words")).toList();
    for (int index = 0; index < words.size(); ++index) {
        QVariantMap word = words.at(index).toMap();
        if (word.contains(QStringLiteral("startMs")))
            word.insert(QStringLiteral("startMs"), word.value(QStringLiteral("startMs")).toLongLong() + offset);
        if (word.contains(QStringLiteral("endMs")))
            word.insert(QStringLiteral("endMs"), word.value(QStringLiteral("endMs")).toLongLong() + offset);
        words[index] = word;
    }
    if (!words.isEmpty()) segment.insert(QStringLiteral("words"), words);
}

} // namespace

QVariantList DubbingTimingService::fitSegments(const QVariantList &segments,
                                                QAtomicInteger<bool> *cancel,
                                                QString *error)
{
    const auto cancelled = [cancel]() { return cancel && cancel->loadAcquire(); };
    QVariantList result = segments;
    const double maxRate = 1.12;
    for (int i = 0; i < result.size(); ++i) {
        if (cancelled()) {
            if (error) *error = QStringLiteral("Timing fit cancelled.");
            return {};
        }
        QVariantMap segment = result.at(i).toMap();
        if (!segment.value(QStringLiteral("timingConflict")).toBool()
            && (segment.value(QStringLiteral("fitMethod")).toString() == QStringLiteral("atempo")
                || segment.value(QStringLiteral("fitMethod")).toString() == QStringLiteral("natural-with-padding")))
            continue;

        const QString input = segment.value(QStringLiteral("clipPath")).toString();
        if (input.isEmpty() || !QFileInfo::exists(input)) {
            result[i] = segment;
            continue;
        }
        const qint64 slotMs = qMax<qint64>(1, segment.value(QStringLiteral("endMs")).toLongLong()
                                              - segment.value(QStringLiteral("startMs")).toLongLong());
        const qint64 naturalMs = qMax<qint64>(1, segment.value(QStringLiteral("sourceDurationMs")).toLongLong());
        const double fitFactor = double(naturalMs) / double(slotMs);
        if (segment.value(QStringLiteral("durationMetric")).toString()
            == QStringLiteral("phoneme-distance")) {
            const bool conflict = fitFactor < 0.80 || fitFactor > 1.20;
            segment.insert(QStringLiteral("fitFactor"), fitFactor);
            segment.insert(QStringLiteral("timingConflict"), conflict);
            segment.insert(QStringLiteral("fitMethod"), QStringLiteral("paper-dt-natural"));
            segment.insert(QStringLiteral("state"), conflict ? QStringLiteral("conflict") : QStringLiteral("ready"));
            result[i] = segment;
            continue;
        }
        if (fitFactor > maxRate) {
            segment.insert(QStringLiteral("timingConflict"), true);
            segment.insert(QStringLiteral("fitMethod"), QStringLiteral("none"));
            segment.insert(QStringLiteral("state"), QStringLiteral("conflict"));
            result[i] = segment;
            continue;
        }
        if (fitFactor > 1.0) {
            const QString output = input + QStringLiteral(".fitted.wav");
            AudioRenderResult renderResult;
            QString renderError;
            if (!AudioTimelineRenderer::renderClip(
                    input, output, segment.value(QStringLiteral("sampleRate")).toInt(),
                    qMax(1, qRound64(slotMs * segment.value(QStringLiteral("sampleRate")).toInt() / 1000.0)),
                    fitFactor, &renderResult, &renderError)) {
                segment.insert(QStringLiteral("timingConflict"), true);
                segment.insert(QStringLiteral("fitMethod"), QStringLiteral("failed"));
                segment.insert(QStringLiteral("fitError"), renderError);
                segment.insert(QStringLiteral("state"), QStringLiteral("conflict"));
            } else {
                segment.insert(QStringLiteral("clipPath"), output);
                segment.insert(QStringLiteral("durationMs"), slotMs);
                segment.insert(QStringLiteral("sampleCount"),
                               qRound64(slotMs * segment.value(QStringLiteral("sampleRate")).toInt() / 1000.0));
                segment.insert(QStringLiteral("timingConflict"), false);
                segment.insert(QStringLiteral("fitMethod"), renderResult.usedFallback
                               ? QStringLiteral("linear-fallback") : QStringLiteral("atempo"));
                segment.insert(QStringLiteral("state"), QStringLiteral("ready"));
            }
        } else {
            segment.insert(QStringLiteral("timingConflict"), false);
            segment.insert(QStringLiteral("fitMethod"), QStringLiteral("natural-with-padding"));
            segment.insert(QStringLiteral("state"), QStringLiteral("ready"));
        }
        segment.insert(QStringLiteral("fitFactor"), fitFactor);
        result[i] = segment;
    }
    return result;
}

QVariantMap DubbingTimingService::analyzeSpeechOverlaps(const QVariantList &segments,
                                                         qint64 minimumGapMs)
{
    const qint64 gapMs = qBound<qint64>(qint64(0), minimumGapMs, qint64(5000));
    const QVector<SpeechInterval> intervals = sortedIntervals(segments);
    QVariantList conflicts;
    QVariantList unmeasuredSegments;
    qint64 timelineEndMs = 0;
    for (int currentIndex = 0; currentIndex < intervals.size(); ++currentIndex) {
        const SpeechInterval &current = intervals.at(currentIndex);
        if (!current.hasMeasuredVoiceDuration) {
            unmeasuredSegments.append(QVariantMap{{QStringLiteral("index"), current.index},
                                                  {QStringLiteral("id"), current.id}});
        }
        timelineEndMs = qMax(timelineEndMs, current.voiceEndMs);
        for (int previousIndex = 0; previousIndex < currentIndex; ++previousIndex) {
            const SpeechInterval &previous = intervals.at(previousIndex);
            if (previous.voiceEndMs + gapMs <= current.startMs) continue;
            const qint64 overlapMs = previous.voiceEndMs + gapMs - current.startMs;
            const bool intentional = previous.intentional || current.intentional;
            conflicts.append(QVariantMap{
                {QStringLiteral("firstIndex"), previous.index},
                {QStringLiteral("secondIndex"), current.index},
                {QStringLiteral("firstId"), previous.id},
                {QStringLiteral("secondId"), current.id},
                {QStringLiteral("firstStartMs"), previous.startMs},
                {QStringLiteral("firstVoiceEndMs"), previous.voiceEndMs},
                {QStringLiteral("secondStartMs"), current.startMs},
                {QStringLiteral("secondVoiceEndMs"), current.voiceEndMs},
                {QStringLiteral("overlapMs"), overlapMs},
                {QStringLiteral("minimumGapMs"), gapMs},
                {QStringLiteral("intentional"), intentional},
                {QStringLiteral("measured"), previous.hasMeasuredVoiceDuration
                                             && current.hasMeasuredVoiceDuration},
                {QStringLiteral("blocking"), !intentional}
            });
        }
    }
    int blocking = 0;
    for (const QVariant &entry : conflicts) {
        if (entry.toMap().value(QStringLiteral("blocking")).toBool()) ++blocking;
    }
    return {{QStringLiteral("mode"), QStringLiteral("keep")},
            {QStringLiteral("minimumGapMs"), gapMs},
            {QStringLiteral("conflicts"), conflicts},
            {QStringLiteral("unmeasuredSegments"), unmeasuredSegments},
            {QStringLiteral("blockingConflictCount"), blocking},
            {QStringLiteral("timelineDurationMs"), timelineEndMs},
            {QStringLiteral("revisedTimelineDurationMs"), timelineEndMs},
            {QStringLiteral("durationIncreaseMs"), 0},
            {QStringLiteral("revisions"), QVariantList{}}};
}

QVariantList DubbingTimingService::rippleForward(const QVariantList &segments,
                                                  qint64 minimumGapMs,
                                                  QVariantMap *report,
                                                  QString *error)
{
    const qint64 gapMs = qBound<qint64>(qint64(0), minimumGapMs, qint64(5000));
    QVariantList result = segments;
    const QVector<SpeechInterval> intervals = sortedIntervals(segments);
    if (intervals.isEmpty()) {
        if (error) *error = QStringLiteral("Generate valid timed voice clips before resolving overlaps.");
        return {};
    }
    const QVariantMap before = analyzeSpeechOverlaps(segments, gapMs);
    if (!before.value(QStringLiteral("unmeasuredSegments")).toList().isEmpty()) {
        if (error) *error = QStringLiteral(
            "Generate every voice clip before using Ripple forward; one or more segments lack a measured duration.");
        return {};
    }
    QVariantList revisions;
    qint64 cursorMs = 0;
    qint64 originalEndMs = 0;
    for (const SpeechInterval &interval : intervals) {
        QVariantMap segment = result.at(interval.index).toMap();
        const qint64 originalStart = interval.startMs;
        const qint64 originalEnd = interval.subtitleEndMs;
        originalEndMs = qMax(originalEndMs, qMax(originalEnd, interval.voiceEndMs));
        const qint64 revisedStart = interval.intentional ? originalStart
            : qMax(originalStart, cursorMs == 0 ? originalStart : cursorMs + gapMs);
        const qint64 offset = revisedStart - originalStart;
        const qint64 revisedEnd = revisedStart + interval.voiceDurationMs;
        segment.insert(QStringLiteral("rippleOriginalStartMs"), originalStart);
        segment.insert(QStringLiteral("rippleOriginalEndMs"), originalEnd);
        segment.insert(QStringLiteral("rippleOffsetMs"), offset);
        segment.insert(QStringLiteral("startMs"), revisedStart);
        segment.insert(QStringLiteral("endMs"), revisedEnd);
        segment.insert(QStringLiteral("speechOverlapConflict"), false);
        segment.insert(QStringLiteral("timingResolution"), interval.intentional
                       ? QStringLiteral("intentional-overlap") : QStringLiteral("ripple-forward"));
        shiftWords(segment, offset);
        result[interval.index] = segment;
        revisions.append(QVariantMap{
            {QStringLiteral("index"), interval.index}, {QStringLiteral("id"), interval.id},
            {QStringLiteral("originalStartMs"), originalStart}, {QStringLiteral("originalEndMs"), originalEnd},
            {QStringLiteral("revisedStartMs"), revisedStart}, {QStringLiteral("revisedEndMs"), revisedEnd},
            {QStringLiteral("offsetMs"), offset}, {QStringLiteral("intentional"), interval.intentional}
        });
        cursorMs = qMax(cursorMs, revisedEnd);
    }
    QVariantMap after = analyzeSpeechOverlaps(result, gapMs);
    after.insert(QStringLiteral("mode"), QStringLiteral("ripple"));
    after.insert(QStringLiteral("revisions"), revisions);
    after.insert(QStringLiteral("timelineDurationMs"), originalEndMs);
    after.insert(QStringLiteral("revisedTimelineDurationMs"), cursorMs);
    after.insert(QStringLiteral("durationIncreaseMs"), qMax<qint64>(0, cursorMs - originalEndMs));
    after.insert(QStringLiteral("originalBlockingConflictCount"),
                 before.value(QStringLiteral("blockingConflictCount")));
    after.insert(QStringLiteral("originalConflicts"), before.value(QStringLiteral("conflicts")));
    if (report) *report = after;
    return result;
}

} // namespace LAStudio
