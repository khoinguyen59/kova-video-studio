#pragma once

#include <QObject>

namespace LAStudio {

class TestColabAlignmentRunner final : public QObject
{
    Q_OBJECT

private slots:
    void testPostsDirectAlignmentContractAndValidatesSpans();
    void testRejectsNonMonotonicAndCancelledResponses();
    void alignmentNotebookMatchesDirectColabContract();
};

} // namespace LAStudio
