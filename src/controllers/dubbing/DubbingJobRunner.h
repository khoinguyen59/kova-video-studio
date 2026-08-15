#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QFutureWatcher>
#include <QAtomicInteger>
#include <memory>
#include <QPointer>
#include <QThread>
#include <atomic>
#include "separation/SeparationTypes.h"
#include "controllers/dubbing/DubbingRunCoordinator.h"

namespace LAStudio {

class SttSessionController;
class TtsEngine;
class ModelManager;
class RuntimeManager;
class TranslationEngine;
class MediaToolService;
class MediaIngestService;
class SourceSeparationService;
class DubbingTranscriptionJob;
class DubbingSynthesisJob;
class DubbingExportJob;
class DubbingTranslationJob;
class DubbingTranslationFixService;
class Settings;
class ColabSession;
class ColabSeparationRunner;
class SubtitleOcrController;

class DubbingJobRunner : public QObject
{
    Q_OBJECT
public:
    explicit DubbingJobRunner(SttSessionController *sttSession, TtsEngine *tts,
                              TranslationEngine *translation,
                              ModelManager *models = nullptr, RuntimeManager *runtimes = nullptr,
                              QObject *parent = nullptr);
    DubbingJobRunner(SttSessionController *sttSession, TtsEngine *tts,
                     ModelManager *models = nullptr, RuntimeManager *runtimes = nullptr,
                     QObject *parent = nullptr);
    ~DubbingJobRunner() override;

    bool processing() const { return m_run.processing(); }
    QString stage() const { return m_run.stageName(); }
    int progress() const { return m_run.progress(); }
    QString activityStatus() const { return m_colabSeparationActivityStatus; }
    QVariantMap activityTransferProgress() const;
    QString lastError() const { return m_run.lastError(); }
    QString previewPath() const { return m_previewPath; }
    QString dubbedVocalPath() const { return m_dubbedVocalPath; }
    QString exportPath() const { return m_exportPath; }
    QString runId() const { return m_run.runId(); }
    QString nodeRunId() const { return m_run.nodeRunId(); }

    void startIngest(const QString &path);
    void startSourceSeparation(const QString &audioPath,
                               const QVariantMap &modelConfiguration = QVariantMap());
    void startTranscription(const QString &sourceLanguage, const QString &sourceMediaPath,
                            const QString &fallbackAudioPath = QString(),
                            const QVariantMap &modelConfiguration = QVariantMap());
    void startTranslation(const QString &sourceLanguage, const QString &targetLanguage, const QVariantList &segments,
                          const QVariantMap &modelConfiguration = QVariantMap());
    void setRemoteServices(Settings *settings, ColabSession *translationSession,
                           ColabSession *ttsSession, ColabSession *voiceCloneSession,
                           ColabSession *separationSession,
                           ColabSession *alignmentSession,
                           ColabSession *chatSession = nullptr);
    void setTranslationFixConfiguration(const QVariantMap &configuration);
    // Subtitle OCR stays a single shared controller/runtime. The runner only
    // orchestrates it for the Dubbing Transcribe node; it never creates a
    // second OCR pipeline or downloads a runtime.
    void setSubtitleOcrController(SubtitleOcrController *controller);
    void startAudioGeneration(const QVariantList &segments, const QString &projectPath,
                              const QVariantMap &synthesisSettings = QVariantMap());
    void fitTiming(const QVariantList &segments, const QString &projectPath);
    void cancel();
    bool renderPreview(const QVariantList &segments, const QString &projectPath, const QString &path = QString());
    bool startExport(const QString &sourceMediaPath, const QString &outputPath);
    bool startExport(const QString &sourceMediaPath, const QString &audioPath,
                     const QString &outputPath, const QVariantList &segments = {},
                     const QVariantMap &subtitleConfiguration = QVariantMap());

