#pragma once

#include <QObject>

namespace LAStudio {

class TestSubtitleOcrRuntimeService final : public QObject
{
    Q_OBJECT

private slots:
    void manifestDescribesBundledRuntimeAndAllRequiredLanguagePacks();
    void bundledRuntimeManifestMustMatchBinary();
    void bundledRuntimeWinsOverLegacyManagedExecutable();
    void verifiedLanguageReplacementIsAtomicAndChecksumProtected();
    void runtimeActivationIsAtomicAndRestartDiscoveryUsesAppOwnedPath();
    void cancelAndRetryKeepExistingLanguageDataUntouched();
    void missingPackageRuntimeFailsWithoutStartingAnInstaller();
    void healthCheckFailureDoesNotActivateStagingRuntime();
    void failedInstallerCacheRequiresExplicitCleanup();
    void qmlRouteRoiAndManagedRuntimeControlsAreWired();
    void packageScriptStagesPaddleRuntimeWithBomSafeManifest();
    void responsiveLayoutSharedMediaAndHomeCardsAreWired();
};

} // namespace LAStudio
