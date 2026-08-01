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
    void usesExactManagedTessdataForLanguagePreflightAndRecognition();
    void keepsLowerRegionPresetSeparateFromFullFrameReset();
    void runsManagedAdapterPersistsReviewedSegmentsAndExports();
    void transfersReviewedSegmentsToSubtitleVoiceAndDubbing();
    void cancelsAndRetriesWithoutLeavingOcrStaging();
    void importsSharedStagedMediaWithoutRedownloadAndPreservesSourceOnProbeFailure();
    void importsSharedMediaWithAnUnknownContentLength();
};

} // namespace LAStudio
