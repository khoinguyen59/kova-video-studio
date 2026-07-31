#include "controllers/subtitles/SubtitleOcrController.h"

#include "controllers/dubbing/DubbingController.h"
#include "controllers/tts/SubtitleVoiceController.h"
#include "core/MediaRuntimeLocator.h"
#include "core/PathUtils.h"
#include "subtitles/SubtitleOcrRuntimeLocator.h"
#include "subtitles/SubtitleOcrRuntimeService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUuid>

namespace LAStudio {
namespace {

constexpr int kSubtitleOcrProjectVersion = 1;

QString ffmpegTime(qint64 timestampMs)
{
    return QString::number(qMax<qint64>(0, timestampMs) / 1000.0, 'f', 3);
}

QString processFailure(const QString &stage, const QByteArray &standardError)
{
    const QString detail = QString::fromUtf8(standardError).trimmed();
    return detail.isEmpty() ? QStringLiteral("Subtitle OCR %1 failed.").arg(stage)
                            : QStringLiteral("Subtitle OCR %1 failed: %2").arg(stage, detail);
}

} // namespace

SubtitleOcrController::SubtitleOcrController(SubtitleVoiceController *subtitleVoice,
                                             DubbingController *dubbing, QObject *parent)
    : QObject(parent), m_subtitleVoice(subtitleVoice), m_dubbing(dubbing)
{
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &SubtitleOcrController::onProcessFinished);
    connect(&m_process, &QProcess::errorOccurred, this, &SubtitleOcrController::onProcessError);
    if (m_dubbing) {
        connect(m_dubbing, &DubbingController::linkImportChanged,
                this, &SubtitleOcrController::onSharedMediaImportChanged);
        connect(m_dubbing, &DubbingController::errorChanged,
                this, &SubtitleOcrController::onSharedMediaImportError);
    }
}

SubtitleOcrController::~SubtitleOcrController()
{
    if (m_process.state() != QProcess::NotRunning) m_process.kill();
    cleanWorkspace();
}

QUrl SubtitleOcrController::sourceUrl() const
{
    return m_sourcePath.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_sourcePath);
}

QUrl SubtitleOcrController::cropPreviewUrl() const
{
    return m_cropPreviewPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_cropPreviewPath);
}

bool SubtitleOcrController::runtimeAvailable() const
{
    if (m_runtimeService) return m_runtimeService->runtimeAvailable();
    return !SubtitleOcrRuntimeLocator::resolveTesseract().isEmpty();
}

QString SubtitleOcrController::runtimePath() const
{
    if (m_runtimeService) return m_runtimeService->runtimePath();
    return SubtitleOcrRuntimeLocator::resolveTesseract();
}

void SubtitleOcrController::refreshRuntime()
{
    if (m_runtimeService) m_runtimeService->refresh();
    emit runtimeChanged();
}

void SubtitleOcrController::setRuntimeService(SubtitleOcrRuntimeService *runtimeService)
{
    if (m_runtimeService == runtimeService) return;
    if (m_runtimeService) disconnect(m_runtimeService, nullptr, this, nullptr);
    m_runtimeService = runtimeService;
    if (m_runtimeService) {
        connect(m_runtimeService, &SubtitleOcrRuntimeService::runtimeChanged,
                this, &SubtitleOcrController::runtimeChanged);
        connect(m_runtimeService, &SubtitleOcrRuntimeService::stateChanged,
                this, &SubtitleOcrController::runtimeChanged);
    }
    emit runtimeChanged();
}

void SubtitleOcrController::setError(const QString &message)
{
    if (m_error == message) return;
    m_error = message;
    emit errorChanged();
}

void SubtitleOcrController::setPhase(const QString &phase)
{
    if (m_phase == phase) return;
    m_phase = phase;
    emit phaseChanged();
}

void SubtitleOcrController::setProcessing(bool processing)
{
    if (m_processing == processing) return;
    m_processing = processing;
    emit processingChanged();
}

void SubtitleOcrController::setProgress(int value, bool available)
{
    value = qBound(0, value, 100);
    if (m_progress == value && m_progressAvailable == available) return;
    m_progress = value;
    m_progressAvailable = available;
    emit progressChanged();
}

