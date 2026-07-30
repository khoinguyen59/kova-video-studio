#pragma once

#include <QObject>

namespace LAStudio {

class TestColabTranslationRunner final : public QObject
{
    Q_OBJECT

private slots:
    void postsBatchToDirectWorkerOnly();
    void progressReflectsCompletedSegments();
    void invalidPatchReportsItsBrokenField();
    void needsReviewPatchContinuesTranslation();
    void cancellationAbortsDirectWorkerRequest();
    void languageNotebookMatchesDirectTranslationContract();
};

} // namespace LAStudio
