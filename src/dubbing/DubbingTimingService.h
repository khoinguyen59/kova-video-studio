#pragma once

#include <QAtomicInteger>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace LAStudio {

class DubbingTimingService final
{
public:
    // Pure data boundary for timing fit.  The caller owns publication of the
    // returned list and the service never touches QObject/controller state.
    static QVariantList fitSegments(const QVariantList &segments,
                                    QAtomicInteger<bool> *cancel = nullptr,
                                    QString *error = nullptr);

    // Reads the measured TTS duration (durationMs/sourceDurationMs) and finds
    // speech collisions on one global track. The report is deterministic and
    // value-only so preview and apply cannot disagree.
    static QVariantMap analyzeSpeechOverlaps(const QVariantList &segments,
                                             qint64 minimumGapMs = 80);

    // Moves later clips and their reviewed subtitle intervals forward without
    // changing any original media asset. Intentional overlaps stay put.
    static QVariantList rippleForward(const QVariantList &segments,
                                      qint64 minimumGapMs,
                                      QVariantMap *report = nullptr,
                                      QString *error = nullptr);
};

} // namespace LAStudio