bool SubtitleOcrController::ensureWorkspace()
{
    if (!m_workspacePath.isEmpty()) return true;
    const QString root = QDir(PathUtils::cacheDir()).filePath(QStringLiteral("subtitle-ocr"));
    if (!QDir().mkpath(root)) {
        fail(QStringLiteral("Cannot create app-owned Subtitle OCR staging storage."));
        return false;
    }
    m_workspacePath = QDir(root).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(m_workspacePath)) {
        m_workspacePath.clear();
        fail(QStringLiteral("Cannot create Subtitle OCR staging workspace."));
        return false;
    }
    return true;
}

void SubtitleOcrController::cleanWorkspace()
{
    if (!m_workspacePath.isEmpty()) QDir(m_workspacePath).removeRecursively();
    m_workspacePath.clear();
    if (!m_cropPreviewPath.isEmpty()) {
        m_cropPreviewPath.clear();
        emit cropPreviewChanged();
    }
}

void SubtitleOcrController::startProcess(Operation operation, const QString &program,
                                         const QStringList &arguments)
{
    if (program.isEmpty()) {
        fail(QStringLiteral("Required Subtitle OCR runtime is unavailable."));
        return;
    }
    m_operation = operation;
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    m_process.start();
}

bool SubtitleOcrController::loadSource(const QString &path)
{
    if (m_processing) return false;
    const QString localPath = PathUtils::urlToLocalPath(path);
    const QFileInfo info(localPath);
    if (!info.isFile() || info.size() <= 0) {
        setError(QStringLiteral("Choose a readable video file before running Subtitle OCR."));
        return false;
    }
    const MediaRuntimePaths media = MediaRuntimeLocator::resolve();
    if (!media.hasFfprobe()) {
        setError(QStringLiteral("FFprobe is required to inspect the video before Subtitle OCR."));
        return false;
    }
    m_pendingSourcePath = info.absoluteFilePath();
    m_cancelRequested = false;
    setError({});
    setProcessing(true);
    setPhase(QStringLiteral("probing"));
    setProgress(0, false);
    startProcess(Operation::Probe, media.ffprobe,
                 {QStringLiteral("-v"), QStringLiteral("error"),
                  QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                  QStringLiteral("-show_entries"), QStringLiteral("stream=width,height:format=duration"),
                  QStringLiteral("-of"), QStringLiteral("json"), m_pendingSourcePath});
    return true;
}

bool SubtitleOcrController::useDownloadedMedia(const QString &path)
{
    if (m_processing || m_sourceImporting) return false;
    m_waitingForSharedMedia = false;
    setSourceImportState(false);
    const bool accepted = loadSource(path);
    if (!accepted) {
        m_sourceImportError = m_error;
        emit sourceImportChanged();
    }
    return accepted;
}

bool SubtitleOcrController::importSourceLink(const QString &url)
{
    const QString candidate = url.trimmed();
    if (!m_dubbing) {
        setSourceImportState(false, {}, QStringLiteral("Shared public-media import is unavailable in this build."));
        return false;
    }
    if (m_processing || m_sourceImporting) {
        setSourceImportState(false, {}, QStringLiteral("Finish or cancel the current Subtitle OCR source operation first."));
        return false;
    }
    if (candidate.isEmpty()) {
        setSourceImportState(false, {}, QStringLiteral("Enter a media link before importing it."));
        return false;
    }

    // This URL is deliberately memory-only: no project, settings or log write
    // occurs here. It exists solely to support the visible Retry action.
    m_lastSourceImportUrl = candidate;
    m_waitingForSharedMedia = true;
    m_sourceImportCancelRequested = false;
    setSourceImportState(true, QStringLiteral("Starting shared media import"));
    if (m_dubbing->downloadMediaFromLink(candidate)) return true;

    m_waitingForSharedMedia = false;
    setSourceImportState(false, {}, m_dubbing->lastError().isEmpty()
        ? QStringLiteral("Could not start the shared media import.") : m_dubbing->lastError());
    return false;
}

void SubtitleOcrController::cancelSourceImport()
{
    if (!m_sourceImporting && !m_waitingForSharedMedia) return;
    // Keep the UI in a canceling state until the shared service has actually
    // cleaned its partial transfer. Retrying earlier would race that one
    // shared downloader and be rejected as a second concurrent import.
    m_sourceImportCancelRequested = true;
    setSourceImportState(true, QStringLiteral("Canceling shared media import"));
    if (m_dubbing) m_dubbing->cancelMediaLinkImport();
}

bool SubtitleOcrController::retrySourceImport()
{
    if (m_lastSourceImportUrl.isEmpty()) {
        setSourceImportState(false, {}, QStringLiteral("There is no media link available to retry."));
        return false;
    }
    return importSourceLink(m_lastSourceImportUrl);
}

