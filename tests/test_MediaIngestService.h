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
    void rejectsPrivateRedirectBeforeAnyStaging();
    void rejectsUnsafePublicAdapterResults();
    void resolvesPublicVideoPageThroughManagedAdapter();
    void resolverTimeoutCanRetry();
    void resolverArgumentsKeepUntrustedUrlPositional();
    void controllerCommitsDirectLinkOnlyAfterRealProbeAndNormalization();
    void standaloneDownloadHandsOffOwnedMediaWithoutSecondDownload();
    void standaloneDownloadKeepsExistingProjectWhenProbeFails();
    void controllerQueuesMultipleDirectDownloadsWithoutPersistingUrls();
    void mediaBatchContinuesAfterARealWorkerFailure();
    void downloadRouteAndDubbingLinkControlAreWired();
};

} // namespace LAStudio
