#include "test_SubtitleOcrPipeline.h"

#include "subtitles/SubtitleOcrPipeline.h"

#include <QtTest>

namespace LAStudio {

void TestSubtitleOcrPipeline::mapsNormalizedRoiToSourcePixels()
{
    const SubtitleOcrRoi roi{0.10, 0.72, 0.80, 0.22};
    QVERIFY(roi.isValid());
    const SubtitleOcrRect rect = SubtitleOcrPipeline::sourceRect(roi, 1920, 1080);
    QCOMPARE(rect.x, 192);
    QCOMPARE(rect.y, 778);
    QCOMPARE(rect.width, 1536);
    QCOMPARE(rect.height, 238);
    const SubtitleOcrRoi invalidRoi{0.9, 0.9, 0.2, 0.2};
    QVERIFY(!invalidRoi.isValid());
}

void TestSubtitleOcrPipeline::buildsPortableFfmpegCropArguments()
{
    const QStringList arguments = SubtitleOcrPipeline::ffmpegCropArguments(
        SubtitleOcrRoi{0.25, 0.50, 0.50, 0.25}, 1280, 720);
    QCOMPARE(arguments, QStringList({QStringLiteral("-vf"), QStringLiteral("crop=640:180:320:360")}));
}

void TestSubtitleOcrPipeline::samplesDurationWithoutDuplicatingTheFinalFrame()
{
    QCOMPARE(SubtitleOcrPipeline::sampleTimes(5000, 2000), QVector<qint64>({0, 2000, 4000, 5000}));
    QCOMPARE(SubtitleOcrPipeline::sampleTimes(4000, 2000), QVector<qint64>({0, 2000, 4000}));
    QVERIFY(SubtitleOcrPipeline::sampleTimes(1000, 0).isEmpty());
}

void TestSubtitleOcrPipeline::mergesRepeatedTextAndSkipsLowConfidenceObservations()
{
    const QVector<SubtitleOcrObservation> observations{
        {0, QStringLiteral("Hello"), 0.95},
        {1000, QStringLiteral("Hello"), 0.90},
        {2000, QStringLiteral(""), 1.0},
        {3000, QStringLiteral("World"), 0.91},
        {4000, QStringLiteral("ignored"), 0.20},
    };
    const QVector<SubtitleOcrSegment> segments = SubtitleOcrPipeline::mergeObservations(observations, 1000, 0.80);
    QCOMPARE(segments.size(), 2);
    QCOMPARE(segments.at(0).startMs, qint64(0));
    QCOMPARE(segments.at(0).endMs, qint64(2000));
    QCOMPARE(segments.at(0).text, QStringLiteral("Hello"));
    QCOMPARE(segments.at(1).startMs, qint64(3000));
    QCOMPARE(segments.at(1).endMs, qint64(4000));
}

void TestSubtitleOcrPipeline::exportsStableSrtTiming()
{
    const QString srt = SubtitleOcrPipeline::toSrt({
        {0, 1250, QStringLiteral("First line"), 0.90},
        {3723, 6000, QStringLiteral("Second line"), 0.90},
    });
    QCOMPARE(srt, QStringLiteral("1\n00:00:00,000 --> 00:00:01,250\nFirst line\n\n"
                                 "2\n00:00:03,723 --> 00:00:06,000\nSecond line\n\n"));
}

} // namespace LAStudio