void SubtitleOcrController::setSourceImportState(bool importing, const QString &status,
                                                 const QString &error)
{
    const qint64 received = importing ? m_sourceImportReceivedBytes : 0;
    const qint64 total = importing ? m_sourceImportTotalBytes : -1;
    if (m_sourceImporting == importing && m_sourceImportStatus == status
        && m_sourceImportError == error && m_sourceImportReceivedBytes == received
        && m_sourceImportTotalBytes == total) {
        return;
    }
    m_sourceImporting = importing;
    m_sourceImportStatus = status;
    m_sourceImportError = error;
    m_sourceImportReceivedBytes = received;
    m_sourceImportTotalBytes = total;
    emit sourceImportChanged();
}

void SubtitleOcrController::onSharedMediaImportChanged()
{
    if (!m_waitingForSharedMedia || !m_dubbing) return;
    if (m_dubbing->linkImporting()) {
        m_sourceImportReceivedBytes = m_dubbing->linkImportReceivedBytes();
        m_sourceImportTotalBytes = m_dubbing->linkImportTotalBytes();
        setSourceImportState(true, m_dubbing->linkImportStatus());
        return;
    }
    if (m_dubbing->downloadedMediaReady()) {
        const QString stagedPath = m_dubbing->downloadedMediaPath();
        m_waitingForSharedMedia = false;
        const bool canceled = m_sourceImportCancelRequested;
        m_sourceImportCancelRequested = false;
        if (canceled) {
            setSourceImportState(false, {}, QStringLiteral("Media link import canceled."));
            return;
        }
        setSourceImportState(false, QStringLiteral("Inspecting staged media"));
        if (!loadSource(stagedPath)) {
            m_sourceImportError = m_error;
            emit sourceImportChanged();
        }
    }
}

void SubtitleOcrController::onSharedMediaImportError()
{
    if (!m_waitingForSharedMedia || !m_dubbing) return;
    const QString error = m_dubbing->lastError();
    if (error.isEmpty()) return;
    m_waitingForSharedMedia = false;
    m_sourceImportCancelRequested = false;
    setSourceImportState(false, {}, error);
}

void SubtitleOcrController::completeProbe(const QByteArray &output)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
    const QJsonObject root = document.object();
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    const QJsonObject stream = streams.isEmpty() ? QJsonObject() : streams.at(0).toObject();
    const int width = stream.value(QStringLiteral("width")).toInt();
    const int height = stream.value(QStringLiteral("height")).toInt();
    bool durationOk = false;
    const double durationSeconds = root.value(QStringLiteral("format")).toObject()
        .value(QStringLiteral("duration")).toVariant().toDouble(&durationOk);
    if (parseError.error != QJsonParseError::NoError || width <= 0 || height <= 0
        || !durationOk || durationSeconds <= 0.0) {
        fail(QStringLiteral("The selected file has no readable video stream for Subtitle OCR."));
        return;
    }
    cleanWorkspace();
    m_sourcePath = m_pendingSourcePath;
    m_pendingSourcePath.clear();
    m_sourceWidth = width;
    m_sourceHeight = height;
    m_durationMs = qRound64(durationSeconds * 1000.0);
    m_segments.clear();
    setError({});
    setProgress(0, false);
    setProcessing(false);
    setPhase(QStringLiteral("ready"));
    if (m_sourceImportStatus == QStringLiteral("Inspecting staged media")) {
        setSourceImportState(false);
    }
    emit sourceChanged();
    emit segmentsChanged();
}

bool SubtitleOcrController::setRoi(double x, double y, double width, double height)
{
    const SubtitleOcrRoi candidate{x, y, width, height};
    if (!candidate.isValid()) {
        setError(QStringLiteral("Subtitle OCR region must remain inside the source frame and cannot be empty."));
        return false;
    }
    m_roi = candidate;
    setError({});
    emit roiChanged();
    return true;
}

void SubtitleOcrController::resetRoi()
{
    // Reset means remove the crop and return to the complete source frame.
    // The subtitle-oriented crop is a separate, explicit preset so the two
    // visible actions never silently do the same thing.
    m_roi = SubtitleOcrRoi{0.0, 0.0, 1.0, 1.0};
    emit roiChanged();
}

void SubtitleOcrController::setLowerRegionPreset()
{
    m_roi = SubtitleOcrRoi{};
    emit roiChanged();
}

