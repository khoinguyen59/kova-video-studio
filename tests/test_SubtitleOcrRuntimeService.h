#pragma once

#include <QObject>

namespace LAStudio {

class TestSubtitleOcrRuntimeService final : public QObject
{
    Q_OBJECT

private slots:
    void manifestPinsRuntimeAndAllRequiredLanguagePacks();
    void verifiedLanguageReplacementIsAtomicAndChecksumProtected();
    void runtimeActivationIsAtomicAndRestartDiscoveryUsesAppOwnedPath();
    void cancelAndRetryKeepExistingRuntimeUntouched();
    void qmlRouteRoiAndManagedRuntimeControlsAreWired();
};

} // namespace LAStudio
