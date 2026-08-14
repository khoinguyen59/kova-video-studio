#pragma once

#include <QObject>
#include <QProcess>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QQueue>
#include <QSharedPointer>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqml.h>

#include "subtitles/SubtitleOcrPipeline.h"

namespace LAStudio {

class DubbingController;
class SubtitleVoiceController;
class SubtitleOcrRuntimeService;
class ColabSession;
class ColabSubtitleOcrRunner;
struct MediaRuntimePaths;

// Asynchronous, offline hard-subtitle OCR controller. FFmpeg, FFprobe and
// Tesseract are always invoked with QProcess argument lists. A managed runtime
// may be installed only through the separate explicit user action service.
class SubtitleOcrController final : public QObject
{
    Q_OBJECT
    QML_UNCREATABLE("SubtitleOcrController is managed by AppController")

    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY sourceChanged)
    Q_PROPERTY(QUrl sourceUrl READ sourceUrl NOTIFY sourceChanged)
    Q_PROPERTY(int sourceWidth READ sourceWidth NOTIFY sourceChanged)
    Q_PROPERTY(int sourceHeight READ sourceHeight NOTIFY sourceChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY sourceChanged)
    Q_PROPERTY(double roiX READ roiX NOTIFY roiChanged)
    Q_PROPERTY(double roiY READ roiY NOTIFY roiChanged)
    Q_PROPERTY(double roiWidth READ roiWidth NOTIFY roiChanged)
    Q_PROPERTY(double roiHeight READ roiHeight NOTIFY roiChanged)
    Q_PROPERTY(QString ocrLanguage READ ocrLanguage NOTIFY settingsChanged)
    Q_PROPERTY(QString executionRoute READ executionRoute NOTIFY settingsChanged)
    Q_PROPERTY(QString localEngineId READ localEngineId NOTIFY settingsChanged)
    Q_PROPERTY(QString localEngineVersion READ localEngineVersion NOTIFY settingsChanged)
    Q_PROPERTY(bool localRouteReady READ localRouteReady NOTIFY runtimeChanged)
    Q_PROPERTY(QString localRuntimeState READ localRuntimeState NOTIFY runtimeChanged)
    Q_PROPERTY(QString colabModelId READ colabModelId NOTIFY settingsChanged)
    Q_PROPERTY(bool colabRouteReady READ colabRouteReady NOTIFY colabRouteChanged)
    Q_PROPERTY(QString colabRouteStatus READ colabRouteStatus NOTIFY colabRouteChanged)
    Q_PROPERTY(QString colabNotebookFile READ colabNotebookFile CONSTANT)
    Q_PROPERTY(qint64 sampleIntervalMs READ sampleIntervalMs NOTIFY settingsChanged)
    Q_PROPERTY(double minimumConfidence READ minimumConfidence NOTIFY settingsChanged)
    Q_PROPERTY(int maxConcurrentWorkers READ maxConcurrentWorkers NOTIFY settingsChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool progressAvailable READ progressAvailable NOTIFY progressChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(QString resultStatus READ resultStatus NOTIFY resultChanged)
    Q_PROPERTY(QVariantMap runStatistics READ runStatistics NOTIFY runStatisticsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString diagnostics READ diagnostics NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool canRetryFrameExtraction READ canRetryFrameExtraction NOTIFY frameRetryChanged)
    Q_PROPERTY(QVariantList segments READ segments NOTIFY segmentsChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    Q_PROPERTY(QUrl cropPreviewUrl READ cropPreviewUrl NOTIFY cropPreviewChanged)
    Q_PROPERTY(bool runtimeAvailable READ runtimeAvailable NOTIFY runtimeChanged)
    Q_PROPERTY(QString runtimePath READ runtimePath NOTIFY runtimeChanged)
    Q_PROPERTY(bool sourceImporting READ sourceImporting NOTIFY sourceImportChanged)
    Q_PROPERTY(QString sourceImportStatus READ sourceImportStatus NOTIFY sourceImportChanged)
    Q_PROPERTY(qint64 sourceImportReceivedBytes READ sourceImportReceivedBytes NOTIFY sourceImportChanged)
    Q_PROPERTY(qint64 sourceImportTotalBytes READ sourceImportTotalBytes NOTIFY sourceImportChanged)
    Q_PROPERTY(QString sourceImportError READ sourceImportError NOTIFY sourceImportChanged)

public:
    explicit SubtitleOcrController(SubtitleVoiceController *subtitleVoice,
                                   DubbingController *dubbing, QObject *parent = nullptr);
    ~SubtitleOcrController() override;

    QString sourcePath() const { return m_sourcePath; }
    QUrl sourceUrl() const;
    int sourceWidth() const { return m_sourceWidth; }
    int sourceHeight() const { return m_sourceHeight; }
    qint64 durationMs() const { return m_durationMs; }
    double roiX() const { return m_roi.x; }
    double roiY() const { return m_roi.y; }
    double roiWidth() const { return m_roi.width; }
    double roiHeight() const { return m_roi.height; }
    QString ocrLanguage() const { return m_ocrLanguage; }
    QString executionRoute() const { return m_executionRoute; }
    QString localEngineId() const { return m_localEngineId; }
    QString localEngineVersion() const;
    bool localRouteReady() const;
    QString localRuntimeState() const;
    QString colabModelId() const { return m_colabModelId; }
    bool colabRouteReady() const;
    QString colabRouteStatus() const;
    QString colabNotebookFile() const;
    qint64 sampleIntervalMs() const { return m_sampleIntervalMs; }
    double minimumConfidence() const { return m_minimumConfidence; }
    int maxConcurrentWorkers() const { return m_maxConcurrentWorkers; }
    bool processing() const { return m_processing; }
    int progress() const { return m_progress; }
    bool progressAvailable() const { return m_progressAvailable; }
    QString phase() const { return m_phase; }
    QString resultStatus() const { return m_resultStatus; }
    QVariantMap runStatistics() const;
    QString error() const { return m_error; }
    QString diagnostics() const { return m_diagnostics; }
    bool canRetryFrameExtraction() const;
    QVariantList segments() const { return m_segments; }
    QString projectPath() const { return m_projectPath; }
    QUrl cropPreviewUrl() const;
    bool runtimeAvailable() const;
    QString runtimePath() const;
    // Headless acceptance diagnostics only.  A completed OCR run must not
    // retain an FFmpeg or recognition worker child process.
    int activeChildProcessCount() const;
    bool sourceImporting() const { return m_sourceImporting; }
    QString sourceImportStatus() const { return m_sourceImportStatus; }
    qint64 sourceImportReceivedBytes() const { return m_sourceImportReceivedBytes; }
    qint64 sourceImportTotalBytes() const { return m_sourceImportTotalBytes; }
    QString sourceImportError() const { return m_sourceImportError; }

    Q_INVOKABLE bool loadSource(const QString &path);
    // Subtitle OCR deliberately accepts local files only. Public links are
    // acquired by the local media downloader or chosen from a local file,
    // then selected
    // from the media library as a local file.
    Q_INVOKABLE bool importSourceLink(const QString &url);
    Q_INVOKABLE void cancelSourceImport();
    Q_INVOKABLE bool retrySourceImport();
    Q_INVOKABLE bool useDownloadedMedia(const QString &path);
    Q_INVOKABLE bool requestCropPreview(qint64 positionMs = 0);
    Q_INVOKABLE bool run();
    Q_INVOKABLE bool retry();
    Q_INVOKABLE bool retryFrameExtraction();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool setRoi(double x, double y, double width, double height);
    Q_INVOKABLE void setLowerRegionPreset();
    Q_INVOKABLE void resetRoi();
    Q_INVOKABLE bool setOcrLanguage(const QString &language);
    Q_INVOKABLE bool setExecutionRoute(const QString &route);
    Q_INVOKABLE bool setLocalEngine(const QString &engineId);
    Q_INVOKABLE bool setColabModelId(const QString &modelId);
    Q_INVOKABLE bool setSampleIntervalMs(qint64 intervalMs);
    Q_INVOKABLE bool setMinimumConfidence(double confidence);
    Q_INVOKABLE bool setMaxConcurrentWorkers(int workers);
    // Headless benchmark support.  The normal desktop route always leaves this
    // at zero and processes the whole source; the value is part of the cache
    // key so a benchmark artifact can never satisfy a full production run.
    bool setBenchmarkSampleLimit(int limit);
    Q_INVOKABLE void updateSegment(int index, const QVariantMap &patch);
    Q_INVOKABLE void removeSegment(int index);
    Q_INVOKABLE bool saveProject(const QString &path = QString());
    Q_INVOKABLE bool openProject(const QString &path);
    Q_INVOKABLE bool exportSrt(const QString &path);
    Q_INVOKABLE bool exportText(const QString &path);
    Q_INVOKABLE bool sendToSubtitleVoice();
    Q_INVOKABLE bool sendToDubbing();
    Q_INVOKABLE void refreshRuntime();
    void setRuntimeService(SubtitleOcrRuntimeService *runtimeService);
    void setColabSession(ColabSession *session);

signals:
    void sourceChanged();
    void roiChanged();
    void settingsChanged();
    void processingChanged();
    void progressChanged();
    void phaseChanged();
    void resultChanged();
    void runStatisticsChanged();
    void errorChanged();
    void diagnosticsChanged();
    void frameRetryChanged();
    void segmentsChanged();
    void projectChanged();
    void cropPreviewChanged();
    void runtimeChanged();
    void colabRouteChanged();
    void sourceImportChanged();

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void onFrameExtractionTimeout();
    void onColabRecognitionFinished(const QString &text, double confidence);
    void onColabRecognitionFailed(const QString &message);

private:
    enum class Operation {
        None,
        Probe,
        CropPreview,
        VerifyLanguage,
        VerifyPaddleRuntime,
        ExtractFrame,
        ExtractChunk,
        RecognizeFrame,
        RecognizePaddleChunk,
        RecognizeColabFrame,
    };

    bool ensureWorkspace();
    void cleanWorkspace(bool retainDiagnostics = false);
    void startProcess(Operation operation, const QString &program, const QStringList &arguments);
    void beginOcrSamples();
    void beginNextSample();
    void beginCacheLookup();
    void onSourceFingerprintReady();
    void beginNextChunk();
    void queueChunkFrames();
    void pumpRecognitionQueue();
    bool beginPaddleRecognitionChunk();
    void onRecognitionFinished(QProcess *process, int exitCode, QProcess::ExitStatus status);
    void onRecognitionError(QProcess *process, QProcess::ProcessError error);
    void releaseRecognitionWorkers();
    void updateForwardProgress();
    void checkForwardProgress();
    void releaseActiveCacheKey();
    bool loadCachedResult();
    bool storeCachedResult();
    QString cacheFilePath() const;
    QString cacheKeyMaterial() const;
    void beginRecognition();
    void completeProbe(const QByteArray &output);
    void completeRun();
    void completeCancellation();
    void fail(const QString &message, Operation failedOperation = Operation::None,
              const QString &resultStatus = QStringLiteral("failed"));
    void setError(const QString &message);
    void clearDiagnostics();
    void appendDiagnostic(const QString &event, const QString &detail);
    void recordFrameExtractionStart(const MediaRuntimePaths &media, qint64 timestampMs,
                                    const SubtitleOcrRect &crop);
    bool validateCurrentFrame(QByteArray *hash, QString *errorMessage);
    bool validateFrame(const QString &path, QByteArray *hash, QString *errorMessage);
    void setPhase(const QString &phase);
    void setResultStatus(const QString &status);
    void setProcessing(bool processing);
    void setProgress(int value, bool available);
    void resetRunStatistics();
    void appendObservation(const SubtitleOcrObservation &observation);
    void setSourceImportState(bool importing, const QString &status = QString(),
                              const QString &error = QString());
    bool usesPaddleLocalEngine() const;
    bool usesTesseractLocalEngine() const;
    bool applyProject(const QVariantMap &project, const QString &absoluteProjectPath);
    static QVariantList segmentsToVariant(const QVector<SubtitleOcrSegment> &segments);
    static QVector<SubtitleOcrSegment> segmentsFromVariant(const QVariantList &segments, QString *error);
    static bool writeTextFile(const QString &path, const QString &content);

    struct RecognitionItem {
        QByteArray frameHash;
        QString framePath;
        QVector<int> sampleIndexes;
        bool completed = false;
        QString recognizedText;
        double recognizedConfidence = 0.0;
    };
    struct RecognitionWorker {
        QProcess *process = nullptr;
        QSharedPointer<RecognitionItem> item;
    };

    SubtitleVoiceController *m_subtitleVoice = nullptr;
    DubbingController *m_dubbing = nullptr;
    SubtitleOcrRuntimeService *m_runtimeService = nullptr;
    ColabSession *m_colabSession = nullptr;
    ColabSubtitleOcrRunner *m_colabRunner = nullptr;
    QThread m_colabThread;
    QProcess m_process;
    QTimer m_frameExtractionTimeout;
    QTimer m_forwardProgressTimer;
    QElapsedTimer m_runElapsed;
    QFutureWatcher<QString> m_sourceFingerprintWatcher;
    Operation m_operation = Operation::None;
    bool m_processing = false;
    bool m_cancelRequested = false;
    bool m_frameExtractionTimedOut = false;
    QString m_sourcePath;
    QString m_pendingSourcePath;
    int m_sourceWidth = 0;
    int m_sourceHeight = 0;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    int m_rotationDegrees = 0;
    QString m_sampleAspectRatio;
    QString m_displayAspectRatio;
    qint64 m_durationMs = 0;
    SubtitleOcrRoi m_roi;
    QString m_ocrLanguage = QStringLiteral("eng");
    QString m_executionRoute = QStringLiteral("local-cpu");
    QString m_localEngineId = QStringLiteral("paddleocr-ppocrv6-tiny");
    QString m_colabModelId = QStringLiteral("pp-ocrv5-multilingual-3.1");
    qint64 m_sampleIntervalMs = 800;
    double m_minimumConfidence = 0.50;
    int m_maxConcurrentWorkers = 1;
    int m_benchmarkSampleLimit = 0;
    int m_progress = 0;
    bool m_progressAvailable = false;
    QString m_phase = QStringLiteral("idle");
    QString m_resultStatus = QStringLiteral("idle");
    QString m_error;
    QString m_diagnostics;
    QVariantList m_segments;
    QString m_projectPath;
    QString m_workspacePath;
    QString m_cropPreviewPath;
    bool m_sourceImporting = false;
    QString m_sourceImportStatus;
    QString m_sourceImportError;
    qint64 m_sourceImportReceivedBytes = 0;
    qint64 m_sourceImportTotalBytes = -1;
    QVector<qint64> m_samples;
    QVector<SubtitleOcrObservation> m_observations;
    int m_scheduledSampleCount = 0;
    int m_readableCropCount = 0;
    int m_ocrAttemptCount = 0;
    int m_ocrSuccessCount = 0;
    int m_nonEmptyRawResultCount = 0;
    int m_filterCandidateCount = 0;
    int m_publishedSegmentCount = 0;
    int m_exportedSegmentCount = 0;
    int m_extractedFrameCount = 0;
    int m_deduplicatedFrameCount = 0;
    int m_recognizedFrameCount = 0;
    int m_ffmpegProcessCount = 0;
    int m_tesseractProcessCount = 0;
    int m_paddleProcessCount = 0;
    double m_paddleCpuSeconds = 0.0;
    qint64 m_paddlePeakWorkingSetBytes = 0;
    int m_completedSampleCount = 0;
    qint64 m_lastForwardProgressMs = 0;
    bool m_cacheReused = false;
    bool m_cacheKeyActive = false;
    QString m_sourceFingerprint;
    QString m_cacheKey;
    int m_chunkStartIndex = 0;
    int m_chunkEndIndex = 0;

    QVector<RecognitionWorker> m_recognitionWorkers;
    QQueue<QSharedPointer<RecognitionItem>> m_recognitionQueue;
    QHash<QByteArray, QSharedPointer<RecognitionItem>> m_uniqueFrames;
    QVector<QSharedPointer<RecognitionItem>> m_paddleChunkItems;
    QString m_paddleRequestPath;
    QString m_paddleResponsePath;
    int m_sampleIndex = 0;
    Operation m_lastFailedOperation = Operation::None;
    QString m_currentFramePath;
    SubtitleOcrRect m_currentCrop;
    QByteArray m_previousFrameHash;
    QString m_previousText;
    double m_previousConfidence = 0.0;
};

} // namespace LAStudio
