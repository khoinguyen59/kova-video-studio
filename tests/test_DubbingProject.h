#pragma once

#include <QObject>

namespace LAStudio {

class TestDubbingProject : public QObject
{
    Q_OBJECT
private slots:
    void roundTripsVersionedJson();
    void migratesLegacyProjectsToLlmRewritePipeline();
    void rejectsUnknownSchema();
    void mergesSegmentPatchesByStableId();
    void rejectsUnknownAndDuplicateSegmentPatches();
    void importingMediaDoesNotStartProcessing();
    void dubbingEntryGatePersistsChoiceWithoutMutatingProject();
    void automaticPreflightUsesPersistedLanguageSingleSourceOfTruth();
    void automaticPreflightExposesActionableSourceAndStageStates();
    void automaticPreflightFixTargetsAndNoOpConfigurationsAreExplicit();
    void automaticPreflightReadinessMatrixRejectsFalseReadyStates();
    void automaticWorkflowDoesNotStartWithUnresolvedPreflight();
    void automaticWorkflowRequiresFreshPreflightApproval();
    void customWorkflowOpensFirstMissingNodeSetup();
    void qualityModesExposeExpectedDefaultVoiceModel();
    void standardModesPreserveExplicitNodeModelsOnOpen();
    void sourceSeparationExposesModelSelection();
    void workflowStagesExposeEightProductionBackedSteps();
    void targetLanguageUpdatesVoiceNodeLanguage();
    void rejectsRerunningUnsupportedStep();
    void transcriptionRequiresReadyModel();
    void alignmentRefinementFallsBackWithoutDependencies();
    void audioGenerationWaitsForCompletedSynthesis();
    void audioGenerationUsesSelectedVoiceForEverySegment();
    void selectsBestAutomaticVoiceReference();
    void audioGenerationUsesSavedCloneVoiceForEverySegment();
    void localSavedVoiceRequiresPersistentProfile();
    void zeroCloneVoicePresetBlocksSynthesisWithoutFallback();
    void cloneVoicePresetSelectionPersistsAndMissingPresetBlocks();
    void changingCloneVoicePresetAppliesToEntireNextRun();
    void voiceClonePresetLibraryPersistsAtomicallyAndProtectsSource();
    void voiceClonePresetLibraryMigratesLegacyArrayOnEdit();
    void audioMixRunsAsynchronously();
    void audioMixCreatesIndependentVocalStem();
    void commitsMediaExportAtomically();
    void sourceTextEditInvalidatesWordTiming();
    void unchangedTextEditPreservesTranslationMetadata();
    void targetTextEditRefreshesDurationMetadata();
    void exportsSubtitlesAndReviewPackage();
    void importsDubbingSubtitleFormatsWithoutInventingTiming();
    void persistsDubbingSubtitleStyleAndExportsUnicodeAss();
    void preservesConfiguredLineSpacingInBurnInAss();
    void dubbingSubtitleUiWiresImportPreviewAndBurnIn();
    void resolvesGlobalTimingConflictsWithRippleAndUndo();
    void dubbingTimingUiWiresPreviewApplyAndUndo();
    void dubbingExportUiSeparatesMp4AndEditableCapCutDraft();
    void exportsSelfContainedCapCutDraftWithUnverifiedImportStatus();
    void capCutExportDoesNotMislabelUnseparatedAnalysisAudioAsVocals();
    void segmentNormalizerSplitsLongAsrTranscript();
    void segmentNormalizerUsesAlignedWordBoundaries();
    void segmentNormalizerRebuildsAcrossAsrBoundaries();
    void countsVietnameseSyllablesAndPlansBudget();
    void selectsImprovingDurationCandidate();
    void prefersWithinBudgetDurationCandidate();
    void prefersClosestRepairCandidateOutsideBudget();
    void buildsPauseAlignedTtsChunks();
    void extractsAlignedPauses();
    void roundTripsDurationSettings();
    void normalizesLmStudioTranslationFixConfiguration();
    void parsesLmStudioTranslationFixResponses();
    void buildsConsistentCliInvocations();
    void classifiesCliDiagnostics();
    void discoversCliModelsFromLocalConfiguration();
    void fixesOnlyTranslationsOverPhonemeLimit();
    void ranksPartialTranslationFixesByBudgetDistance();
    void remoteTranslationRoutesDoNotFallbackBetweenGatewayAndColab();
    void remoteTtsRoutesDoNotFallbackBetweenGatewayAndColab();
    void colabDubbingVoiceCloningIsDirectAndRequiresConsent();
    void dubbingDirectColabVoiceCloneReusesProfileAcrossSegments();
    void colabSourceSeparationDoesNotFallbackToLocal();
    void unavailableLocalSourceSeparationDoesNotUseOriginalAudio();
    void failedSeparationBackendDoesNotUseOriginalAudio();
    void incompleteSeparationStemsDoNotCompleteTheNode();
    void dubbingRejectsAConnectedColabWorkerForTheWrongModel();
    void remoteDubbingWorkflowIsReadyWithoutLocalModels();
    void dubbingColabModelsMapToExactNotebooks();
    void dubbingUiUsesExactModelWorkers();
    void dubbingEntryAndAutomaticSetupCannotBypassConfiguration();
    void dubbingTranscriptionWaitsForFreshDecodedAudio();
    void normalizesOcrOnlyTranscriptWithProvenance();
    void fusesMatchingAndShiftedTranscriptWithoutDuplicates();
    void exposesConflictEvidenceWithoutSilentChoice();
    void preservesFusionAndTranscriptSettingsAcrossProjectReload();
    void ocrOnlyTranscriptUsesTheSharedSubtitleOcrController();
    void sttOnlyTranscriptDoesNotRequireOcrRuntime();
    void combinedTranscriptRunsSttAndSharedOcrWithoutFallback();
    void combinedTranscriptReportsOcrFailureWithoutSttFallback();
    void reviewerMustResolveFusionConflictExplicitly();
    void transcriptModePersistsAndColabCardsUseOnlyActiveSourceAndRoute();
    void fusionPoliciesAndBulkResolutionPreserveOriginalEvidence();
    void unresolvedTranscriptConflictsBlockTranslationAndManualReviewPersists();
    void aiReconciliationCapabilityAndReviewDecisionsPreserveEvidence();
    void transcriptConflictUiAndColabSetupWireProductionController();
};

} // namespace LAStudio
