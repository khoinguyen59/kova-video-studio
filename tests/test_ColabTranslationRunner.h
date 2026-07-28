#pragma once

#include <QObject>

namespace LAStudio {

class TestColabTranslationRunner final : public QObject
{
    Q_OBJECT

private slots:
    void postsBatchToDirectWorkerOnly();
    void cancellationAbortsDirectWorkerRequest();
    void languageNotebookMatchesDirectTranslationContract();
};

} // namespace LAStudio
