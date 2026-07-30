#pragma once

#include <QObject>

namespace LAStudio {

class TestWorkflowGraph : public QObject
{
    Q_OBJECT
private slots:
    void rejectsCyclesAndTypeMismatch();
    void runsRegisteredNodesInTopologicalOrder();
    void exposesOnlyActiveNodeMeasuredProgress();
    void serializesAndRestoresVersionedLinks();
    void rejectsMissingPortsAndMultipleSingleInputs();
    void buildsCanonicalDubbingWorkflowDefinition();
    void commitsAndResolvesAtomicArtifacts();
    void validatesDubbingGraphAgainstNodeContracts();
    void validatesTypedTranscriptArtifacts();
    void executesCoreDubbingNodeAdapter();
    void pausesAndResumesReviewGate();
    void persistsReviewRequestsAtomically();
    void appendsOrderedRunJournalEvents();
    void resumesInterruptedRunFromJournalSnapshot();
    void recordsCancellationSeparatelyFromFailure();
};

} // namespace LAStudio
