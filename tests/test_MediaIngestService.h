#pragma once

#include <QObject>

namespace LAStudio {

class TestMediaIngestService final : public QObject {
    Q_OBJECT

private slots:
    void rejectsMissingInputExactlyOnce();
    void prefersBundledMediaToolsOverExternalConfiguration();
    void downloadsDirectLoopbackMediaIntoOwnedStaging();
    void reportsByteProgressForDirectMediaDownload();
    void cancelRemovesPartialStagedMedia();
    void rejectsOversizedMediaBeforeStaging();
    void rejectsUnsafeRemoteMediaUrl();
    void rejectsUnsafePublicAdapterResults();
    void resolvesPublicVideoPageThroughManagedAdapter();
    void controllerCommitsDirectLinkOnlyAfterRealProbeAndNormalization();
    void standaloneDownloadHandsOffOwnedMediaWithoutSecondDownload();
    void standaloneDownloadKeepsExistingProjectWhenProbeFails();
    void downloadRouteAndDubbingLinkControlAreWired();
};

} // namespace LAStudio
