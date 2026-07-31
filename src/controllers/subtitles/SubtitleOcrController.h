#pragma once

#include <QObject>
#include <QProcess>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqml.h>

#include "subtitles/SubtitleOcrPipeline.h"

namespace LAStudio {

class DubbingController;
class SubtitleVoiceController;
class SubtitleOcrRuntimeService;

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
    Q_PROPERTY(qint64 sampleIntervalMs READ sampleIntervalMs NOTIFY settingsChanged)
    Q_PROPERTY(double minimumConfidence READ minimumConfidence NOTIFY settingsChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool progressAvailable READ progressAvailable NOTIFY progressChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
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
    qint64 sampleIntervalMs() const { return m_sampleIntervalMs; }
    double minimumConfidence() const { return m_minimumConfidence; }
    bool processing() const { return m_processing; }
    int progress() const { return m_progress; }
    bool progressAvailable() const { return m_progressAvailable; }
    QString phase() const { return m_phase; }
    QString error() const { return m_error; }
    QVariantList segments() const { return m_segments; }
    QString projectPath() const { return m_projectPath; }
    QUrl cropPreviewUrl() const;
    bool runtimeAvailable() const;
    QString runtimePath() const;
    bool sourceImporting() const { return m_sourceImporting; }
    QString sourceImportStatus() const { return m_sourceImportStatus; }
    qint64 sourceImportReceivedBytes() const { return m_sourceImportReceivedBytes; }
    qint64 sourceImportTotalBytes() const { return m_sourceImportTotalBytes; }
    QString sourceImportError() const { return m_sourceImportError; }

    Q_INVOKABLE bool loadSource(const QString &path);
    // Public media URLs always delegate to DubbingController's existing
    // RemoteMediaImportService.  The staged file is then probed as an OCR
    // source; the URL itself is kept only in-memory for an explicit retry.
    Q_INVOKABLE bool importSourceLink(const QString &url);
    Q_INVOKABLE void cancelSourceImport();
    Q_INVOKABLE bool retrySourceImport();
    Q_INVOKABLE bool useDownloadedMedia(const QString &path);
    Q_INVOKABLE bool requestCropPreview(qint64 positionMs = 0);
    Q_INVOKABLE bool run();
    Q_INVOKABLE bool retry();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool setRoi(double x, double y, double width, double height);
    Q_INVOKABLE void setLowerRegionPreset();
    Q_INVOKABLE void resetRoi();
    Q_INVOKABLE bool setOcrLanguage(const QString &language);
    Q_INVOKABLE bool setSampleIntervalMs(qint64 intervalMs);
    Q_INVOKABLE bool setMinimumConfidence(double confidence);
    Q_INVOKABLE void updateSegment(int index, const QVariantMap &patch);
    Q_INVOKABLE void removeSegment(int index);
    Q_INVOKABLE bool saveProject(const QString &path = QString());
    Q_INVOKABLE bool openProject(const QString &path);
    Q_INVOKABLE bool exportSrt(const QString &path) const;
    Q_INVOKABLE bool exportText(const QString &path) const;
    Q_INVOKABLE bool sendToSubtitleVoice();
    Q_INVOKABLE bool sendToDubbing();
    Q_INVOKABLE void refreshRuntime();
    void setRuntimeService(SubtitleOcrRuntimeService *runtimeService);

signals:
    void sourceChanged();
    void roiChanged();
    void settingsChanged();
    void processingChanged();
    void progressChanged();
    void phaseChanged();
    void errorChanged();
    void segmentsChanged();
    void projectChanged();
    void cropPreviewChanged();
    void runtimeChanged();
    void sourceImportChanged();

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void onSharedMediaImportChanged();
    void onSharedMediaImportError();

private:
    enum class Operation {
        None,
        Probe,
        CropPreview,
        VerifyLanguage,
        ExtractFrame,
        RecognizeFrame,
    };

    bool ensureWorkspace();
    void cleanWorkspace();
    void startProcess(Operation operation, const QString &program, const QStringList &arguments);
    void beginOcrSamples();
    void beginNextSample();
    void beginRecognition();
    void completeProbe(const QByteArray &output);
    void completeRun();
    void completeCancellation();
    void fail(const QString &message);
    void setError(const QString &message);
    void setPhase(const QString &phase);
    void setProcessing(bool processing);
    void setProgress(int value, bool available);
    void setSourceImportState(bool importing, const QString &status = QString(),
                              const QString &error = QString());
    bool applyProject(const QVariantMap &project, const QString &absoluteProjectPath);
    static QVariantList segmentsToVariant(const QVector<SubtitleOcrSegment> &segments);
    static QVector<SubtitleOcrSegment> segmentsFromVariant(const QVariantList &segments, QString *error);
    static bool writeTextFile(const QString &path, const QString &content);

    SubtitleVoiceController *m_subtitleVoice = nullptr;
    DubbingController *m_dubbing = nullptr;
    SubtitleOcrRuntimeService *m_runtimeService = nullptr;
    QProcess m_process;
    Operation m_operation = Operation::None;
    bool m_processing = false;
    bool m_cancelRequested = false;
    QString m_sourcePath;
    QString m_pendingSourcePath;
    int m_sourceWidth = 0;
    int m_sourceHeight = 0;
    qint64 m_durationMs = 0;
    SubtitleOcrRoi m_roi;
    QString m_ocrLanguage = QStringLiteral("eng");
    qint64 m_sampleIntervalMs = 800;
    double m_minimumConfidence = 0.50;
    int m_progress = 0;
    bool m_progressAvailable = false;
    QString m_phase = QStringLiteral("idle");
    QString m_error;
    QVariantList m_segments;
    QString m_projectPath;
    QString m_workspacePath;
    QString m_cropPreviewPath;
    bool m_waitingForSharedMedia = false;
    bool m_sourceImportCancelRequested = false;
    bool m_sourceImporting = false;
    QString m_sourceImportStatus;
    QString m_sourceImportError;
    QString m_lastSourceImportUrl;
    qint64 m_sourceImportReceivedBytes = 0;
    qint64 m_sourceImportTotalBytes = -1;
    QVector<qint64> m_samples;
    QVector<SubtitleOcrObservation> m_observations;
    int m_sampleIndex = 0;
    QString m_currentFramePath;
    QByteArray m_previousFrameHash;
    QString m_previousText;
    double m_previousConfidence = 0.0;
};

} // namespace LAStudio
