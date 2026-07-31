#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <QByteArray>

namespace LAStudio {

struct SubtitleOcrRoi {
    double x = 0.10;
    double y = 0.72;
    double width = 0.80;
    double height = 0.22;

    bool isValid() const;
};

struct SubtitleOcrRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct SubtitleOcrObservation {
    qint64 timestampMs = 0;
    QString text;
    double confidence = 0.0;
};

struct SubtitleOcrSegment {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
    double confidence = 0.0;
};

class SubtitleOcrPipeline final {
public:
    static SubtitleOcrRect sourceRect(const SubtitleOcrRoi &roi, int sourceWidth, int sourceHeight);
    static QStringList ffmpegCropArguments(const SubtitleOcrRoi &roi, int sourceWidth, int sourceHeight);
    static QVector<qint64> sampleTimes(qint64 durationMs, qint64 intervalMs);
    static QVector<SubtitleOcrSegment> mergeObservations(const QVector<SubtitleOcrObservation> &observations,
                                                          qint64 intervalMs, double minimumConfidence);
    static SubtitleOcrObservation parseTesseractTsv(const QByteArray &tsv, qint64 timestampMs);
    static QString toSrt(const QVector<SubtitleOcrSegment> &segments);
};

} // namespace LAStudio
