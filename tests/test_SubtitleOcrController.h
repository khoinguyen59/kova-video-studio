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
    void rejectsIncompletePaddleOcrRuntimeManifest();
    void runsPaddleOcrBatchAdapterWithoutTesseractFallback();
    void localPaddleRouteRejectsUnbundledLanguageBeforeProcessing();
    void keepsLowerRegionPresetSeparateFromFullFrameReset();
    void runsManagedAdapterPersistsReviewedSegmentsAndExports();
    void transfersReviewedSegmentsToSubtitleVoiceAndDubbing();
    void cancelsAndRetriesWithoutLeavingOcrStaging();
    void rejectsUnreadableFrameAndRetriesTheSameSample();
    void timesOutFrameExtractionAndKeepsDiagnosticsForRetry();
    void rejectsNoTextCompletionClearsStaleSegmentsAndBlocksExport();
    void extractsBottomRoiWithTheStagedPackagedFfmpegRuntime();
    void acceptsLocallyStagedMediaAndPreservesSourceOnProbeFailure();
    void rejectsRemoteMediaLinksBeforeAnyDesktopRequest();
};

} // namespace LAStudio
