#pragma once

#include <QObject>

namespace LAStudio {

class TestSubtitleOcrPipeline final : public QObject {
    Q_OBJECT

private slots:
    void mapsNormalizedRoiToSourcePixels();
    void buildsPortableFfmpegCropArguments();
    void samplesDurationWithoutDuplicatingTheFinalFrame();
    void mergesRepeatedTextAndSkipsLowConfidenceObservations();
    void exportsStableSrtTiming();
};

} // namespace LAStudio
