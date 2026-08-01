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
    QCOMPARE(arguments, QStringList({
        QStringLiteral("-vf"),
        QStringLiteral("crop=640:180:320:360:exact=1,scale=iw*3:ih*3:flags=lanczos,format=gray")}));
}

void TestSubtitleOcrPipeline::samplesDurationWithoutDuplicatingTheFinalFrame()
{
    QCOMPARE(SubtitleOcrPipeline::sampleTimes(5000, 2000), QVector<qint64>({0, 2000, 4000}));
    QCOMPARE(SubtitleOcrPipeline::sampleTimes(4000, 2000), QVector<qint64>({0, 2000, 3000}));
    QCOMPARE(SubtitleOcrPipeline::lastDecodableTimestamp(1), qint64(0));
    QCOMPARE(SubtitleOcrPipeline::lastDecodableTimestamp(110000), qint64(109000));
    QVERIFY(SubtitleOcrPipeline::sampleTimes(1000, 0).isEmpty());
    const QVector<qint64> nearEnd = SubtitleOcrPipeline::sampleTimes(899841, 800);
    QVERIFY(!nearEnd.isEmpty());
    QCOMPARE(nearEnd.constLast(), qint64(898841));
    for (int index = 1; index < nearEnd.size(); ++index)
        QVERIFY(nearEnd.at(index) > nearEnd.at(index - 1));
}

void TestSubtitleOcrPipeline::rejectsNormalizedRegionsThatRoundToZeroPixels()
{
    const SubtitleOcrRect rect = SubtitleOcrPipeline::sourceRect(
        SubtitleOcrRoi{0.0, 0.0, 0.0001, 0.50}, 320, 180);
    QCOMPARE(rect.width, 0);
    QCOMPARE(rect.height, 0);
    QVERIFY(SubtitleOcrPipeline::ffmpegCropArguments(
        SubtitleOcrRoi{0.0, 0.0, 0.0001, 0.50}, 320, 180).isEmpty());
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

void TestSubtitleOcrPipeline::parsesMultilineUnicodeTesseractTsv()
{
    const QByteArray tsv = QByteArrayLiteral("level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n")
        + QString::fromUtf8("5\t1\t1\t1\t1\t1\t0\t0\t10\t10\t96\tXin\n"
                            "5\t1\t1\t1\t1\t2\t10\t0\t10\t10\t94\tchào\n"
                            "5\t1\t1\t1\t2\t1\t0\t10\t10\t10\t92\t中文 日本語 한국어\n")
              .toUtf8();
    const SubtitleOcrObservation observation = SubtitleOcrPipeline::parseTesseractTsv(tsv, 1250);
    QCOMPARE(observation.timestampMs, qint64(1250));
    QCOMPARE(observation.text, QString::fromUtf8("Xin chào\n中文 日本語 한국어"));
    QVERIFY(observation.confidence > 0.90 && observation.confidence < 0.95);
}

void TestSubtitleOcrPipeline::rejectsUnpublishableOrNonHanChineseSegments()
{
    QString error;
    QVERIFY(!SubtitleOcrPipeline::validatePublishableSegments({}, false, &error));
    QVERIFY(error.contains(QStringLiteral("No OCR observations")));

    const QVector<SubtitleOcrSegment> invalidTiming{{1000, 1800, QString::fromUtf8("有效"), 0.9},
                                                     {1000, 2000, QString::fromUtf8("字幕"), 0.9}};
    QVERIFY(!SubtitleOcrPipeline::validatePublishableSegments(invalidTiming, true, &error));
    QVERIFY(error.contains(QStringLiteral("non-increasing")));

    const QVector<SubtitleOcrSegment> latin{{0, 800, QStringLiteral("subtitle"), 0.9}};
    QVERIFY(!SubtitleOcrPipeline::validatePublishableSegments(latin, true, &error));
    QVERIFY(error.contains(QStringLiteral("Unicode Han")));

    const QVector<SubtitleOcrSegment> chinese{{0, 800, QString::fromUtf8("中文字幕"), 0.9},
                                               {800, 1600, QString::fromUtf8("第二行"), 0.9}};
    QVERIFY(SubtitleOcrPipeline::containsHanText(chinese.constFirst().text));
    QVERIFY(SubtitleOcrPipeline::validatePublishableSegments(chinese, true, &error));
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
