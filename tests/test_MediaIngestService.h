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
    void resolverArgumentsUseExplicitCookiesOnlyWhenProvided();
    void douyinBrowserArgumentsUseDedicatedProfileOnly();
    void douyinBrowserDoesNotUseBrowserCookieImportFlags();
    void explicitCookieFileIsCopiedTemporarilyAndRemovedAfterResolver();
    void resolverFreshCookieDiagnosticIsActionable();
    void controllerDownloadsSharedTextLocallyAndClearsCookieSelection();
    void controllerAddsMultipleManualFilesWithoutDownloader();
    void legacyLinkHandoffIsDisabled();
    void manualMediaLibraryHasNoSourceUrls();
    void sharedVideoTextIsExtractedForLocalDownloader();
    void singleImportedMediaBecomesTheActiveProject();
    void mediaLibraryRunsOnlyTheLaterSelectedActionSubset();
    void mediaBatchContinuesAfterARealWorkerFailure();
    void mediaBatchCanRunEachStageAcrossTheSelectedQueue();
    void downloadRouteAndDubbingLinkControlAreWired();
};

} // namespace LAStudio