bool SubtitleOcrController::setOcrLanguage(const QString &language)
{
    const QString normalized = language.trimmed();
    if (normalized.isEmpty() || normalized.contains(QRegularExpression(QStringLiteral("[^A-Za-z0-9_+]")))) {
        setError(QStringLiteral("Choose a valid installed Tesseract language code."));
        return false;
    }
    if (m_ocrLanguage == normalized) return true;
    m_ocrLanguage = normalized;
    emit settingsChanged();
    return true;
}

bool SubtitleOcrController::setSampleIntervalMs(qint64 intervalMs)
{
    if (intervalMs < 100 || intervalMs > 30000) {
        setError(QStringLiteral("Subtitle OCR sample interval must be between 100 ms and 30 seconds."));
        return false;
    }
    m_sampleIntervalMs = intervalMs;
    emit settingsChanged();
    return true;
}

bool SubtitleOcrController::setMinimumConfidence(double confidence)
{
    if (confidence < 0.0 || confidence > 1.0) {
        setError(QStringLiteral("Subtitle OCR confidence must be between 0 and 1."));
        return false;
    }
    m_minimumConfidence = confidence;
    emit settingsChanged();
    return true;
}

bool SubtitleOcrController::requestCropPreview(qint64 positionMs)
{
    if (m_processing || m_sourcePath.isEmpty() || m_sourceWidth <= 0 || m_sourceHeight <= 0) return false;
    const MediaRuntimePaths media = MediaRuntimeLocator::resolve();
    if (!media.hasFfmpeg()) {
        setError(QStringLiteral("FFmpeg is required to preview the Subtitle OCR region."));
        return false;
    }
    const QStringList crop = SubtitleOcrPipeline::ffmpegCropArguments(m_roi, m_sourceWidth, m_sourceHeight);
    if (crop.isEmpty()) {
        setError(QStringLiteral("Choose a valid Subtitle OCR region before previewing it."));
        return false;
    }
    if (!ensureWorkspace()) return false;
    m_cropPreviewPath = QDir(m_workspacePath).filePath(QStringLiteral("crop-preview.png"));
    m_cancelRequested = false;
    setError({});
    setProcessing(true);
    setPhase(QStringLiteral("previewing-crop"));
    setProgress(0, false);
    QStringList arguments{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                          QStringLiteral("-ss"), ffmpegTime(qMin(positionMs, m_durationMs)),
                          QStringLiteral("-i"), m_sourcePath};
    arguments += crop;
    arguments += {QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-y"), m_cropPreviewPath};
    startProcess(Operation::CropPreview, media.ffmpeg, arguments);
    return true;
}

bool SubtitleOcrController::run()
{
    if (m_processing) return false;
    if (m_sourcePath.isEmpty() || m_sourceWidth <= 0 || m_sourceHeight <= 0 || m_durationMs <= 0) {
        setError(QStringLiteral("Choose and inspect a video before running Subtitle OCR."));
        return false;
    }
    const QString tesseract = runtimePath();
    if (tesseract.isEmpty() || !runtimeAvailable()) {
        setError(QStringLiteral("Subtitle OCR runtime is unavailable. Use Install runtime in Subtitle OCR, then install the selected language pack before running."));
        emit runtimeChanged();
        return false;
    }
    const MediaRuntimePaths media = MediaRuntimeLocator::resolve();
    if (!media.hasFfmpeg()) {
        setError(QStringLiteral("FFmpeg is required to extract Subtitle OCR frames."));
        return false;
    }
    if (SubtitleOcrPipeline::ffmpegCropArguments(m_roi, m_sourceWidth, m_sourceHeight).isEmpty()) {
        setError(QStringLiteral("Choose a valid Subtitle OCR region before running."));
        return false;
    }
    cleanWorkspace();
    if (!ensureWorkspace()) return false;
    m_samples = SubtitleOcrPipeline::sampleTimes(m_durationMs, m_sampleIntervalMs);
    if (m_samples.isEmpty()) {
        fail(QStringLiteral("No Subtitle OCR sample timestamps could be created."));
        return false;
    }
    m_observations.clear();
    m_sampleIndex = 0;
    m_previousFrameHash.clear();
    m_previousText.clear();
    m_previousConfidence = 0.0;
    m_cancelRequested = false;
    setError({});
    setProcessing(true);
    // An executable by itself is not a usable OCR runtime: language data is
    // resolved independently by Tesseract. Verify the selected language before
    // extracting any video frame, so a missing `vie`/`eng` install does not
    // look like a video failure after a long operation.
    setPhase(QStringLiteral("checking-language"));
    setProgress(0, false);
    startProcess(Operation::VerifyLanguage, tesseract, {QStringLiteral("--list-langs")});
    return true;
}

bool SubtitleOcrController::retry()
{
    return run();
}

void SubtitleOcrController::beginOcrSamples()
{
    setPhase(QStringLiteral("extracting-frame"));
    setProgress(0, true);
    beginNextSample();
}

void SubtitleOcrController::beginNextSample()
{
    if (!m_processing || m_cancelRequested) {
        completeCancellation();
        return;
    }
    if (m_sampleIndex >= m_samples.size()) {
        completeRun();
        return;
    }
    const MediaRuntimePaths media = MediaRuntimeLocator::resolve();
    const QStringList crop = SubtitleOcrPipeline::ffmpegCropArguments(m_roi, m_sourceWidth, m_sourceHeight);
    if (!media.hasFfmpeg() || crop.isEmpty()) {
        fail(QStringLiteral("Subtitle OCR frame extraction is no longer configured."));
        return;
    }
    const qint64 timestampMs = m_samples.at(m_sampleIndex);
    m_currentFramePath = QDir(m_workspacePath).filePath(
        QStringLiteral("frame-%1.png").arg(m_sampleIndex, 6, 10, QLatin1Char('0')));
    setPhase(QStringLiteral("extracting frame %1/%2").arg(m_sampleIndex + 1).arg(m_samples.size()));
    QStringList arguments{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                          QStringLiteral("-ss"), ffmpegTime(timestampMs), QStringLiteral("-i"), m_sourcePath};
    arguments += crop;
    arguments += {QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-y"), m_currentFramePath};
    startProcess(Operation::ExtractFrame, media.ffmpeg, arguments);
}

void SubtitleOcrController::beginRecognition()
{
    const QString tesseract = runtimePath();
    if (tesseract.isEmpty()) {
        fail(QStringLiteral("Subtitle OCR runtime became unavailable during processing."));
        emit runtimeChanged();
        return;
    }
    setPhase(QStringLiteral("recognizing frame %1/%2").arg(m_sampleIndex + 1).arg(m_samples.size()));
    startProcess(Operation::RecognizeFrame, tesseract,
                 {m_currentFramePath, QStringLiteral("stdout"), QStringLiteral("-l"), m_ocrLanguage,
                  QStringLiteral("--psm"), QStringLiteral("6"), QStringLiteral("tsv")});
}

void SubtitleOcrController::onProcessError(QProcess::ProcessError error)
{
    if (!m_processing || error == QProcess::Crashed) return;
    if (error == QProcess::FailedToStart) {
        fail(QStringLiteral("Subtitle OCR process could not be started. Check the managed runtime installation."));
    }
}

void SubtitleOcrController::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_processing) return;
    const QByteArray output = m_process.readAllStandardOutput();
    const QByteArray standardError = m_process.readAllStandardError();
    const Operation operation = m_operation;
    m_operation = Operation::None;
    if (m_cancelRequested) {
        completeCancellation();
        return;
    }
    if (status != QProcess::NormalExit || exitCode != 0) {
        const QString stage = operation == Operation::Probe ? QStringLiteral("video probe")
            : operation == Operation::CropPreview ? QStringLiteral("crop preview")
            : operation == Operation::VerifyLanguage ? QStringLiteral("Tesseract language check")
            : operation == Operation::ExtractFrame ? QStringLiteral("frame extraction")
            : QStringLiteral("Tesseract recognition");
        fail(processFailure(stage, standardError));
        return;
    }
    if (operation == Operation::Probe) {
        completeProbe(output);
        return;
    }
    if (operation == Operation::CropPreview) {
        if (!QFileInfo(m_cropPreviewPath).isFile() || QFileInfo(m_cropPreviewPath).size() <= 0) {
            fail(QStringLiteral("Subtitle OCR crop preview did not produce an image."));
            return;
        }
        setProcessing(false);
        setPhase(QStringLiteral("ready"));
        setProgress(0, false);
        emit cropPreviewChanged();
        return;
    }
    if (operation == Operation::VerifyLanguage) {
        const QStringList installedLanguages = QString::fromUtf8(output).split(
            QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
        QStringList missingLanguages;
        for (const QString &language : m_ocrLanguage.split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
            if (!installedLanguages.contains(language)) missingLanguages.append(language);
        }
        if (!missingLanguages.isEmpty()) {
            fail(QStringLiteral("The selected Tesseract language data is not installed: %1. Install the matching traineddata file, refresh the OCR runtime, and try again.")
                 .arg(missingLanguages.join(QStringLiteral(", "))));
            return;
        }
        beginOcrSamples();
        return;
    }
    if (operation == Operation::ExtractFrame) {
        QFile frame(m_currentFramePath);
        if (!frame.open(QIODevice::ReadOnly)) {
            fail(QStringLiteral("Subtitle OCR frame extraction did not produce a readable crop."));
            return;
        }
        const QByteArray hash = QCryptographicHash::hash(frame.readAll(), QCryptographicHash::Sha256);
        frame.close();
        if (!m_previousFrameHash.isEmpty() && hash == m_previousFrameHash) {
            m_observations.append({m_samples.at(m_sampleIndex), m_previousText, m_previousConfidence});
            ++m_sampleIndex;
            setProgress(qRound(100.0 * m_sampleIndex / m_samples.size()), true);
            beginNextSample();
            return;
        }
        m_previousFrameHash = hash;
        beginRecognition();
        return;
    }
    if (operation == Operation::RecognizeFrame) {
        const SubtitleOcrObservation observation = SubtitleOcrPipeline::parseTesseractTsv(
            output, m_samples.at(m_sampleIndex));
        m_previousText = observation.text;
        m_previousConfidence = observation.confidence;
        m_observations.append(observation);
        ++m_sampleIndex;
        setProgress(qRound(100.0 * m_sampleIndex / m_samples.size()), true);
        beginNextSample();
    }
}

void SubtitleOcrController::completeRun()
{
    m_segments = segmentsToVariant(SubtitleOcrPipeline::mergeObservations(
        m_observations, m_sampleIntervalMs, m_minimumConfidence));
    cleanWorkspace();
    setProcessing(false);
    setProgress(100, true);
    setPhase(QStringLiteral("completed"));
    setError({});
    emit segmentsChanged();
}

void SubtitleOcrController::completeCancellation()
{
    m_cancelRequested = false;
    m_operation = Operation::None;
    m_pendingSourcePath.clear();
    cleanWorkspace();
    setProcessing(false);
    setProgress(0, false);
    setPhase(QStringLiteral("canceled"));
    setError({});
}

void SubtitleOcrController::fail(const QString &message)
{
    if (m_process.state() != QProcess::NotRunning) m_process.kill();
    m_operation = Operation::None;
    m_cancelRequested = false;
    m_pendingSourcePath.clear();
    cleanWorkspace();
    setProcessing(false);
    setProgress(0, false);
    setPhase(QStringLiteral("error"));
    setError(message);
}

void SubtitleOcrController::cancel()
{
    if (!m_processing) return;
    m_cancelRequested = true;
    if (m_process.state() != QProcess::NotRunning) m_process.kill();
    else completeCancellation();
}

QVariantList SubtitleOcrController::segmentsToVariant(const QVector<SubtitleOcrSegment> &segments)
{
    QVariantList result;
    result.reserve(segments.size());
    for (const SubtitleOcrSegment &segment : segments) {
        result.append(QVariantMap{{QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                  {QStringLiteral("startMs"), segment.startMs},
                                  {QStringLiteral("endMs"), segment.endMs},
                                  {QStringLiteral("text"), segment.text},
                                  {QStringLiteral("confidence"), segment.confidence}});
    }
    return result;
}

QVector<SubtitleOcrSegment> SubtitleOcrController::segmentsFromVariant(const QVariantList &segments,
                                                                        QString *error)
{
    QVector<SubtitleOcrSegment> result;
    result.reserve(segments.size());
    for (const QVariant &entry : segments) {
        const QVariantMap item = entry.toMap();
        const qint64 startMs = item.value(QStringLiteral("startMs")).toLongLong();
        const qint64 endMs = item.value(QStringLiteral("endMs")).toLongLong();
        const QString text = item.value(QStringLiteral("text")).toString().trimmed();
        const double confidence = item.value(QStringLiteral("confidence")).toDouble();
        if (startMs < 0 || endMs <= startMs || text.isEmpty() || confidence < 0.0 || confidence > 1.0) {
            if (error) *error = QStringLiteral("Subtitle OCR project contains an invalid transcript segment.");
            return {};
        }
        result.append({startMs, endMs, text, confidence});
    }
    return result;
}

void SubtitleOcrController::updateSegment(int index, const QVariantMap &patch)
{
    if (m_processing || index < 0 || index >= m_segments.size()) return;
    QVariantMap segment = m_segments.at(index).toMap();
    for (auto it = patch.cbegin(); it != patch.cend(); ++it) segment.insert(it.key(), it.value());
    const qint64 startMs = segment.value(QStringLiteral("startMs")).toLongLong();
    const qint64 endMs = segment.value(QStringLiteral("endMs")).toLongLong();
    if (startMs < 0 || endMs <= startMs || segment.value(QStringLiteral("text")).toString().trimmed().isEmpty()) {
        setError(QStringLiteral("Subtitle OCR segment needs text and an end time after its start time."));
        return;
    }
    segment.insert(QStringLiteral("startMs"), startMs);
    segment.insert(QStringLiteral("endMs"), endMs);
    segment.insert(QStringLiteral("text"), segment.value(QStringLiteral("text")).toString().trimmed());
    m_segments[index] = segment;
    setError({});
    emit segmentsChanged();
}

void SubtitleOcrController::removeSegment(int index)
{
    if (m_processing || index < 0 || index >= m_segments.size()) return;
    m_segments.removeAt(index);
    emit segmentsChanged();
}

bool SubtitleOcrController::writeTextFile(const QString &path, const QString &content)
{
    const QString localPath = PathUtils::urlToLocalPath(path);
    if (localPath.isEmpty()) return false;
    QSaveFile output(localPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    const QByteArray utf8 = content.toUtf8();
    return output.write(utf8) == utf8.size() && output.commit();
}

bool SubtitleOcrController::exportSrt(const QString &path) const
{
    QString error;
    const QVector<SubtitleOcrSegment> parsed = segmentsFromVariant(m_segments, &error);
    return !parsed.isEmpty() && writeTextFile(path, SubtitleOcrPipeline::toSrt(parsed));
}

bool SubtitleOcrController::exportText(const QString &path) const
{
    QString error;
    const QVector<SubtitleOcrSegment> parsed = segmentsFromVariant(m_segments, &error);
    QStringList lines;
    for (const SubtitleOcrSegment &segment : parsed) lines.append(segment.text);
    return !lines.isEmpty() && writeTextFile(path, lines.join(QStringLiteral("\n\n")) + QLatin1Char('\n'));
}

bool SubtitleOcrController::saveProject(const QString &path)
{
    if (m_processing) return false;
    const QString destination = PathUtils::urlToLocalPath(path.trimmed().isEmpty() ? m_projectPath : path);
    if (destination.isEmpty()) {
        setError(QStringLiteral("Choose a Subtitle OCR project path before saving."));
        return false;
    }
    QString segmentsError;
    if (!m_segments.isEmpty() && segmentsFromVariant(m_segments, &segmentsError).isEmpty()) {
        setError(segmentsError);
        return false;
    }
    QJsonObject project{{QStringLiteral("schemaVersion"), kSubtitleOcrProjectVersion},
                        {QStringLiteral("sourcePath"), m_sourcePath},
                        {QStringLiteral("sourceWidth"), m_sourceWidth},
                        {QStringLiteral("sourceHeight"), m_sourceHeight},
                        {QStringLiteral("durationMs"), static_cast<double>(m_durationMs)},
                        {QStringLiteral("roi"), QJsonObject{{QStringLiteral("x"), m_roi.x},
                                                            {QStringLiteral("y"), m_roi.y},
                                                            {QStringLiteral("width"), m_roi.width},
                                                            {QStringLiteral("height"), m_roi.height}}},
                        {QStringLiteral("ocrLanguage"), m_ocrLanguage},
                        {QStringLiteral("sampleIntervalMs"), static_cast<double>(m_sampleIntervalMs)},
                        {QStringLiteral("minimumConfidence"), m_minimumConfidence},
                        {QStringLiteral("segments"), QJsonArray::fromVariantList(m_segments)}};
    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(QStringLiteral("Cannot write Subtitle OCR project."));
        return false;
    }
    const QByteArray document = QJsonDocument(project).toJson(QJsonDocument::Indented);
    if (file.write(document) != document.size() || !file.commit()) {
        setError(QStringLiteral("Cannot atomically save Subtitle OCR project."));
        return false;
    }
    m_projectPath = QFileInfo(destination).absoluteFilePath();
    setError({});
    emit projectChanged();
    return true;
}

bool SubtitleOcrController::applyProject(const QVariantMap &project, const QString &absoluteProjectPath)
{
    const int version = project.contains(QStringLiteral("schemaVersion"))
        ? project.value(QStringLiteral("schemaVersion")).toInt() : -1;
    const QString source = PathUtils::urlToLocalPath(project.value(QStringLiteral("sourcePath")).toString());
    const QVariantMap roiMap = project.value(QStringLiteral("roi")).toMap();
    const SubtitleOcrRoi roi{roiMap.value(QStringLiteral("x")).toDouble(),
                             roiMap.value(QStringLiteral("y")).toDouble(),
                             roiMap.value(QStringLiteral("width")).toDouble(),
                             roiMap.value(QStringLiteral("height")).toDouble()};
    const int width = project.value(QStringLiteral("sourceWidth")).toInt();
    const int height = project.value(QStringLiteral("sourceHeight")).toInt();
    const qint64 duration = project.value(QStringLiteral("durationMs")).toLongLong();
    const QString language = project.value(QStringLiteral("ocrLanguage")).toString().trimmed();
    const qint64 interval = project.value(QStringLiteral("sampleIntervalMs")).toLongLong();
    const double confidence = project.value(QStringLiteral("minimumConfidence")).toDouble();
    QString segmentsError;
    const QVector<SubtitleOcrSegment> parsed = segmentsFromVariant(
        project.value(QStringLiteral("segments")).toList(), &segmentsError);
    if (version != kSubtitleOcrProjectVersion || source.isEmpty() || !QFileInfo(source).isFile()
        || width <= 0 || height <= 0 || duration <= 0 || !roi.isValid() || language.isEmpty()
        || interval < 100 || interval > 30000 || confidence < 0.0 || confidence > 1.0
        || (!project.value(QStringLiteral("segments")).toList().isEmpty() && parsed.isEmpty())) {
        setError(segmentsError.isEmpty() ? QStringLiteral("Invalid or incompatible Subtitle OCR project.") : segmentsError);
        return false;
    }
    cleanWorkspace();
    m_sourcePath = QFileInfo(source).absoluteFilePath();
    m_sourceWidth = width;
    m_sourceHeight = height;
    m_durationMs = duration;
    m_roi = roi;
    m_ocrLanguage = language;
    m_sampleIntervalMs = interval;
    m_minimumConfidence = confidence;
    m_segments = project.value(QStringLiteral("segments")).toList();
    m_projectPath = absoluteProjectPath;
    setError({});
    setPhase(QStringLiteral("ready"));
    setProgress(0, false);
    emit sourceChanged();
    emit roiChanged();
    emit settingsChanged();
    emit segmentsChanged();
    emit projectChanged();
    return true;
}

bool SubtitleOcrController::openProject(const QString &path)
{
    if (m_processing) return false;
    const QString localPath = PathUtils::urlToLocalPath(path);
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("Cannot open Subtitle OCR project."));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(QStringLiteral("Invalid Subtitle OCR project JSON."));
        return false;
    }
    return applyProject(document.object().toVariantMap(), QFileInfo(localPath).absoluteFilePath());
}

