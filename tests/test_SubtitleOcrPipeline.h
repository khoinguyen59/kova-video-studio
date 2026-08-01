#pragma once

#include <QObject>

namespace LAStudio {

class TestSubtitleOcrPipeline final : public QObject {
    Q_OBJECT

private slots:
    void mapsNormalizedRoiToSourcePixels();
    void buildsPortableFfmpegCropArguments();
    void samplesDurationWithoutDuplicatingTheFinalFrame();
    void rejectsNormalizedRegionsThatRoundToZeroPixels();
    void mergesRepeatedTextAndSkipsLowConfidenceObservations();
    void parsesMultilineUnicodeTesseractTsv();
    void rejectsUnpublishableOrNonHanChineseSegments();
    void exportsStableSrtTiming();
};

} // namespace LAStudio
