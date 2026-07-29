#pragma once

#include <QObject>

namespace LAStudio {

class TestColabTtsRunner final : public QObject
{
    Q_OBJECT

private slots:
    void testPostsDirectWorkerSpeechRequest();
    void testTtsModelNotebookMapping();
    void ttsNotebookMatchesDirectColabContract();
};

} // namespace LAStudio
