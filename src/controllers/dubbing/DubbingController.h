#pragma once

#include <QObject>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QSet>
#include <QtQml/qqml.h>
#include <memory>

#include "dubbing/DubbingProject.h"
#include "workflows/NodeRegistry.h"
#include "workflows/WorkflowGraphRunner.h"

namespace LAStudio {

class SttSessionController;
class TtsEngine;
class ModelManager;
class RuntimeManager;
class TranslationEngine;
class DubbingJobRunner;
class DubbingTranslationFixService;
class CapabilityFamilyModel;
class Settings;
class ColabSession;
class VoiceClonePresetService;
class RemoteMediaImportService;
class SubtitleOcrController;

class DubbingController : public QObject
{
    Q_OBJECT
    QML_UNCREATABLE("DubbingController is managed by AppController")

    Q_PROPERTY(bool hasProject READ hasProject NOTIFY projectChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    Q_PROPERTY(QString sourceMediaPath READ sourceMediaPath NOTIFY projectChanged)
    Q_PROPERTY(QUrl sourceMediaUrl READ sourceMediaUrl NOTIFY projectChanged)
    Q_PROPERTY(QUrl playbackMediaUrl READ playbackMediaUrl NOTIFY previewChanged)
    Q_PROPERTY(QString normalizedAudioPath READ normalizedAudioPath NOTIFY projectChanged)
    Q_PROPERTY(QString vocalsPath READ vocalsPath NOTIFY projectChanged)
    Q_PROPERTY(QString backgroundPath READ backgroundPath NOTIFY projectChanged)
    Q_PROPERTY(QString sourceLanguage READ sourceLanguage WRITE setSourceLanguage NOTIFY projectChanged)
    Q_PROPERTY(QString targetLanguage READ targetLanguage WRITE setTargetLanguage NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap durationControl READ durationControl WRITE setDurationControl NOTIFY projectChanged)
    Q_PROPERTY(QVariantList speakers READ speakers NOTIFY projectChanged)
    Q_PROPERTY(QVariantList segments READ segments NOTIFY segmentsChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY processingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY processingChanged)
    Q_PROPERTY(bool progressAvailable READ progressAvailable NOTIFY processingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QString previewPath READ previewPath NOTIFY previewChanged)
    Q_PROPERTY(QString dubbedVocalPath READ dubbedVocalPath NOTIFY previewChanged)
    Q_PROPERTY(QString exportPath READ exportPath NOTIFY exportChanged)
    Q_PROPERTY(QString capCutDraftPath READ capCutDraftPath NOTIFY exportChanged)
    Q_PROPERTY(QString capCutDraftWarning READ capCutDraftWarning NOTIFY exportChanged)
    // A batch item represents a locally staged media file and its durable
    // per-item outputs. Public source URLs are transient local-download input
    // only and are removed before a library item becomes ready.
    Q_PROPERTY(QVariantList mediaQueueItems READ mediaQueueItems NOTIFY mediaQueueChanged)
    Q_PROPERTY(bool mediaQueueDownloading READ mediaQueueDownloading NOTIFY mediaQueueChanged)
    Q_PROPERTY(bool mediaQueueProcessing READ mediaQueueProcessing NOTIFY mediaQueueChanged)
    Q_PROPERTY(QString mediaQueueStatus READ mediaQueueStatus NOTIFY mediaQueueChanged)
    Q_PROPERTY(int mediaQueueProgress READ mediaQueueProgress NOTIFY mediaQueueChanged)
    Q_PROPERTY(bool mediaDownloadCookieFileConfigured READ mediaDownloadCookieFileConfigured NOTIFY mediaQueueChanged)
    Q_PROPERTY(QVariantList workflowNodes READ workflowNodes NOTIFY workflowChanged)
    // Presentation-only aggregation of the durable workflow node ids.  The
    // serialized graph deliberately keeps its existing ids so projects made
    // by earlier releases can still resume and rerun their production nodes.
    Q_PROPERTY(QVariantList workflowStages READ workflowStages NOTIFY workflowChanged)
    Q_PROPERTY(QVariantMap workflowNodeConfigurations READ workflowNodeConfigurations NOTIFY workflowChanged)
    Q_PROPERTY(QVariantMap transcriptConfiguration READ transcriptConfiguration NOTIFY projectChanged)
    Q_PROPERTY(int unresolvedTranscriptConflictCount READ unresolvedTranscriptConflictCount NOTIFY segmentsChanged)
    // Subtitle OCR is intentionally independent from the audio STT runner.
    // Exposing its state separately keeps a running STT job from disabling
    // the OCR action (and the other way round).
    Q_PROPERTY(bool subtitleOcrProcessing READ subtitleOcrProcessing NOTIFY subtitleOcrProcessingChanged)
    Q_PROPERTY(bool sttCanRunAlongsideSubtitleOcr READ sttCanRunAlongsideSubtitleOcr NOTIFY workflowChanged)
    Q_PROPERTY(bool subtitleOcrCanRunAlongsideStt READ subtitleOcrCanRunAlongsideStt NOTIFY workflowChanged)
    Q_PROPERTY(QVariantMap dubbingOcrRoi READ dubbingOcrRoi NOTIFY projectChanged)
    Q_PROPERTY(bool dubbingOcrRoiVisible READ dubbingOcrRoiVisible NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap subtitleConfiguration READ subtitleConfiguration NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap timingConfiguration READ timingConfiguration NOTIFY timingResolutionChanged)
    Q_PROPERTY(QVariantList timingConflicts READ timingConflicts NOTIFY timingResolutionChanged)
    Q_PROPERTY(QVariantMap timingResolutionPreview READ timingResolutionPreview NOTIFY timingResolutionChanged)
    Q_PROPERTY(bool timingUndoAvailable READ timingUndoAvailable NOTIFY timingResolutionChanged)
    Q_PROPERTY(bool workflowReady READ workflowReady NOTIFY workflowChanged)
    Q_PROPERTY(QString workflowStatusText READ workflowStatusText NOTIFY workflowChanged)
    Q_PROPERTY(QString workflowId READ workflowId CONSTANT)
    Q_PROPERTY(int workflowVersion READ workflowVersion CONSTANT)
    Q_PROPERTY(bool workflowGraphValid READ workflowGraphValid NOTIFY workflowChanged)
    Q_PROPERTY(QString workflowRunId READ workflowRunId NOTIFY processingChanged)
    Q_PROPERTY(QString workflowNodeRunId READ workflowNodeRunId NOTIFY processingChanged)
    Q_PROPERTY(bool workflowWaitingForInput READ workflowWaitingForInput NOTIFY workflowChanged)
    Q_PROPERTY(QVariantMap workflowReviewRequest READ workflowReviewRequest NOTIFY workflowChanged)
    Q_PROPERTY(bool workflowRecoveryAvailable READ workflowRecoveryAvailable NOTIFY workflowChanged)
    Q_PROPERTY(QVariantMap workflowRecovery READ workflowRecovery NOTIFY workflowChanged)
    Q_PROPERTY(QString workflowMode READ workflowMode NOTIFY workflowChanged)
    // The entry gate is intentionally distinct from workflowMode: entering a
    // tab must never create, resume, or mutate a workflow merely to show UI.
    Q_PROPERTY(bool dubbingEntryGateActive READ dubbingEntryGateActive NOTIFY workflowChanged)
    Q_PROPERTY(QString savedDubbingEntryMode READ savedDubbingEntryMode NOTIFY projectChanged)
    Q_PROPERTY(QString currentStepId READ currentStepId NOTIFY workflowChanged)
    Q_PROPERTY(QVariantMap currentStepOutput READ currentStepOutput NOTIFY workflowChanged)
    Q_PROPERTY(QString lastCompletedStepId READ lastCompletedStepId NOTIFY workflowChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(bool translationFixing READ translationFixing NOTIFY translationFixChanged)
    Q_PROPERTY(int translationFixProgress READ translationFixProgress NOTIFY translationFixChanged)
    Q_PROPERTY(QString translationFixStatus READ translationFixStatus NOTIFY translationFixChanged)
    Q_PROPERTY(QVariantMap translationFixConfiguration READ translationFixConfiguration NOTIFY translationFixChanged)
    Q_PROPERTY(int translationFixCandidateCount READ translationFixCandidateCount NOTIFY segmentsChanged)
    Q_PROPERTY(QString dubbingQuality READ dubbingQuality WRITE setDubbingQuality NOTIFY projectChanged)
    Q_PROPERTY(QString adaptiveProvider READ adaptiveProvider NOTIFY translationFixChanged)
    Q_PROPERTY(bool adaptiveReady READ adaptiveReady NOTIFY workflowChanged)
    Q_PROPERTY(QString adaptiveStatusText READ adaptiveStatusText NOTIFY workflowChanged)
    Q_PROPERTY(bool customReady READ customReady NOTIFY workflowChanged)
    Q_PROPERTY(QString customStatusText READ customStatusText NOTIFY workflowChanged)
    Q_PROPERTY(bool settingsLocked READ settingsLocked NOTIFY workflowChanged)
    Q_PROPERTY(bool automaticSetupActive READ automaticSetupActive NOTIFY workflowChanged)
    Q_PROPERTY(QString automaticStatusText READ automaticStatusText NOTIFY workflowChanged)
    Q_PROPERTY(QVariantList automaticEvents READ automaticEvents NOTIFY workflowChanged)
    Q_PROPERTY(QVariantMap automaticPreflight READ automaticPreflight NOTIFY workflowChanged)
    Q_PROPERTY(QVariantList ttsVoiceOptions READ ttsVoiceOptions NOTIFY cloneVoiceSelectionChanged)
    Q_PROPERTY(QString selectedTtsVoiceId READ selectedTtsVoiceId NOTIFY cloneVoiceSelectionChanged)
    Q_PROPERTY(bool ttsVoiceSelectionValid READ ttsVoiceSelectionValid NOTIFY cloneVoiceSelectionChanged)
    Q_PROPERTY(QString ttsVoiceSelectionError READ ttsVoiceSelectionError NOTIFY cloneVoiceSelectionChanged)
    Q_PROPERTY(QVariantList colabSetupStages READ colabSetupStages NOTIFY colabSetupChanged)
    Q_PROPERTY(bool colabSetupChecking READ colabSetupChecking NOTIFY colabSetupChanged)
    Q_PROPERTY(QString colabSetupSummary READ colabSetupSummary NOTIFY colabSetupChanged)

public:
    explicit DubbingController(SttSessionController *sttSession, TtsEngine *tts,
                               TranslationEngine *translation,
                               ModelManager *models = nullptr, RuntimeManager *runtimes = nullptr,
                               QObject *parent = nullptr);
    DubbingController(SttSessionController *sttSession, TtsEngine *tts,
                      ModelManager *models = nullptr, RuntimeManager *runtimes = nullptr,
                      QObject *parent = nullptr);
    ~DubbingController() override;
    void setRemoteServices(Settings *settings, ColabSession *translationSession,
                            ColabSession *ttsSession, ColabSession *voiceCloneSession,
                            ColabSession *separationSession,
                            ColabSession *alignmentSession);
    void setVoiceClonePresetService(VoiceClonePresetService *service);
    void setSubtitleOcrController(SubtitleOcrController *controller);

    bool hasProject() const { return !m_project.projectPath.isEmpty(); }
    QString projectPath() const { return m_project.projectPath; }
    QString sourceMediaPath() const { return m_project.sourceMediaPath; }
    QString normalizedAudioPath() const { return m_project.masterAudioPath; }
    QString vocalsPath() const { return m_project.analysisAudioPath; }
    QString backgroundPath() const { return m_project.backgroundAudioPath; }
    QUrl sourceMediaUrl() const {
        if (m_project.sourceMediaPath.isEmpty()) return QUrl();
        return QUrl::fromLocalFile(m_project.sourceMediaPath);
    }
    QUrl playbackMediaUrl() const;
    QString sourceLanguage() const { return m_project.sourceLanguage; }
    QString targetLanguage() const { return m_project.targetLanguage; }
    QVariantMap durationControl() const { return m_project.durationControl; }
    QVariantList speakers() const { return m_project.speakers; }
    QVariantList segments() const { return m_project.segments; }
    bool processing() const;
    QString stage() const;
    int progress() const;
    bool progressAvailable() const;
    // Activity is a presentation surface: expose the current aggregate user
    // stage rather than leaking graph node ids such as "model-setup" or
    // "source-separate" into the popup.
    QVariantMap activityStageInfo() const;
    QString lastError() const;
    QString previewPath() const;
    QString dubbedVocalPath() const;
    QString exportPath() const;
    QString capCutDraftPath() const { return m_capCutDraftPath; }
    QString capCutDraftWarning() const { return m_capCutDraftWarning; }
    QVariantList mediaQueueItems() const { return m_mediaQueueItems; }
    bool mediaQueueDownloading() const;
    bool mediaQueueProcessing() const { return m_mediaQueueProcessing; }
    QString mediaQueueStatus() const { return m_mediaQueueStatus; }
    int mediaQueueProgress() const;
    bool mediaDownloadCookieFileConfigured() const;
    QVariantList workflowNodes() const;
    QVariantList workflowStages() const;
    QVariantMap workflowNodeConfigurations() const { return m_workflowNodeConfigurations; }
    QVariantMap transcriptConfiguration() const { return m_project.transcriptConfiguration; }
    int unresolvedTranscriptConflictCount() const;
    bool subtitleOcrProcessing() const;
    bool sttCanRunAlongsideSubtitleOcr() const;
    bool subtitleOcrCanRunAlongsideStt() const;
    QVariantMap dubbingOcrRoi() const;
    bool dubbingOcrRoiVisible() const;
    QVariantMap subtitleConfiguration() const;
    QVariantMap timingConfiguration() const;
    QVariantList timingConflicts() const;
    QVariantMap timingResolutionPreview() const { return m_timingResolutionPreview; }
    bool timingUndoAvailable() const { return !m_timingUndoSegments.isEmpty(); }
    bool workflowReady() const;
    QString workflowStatusText() const;
    QString workflowId() const;
    int workflowVersion() const;
    bool workflowGraphValid() const;
    QString workflowRunId() const;
    QString workflowNodeRunId() const;
    bool workflowWaitingForInput() const;
    QVariantMap workflowReviewRequest() const;
    bool workflowRecoveryAvailable() const { return !m_workflowRecovery.isEmpty(); }
    QVariantMap workflowRecovery() const { return m_workflowRecovery; }
    QString workflowMode() const { return m_workflowMode; }
    bool dubbingEntryGateActive() const { return m_dubbingEntryGateActive; }
    QString savedDubbingEntryMode() const { return m_project.workflowEntryMode; }
    QString currentStepId() const;
    QVariantMap currentStepOutput() const;
    QString lastCompletedStepId() const { return m_lastCompletedStepId; }
    QVariantList history() const { return m_history; }
    bool translationFixing() const;
    int translationFixProgress() const;
    QString translationFixStatus() const;
    QVariantMap translationFixConfiguration() const;
    int translationFixCandidateCount() const;
    QString dubbingQuality() const { return m_project.dubbingQuality; }
    QString adaptiveProvider() const;
    bool adaptiveReady() const;
    QString adaptiveStatusText() const;
    bool customReady() const;
    QString customStatusText() const;
    bool settingsLocked() const;
    bool automaticSetupActive() const { return m_automaticSetupActive; }
    QString automaticStatusText() const { return m_automaticStatusText; }
    QVariantList automaticEvents() const { return m_automaticEvents; }
    QVariantMap automaticPreflight() const;
    QVariantList cloneVoicePresets() const { return m_cloneVoicePresets; }
    QString selectedCloneVoicePresetId() const { return m_project.ttsVoiceId; }
    QString cloneVoicePresetFamily() const;
    bool cloneVoiceSelectionRequired() const { return m_voiceClonePresetsService != nullptr; }
    bool cloneVoiceSelectionValid() const;
    QString cloneVoiceSelectionError() const;
    QVariantList ttsVoiceOptions() const;
    QString selectedTtsVoiceId() const { return m_project.ttsVoiceId; }
    bool ttsVoiceSelectionValid() const { return cloneVoiceSelectionValid(); }
    QString ttsVoiceSelectionError() const { return cloneVoiceSelectionError(); }
    QVariantList colabSetupStages() const;
    bool colabSetupChecking() const { return !m_colabSetupPendingChecks.isEmpty(); }
    QString colabSetupSummary() const { return m_colabSetupSummary; }

    void setSourceLanguage(const QString &value);
    void setTargetLanguage(const QString &value);
    void setDurationControl(const QVariantMap &value);
    void setDubbingQuality(const QString &value);

    Q_INVOKABLE bool newProject(const QString &path = QString());
    Q_INVOKABLE bool openProject(const QString &path);
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool saveProjectAs(const QString &path);
    Q_INVOKABLE bool deleteHistoryItem(const QString &id);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void closeProject();
    Q_INVOKABLE bool importMedia(const QString &pathOrUrl);
    // Each non-empty line is an independent public URL. Share text is reduced
    // to its URL locally, then the managed CPU-only downloader stages it. The
    // URL is removed from memory as soon as the local media file is ready and
    // is never persisted in a project, settings, history, or output metadata.
    Q_INVOKABLE int enqueueMediaLinks(const QString &urls);
    // Cookies are opt-in, supplied only as a Netscape export, copied into a
    // private temporary file for one public-page resolve, then discarded.
    // LA Studio never reads a browser cookie store.
    Q_INVOKABLE bool setMediaDownloadCookieFile(const QString &path);
    Q_INVOKABLE void clearMediaDownloadCookieFile();
    Q_INVOKABLE int enqueueMediaFiles(const QVariantList &paths);
    Q_INVOKABLE bool setMediaQueueItemSelected(const QString &itemId, bool selected);
    Q_INVOKABLE bool retryMediaQueueItem(const QString &itemId);
    Q_INVOKABLE bool removeMediaQueueItem(const QString &itemId);
    Q_INVOKABLE void clearCompletedMediaQueue();
    // Runs the selected downloaded files one at a time.  Task dependencies are
    // explicit: translation and voice generation require STT; voice generation
    // requires translation.  Audio is emitted as WAV to avoid an implicit codec
    // conversion or a lossy fallback.
    Q_INVOKABLE bool startMediaQueue(const QVariantMap &tasks);
    Q_INVOKABLE void cancelMediaQueue();
    Q_INVOKABLE void transcribeSource();
    // Starts only the configured Subtitle OCR worker.  The completed OCR
    // result is durable but is not promoted over an existing STT transcript;
    // reconciliation remains the explicit local-only action.
    Q_INVOKABLE bool runSubtitleOcrIndependently();
    // Reconciliation intentionally does not invoke STT or OCR. It combines
    // the two durable independent transcript results and leaves conflicts for
    // review before translation.
    Q_INVOKABLE bool reconcileTranscriptSources();
    Q_INVOKABLE void translateSource();
    Q_INVOKABLE void generateAudio();
    Q_INVOKABLE void cancelProcessing();
    Q_INVOKABLE bool renderPreview(const QString &path = QString());
    Q_INVOKABLE bool exportMedia(const QString &path);
    Q_INVOKABLE bool exportAudioStem(const QString &stem, const QString &path);
    Q_INVOKABLE bool exportSubtitles(const QString &path, bool useTargetText = true);
    Q_INVOKABLE bool importSubtitles(const QString &path,
                                     const QString &untimedStrategy = QStringLiteral("existing-segment"));
    // Manual Colab artifact handoff is scoped to the selected production task.
    // The controller owns the allow-list and copies accepted files into the
    // project cache; QML must not be able to bypass validation with arbitrary
    // paths or extensions.
    Q_INVOKABLE QVariantMap workflowArtifactSpec(const QString &nodeId) const;
    // A presentation task can aggregate durable production nodes (for
    // example, Alignment includes fit-timing).  Expose every valid handoff
    // for that active task so the operator never has to find a hidden result
    // pane or guess which file belongs to the next step.
    Q_INVOKABLE QVariantList workflowArtifactSpecsForStage(const QString &nodeId) const;
    // A manual artifact can replace only the matching active worker.  This is
    // deliberately narrow: an upload for a later task must never cancel the
    // current task or mutate its result.
    Q_INVOKABLE bool canOverrideRunningWorkflowArtifact(const QString &nodeId) const;
    // Read-only state for the task-level upload UI.  Percent is supplied only
    // when the running worker has measured it; otherwise the UI shows phase
    // text rather than an invented progress value.
    Q_INVOKABLE QVariantMap workflowArtifactHandoffStatus(const QString &nodeId) const;
    Q_INVOKABLE bool importWorkflowArtifactFiles(const QString &nodeId,
                                                 const QVariantList &paths);
    Q_INVOKABLE bool setSubtitleStyle(const QVariantMap &style);
    Q_INVOKABLE bool setSubtitleTextSource(const QString &source);
    Q_INVOKABLE bool setSubtitleBurnIn(bool enabled);
    Q_INVOKABLE QVariantMap previewTimingResolution(const QString &mode,
                                                     int minimumGapMs = 80);
    Q_INVOKABLE bool applyTimingResolution(const QString &mode,
                                           int minimumGapMs = 80);
    Q_INVOKABLE bool undoTimingResolution();
    Q_INVOKABLE bool setIntentionalTimingOverlap(int segmentIndex, bool enabled);
    Q_INVOKABLE bool exportPackage(const QString &directoryPath);
    Q_INVOKABLE bool exportCapCutDraft(const QString &directoryPath);
    // Imports reviewed OCR results only after an existing Dubbing project is
    // open. The source media/project is left unchanged on validation failure.
    Q_INVOKABLE bool replaceTranscriptSegments(const QVariantList &ocrSegments);
    Q_INVOKABLE bool resolveTranscriptConflict(int index, const QString &choice);
    Q_INVOKABLE bool resolveAllTranscriptConflicts(const QString &choice);
    Q_INVOKABLE bool setTranscriptFusionPolicy(const QString &policy);
    Q_INVOKABLE QVariantMap transcriptConflictAiAvailability() const;
    Q_INVOKABLE bool requestTranscriptConflictAiSuggestion(int index = -1);
    Q_INVOKABLE bool acceptTranscriptConflictAiSuggestion(int index);
    Q_INVOKABLE bool rejectTranscriptConflictAiSuggestion(int index);
    Q_INVOKABLE void addSegment(qint64 startMs, qint64 endMs, const QString &sourceText = QString());
    Q_INVOKABLE void updateSegment(int index, const QVariantMap &patch);
    Q_INVOKABLE void removeSegment(int index);
    Q_INVOKABLE void addSpeaker(const QString &name = QString());
    Q_INVOKABLE void setSpeakerVoice(int speakerIndex, const QVariantMap &voice);
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void resetStandardWorkflowNodeModels();
    Q_INVOKABLE void beginDubbingEntry();
    Q_INVOKABLE bool chooseDubbingEntryMode(const QString &mode);
    Q_INVOKABLE void reopenDubbingEntryGate();
    Q_INVOKABLE QString defaultWorkflowModelFamily(const QString &nodeId) const;
    Q_INVOKABLE void prepareWorkflow();
    Q_INVOKABLE bool runWorkflow(const QString &outputPath = QString());
    // Automatic runs are explicitly approved from the preflight wizard.  The
    // approval is bound to a non-secret configuration fingerprint so changing
    // a route/model/worker requires review and Check again before execution.
    Q_INVOKABLE bool approveAutomaticPreflight();
    Q_INVOKABLE bool startAutomaticWorkflow(const QString &outputPath);
    Q_INVOKABLE void pauseAutomaticWorkflow();
    Q_INVOKABLE void startStepByStep();
    Q_INVOKABLE bool runCurrentStep(const QString &outputPath = QString());
    Q_INVOKABLE bool rerunStep(const QString &stepId, const QString &outputPath = QString());
    Q_INVOKABLE QVariantMap stepOutput(const QString &stepId) const;
    Q_INVOKABLE bool approveWorkflowReview(const QVariantMap &artifact = QVariantMap());
    Q_INVOKABLE bool rejectWorkflowReview(const QString &reason = QString());
    Q_INVOKABLE bool resumeInterruptedWorkflow();
    Q_INVOKABLE bool discardInterruptedWorkflow();
    Q_INVOKABLE bool setWorkflowNodeModel(const QString &nodeId,
                                          const QString &familyId,
                                          const QString &runtimeId,
                                          const QString &runtimeVersion,
                                          const QVariantMap &selectedFiles = QVariantMap());
    Q_INVOKABLE bool loadWorkflowNodeModel(const QString &nodeId);
    Q_INVOKABLE bool unloadWorkflowNodeModel(const QString &nodeId);
    Q_INVOKABLE bool reloadWorkflowNodeModel(const QString &nodeId);
    Q_INVOKABLE bool setWorkflowNodeParameters(const QString &nodeId, const QVariantMap &parameters);
    Q_INVOKABLE bool setDubbingOcrRoi(const QVariantMap &roi);
    Q_INVOKABLE bool presetDubbingOcrLowerRegion();
    Q_INVOKABLE bool resetDubbingOcrRoi();
    Q_INVOKABLE bool previewDubbingOcrCrop(qint64 positionMs = 0);
    Q_INVOKABLE QVariantList colabModelOptionsForNode(const QString &nodeId) const;
    Q_INVOKABLE QString defaultColabModelForNode(const QString &nodeId) const;
    Q_INVOKABLE QString colabNotebookForNode(const QString &nodeId,
                                             const QString &modelId) const;
    Q_INVOKABLE bool selectWorkflowColabModel(const QString &nodeId,
                                              const QString &modelId);
    Q_INVOKABLE bool fixTranslations(const QVariantMap &configuration = QVariantMap());
    Q_INVOKABLE bool fixTranslationSegment(
        int index, const QVariantMap &configuration = QVariantMap());
    Q_INVOKABLE bool translationSegmentNeedsFix(int index) const;
    Q_INVOKABLE void testTranslationFixConnection(
        const QVariantMap &configuration = QVariantMap());
    Q_INVOKABLE QVariantList translationFixCliModelOptions(
        const QString &cliAgent) const;
    Q_INVOKABLE void cancelTranslationFix();
    Q_INVOKABLE void setAdaptiveConfiguration(const QVariantMap &configuration);
    Q_INVOKABLE bool selectCloneVoicePreset(const QString &presetId);
    Q_INVOKABLE void refreshCloneVoicePresets();
    Q_INVOKABLE bool selectTtsVoice(const QString &voiceId);
    Q_INVOKABLE void refreshTtsVoices() { refreshCloneVoicePresets(); }
    // Direct-Colab credentials remain only in the corresponding ColabSession.
    // The controller stores a model-only, in-memory verification snapshot.
    Q_INVOKABLE bool connectWorkflowColabStage(const QString &stageId,
                                               const QString &modelId,
                                               const QString &workerUrl,
                                               const QString &bearerToken);
    // Opt-in convenience route for a Unified Dubbing Colab worker. The
    // supplied base URL/token are expanded into one exact, capability/model
    // endpoint per stage; Local and API Gateway selections are never touched.
    Q_INVOKABLE bool connectUnifiedWorkflowColab(const QString &workerUrl,
                                                 const QString &bearerToken);
    Q_INVOKABLE bool checkWorkflowColabStage(const QString &stageId);
    Q_INVOKABLE void disconnectWorkflowColabStage(const QString &stageId);
    Q_INVOKABLE bool validateAllWorkflowColabStages();

signals:
    void subtitleOcrProcessingChanged();
    void projectChanged();
    void segmentsChanged();
    void processingChanged();
    void errorChanged();
    void previewChanged();
    void exportChanged();
    void workflowChanged();
    void historyChanged();
    void translationFixChanged();
    void translationFixConnectionTested(bool success, const QString &message);
    void cloneVoiceSelectionChanged();
    void colabSetupChanged();
    void timingResolutionChanged();
    void mediaQueueChanged();
    void workflowSetupRequired(const QString &nodeId, const QString &setupKind,
                               const QString &message);

private slots:
    void onIngestFinished(bool success, const QVariantMap &manifest);
    void onBatchMediaDownloadFinished(bool success, const QString &localPath, const QString &error);

private:
    bool ensureProject(const QString &path);
    void setError(const QString &message);
    void setBusyError(const QString &message);
    void persistAfterEdit();
    void invalidateTimingOutputs();
    void setWorkflowMode(const QString &mode);
    void setCurrentStep(const QString &stepId);
    void advanceManualStep(const QString &completedStepId);
    bool configureWorkflowNodeModel(const QString &nodeId,
                                    const QString &familyId,
                                    const QString &runtimeId,
                                    const QString &runtimeVersion,
                                    const QVariantMap &selectedFiles,
                                    bool loadSession);
    CapabilityFamilyModel *automaticModel(const QString &capabilityId);
    bool ensureAutomaticModel(const QString &nodeId, const QString &capabilityId,
                              bool loadSession);
    bool ensureAutomaticAdaptiveModel();
    // Subtitle OCR owns a separate worker and may run beside an active STT
    // transcription only.  It must never bypass a batch, automatic workflow,
    // translation fix, or another non-STT production stage.
    bool canRunIndependentSubtitleOcrAlongsideCurrentWork() const;
    bool canRunIndependentAudioSttAlongsideCurrentWork() const;
    QVariantMap firstCustomSetupIssue() const;
    void resetStandardTranslationFixConfiguration();
    void advanceAutomaticSetup();
    void scheduleAutomaticSetupAdvance();
    void prepareAutomaticVoiceRuntime();
    void finishAutomaticSetupFailure(const QString &message);
    void appendAutomaticEvent(const QString &message, const QString &state,
                              const QString &nodeId = QString());
    bool hasUnresolvedTranscriptConflicts() const;
    void setAutomaticStatus(const QString &message);
    void discoverInterruptedWorkflow();
    static QString visibleStepForNode(const QString &nodeId);
    void loadHistory();
    void recordHistoryEntry();
    QString historyPath() const;
    void configureRemoteRewriteFromGateway();
    QVariantMap selectedCloneVoicePreset() const;
    bool applySelectedCloneVoiceToSynthesis(QVariantMap *settings);
    ColabSession *colabSessionForStage(const QString &stageId) const;
    static QString colabCapabilityForStage(const QString &stageId);
    QString selectedColabModelForStage(const QString &stageId) const;
    QString automaticPreflightFingerprint() const;
    QSet<QString> activeDownloadKeys() const;
    void captureNewAutomaticDownloads(const QSet<QString> &before);
    QVariantList automaticSetupDownloads() const;
    bool stageUsesDirectColab(const QString &stageId) const;
    // Direct-Colab connection is an independent capability setting.  This
    // predicate is used only when starting the currently selected transcript
    // action, so preparing STT never disables Subtitle OCR (or vice versa).
    bool stageRequiredForCurrentTranscriptAction(const QString &stageId) const;
    bool snapshotSelectedColabStagesForWorkflow();
    void observeColabSession(const QString &stageId, ColabSession *session);
    void refreshColabSetupSnapshot(const QString &stageId, bool verified);
    QVariantMap effectiveTranscriptConfiguration(bool captureOcrSettings);
    void applyStoredSubtitleOcrConfiguration();
    int mediaQueueIndex(const QString &itemId) const;
    void replaceMediaQueueItem(int index, const QVariantMap &item);
    void startNextQueuedMediaDownload();
    void startNextMediaQueueItem();
    void startNextMediaQueueStageItem();
    void startMediaQueueStage(const QString &stage);
    void completeCurrentMediaQueueStage(const QString &stage);
    void completeCurrentMediaQueueItem(bool success, const QString &message = QString());
    void finishMediaQueueRun(const QString &message = QString());
    void updateMediaQueueProgressFromRunner();
    QVariantMap normalizedMediaQueueTasks(const QVariantMap &tasks, QString *error) const;
    QStringList mediaQueueStagePlan() const;
    bool mediaQueueOperationRequiresSavedProject() const;
    bool loadMediaQueueProject(const QVariantMap &item, DubbingProject *project,
                               QString *error) const;
    DubbingProject newMediaQueueProject(const QVariantMap &item) const;
    QString mediaQueueOutputDirectory(const QString &itemId) const;
    void recordMediaQueueOutput(const QString &key, const QString &path);
    bool writeMediaQueueSubtitles(const QString &key, bool useTargetText);

    DubbingProject m_project;
    Settings *m_settings = nullptr;
    DubbingJobRunner *m_runner = nullptr;
    SubtitleOcrController *m_subtitleOcr = nullptr;
    bool m_independentSubtitleOcrActive = false;
    bool m_independentSubtitleOcrLoadingSource = false;
    QString m_independentSubtitleOcrSourcePath;
    QList<QMetaObject::Connection> m_independentSubtitleOcrConnections;
    NodeRegistry *m_workflowRegistry = nullptr;
    WorkflowGraphRunner *m_workflowRunner = nullptr;
    std::unique_ptr<WorkflowReviewStore> m_workflowReviewStore;
    std::unique_ptr<WorkflowRunJournal> m_workflowJournal;
    QVariantMap m_workflowReviewRequest;
    QVariantMap m_workflowRecovery;
    QString m_activeReviewId;
    QString m_workflowMode = QStringLiteral("idle");
    bool m_dubbingEntryGateActive = false;
    QString m_currentStepId = QStringLiteral("import");
    QVariantMap m_stepOutputs;
    QString m_lastCompletedStepId;
    QString m_pendingExportPath;
    QString m_capCutDraftPath;
    QString m_capCutDraftWarning;
    QVariantMap m_timingResolutionPreview;
    QVariantList m_timingUndoSegments;
    // Public links use the managed local CPU downloader; manual files bypass
    // every downloader entirely. Neither path uses a Colab or GPU worker.
    RemoteMediaImportService *m_remoteMediaImport = nullptr;
    QVariantList m_mediaQueueItems;
    QString m_activeMediaQueueDownloadId;
    QString m_activeMediaQueueItemId;
    QVariantMap m_mediaQueueTasks;
    QString m_mediaQueueExecutionMode = QStringLiteral("per-media");
    QStringList m_mediaQueueStagePlan;
    int m_mediaQueueStagePlanIndex = 0;
    QHash<QString, DubbingProject> m_mediaQueueProjects;
    QString m_mediaQueueStage;
    QString m_mediaQueueStatus;
    bool m_mediaQueueProcessing = false;
    bool m_mediaQueueCancelling = false;
    DubbingProject m_mediaQueueOriginalProject;
    QVariantMap m_mediaQueueOriginalNodeConfigurations;
    QString m_mediaQueueOriginalPreviewPath;
    QString m_mediaQueueOriginalExportPath;
    bool m_mediaQueueOriginalProjectCaptured = false;
    QVariantList m_history;
    QVariantMap m_workflowNodeConfigurations;
    SttSessionController *m_sttSession = nullptr;
    TtsEngine *m_tts = nullptr;
    TranslationEngine *m_translation = nullptr;
    DubbingTranslationFixService *m_translationFix = nullptr;
    ModelManager *m_models = nullptr;
    RuntimeManager *m_runtimes = nullptr;
    std::unique_ptr<CapabilityFamilyModel> m_automaticSttModel;
    std::unique_ptr<CapabilityFamilyModel> m_automaticVoiceIsolationModel;
    std::unique_ptr<CapabilityFamilyModel> m_automaticTranslationModel;
    std::unique_ptr<CapabilityFamilyModel> m_automaticTtsModel;
    std::unique_ptr<CapabilityFamilyModel> m_automaticLlmModel;
    bool m_automaticSetupActive = false;
    bool m_automaticAdvanceScheduled = false;
    QString m_automaticSetupNodeId;
    QString m_automaticOutputPath;
    QString m_automaticStatusText;
    QVariantList m_automaticEvents;
    QSet<QString> m_automaticDownloadsQueued;
    // Only bytes from downloads queued by this Automatic run are eligible for
    // a percentage.  Other gallery downloads must never make Dubbing show a
    // misleading 5% or 8% progress bar.
    QSet<QString> m_automaticDownloadKeys;
    QSet<QString> m_automaticConfiguredNodes;
    QString m_automaticPreflightFingerprint;
    VoiceClonePresetService *m_voiceClonePresetsService = nullptr;
    QMetaObject::Connection m_cloneVoicePresetsConnection;
    QVariantList m_cloneVoicePresets;
    QHash<QString, QMetaObject::Connection> m_colabSetupConnections;
    QHash<QString, QString> m_colabSetupSnapshots;
    QSet<QString> m_colabSetupPendingChecks;
    QString m_colabSetupSummary;
};

} // namespace LAStudio
