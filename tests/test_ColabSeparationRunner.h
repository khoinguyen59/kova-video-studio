#pragma once

#include <QObject>

namespace LAStudio {

class TestColabSeparationRunner final : public QObject
{
    Q_OBJECT

private slots:
    void testUsesDirectJobAndArtifactContract();
    void testCancellationDiscardsPartialArtifacts();
    void voiceCloneReferenceUsesCachedVocalsOnly();
    void separationNotebookMatchesDirectColabContract();
};

} // namespace LAStudio