    // Helpers to let controller update/clear state in runner
    void setPreviewPath(const QString &path);
    void setDubbedVocalPath(const QString &path) { m_dubbedVocalPath = path; emit stateChanged(); }
    void setExportPath(const QString &path);
    void clearError();
    void setProcessingState(bool value, const QString &stage, int progress);
    void setError(const QString &message);
    // Reports an interaction rejection without failing or cancelling an
    // already-running worker.
    void setBusyError(const QString &message);
    void setBackgroundAudioPath(const QString &path) { m_backgroundAudioPath = path; }

signals:
    void stateChanged();
    void segmentsUpdated(const QVariantList &segments);
    void segmentUpdated(int index, const QVariantMap &patch);
    void errorOccurred(const QString &message);
    void ingestFinished(bool success, const QVariantMap &manifest);
    void sourceSeparationFinished(const QVariantMap &outputs);
    void stageCompleted(const QString &nodeId, const QVariantMap &outputs);

private slots:
    void onIngestFinished(bool success, const QVariantMap &manifest, const QString &error);
    void onSourceSeparationFinished(const SeparationResult &result);
    void onTimingFinished();

private:
    void setProcessing(bool value, const QString &stage, int progress);
    void finishTranslation(const QVariantList &segments);
    void finishTranscript(const QVariantList &segments);
    void startOcrTranscript(const QVariantMap &parameters);
    void failTranscriptSource(const QString &source, const QString &message);
    void onSubtitleOcrSourceChanged();
    void onSubtitleOcrSegmentsChanged();
    void onSubtitleOcrErrorChanged();
    void onSubtitleOcrProgressChanged();

    QPointer<SttSessionController> m_sttSession;
    QPointer<TtsEngine> m_tts;
    QPointer<TranslationEngine> m_translation;
    QPointer<ModelManager> m_models;
    QPointer<RuntimeManager> m_runtimes;
    QPointer<SubtitleOcrController> m_subtitleOcr;

    DubbingRunCoordinator m_run;
    QString m_previewPath;
    QString m_dubbedVocalPath;
    QString m_exportPath;

    QVariantList m_activeSegments;
    QString m_projectPath;
    QString m_backgroundAudioPath;

    MediaIngestService *m_mediaIngest = nullptr;
    SourceSeparationService *m_sourceSeparation = nullptr;
    DubbingTranscriptionJob *m_transcriptionJob = nullptr;
    DubbingSynthesisJob *m_synthesisJob = nullptr;
    DubbingExportJob *m_exportJob = nullptr;
    DubbingTranslationJob *m_translationJob = nullptr;
    DubbingTranslationFixService *m_autoTranslationFix = nullptr;
    QPointer<ColabSession> m_colabSeparationSession;
    ColabSeparationRunner *m_colabSeparationRunner = nullptr;
    QThread m_colabSeparationThread;
    std::shared_ptr<std::atomic_bool> m_colabSeparationCancellation;
    QMetaObject::Connection m_colabSeparationSessionConnection;
    QVariantMap m_translationConfiguration;
    QVariantMap m_translationFixConfiguration;
    QString m_translationSourceLanguage;
    QString m_translationTargetLanguage;
    QString m_transcriptSourceMode = QStringLiteral("stt");
    QVariantMap m_transcriptParameters;
    QString m_ocrTranscriptSourcePath;
    bool m_ocrTranscriptActive = false;
    bool m_ocrTranscriptLoadingSource = false;
    bool m_sttTranscriptActive = false;
    QVariantList m_sttTranscriptSegments;
    QVariantList m_ocrTranscriptSegments;
    QList<QMetaObject::Connection> m_subtitleOcrConnections;
    QString m_pendingSourceAudioPath;
    QString m_colabSeparationActivityStatus;
    QString m_colabSeparationArtifact;
    qint64 m_colabSeparationReceivedBytes = 0;
    qint64 m_colabSeparationTotalBytes = -1;
    bool m_colabSeparationTransferActive = false;
    QFutureWatcher<QVariantList> *m_timingWatcher = nullptr;
    std::shared_ptr<QAtomicInteger<bool>> m_timingCancel;
};

} // namespace LAStudio
