#pragma once

#include <QObject>

namespace LAStudio {

class TestSubtitleOcrController final : public QObject
{
    Q_OBJECT

private slots:
    void blocksMissingManagedRuntimeWithoutSilentDownload();
    void rejectsInvalidVideoProbeWithoutReplacingCurrentSource();
    void blocksMissingSelectedLanguageBeforeFrameExtraction();
    void blocksColabRouteWithoutAnExactVerifiedProfile();
    void projectNeverPersistsTemporaryColabCredentials();
    void usesExactManagedTessdataForLanguagePreflightAndRecognition();
    void keepsLowerRegionPresetSeparateFromFullFrameReset();
    void runsManagedAdapterPersistsReviewedSegmentsAndExports();
    void transfersReviewedSegmentsToSubtitleVoiceAndDubbing();
    void cancelsAndRetriesWithoutLeavingOcrStaging();
    void rejectsUnreadableFrameAndRetriesTheSameSample();
    void timesOutFrameExtractionAndKeepsDiagnosticsForRetry();
    void extractsBottomRoiWithTheStagedPackagedFfmpegRuntime();
    void importsSharedStagedMediaWithoutRedownloadAndPreservesSourceOnProbeFailure();
    void importsSharedMediaWithAnUnknownContentLength();
};

} // namespace LAStudio