bool SubtitleOcrController::sendToSubtitleVoice()
{
    if (!m_subtitleVoice || m_segments.isEmpty()) {
        setError(QStringLiteral("Run Subtitle OCR and review at least one segment before opening Subtitle Voice."));
        return false;
    }
    QString parseError;
    const QVector<SubtitleOcrSegment> parsed = segmentsFromVariant(m_segments, &parseError);
    if (parsed.isEmpty()) {
        setError(parseError);
        return false;
    }
    const QString pattern = QDir(PathUtils::cacheDir()).filePath(QStringLiteral("subtitle-ocr-transfer-XXXXXX.srt"));
    QTemporaryFile transfer(pattern);
    if (!transfer.open()) {
        setError(QStringLiteral("Cannot create a temporary Subtitle OCR transfer file."));
        return false;
    }
    const QByteArray srt = SubtitleOcrPipeline::toSrt(parsed).toUtf8();
    if (transfer.write(srt) != srt.size() || !transfer.flush() || !m_subtitleVoice->importSrt(transfer.fileName())) {
        setError(QStringLiteral("Could not transfer reviewed OCR subtitles to Subtitle Voice."));
        return false;
    }
    setError({});
    return true;
}

bool SubtitleOcrController::sendToDubbing()
{
    if (!m_dubbing || m_segments.isEmpty()) {
        setError(QStringLiteral("Run Subtitle OCR and review at least one segment before using it in Dubbing."));
        return false;
    }
    if (!m_dubbing->replaceTranscriptSegments(m_segments)) {
        setError(m_dubbing->lastError());
        return false;
    }
    setError({});
    return true;
}

} // namespace LAStudio
