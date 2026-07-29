#pragma once

#include <QObject>

namespace LAStudio {

class TestColabChatRunner final : public QObject
{
    Q_OBJECT

private slots:
    void streamsDirectColabChatOnly();
    void cancellationAbortsDirectStream();
    void languageNotebookMatchesDirectChatContract();
};

} // namespace LAStudio
