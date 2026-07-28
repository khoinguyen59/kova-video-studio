#pragma once

#include <QObject>

namespace LAStudio {

class TestColabTtsRunner final : public QObject
{
    Q_OBJECT

private slots:
    void testPostsDirectWorkerSpeechRequest();
    void ttsNotebookMatchesDirectColabContract();
};

} // namespace LAStudio
