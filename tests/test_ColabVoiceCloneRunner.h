#pragma once

#include <QObject>

namespace LAStudio {

class TestColabVoiceCloneRunner final : public QObject
{
    Q_OBJECT

private slots:
    void testRunsVoiceProfileAndGenerationDirectlyOnColab();
    void controllerReusesProfileOnlyForMatchingDurableReference();
    void controllerAllowsAutomaticReferenceTranscript();
    void savedPresetSurvivesRestartAndInvalidatesTemporaryProfile();
    void testRejectsProfileWithoutConsent();
    void exactModelMappingMatchesCatalogAndNotebooks();
};

} // namespace LAStudio
