#include "controllers/subtitles/SubtitleOcrController.h"

#include "controllers/dubbing/DubbingController.h"
#include "controllers/tts/SubtitleVoiceController.h"
#include "core/MediaRuntimeLocator.h"
#include "core/PathUtils.h"
#include "remote/ColabSession.h"
#include "subtitles/ColabSubtitleOcrRunner.h"
#include "subtitles/PaddleOcrRuntimeLocator.h"
#include "subtitles/SubtitleOcrRuntimeLocator.h"
#include "subtitles/SubtitleOcrRuntimeService.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace LAStudio {
namespace {

constexpr int kSubtitleOcrProjectVersion = 2;
const QString kColabSubtitleOcrCapability = QStringLiteral("subtitle-ocr");
const QString kColabSubtitleOcrModel = QStringLiteral("pp-ocrv5-multilingual-3.1");
const QString kColabSubtitleOcrNotebook = QStringLiteral("LA_STUDIO_SUBTITLE_OCR_PP_OCRV5_GPU.ipynb");
constexpr qsizetype kMaxDiagnosticCharacters = 16000;
constexpr int kFrameExtractionTimeoutMs = 30000;
constexpr int kForwardProgressTimeoutMs = 60000;
constexpr int kChunkSampleCount = 48;
constexpr int kSubtitleOcrCacheVersion = 2;
QSet<QString> s_activeOcrCacheKeys;

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

QString boundedDiagnosticText(const QString &value)
{
    const QString normalized = value.trimmed();
    return normalized.size() <= 4000
        ? normalized
        : normalized.left(4000) + QStringLiteral(" [truncated]");
}

QString normalizedRoiText(const SubtitleOcrRoi &roi)
{
    return QStringLiteral("x=%1 y=%2 w=%3 h=%4")
        .arg(roi.x, 0, 'f', 6).arg(roi.y, 0, 'f', 6)
        .arg(roi.width, 0, 'f', 6).arg(roi.height, 0, 'f', 6);
}

QString cropText(const SubtitleOcrRect &crop)
{
    return QStringLiteral("x=%1 y=%2 w=%3 h=%4")
        .arg(crop.x).arg(crop.y).arg(crop.width).arg(crop.height);
}

int normalizedRotation(int rotation)
{
    rotation %= 360;
    if (rotation < 0) rotation += 360;
    return rotation;
}

bool parseAspectRatio(const QString &text, double *value)
{
    const QStringList parts = text.split(QLatin1Char(':'));
    if (parts.size() != 2) return false;
    bool numeratorOk = false;
    bool denominatorOk = false;
    const double numerator = parts.at(0).toDouble(&numeratorOk);
    const double denominator = parts.at(1).toDouble(&denominatorOk);
    if (!numeratorOk || !denominatorOk || numerator <= 0.0 || denominator <= 0.0) return false;
    if (value) *value = numerator / denominator;
    return true;
}

int frameExtractionTimeoutMs()
{
#ifdef LASTUDIO_UNIT_TESTS
    bool parsed = false;
    const int requested = qEnvironmentVariableIntValue(
        "LASTUDIO_TEST_SUBTITLE_OCR_FRAME_TIMEOUT_MS", &parsed);
    if (parsed && requested > 0) return requested;
#endif
    return kFrameExtractionTimeoutMs;
}

QString sha256File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFile::NoError) return {};
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}

int defaultOcrWorkerCount()
{
    return qBound(1, QThread::idealThreadCount(), 4);
}

QString normalizedLocalEngineId(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("tesseract-baseline")) return normalized;
    if (normalized == QString::fromLatin1(PaddleOcrRuntimeLocator::engineId())) return normalized;
    return {};
}

bool parsePaddleHealth(const QByteArray &output, QString *error)
{
    const QJsonDocument document = QJsonDocument::fromJson(output);
    const QJsonObject root = document.object();
    if (!document.isObject() || !root.value(QStringLiteral("ok")).toBool()
        || root.value(QStringLiteral("engineId")).toString()
               != QString::fromLatin1(PaddleOcrRuntimeLocator::engineId())
        || root.value(QStringLiteral("engineVersion")).toString()
               != QString::fromLatin1(PaddleOcrRuntimeLocator::engineVersion())
        || !root.value(QStringLiteral("manifestVerified")).toBool()) {
        if (error) {
            *error = root.value(QStringLiteral("error")).toString().trimmed();
            if (error->isEmpty()) *error = QStringLiteral("PaddleOCR health response is invalid.");
        }
        return false;
    }
    return true;
}

} // namespace

SubtitleOcrController::SubtitleOcrController(SubtitleVoiceController *subtitleVoice,
                                             DubbingController *dubbing, QObject *parent)
    : QObject(parent), m_subtitleVoice(subtitleVoice), m_dubbing(dubbing)
{
    m_maxConcurrentWorkers = defaultOcrWorkerCount();
    const QString configuredEngine = normalizedLocalEngineId(
        qEnvironmentVariable("LASTUDIO_SUBTITLE_OCR_ENGINE"));
    if (!configuredEngine.isEmpty()) m_localEngineId = configuredEngine;
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &SubtitleOcrController::onProcessFinished);
    connect(&m_process, &QProcess::errorOccurred, this, &SubtitleOcrController::onProcessError);
    m_frameExtractionTimeout.setSingleShot(true);
    connect(&m_frameExtractionTimeout, &QTimer::timeout,
            this, &SubtitleOcrController::onFrameExtractionTimeout);
    m_forwardProgressTimer.setInterval(1000);
    connect(&m_forwardProgressTimer, &QTimer::timeout,
            this, &SubtitleOcrController::checkForwardProgress);
    connect(&m_sourceFingerprintWatcher, &QFutureWatcher<QString>::finished,
            this, &SubtitleOcrController::onSourceFingerprintReady);
    m_colabRunner = new ColabSubtitleOcrRunner;
    m_colabRunner->moveToThread(&m_colabThread);
    connect(&m_colabThread, &QThread::finished, m_colabRunner, &QObject::deleteLater);
    connect(m_colabRunner, &ColabSubtitleOcrRunner::finished,
            this, &SubtitleOcrController::onColabRecognitionFinished);
    connect(m_colabRunner, &ColabSubtitleOcrRunner::failed,
            this, &SubtitleOcrController::onColabRecognitionFailed);
    m_colabThread.start();
}

SubtitleOcrController::~SubtitleOcrController()
{
    m_frameExtractionTimeout.stop();
    m_forwardProgressTimer.stop();
    if (m_sourceFingerprintWatcher.isRunning()) m_sourceFingerprintWatcher.cancel();
    if (m_process.state() != QProcess::NotRunning) m_process.kill();
    releaseRecognitionWorkers();
    releaseActiveCacheKey();
    if (m_colabRunner && m_colabThread.isRunning()) {
        QMetaObject::invokeMethod(m_colabRunner, "cancel", Qt::QueuedConnection);
        m_colabThread.quit();
        m_colabThread.wait(3000);
    }
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
    if (usesPaddleLocalEngine()) return PaddleOcrRuntimeLocator::resolve().isUsable();
    if (m_runtimeService) return m_runtimeService->runtimeAvailable();
    return !SubtitleOcrRuntimeLocator::resolveTesseract().isEmpty();
}

bool SubtitleOcrController::localRouteReady() const
{
    if (!runtimeAvailable()) return false;
    if (usesPaddleLocalEngine())
        return PaddleOcrRuntimeLocator::supportsBundledLanguage(m_ocrLanguage);
    // Tesseract's managed language state is intentionally verified through
    // the same `--list-langs` process boundary that recognition will use.
    // This also covers an explicit environment runtime, whose language data
    // is outside of the package service's inventory.
    return true;
}

QString SubtitleOcrController::localRuntimeState() const
{
    if (usesPaddleLocalEngine()) {
        QString error;
        if (!PaddleOcrRuntimeLocator::resolve().isUsable(&error)) return QStringLiteral("Missing");
        return PaddleOcrRuntimeLocator::supportsBundledLanguage(m_ocrLanguage)
            ? QStringLiteral("Ready") : QStringLiteral("Unsupported language");
    }
    if (m_runtimeService) return m_runtimeService->stateName();
    return runtimeAvailable() ? QStringLiteral("Ready") : QStringLiteral("Missing");
}

QString SubtitleOcrController::runtimePath() const
{
    if (usesPaddleLocalEngine()) return PaddleOcrRuntimeLocator::resolve().pythonPath;
    if (m_runtimeService) return m_runtimeService->runtimePath();
    return SubtitleOcrRuntimeLocator::resolveTesseract();
}

int SubtitleOcrController::activeChildProcessCount() const
{
    int active = m_process.state() == QProcess::NotRunning ? 0 : 1;
    for (const RecognitionWorker &worker : m_recognitionWorkers) {
        if (worker.process && worker.process->state() != QProcess::NotRunning) ++active;
    }
    return active;
}

QString SubtitleOcrController::localEngineVersion() const
{
    return usesPaddleLocalEngine() ? QString::fromLatin1(PaddleOcrRuntimeLocator::engineVersion())
                                   : QStringLiteral("5.5.1");
}

bool SubtitleOcrController::usesPaddleLocalEngine() const
{
    return m_localEngineId == QString::fromLatin1(PaddleOcrRuntimeLocator::engineId());
}

bool SubtitleOcrController::usesTesseractLocalEngine() const
{
    return m_localEngineId == QStringLiteral("tesseract-baseline");
}

void SubtitleOcrController::refreshRuntime()
{
    if (usesTesseractLocalEngine() && m_runtimeService) m_runtimeService->refresh();
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

bool SubtitleOcrController::colabRouteReady() const
{
    return m_colabSession
        && m_colabSession->hasVerifiedRoute(kColabSubtitleOcrCapability, m_colabModelId);
}

QString SubtitleOcrController::colabRouteStatus() const
{
    if (!m_colabSession) return QStringLiteral("Subtitle OCR Colab session is unavailable in this build.");
    QString diagnostic;
    if (m_colabSession->hasVerifiedRoute(kColabSubtitleOcrCapability, m_colabModelId, &diagnostic)) {
        const QString gpu = m_colabSession->reportedGpu().trimmed();
        return gpu.isEmpty() ? QStringLiteral("Verified CUDA worker for %1.").arg(m_colabModelId)
                             : QStringLiteral("Verified CUDA worker (%1) for %2.").arg(gpu, m_colabModelId);
    }
    if (!diagnostic.isEmpty()) return diagnostic;
    return m_colabSession->verificationMessage().trimmed();
}

QString SubtitleOcrController::colabNotebookFile() const
{
    return kColabSubtitleOcrNotebook;
}

QVariantMap SubtitleOcrController::runStatistics() const
{
    return {{QStringLiteral("scheduledSamples"), m_scheduledSampleCount},
            {QStringLiteral("extractedFrames"), m_extractedFrameCount},
            {QStringLiteral("readableCrops"), m_readableCropCount},
            {QStringLiteral("deduplicatedFrames"), m_deduplicatedFrameCount},
            {QStringLiteral("recognizedFrames"), m_recognizedFrameCount},
            {QStringLiteral("ocrAttempts"), m_ocrAttemptCount},
            {QStringLiteral("ocrSuccesses"), m_ocrSuccessCount},
            {QStringLiteral("nonEmptyRawResults"), m_nonEmptyRawResultCount},
            {QStringLiteral("filterCandidates"), m_filterCandidateCount},
            {QStringLiteral("publishedSegments"), m_publishedSegmentCount},
            {QStringLiteral("exportedSegments"), m_exportedSegmentCount},
            {QStringLiteral("ffmpegProcessCount"), m_ffmpegProcessCount},
            {QStringLiteral("tesseractProcessCount"), m_tesseractProcessCount},
            {QStringLiteral("paddleProcessCount"), m_paddleProcessCount},
            {QStringLiteral("ocrWorkerCpuSeconds"), m_paddleCpuSeconds},
            {QStringLiteral("ocrWorkerPeakWorkingSetBytes"), m_paddlePeakWorkingSetBytes},
            {QStringLiteral("ocrEngineId"), m_executionRoute == QStringLiteral("local-cpu")
                ? m_localEngineId : m_colabModelId},
            {QStringLiteral("ocrEngineVersion"), m_executionRoute == QStringLiteral("local-cpu")
                ? localEngineVersion() : QStringLiteral("colab-contract-v1")},
            {QStringLiteral("completedSamples"), m_completedSampleCount},
            {QStringLiteral("elapsedMs"), m_runElapsed.isValid() ? m_runElapsed.elapsed() : 0},
            {QStringLiteral("cacheReused"), m_cacheReused},
            {QStringLiteral("resultStatus"), m_resultStatus}};
}

void SubtitleOcrController::setColabSession(ColabSession *session)
{
    if (m_colabSession == session) return;
    if (m_colabSession) disconnect(m_colabSession, nullptr, this, nullptr);
    m_colabSession = session;
    if (m_colabSession) {
        connect(m_colabSession, &ColabSession::sessionChanged,
                this, &SubtitleOcrController::colabRouteChanged);
        connect(m_colabSession, &ColabSession::verificationChanged,
                this, &SubtitleOcrController::colabRouteChanged);
    }
    emit colabRouteChanged();
}

void SubtitleOcrController::setError(const QString &message)
{
    if (m_error == message) return;
    m_error = message;
    emit errorChanged();
}

bool SubtitleOcrController::canRetryFrameExtraction() const
{
    return !m_processing && m_phase == QStringLiteral("error")
        && (m_lastFailedOperation == Operation::ExtractFrame
            || m_lastFailedOperation == Operation::ExtractChunk)
        && !m_sourcePath.isEmpty() && m_frameWidth > 0 && m_frameHeight > 0
        && m_sampleIndex >= 0 && m_sampleIndex < m_samples.size();
}

void SubtitleOcrController::clearDiagnostics()
{
    if (m_diagnostics.isEmpty()) return;
    m_diagnostics.clear();
    emit diagnosticsChanged();
}

void SubtitleOcrController::appendDiagnostic(const QString &event, const QString &detail)
{
    const QString entry = QStringLiteral("[%1] %2\n%3")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), event,
             boundedDiagnosticText(detail));
    if (!m_diagnostics.isEmpty()) m_diagnostics += QStringLiteral("\n\n");
    m_diagnostics += entry;
    if (m_diagnostics.size() > kMaxDiagnosticCharacters)
        m_diagnostics = m_diagnostics.right(kMaxDiagnosticCharacters);
    emit diagnosticsChanged();
}

void SubtitleOcrController::setPhase(const QString &phase)
{
    if (m_phase == phase) return;
    m_phase = phase;
    emit phaseChanged();
}

void SubtitleOcrController::setResultStatus(const QString &status)
{
    if (m_resultStatus == status) return;
    m_resultStatus = status;
    emit resultChanged();
    emit runStatisticsChanged();
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

void SubtitleOcrController::resetRunStatistics()
{
    m_scheduledSampleCount = 0;
    m_readableCropCount = 0;
    m_ocrAttemptCount = 0;
    m_ocrSuccessCount = 0;
    m_nonEmptyRawResultCount = 0;
    m_filterCandidateCount = 0;
    m_publishedSegmentCount = 0;
    m_exportedSegmentCount = 0;
    m_extractedFrameCount = 0;
    m_deduplicatedFrameCount = 0;
    m_recognizedFrameCount = 0;
    m_ffmpegProcessCount = 0;
    m_tesseractProcessCount = 0;
    m_paddleProcessCount = 0;
    m_paddleCpuSeconds = 0.0;
    m_paddlePeakWorkingSetBytes = 0;
    m_completedSampleCount = 0;
    m_lastForwardProgressMs = 0;
    m_cacheReused = false;
    emit runStatisticsChanged();
}

void SubtitleOcrController::appendObservation(const SubtitleOcrObservation &observation)
{
    m_observations.append(observation);
    if (!observation.text.trimmed().isEmpty()) ++m_nonEmptyRawResultCount;
    if (!observation.text.trimmed().isEmpty()
        && observation.confidence >= m_minimumConfidence) {
        ++m_filterCandidateCount;
    }
    emit runStatisticsChanged();
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

void SubtitleOcrController::cleanWorkspace(bool retainDiagnostics)
{
    if (!m_workspacePath.isEmpty()) {
        const QString workspace = m_workspacePath;
        const bool existed = QFileInfo(workspace).isDir();
        const bool removed = QDir(workspace).removeRecursively();
        if (retainDiagnostics || !m_diagnostics.isEmpty()) {
            appendDiagnostic(QStringLiteral("workspace-cleanup"),
                             QStringLiteral("workspace=%1 existed=%2 removed=%3 completedUtc=%4")
                                 .arg(workspace)
                                 .arg(existed ? QStringLiteral("true") : QStringLiteral("false"))
                                 .arg(removed ? QStringLiteral("true") : QStringLiteral("false"))
                                 .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)));
        }
    }
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
        fail(QStringLiteral("Required Subtitle OCR runtime is unavailable."), operation);
        return;
    }
    m_operation = operation;
    m_process.setProgram(program);
    QStringList processArguments = arguments;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (m_runtimeService && (operation == Operation::VerifyLanguage ||
                             operation == Operation::RecognizeFrame)) {
        // Keep the UI/runtime preflight and every actual OCR invocation on the
        // same explicitly resolved tessdata directory.  This avoids a system
        // TESSDATA_PREFIX making a language look installed when this worker
        // cannot use the verified app-managed file.
        processArguments = m_runtimeService->tesseractDataArguments() + processArguments;
        environment = m_runtimeService->tesseractProcessEnvironment();
    }
    m_process.setArguments(processArguments);
    m_process.setProcessEnvironment(environment);
    if (operation == Operation::ExtractFrame || operation == Operation::ExtractChunk)
        m_frameExtractionTimedOut = false;
    m_process.start();
    if (operation == Operation::ExtractFrame || operation == Operation::ExtractChunk) {
        m_frameExtractionTimeout.start(frameExtractionTimeoutMs());
    }
}

void SubtitleOcrController::recordFrameExtractionStart(const MediaRuntimePaths &media,
                                                        qint64 timestampMs,
                                                        const SubtitleOcrRect &crop)
{
    appendDiagnostic(QStringLiteral("frame-extraction-start"),
                     QStringLiteral("source=%1; ffmpeg=%2; timestampMs=%3; timestamp=%4; frame=%5x%6; "
                                    "rotation=%7; SAR=%8; normalizedRoi=%9; pixelCrop=%10; output=%11")
                         .arg(m_sourcePath, media.ffmpeg).arg(timestampMs).arg(ffmpegTime(timestampMs))
                         .arg(m_frameWidth).arg(m_frameHeight).arg(m_rotationDegrees)
                         .arg(m_sampleAspectRatio.isEmpty() ? QStringLiteral("unknown") : m_sampleAspectRatio)
                         .arg(normalizedRoiText(m_roi), cropText(crop), m_currentFramePath));
}

bool SubtitleOcrController::validateCurrentFrame(QByteArray *hash, QString *errorMessage)
{
    return validateFrame(m_currentFramePath, hash, errorMessage);
}

bool SubtitleOcrController::validateFrame(const QString &path, QByteArray *hash, QString *errorMessage)
{
    const QFileInfo info(path);
    const bool exists = info.isFile();
    const qint64 bytes = exists ? info.size() : 0;
    QByteArray signature;
    QString decodeDetail;
    QImage image;
    if (!exists) {
        decodeDetail = QStringLiteral("crop file is missing");
    } else if (bytes <= 0) {
        decodeDetail = QStringLiteral("crop file is empty");
    } else {
        QFile frame(path);
        if (!frame.open(QIODevice::ReadOnly)) {
            decodeDetail = QStringLiteral("crop file cannot be opened: %1").arg(frame.errorString());
        } else {
            signature = frame.read(8);
            if (signature != QByteArrayLiteral("\x89PNG\r\n\x1a\n")) {
                decodeDetail = QStringLiteral("crop does not have a PNG signature");
            } else {
                QImageReader reader(path);
                reader.setAutoTransform(false);
                image = reader.read();
                if (image.isNull()) {
                    decodeDetail = QStringLiteral("PNG decode failed: %1").arg(reader.errorString());
                } else {
                    frame.seek(0);
                    if (hash) *hash = QCryptographicHash::hash(frame.readAll(), QCryptographicHash::Sha256);
                }
            }
        }
    }
    appendDiagnostic(QStringLiteral("frame-extraction-output"),
                     QStringLiteral("output=%1; exists=%2; bytes=%3; signature=%4; decoded=%5x%6; result=%7")
                         .arg(path)
                         .arg(exists ? QStringLiteral("true") : QStringLiteral("false"))
                         .arg(bytes)
                         .arg(QString::fromLatin1(signature.toHex()))
                         .arg(image.width()).arg(image.height())
                         .arg(decodeDetail.isEmpty() ? QStringLiteral("readable") : decodeDetail));
    if (!decodeDetail.isEmpty()) {
        if (errorMessage) *errorMessage = decodeDetail;
        return false;
    }
    return true;
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
    m_lastFailedOperation = Operation::None;
    m_samples.clear();
    emit frameRetryChanged();
    clearDiagnostics();
    appendDiagnostic(QStringLiteral("probe-start"),
                     QStringLiteral("source=%1; ffprobe=%2")
                         .arg(m_pendingSourcePath, media.ffprobe));
    setError({});
    setProcessing(true);
    setPhase(QStringLiteral("probing"));
    setProgress(0, false);
    startProcess(Operation::Probe, media.ffprobe,
                 {QStringLiteral("-v"), QStringLiteral("error"),
                  QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                  QStringLiteral("-show_entries"),
                  QStringLiteral("stream=width,height,sample_aspect_ratio,display_aspect_ratio:stream_tags=rotate:stream_side_data=rotation:format=duration"),
                  QStringLiteral("-of"), QStringLiteral("json"), m_pendingSourcePath});
    return true;
}

bool SubtitleOcrController::useDownloadedMedia(const QString &path)
{
    if (m_processing || m_sourceImporting) return false;
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
    Q_UNUSED(url);
    // Subtitle OCR accepts local media only.  A public link must first be
    // resolved by the dedicated local downloader and then selected from the
    // local media library; OCR never activates a desktop downloader fallback.
    setSourceImportState(false, {}, QStringLiteral(
        "Download the public link locally, then choose the completed file for Subtitle OCR."));
    return false;
}

void SubtitleOcrController::cancelSourceImport()
{
    if (!m_sourceImporting) return;
    setSourceImportState(false, {}, QStringLiteral("The local media download was canceled from its media library."));
}

bool SubtitleOcrController::retrySourceImport()
{
    setSourceImportState(false, {}, QStringLiteral(
        "Retry the public link with the local downloader, then choose the completed file."));
    return false;
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

void SubtitleOcrController::completeProbe(const QByteArray &output)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
    const QJsonObject root = document.object();
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    const QJsonObject stream = streams.isEmpty() ? QJsonObject() : streams.at(0).toObject();
    const int width = stream.value(QStringLiteral("width")).toInt();
    const int height = stream.value(QStringLiteral("height")).toInt();
    int rotation = 0;
    for (const QJsonValue &sideDataValue : stream.value(QStringLiteral("side_data_list")).toArray()) {
        const QJsonObject sideData = sideDataValue.toObject();
        if (sideData.contains(QStringLiteral("rotation"))) {
            rotation = normalizedRotation(qRound(sideData.value(QStringLiteral("rotation")).toDouble()));
            break;
        }
    }
    if (rotation == 0) {
        bool rotationOk = false;
        const int taggedRotation = stream.value(QStringLiteral("tags")).toObject()
            .value(QStringLiteral("rotate")).toString().toInt(&rotationOk);
        if (rotationOk) rotation = normalizedRotation(taggedRotation);
    }
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
    m_rotationDegrees = rotation;
    const bool transposed = rotation == 90 || rotation == 270;
    m_frameWidth = transposed ? height : width;
    m_frameHeight = transposed ? width : height;
    m_sampleAspectRatio = stream.value(QStringLiteral("sample_aspect_ratio")).toString();
    m_displayAspectRatio = stream.value(QStringLiteral("display_aspect_ratio")).toString();
    double displayAspectRatio = 0.0;
    if (parseAspectRatio(m_displayAspectRatio, &displayAspectRatio)) {
        if (transposed) displayAspectRatio = 1.0 / displayAspectRatio;
        m_sourceWidth = qMax(1, qRound(m_frameHeight * displayAspectRatio));
    } else {
        m_sourceWidth = m_frameWidth;
    }
    m_sourceHeight = m_frameHeight;
    m_durationMs = qRound64(durationSeconds * 1000.0);
    appendDiagnostic(QStringLiteral("probe-complete"),
                     QStringLiteral("source=%1; frame=%2x%3; display=%4x%5; rotation=%6; SAR=%7; DAR=%8; durationMs=%9")
                         .arg(m_sourcePath).arg(m_frameWidth).arg(m_frameHeight)
                         .arg(m_sourceWidth).arg(m_sourceHeight).arg(m_rotationDegrees)
                         .arg(m_sampleAspectRatio.isEmpty() ? QStringLiteral("unknown") : m_sampleAspectRatio)
                         .arg(m_displayAspectRatio.isEmpty() ? QStringLiteral("unknown") : m_displayAspectRatio)
                         .arg(m_durationMs));
    m_segments.clear();
    resetRunStatistics();
    setResultStatus(QStringLiteral("ready"));
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
        setError(QStringLiteral("Choose a valid Subtitle OCR language code."));
        return false;
    }
    if (m_ocrLanguage == normalized) return true;
    m_ocrLanguage = normalized;
    emit settingsChanged();
    emit runtimeChanged();
    return true;
}

bool SubtitleOcrController::setLocalEngine(const QString &engineId)
{
    if (m_processing) {
        setError(QStringLiteral("Wait for Subtitle OCR to finish before changing the local OCR engine."));
        return false;
    }
    const QString normalized = normalizedLocalEngineId(engineId);
    if (normalized.isEmpty()) {
        setError(QStringLiteral("Choose PaddleOCR PP-OCRv6 tiny or the explicit Tesseract baseline."));
        return false;
    }
    if (m_localEngineId == normalized) return true;
    m_localEngineId = normalized;
    setError({});
    emit settingsChanged();
    emit runtimeChanged();
    return true;
}

bool SubtitleOcrController::setExecutionRoute(const QString &route)
{
    const QString normalized = route.trimmed().toLower();
    if (normalized != QStringLiteral("local-cpu") && normalized != QStringLiteral("colab-gpu")) {
        setError(QStringLiteral("Subtitle OCR route must be Local CPU or Colab GPU."));
        return false;
    }
    if (m_executionRoute == normalized) return true;
    m_executionRoute = normalized;
    setError({});
    emit settingsChanged();
    emit colabRouteChanged();
    return true;
}

bool SubtitleOcrController::setColabModelId(const QString &modelId)
{
    const QString normalized = modelId.trimmed().toLower();
    if (normalized != kColabSubtitleOcrModel) {
        setError(QStringLiteral("Choose the supported exact Colab Subtitle OCR model."));
        return false;
    }
    if (m_colabModelId == normalized) return true;
    m_colabModelId = normalized;
    setError({});
    emit settingsChanged();
    emit colabRouteChanged();
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

bool SubtitleOcrController::setMaxConcurrentWorkers(int workers)
{
    if (m_processing) {
        setError(QStringLiteral("Wait for Subtitle OCR to finish before changing the worker limit."));
        return false;
    }
    if (workers < 1 || workers > 4) {
        setError(QStringLiteral("Subtitle OCR worker limit must be between 1 and 4."));
        return false;
    }
    m_maxConcurrentWorkers = workers;
    setError({});
    emit settingsChanged();
    return true;
}

bool SubtitleOcrController::setBenchmarkSampleLimit(int limit)
{
    if (m_processing || limit < 0) return false;
    m_benchmarkSampleLimit = limit;
    return true;
}

bool SubtitleOcrController::requestCropPreview(qint64 positionMs)
{
    if (m_processing || m_sourcePath.isEmpty() || m_frameWidth <= 0 || m_frameHeight <= 0) return false;
    const MediaRuntimePaths media = MediaRuntimeLocator::resolve();
    if (!media.hasFfmpeg()) {
        setError(QStringLiteral("FFmpeg is required to preview the Subtitle OCR region."));
        return false;
    }
    const QStringList crop = SubtitleOcrPipeline::ffmpegCropArguments(m_roi, m_frameWidth, m_frameHeight);
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
                          QStringLiteral("-ss"), ffmpegTime(qMin(positionMs,
                                                                   SubtitleOcrPipeline::lastDecodableTimestamp(m_durationMs))),
                          QStringLiteral("-i"), m_sourcePath};
    arguments += crop;
    arguments += {QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-y"), m_cropPreviewPath};
    startProcess(Operation::CropPreview, media.ffmpeg, arguments);
    return true;
}

bool SubtitleOcrController::run()
{
    if (m_processing) return false;
    if (m_sourcePath.isEmpty() || m_frameWidth <= 0 || m_frameHeight <= 0 || m_durationMs <= 0) {
        setError(QStringLiteral("Choose and inspect a video before running Subtitle OCR."));
        return false;
    }
    const bool useColab = m_executionRoute == QStringLiteral("colab-gpu");
    const bool usePaddle = !useColab && usesPaddleLocalEngine();
    const QString tesseract = runtimePath();
    if (!useColab && !runtimeAvailable()) {
        setError(usePaddle
                     ? QStringLiteral("The package-provisioned PaddleOCR PP-OCRv6 tiny runtime is unavailable or incomplete. Repair the package; LA Studio will not fall back silently to Tesseract.")
                     : QStringLiteral("Subtitle OCR Tesseract baseline runtime is unavailable. Install runtime or repair the package before running."));
        emit runtimeChanged();
        return false;
    }
    if (!useColab && !localRouteReady()) {
        setError(usePaddle
                     ? QStringLiteral("The bundled PaddleOCR PP-OCRv6 tiny runtime is verified only for Simplified Chinese (chi_sim). Select chi_sim, the explicit Tesseract baseline with its matching language pack, or Direct Colab GPU.")
                     : QStringLiteral("The selected Tesseract language data is not installed. Install the matching language pack and retry."));
        emit runtimeChanged();
        return false;
    }
    QString routeError;
    if (useColab && (!m_colabSession
                     || !m_colabSession->hasVerifiedRoute(kColabSubtitleOcrCapability,
                                                          m_colabModelId, &routeError))) {
        const QString guidance = QStringLiteral(
            "Connect and check the exact Colab Subtitle OCR worker before running.");
        setError(routeError.isEmpty() ? guidance
                                      : QStringLiteral("%1 %2").arg(guidance, routeError));
        return false;
    }
    if (useColab && m_ocrLanguage.contains(QLatin1Char('+'))) {
        setError(QStringLiteral("Colab Subtitle OCR runs one explicit language profile at a time. Choose one language before running."));
        return false;
    }
    const MediaRuntimePaths media = MediaRuntimeLocator::resolve();
    if (!media.hasFfmpeg()) {
        setError(QStringLiteral("FFmpeg is required to extract Subtitle OCR frames."));
        return false;
    }
    if (SubtitleOcrPipeline::ffmpegCropArguments(m_roi, m_frameWidth, m_frameHeight).isEmpty()) {
        setError(QStringLiteral("Subtitle OCR region resolves to zero source pixels. Enlarge the region before running."));
        return false;
    }
    cleanWorkspace();
    releaseRecognitionWorkers();
    releaseActiveCacheKey();
    if (!ensureWorkspace()) return false;
    m_samples = SubtitleOcrPipeline::sampleTimes(m_durationMs, m_sampleIntervalMs);
    if (m_benchmarkSampleLimit > 0 && m_samples.size() > m_benchmarkSampleLimit)
        m_samples.resize(m_benchmarkSampleLimit);
    if (m_samples.isEmpty()) {
        fail(QStringLiteral("No Subtitle OCR sample timestamps could be created."));
        return false;
    }
    m_observations.clear();
    resetRunStatistics();
    if (!m_segments.isEmpty()) {
        m_segments.clear();
        emit segmentsChanged();
    }
    m_sampleIndex = 0;
    m_previousFrameHash.clear();
    m_previousText.clear();
    m_previousConfidence = 0.0;
    m_recognitionQueue.clear();
    m_uniqueFrames.clear();
    m_chunkStartIndex = 0;
    m_chunkEndIndex = 0;
    m_sourceFingerprint.clear();
    m_cacheKey.clear();
    m_lastFailedOperation = Operation::None;
    m_scheduledSampleCount = m_samples.size();
    setResultStatus(QStringLiteral("running"));
    emit runStatisticsChanged();
    emit frameRetryChanged();
    m_cancelRequested = false;
    m_runElapsed.start();
    m_lastForwardProgressMs = 0;
    setError({});
    setProcessing(true);
    // Local engine selection is explicit.  PaddleOCR gets a package health
    // check; Tesseract remains an explicit compatibility baseline.  Colab is
    // a separate verified route and intentionally never falls back locally.
    setPhase(useColab ? QStringLiteral("checking-colab-route")
                      : (usePaddle ? QStringLiteral("checking-paddleocr-runtime")
                                   : QStringLiteral("checking-language")));
    setProgress(0, false);
    if (useColab) beginOcrSamples();
    else if (usePaddle) {
        const PaddleOcrRuntimeResolution paddle = PaddleOcrRuntimeLocator::resolve();
        startProcess(Operation::VerifyPaddleRuntime, paddle.pythonPath,
                     {paddle.workerPath, QStringLiteral("--cache-root"), paddle.modelCachePath,
                      QStringLiteral("--manifest"), paddle.manifestPath,
                      QStringLiteral("--health")});
    } else {
        startProcess(Operation::VerifyLanguage, tesseract, {QStringLiteral("--list-langs")});
    }
    return true;
}

bool SubtitleOcrController::retry()
{
    return run();
}

bool SubtitleOcrController::retryFrameExtraction()
{
    if (!canRetryFrameExtraction()) {
        setError(QStringLiteral("There is no failed Subtitle OCR frame extraction available to retry."));
        return false;
    }
    const MediaRuntimePaths media = MediaRuntimeLocator::resolve();
    const QStringList crop = SubtitleOcrPipeline::ffmpegCropArguments(
        m_roi, m_frameWidth, m_frameHeight);
    if (!media.hasFfmpeg() || crop.isEmpty()) {
        setError(QStringLiteral("Subtitle OCR frame extraction is no longer configured."));
        return false;
    }
    cleanWorkspace();
    if (!ensureWorkspace()) return false;
    m_lastFailedOperation = Operation::None;
    emit frameRetryChanged();
    m_cancelRequested = false;
    setError({});
    setProcessing(true);
    setPhase(QStringLiteral("retrying-frame-extraction"));
    setProgress(qRound(100.0 * m_sampleIndex / m_samples.size()), true);
    appendDiagnostic(QStringLiteral("frame-extraction-retry"),
                     QStringLiteral("sample=%1/%2; source=%3; ffmpeg=%4")
                         .arg(m_sampleIndex + 1).arg(m_samples.size())
                         .arg(m_sourcePath, media.ffmpeg));
    if (m_executionRoute == QStringLiteral("local-cpu")) beginNextChunk();
    else beginNextSample();
    return true;
}

void SubtitleOcrController::beginOcrSamples()
{
    setPhase(m_executionRoute == QStringLiteral("local-cpu")
                 ? QStringLiteral("fingerprinting-source")
                 : QStringLiteral("extracting-frame"));
    setProgress(0, true);
    if (m_executionRoute == QStringLiteral("local-cpu")) beginCacheLookup();
    else beginNextSample();
}

void SubtitleOcrController::beginCacheLookup()
{
    if (!m_processing || m_cancelRequested) {
        completeCancellation();
        return;
    }
    setPhase(QStringLiteral("fingerprinting-source"));
    m_sourceFingerprintWatcher.setFuture(QtConcurrent::run(sha256File, m_sourcePath));
}

QString SubtitleOcrController::cacheKeyMaterial() const
{
    const QFileInfo runtime(runtimePath());
    const PaddleOcrRuntimeResolution paddle = usesPaddleLocalEngine()
        ? PaddleOcrRuntimeLocator::resolve() : PaddleOcrRuntimeResolution{};
    const QFileInfo paddleManifest(paddle.manifestPath);
    const QString paddleManifestHash = paddle.manifestPath.isEmpty()
        ? QString() : sha256File(paddle.manifestPath);
    return QStringLiteral("schema=%1\nsource=%2\nfingerprint=%3\nsize=%4\nroi=%5\ninterval=%6\nconfidence=%7\nlanguage=%8\nroute=%9\nengine=%10\nengineVersion=%11\nruntimePath=%12\nengineSize=%13\nengineModified=%14\npaddleManifest=%15\npaddleManifestSha256=%16\npreprocess=scale3-gray-v1\nbenchmarkLimit=%17")
        .arg(kSubtitleOcrCacheVersion)
        .arg(QFileInfo(m_sourcePath).canonicalFilePath(), m_sourceFingerprint)
        .arg(QFileInfo(m_sourcePath).size())
        .arg(normalizedRoiText(m_roi))
        .arg(m_sampleIntervalMs)
        .arg(m_minimumConfidence, 0, 'f', 6)
        .arg(m_ocrLanguage, m_executionRoute,
             m_executionRoute == QStringLiteral("local-cpu") ? m_localEngineId : m_colabModelId)
        .arg(localEngineVersion())
        .arg(runtime.absoluteFilePath())
        .arg(runtime.size())
        .arg(runtime.lastModified().toUTC().toString(Qt::ISODateWithMs))
        .arg(paddleManifest.absoluteFilePath())
        .arg(paddleManifestHash)
        .arg(m_benchmarkSampleLimit);
}

QString SubtitleOcrController::cacheFilePath() const
{
    return QDir(PathUtils::cacheDir()).filePath(
        // Completed OCR artifacts must survive a clean staging workspace, but
        // must not live inside it.  A cancel/retry can therefore remove every
        // temporary crop without deleting a valid, separately keyed result.
        QStringLiteral("subtitle-ocr-cache/results/%1.json").arg(m_cacheKey));
}

bool SubtitleOcrController::loadCachedResult()
{
    QFile file(cacheFilePath());
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = document.object();
    if (document.isNull() || root.value(QStringLiteral("version")).toInt() != kSubtitleOcrCacheVersion
        || root.value(QStringLiteral("key")).toString() != m_cacheKey) return false;
    QString error;
    const QVector<SubtitleOcrSegment> cached = segmentsFromVariant(
        root.value(QStringLiteral("segments")).toArray().toVariantList(), &error);
    const bool requireHan = m_ocrLanguage.compare(QStringLiteral("chi_sim"), Qt::CaseInsensitive) == 0;
    if (!SubtitleOcrPipeline::validatePublishableSegments(cached, requireHan, &error)) return false;
    m_segments = segmentsToVariant(cached);
    m_publishedSegmentCount = m_segments.size();
    m_cacheReused = true;
    m_completedSampleCount = m_samples.size();
    appendDiagnostic(QStringLiteral("result-cache-hit"),
                     QStringLiteral("key=%1; segments=%2").arg(m_cacheKey).arg(m_segments.size()));
    cleanWorkspace();
    setProcessing(false);
    setProgress(100, true);
    setPhase(QStringLiteral("completed"));
    setResultStatus(QStringLiteral("completed"));
    setError({});
    emit runStatisticsChanged();
    emit segmentsChanged();
    return true;
}

bool SubtitleOcrController::storeCachedResult()
{
    if (m_cacheKey.isEmpty() || m_segments.isEmpty()) return false;
    const QFileInfo destination(cacheFilePath());
    if (!QDir().mkpath(destination.absolutePath())) return false;
    QJsonObject root{{QStringLiteral("version"), kSubtitleOcrCacheVersion},
                     {QStringLiteral("key"), m_cacheKey},
                     {QStringLiteral("sourceFingerprint"), m_sourceFingerprint},
                     {QStringLiteral("createdUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                     {QStringLiteral("segments"), QJsonArray::fromVariantList(m_segments)}};
    QSaveFile file(destination.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0) return false;
    return file.commit();
}

void SubtitleOcrController::releaseActiveCacheKey()
{
    if (!m_cacheKeyActive) return;
    s_activeOcrCacheKeys.remove(m_cacheKey);
    m_cacheKeyActive = false;
}

void SubtitleOcrController::onSourceFingerprintReady()
{
    if (!m_processing || m_executionRoute != QStringLiteral("local-cpu")) return;
    m_sourceFingerprint = m_sourceFingerprintWatcher.result();
    if (m_sourceFingerprint.isEmpty()) {
        fail(QStringLiteral("Could not fingerprint the Subtitle OCR source for a safe reusable result."));
        return;
    }
    m_cacheKey = QString::fromLatin1(QCryptographicHash::hash(
        cacheKeyMaterial().toUtf8(), QCryptographicHash::Sha256).toHex());
    if (loadCachedResult()) return;
    if (s_activeOcrCacheKeys.contains(m_cacheKey)) {
        fail(QStringLiteral("An identical Subtitle OCR job is already running. Wait for that job or use its published result."),
             Operation::None, QStringLiteral("matching_job_active"));
        return;
    }
    s_activeOcrCacheKeys.insert(m_cacheKey);
    m_cacheKeyActive = true;
    m_lastForwardProgressMs = m_runElapsed.elapsed();
    m_forwardProgressTimer.start();
    beginNextChunk();
}

void SubtitleOcrController::updateForwardProgress()
{
    m_lastForwardProgressMs = m_runElapsed.isValid() ? m_runElapsed.elapsed() : 0;
    setProgress(qRound(100.0 * m_completedSampleCount / qMax(1, m_samples.size())), true);
    emit runStatisticsChanged();
}

void SubtitleOcrController::checkForwardProgress()
{
    if (!m_processing || m_executionRoute != QStringLiteral("local-cpu") || !m_runElapsed.isValid()) return;
    if (m_runElapsed.elapsed() - m_lastForwardProgressMs > kForwardProgressTimeoutMs) {
        fail(QStringLiteral("Subtitle OCR made no forward progress for 60 seconds; the job was stopped before a misleading long wait."),
             m_operation == Operation::ExtractChunk ? Operation::ExtractChunk : Operation::RecognizeFrame);
    }
}

void SubtitleOcrController::beginNextChunk()
{
    if (!m_processing || m_cancelRequested) {
        completeCancellation();
        return;
    }
    if (m_chunkStartIndex >= m_samples.size()) {
        completeRun();
        return;
    }
    const MediaRuntimePaths media = MediaRuntimeLocator::resolve();
    const QStringList crop = SubtitleOcrPipeline::ffmpegCropArguments(m_roi, m_frameWidth, m_frameHeight);
    const SubtitleOcrRect cropRect = SubtitleOcrPipeline::sourceRect(
        m_roi, m_frameWidth, m_frameHeight);
    if (!media.hasFfmpeg() || crop.size() != 2) {
        fail(QStringLiteral("Subtitle OCR batch frame extraction is no longer configured."), Operation::ExtractChunk);
        return;
    }
    m_chunkEndIndex = qMin(m_chunkStartIndex + kChunkSampleCount, m_samples.size());
    // The final safe-end timestamp is deliberately allowed to be off the
    // regular interval cadence.  Keep it in a one-frame trailing chunk so the
    // fps filter cannot silently omit it (for example 0,30,60,90,109 seconds).
    if (m_chunkEndIndex == m_samples.size() && m_chunkEndIndex - m_chunkStartIndex > 1
        && m_samples.at(m_chunkEndIndex - 1) - m_samples.at(m_chunkEndIndex - 2)
            != m_sampleIntervalMs) {
        --m_chunkEndIndex;
    }
    const int count = m_chunkEndIndex - m_chunkStartIndex;
    const qint64 startMs = m_samples.at(m_chunkStartIndex);
    const QString pattern = QDir(m_workspacePath).filePath(QStringLiteral("frame-%06d.png"));
    // A trailing safe-end sample may be a one-frame chunk.  In that case an
    // fps cadence can wait for a future tick beyond EOF, so extract the seeked
    // frame directly while keeping the same crop/preprocess chain.
    const QString filter = count == 1
        ? crop.at(1)
        : crop.at(1) + QStringLiteral(",fps=fps=1000/%1:round=near").arg(m_sampleIntervalMs);
    setPhase(QStringLiteral("extracting chunk %1-%2/%3")
                 .arg(m_chunkStartIndex + 1).arg(m_chunkEndIndex).arg(m_samples.size()));
    appendDiagnostic(QStringLiteral("batch-extraction-start"),
                     QStringLiteral("chunk=%1-%2; samples=%3; timestampMs=%4; ffmpeg=%5; "
                                    "normalizedRoi=%6; pixelCrop=%7")
                         .arg(m_chunkStartIndex + 1).arg(m_chunkEndIndex).arg(count)
                         .arg(startMs).arg(media.ffmpeg).arg(normalizedRoiText(m_roi))
                         .arg(cropText(cropRect)));
    ++m_ffmpegProcessCount;
    emit runStatisticsChanged();
    startProcess(Operation::ExtractChunk, media.ffmpeg,
                 {QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                  QStringLiteral("error"), QStringLiteral("-ss"), ffmpegTime(startMs),
                  QStringLiteral("-i"), m_sourcePath, QStringLiteral("-an"), QStringLiteral("-vf"), filter,
                  QStringLiteral("-frames:v"), QString::number(count), QStringLiteral("-start_number"),
                  QString::number(m_chunkStartIndex), QStringLiteral("-y"), pattern});
}

void SubtitleOcrController::queueChunkFrames()
{
    bool reusedCompletedRecognition = false;
    for (int index = m_chunkStartIndex; index < m_chunkEndIndex; ++index) {
        const QString framePath = QDir(m_workspacePath).filePath(
            QStringLiteral("frame-%1.png").arg(index, 6, 10, QLatin1Char('0')));
        QByteArray hash;
        QString error;
        ++m_extractedFrameCount;
        if (!validateFrame(framePath, &hash, &error)) {
            fail(QStringLiteral("Subtitle OCR batch extraction did not produce a readable PNG crop: %1").arg(error),
                 Operation::ExtractChunk);
            return;
        }
        ++m_readableCropCount;
        const auto existing = m_uniqueFrames.constFind(hash);
        if (existing != m_uniqueFrames.cend()) {
            ++m_deduplicatedFrameCount;
            QFile::remove(framePath);
            // A later chunk can contain a frame already recognized by a
            // completed worker.  Reuse that exact result immediately instead
            // of queuing a worker that will never run, and count it as real
            // forward progress.  If the worker is still active, append the
            // sample so its completion publishes both timestamps in order.
            if (existing.value()->completed) {
                appendObservation({m_samples.at(index), existing.value()->recognizedText,
                                   existing.value()->recognizedConfidence});
                ++m_completedSampleCount;
                reusedCompletedRecognition = true;
            } else {
                existing.value()->sampleIndexes.append(index);
            }
            continue;
        }
        auto item = QSharedPointer<RecognitionItem>::create();
        item->frameHash = hash;
        item->framePath = framePath;
        item->sampleIndexes.append(index);
        m_uniqueFrames.insert(hash, item);
        m_recognitionQueue.enqueue(item);
    }
    if (reusedCompletedRecognition) updateForwardProgress();
    emit runStatisticsChanged();
    pumpRecognitionQueue();
    if (usesPaddleLocalEngine() && m_operation == Operation::RecognizePaddleChunk) return;
    const bool busy = std::any_of(m_recognitionWorkers.cbegin(), m_recognitionWorkers.cend(),
                                  [](const RecognitionWorker &worker) { return worker.item; });
    if (!busy && m_recognitionQueue.isEmpty()) {
        m_chunkStartIndex = m_chunkEndIndex;
        // Do not start a new FFmpeg process from inside the current process'
        // finished signal.  Queuing preserves the event ordering and handles
        // an all-duplicate trailing chunk without leaving the job running.
        QTimer::singleShot(0, this, [this] {
            if (m_processing && !m_cancelRequested) beginNextChunk();
        });
    }
}

void SubtitleOcrController::pumpRecognitionQueue()
{
    if (!m_processing || m_executionRoute != QStringLiteral("local-cpu")) return;
    if (usesPaddleLocalEngine()) {
        beginPaddleRecognitionChunk();
        return;
    }
    if (m_recognitionWorkers.isEmpty()) {
        for (int index = 0; index < m_maxConcurrentWorkers; ++index) {
            RecognitionWorker worker;
            worker.process = new QProcess(this);
            connect(worker.process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                    [this, process = worker.process](int code, QProcess::ExitStatus status) {
                        onRecognitionFinished(process, code, status);
                    });
            connect(worker.process, &QProcess::errorOccurred, this,
                    [this, process = worker.process](QProcess::ProcessError error) {
                        onRecognitionError(process, error);
                    });
            m_recognitionWorkers.append(worker);
        }
    }
    const QString tesseract = runtimePath();
    if (tesseract.isEmpty()) {
        fail(QStringLiteral("Subtitle OCR runtime became unavailable during batch recognition."));
        return;
    }
    for (RecognitionWorker &worker : m_recognitionWorkers) {
        if (m_recognitionQueue.isEmpty()) break;
        if (worker.item || worker.process->state() != QProcess::NotRunning) continue;
        worker.item = m_recognitionQueue.dequeue();
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        QStringList arguments{worker.item->framePath, QStringLiteral("stdout"), QStringLiteral("-l"),
                              m_ocrLanguage, QStringLiteral("--psm"), QStringLiteral("6"), QStringLiteral("tsv")};
        if (m_runtimeService) {
            arguments = m_runtimeService->tesseractDataArguments() + arguments;
            environment = m_runtimeService->tesseractProcessEnvironment();
        }
        ++m_ocrAttemptCount;
        ++m_tesseractProcessCount;
        setPhase(QStringLiteral("recognizing %1/%2 (%3 workers)")
                     .arg(m_completedSampleCount + 1).arg(m_samples.size()).arg(m_maxConcurrentWorkers));
        worker.process->setProgram(tesseract);
        worker.process->setArguments(arguments);
        worker.process->setProcessEnvironment(environment);
        worker.process->start();
    }
    emit runStatisticsChanged();
}

bool SubtitleOcrController::beginPaddleRecognitionChunk()
{
    if (m_recognitionQueue.isEmpty()) return false;
    const PaddleOcrRuntimeResolution paddle = PaddleOcrRuntimeLocator::resolve();
    if (!paddle.isUsable()) {
        fail(QStringLiteral("PaddleOCR runtime became unavailable during batch recognition."),
             Operation::RecognizePaddleChunk);
        return false;
    }

    m_paddleChunkItems.clear();
    QJsonArray frames;
    while (!m_recognitionQueue.isEmpty()) {
        const QSharedPointer<RecognitionItem> item = m_recognitionQueue.dequeue();
        m_paddleChunkItems.append(item);
        frames.append(QJsonObject{{QStringLiteral("hash"), QString::fromLatin1(item->frameHash.toHex())},
                                  {QStringLiteral("path"), item->framePath}});
    }
    const QString stem = QStringLiteral("paddle-chunk-%1-%2")
        .arg(m_chunkStartIndex).arg(m_chunkEndIndex);
    m_paddleRequestPath = QDir(m_workspacePath).filePath(stem + QStringLiteral(".request.json"));
    m_paddleResponsePath = QDir(m_workspacePath).filePath(stem + QStringLiteral(".response.json"));
    QSaveFile request(m_paddleRequestPath);
    const QJsonObject payload{{QStringLiteral("schemaVersion"), 1},
                              {QStringLiteral("engineId"), QString::fromLatin1(PaddleOcrRuntimeLocator::engineId())},
                              {QStringLiteral("language"), m_ocrLanguage},
                              {QStringLiteral("frames"), frames}};
    if (!request.open(QIODevice::WriteOnly)
        || request.write(QJsonDocument(payload).toJson(QJsonDocument::Compact)) < 0
        || !request.commit()) {
        fail(QStringLiteral("Cannot write the PaddleOCR batch request."), Operation::RecognizePaddleChunk);
        return false;
    }
    QFile::remove(m_paddleResponsePath);
    m_ocrAttemptCount += m_paddleChunkItems.size();
    ++m_paddleProcessCount;
    setPhase(QStringLiteral("recognizing PaddleOCR batch %1-%2/%3")
                 .arg(m_chunkStartIndex + 1).arg(m_chunkEndIndex).arg(m_samples.size()));
    emit runStatisticsChanged();
    startProcess(Operation::RecognizePaddleChunk, paddle.pythonPath,
                 {paddle.workerPath, QStringLiteral("--cache-root"), paddle.modelCachePath,
                  QStringLiteral("--manifest"), paddle.manifestPath,
                  QStringLiteral("--request"), m_paddleRequestPath,
                  QStringLiteral("--response"), m_paddleResponsePath});
    return true;
}

void SubtitleOcrController::onRecognitionError(QProcess *process, QProcess::ProcessError error)
{
    if (!m_processing || error == QProcess::Crashed) return;
    if (error == QProcess::FailedToStart)
        fail(QStringLiteral("Subtitle OCR worker could not be started."), Operation::RecognizeFrame);
    Q_UNUSED(process)
}

void SubtitleOcrController::onRecognitionFinished(QProcess *process, int exitCode, QProcess::ExitStatus status)
{
    auto worker = std::find_if(m_recognitionWorkers.begin(), m_recognitionWorkers.end(),
                               [process](const RecognitionWorker &candidate) { return candidate.process == process; });
    if (worker == m_recognitionWorkers.end() || !worker->item) return;
    const QSharedPointer<RecognitionItem> item = worker->item;
    worker->item.reset();
    const QByteArray output = process->readAllStandardOutput();
    const QByteArray standardError = process->readAllStandardError();
    if (!m_processing || m_cancelRequested) {
        QFile::remove(item->framePath);
        if (m_cancelRequested) completeCancellation();
        return;
    }
    if (status != QProcess::NormalExit || exitCode != 0) {
        fail(processFailure(QStringLiteral("batch Tesseract recognition"), standardError), Operation::RecognizeFrame);
        return;
    }
    const SubtitleOcrObservation recognized = SubtitleOcrPipeline::parseTesseractTsv(
        output, m_samples.at(item->sampleIndexes.constFirst()));
    item->completed = true;
    item->recognizedText = recognized.text;
    item->recognizedConfidence = recognized.confidence;
    ++m_ocrSuccessCount;
    ++m_recognizedFrameCount;
    for (const int index : item->sampleIndexes) {
        appendObservation({m_samples.at(index), recognized.text, recognized.confidence});
        ++m_completedSampleCount;
    }
    QFile::remove(item->framePath);
    updateForwardProgress();
    pumpRecognitionQueue();
    const bool busy = std::any_of(m_recognitionWorkers.cbegin(), m_recognitionWorkers.cend(),
                                  [](const RecognitionWorker &candidate) { return candidate.item; });
    if (!busy && m_recognitionQueue.isEmpty()) {
        m_chunkStartIndex = m_chunkEndIndex;
        beginNextChunk();
    }
}

void SubtitleOcrController::releaseRecognitionWorkers()
{
    for (RecognitionWorker &worker : m_recognitionWorkers) {
        if (!worker.process) continue;
        if (worker.process->state() != QProcess::NotRunning) {
            worker.process->kill();
            worker.process->waitForFinished(3000);
        }
        worker.process->deleteLater();
        worker.process = nullptr;
        worker.item.reset();
    }
    m_recognitionWorkers.clear();
    m_recognitionQueue.clear();
    m_paddleChunkItems.clear();
    m_paddleRequestPath.clear();
    m_paddleResponsePath.clear();
    m_uniqueFrames.clear();
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
    const QStringList crop = SubtitleOcrPipeline::ffmpegCropArguments(m_roi, m_frameWidth, m_frameHeight);
    if (!media.hasFfmpeg() || crop.isEmpty()) {
        fail(QStringLiteral("Subtitle OCR frame extraction is no longer configured."), Operation::ExtractFrame);
        return;
    }
    const qint64 timestampMs = m_samples.at(m_sampleIndex);
    m_currentFramePath = QDir(m_workspacePath).filePath(
        QStringLiteral("frame-%1.png").arg(m_sampleIndex, 6, 10, QLatin1Char('0')));
    m_currentCrop = SubtitleOcrPipeline::sourceRect(m_roi, m_frameWidth, m_frameHeight);
    setPhase(QStringLiteral("extracting frame %1/%2").arg(m_sampleIndex + 1).arg(m_samples.size()));
    recordFrameExtractionStart(media, timestampMs, m_currentCrop);
    QStringList arguments{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                          QStringLiteral("-ss"), ffmpegTime(timestampMs), QStringLiteral("-i"), m_sourcePath};
    arguments += crop;
    arguments += {QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-y"), m_currentFramePath};
    startProcess(Operation::ExtractFrame, media.ffmpeg, arguments);
}

void SubtitleOcrController::beginRecognition()
{
    ++m_ocrAttemptCount;
    emit runStatisticsChanged();
    if (m_executionRoute == QStringLiteral("colab-gpu")) {
        if (!m_colabRunner || !m_colabSession) {
            fail(QStringLiteral("Colab Subtitle OCR worker is unavailable."));
            return;
        }
        QString routeError;
        if (!m_colabSession->hasVerifiedRoute(kColabSubtitleOcrCapability, m_colabModelId, &routeError)) {
            fail(routeError.isEmpty() ? QStringLiteral("Colab Subtitle OCR route is no longer verified.") : routeError);
            return;
        }
        QFile frame(m_currentFramePath);
        if (!frame.open(QIODevice::ReadOnly)) {
            fail(QStringLiteral("Subtitle OCR frame extraction did not produce a readable crop."));
            return;
        }
        const QByteArray croppedFrame = frame.readAll();
        if (croppedFrame.isEmpty() || croppedFrame.size() > 16 * 1024 * 1024) {
            fail(QStringLiteral("Subtitle OCR crop must be a non-empty PNG smaller than 16 MiB."));
            return;
        }
        setPhase(QStringLiteral("recognizing GPU frame %1/%2").arg(m_sampleIndex + 1).arg(m_samples.size()));
        m_operation = Operation::RecognizeColabFrame;
        if (!QMetaObject::invokeMethod(m_colabRunner, "recognize", Qt::QueuedConnection,
                                       Q_ARG(QUrl, m_colabSession->endpoint()),
                                       Q_ARG(QString, m_colabSession->bearerTokenForRequest()),
                                       Q_ARG(QString, m_colabModelId), Q_ARG(QString, m_ocrLanguage),
                                       Q_ARG(QByteArray, croppedFrame),
                                       Q_ARG(bool, m_colabSession->allowsInsecureLocalhostForTests()))) {
            fail(QStringLiteral("Could not dispatch the cropped frame to Colab Subtitle OCR."));
        }
        return;
    }
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

void SubtitleOcrController::onColabRecognitionFinished(const QString &text, double confidence)
{
    if (!m_processing || m_operation != Operation::RecognizeColabFrame) return;
    m_operation = Operation::None;
    if (m_cancelRequested) {
        completeCancellation();
        return;
    }
    const SubtitleOcrObservation observation{m_samples.at(m_sampleIndex), text.trimmed(),
                                             qBound(0.0, confidence, 1.0)};
    m_previousText = observation.text;
    m_previousConfidence = observation.confidence;
    ++m_ocrSuccessCount;
    appendObservation(observation);
    ++m_sampleIndex;
    setProgress((m_sampleIndex * 100) / m_samples.size(), true);
    beginNextSample();
}

void SubtitleOcrController::onColabRecognitionFailed(const QString &message)
{
    if (!m_processing || m_operation != Operation::RecognizeColabFrame) return;
    if (m_cancelRequested) {
        completeCancellation();
        return;
    }
    fail(message.isEmpty() ? QStringLiteral("Colab Subtitle OCR request failed.") : message);
}

void SubtitleOcrController::onProcessError(QProcess::ProcessError error)
{
    if (!m_processing || error == QProcess::Crashed) return;
    if (m_operation == Operation::ExtractFrame || m_operation == Operation::ExtractChunk) {
        appendDiagnostic(QStringLiteral("frame-extraction-process-error"),
                         QStringLiteral("ffmpeg=%1; output=%2; processError=%3; detail=%4")
                             .arg(m_process.program(), m_currentFramePath)
                             .arg(static_cast<int>(error)).arg(m_process.errorString()));
    }
    if (error == QProcess::FailedToStart) {
        fail(QStringLiteral("Subtitle OCR process could not be started. Check the managed runtime installation."),
             m_operation);
    }
}

void SubtitleOcrController::onFrameExtractionTimeout()
{
    if (!m_processing || (m_operation != Operation::ExtractFrame
                          && m_operation != Operation::ExtractChunk)) return;
    appendDiagnostic(QStringLiteral("frame-extraction-timeout"),
                     QStringLiteral("ffmpeg=%1; output=%2; timeoutMs=%3; state=%4")
                         .arg(m_process.program(), m_currentFramePath)
                         .arg(frameExtractionTimeoutMs())
                         .arg(static_cast<int>(m_process.state())));
    m_frameExtractionTimedOut = true;
    if (m_process.state() != QProcess::NotRunning) m_process.kill();
}

void SubtitleOcrController::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_processing) return;
    const QByteArray output = m_process.readAllStandardOutput();
    const QByteArray standardError = m_process.readAllStandardError();
    const Operation operation = m_operation;
    if (operation == Operation::ExtractFrame || operation == Operation::ExtractChunk)
        m_frameExtractionTimeout.stop();
    m_operation = Operation::None;
    if (operation == Operation::ExtractFrame || operation == Operation::ExtractChunk) {
        appendDiagnostic(QStringLiteral("frame-extraction-exit"),
                         QStringLiteral("ffmpeg=%1; output=%2; exitCode=%3; exitStatus=%4; stderr=%5")
                             .arg(m_process.program(), m_currentFramePath).arg(exitCode)
                             .arg(status == QProcess::NormalExit ? QStringLiteral("normal")
                                                                  : QStringLiteral("crashed"))
                             .arg(boundedDiagnosticText(QString::fromUtf8(standardError))));
    }
    if (operation == Operation::Probe) {
        appendDiagnostic(QStringLiteral("probe-exit"),
                         QStringLiteral("ffprobe=%1; exitCode=%2; exitStatus=%3; stderr=%4")
                             .arg(m_process.program()).arg(exitCode)
                             .arg(status == QProcess::NormalExit ? QStringLiteral("normal")
                                                                  : QStringLiteral("crashed"))
                             .arg(boundedDiagnosticText(QString::fromUtf8(standardError))));
    }
    if (m_cancelRequested) {
        completeCancellation();
        return;
    }
    if ((operation == Operation::ExtractFrame || operation == Operation::ExtractChunk)
        && m_frameExtractionTimedOut) {
        m_frameExtractionTimedOut = false;
        fail(QStringLiteral("Subtitle OCR frame extraction timed out. Use Retry frame extraction or Open diagnostics."),
             operation);
        return;
    }
    if (status != QProcess::NormalExit || exitCode != 0) {
        const QString stage = operation == Operation::Probe ? QStringLiteral("video probe")
            : operation == Operation::CropPreview ? QStringLiteral("crop preview")
            : operation == Operation::VerifyLanguage ? QStringLiteral("Tesseract language check")
            : operation == Operation::VerifyPaddleRuntime ? QStringLiteral("PaddleOCR runtime health check")
            : (operation == Operation::ExtractFrame || operation == Operation::ExtractChunk)
                ? QStringLiteral("frame extraction")
            : operation == Operation::RecognizePaddleChunk ? QStringLiteral("PaddleOCR batch recognition")
            : QStringLiteral("Tesseract recognition");
        fail(processFailure(stage, standardError), operation);
        return;
    }
    if (operation == Operation::VerifyPaddleRuntime) {
        QString healthError;
        if (!parsePaddleHealth(output, &healthError)) {
            fail(QStringLiteral("PaddleOCR runtime health check failed: %1").arg(healthError), operation);
            return;
        }
        appendDiagnostic(QStringLiteral("paddle-runtime-health"),
                         QStringLiteral("engine=%1; version=%2; result=passed")
                             .arg(PaddleOcrRuntimeLocator::engineId(), PaddleOcrRuntimeLocator::engineVersion()));
        beginOcrSamples();
        return;
    }
    if (operation == Operation::RecognizePaddleChunk) {
        QFile response(m_paddleResponsePath);
        const QJsonDocument responseDocument = response.open(QIODevice::ReadOnly)
            ? QJsonDocument::fromJson(response.readAll()) : QJsonDocument();
        const QJsonObject root = responseDocument.object();
        if (!responseDocument.isObject() || root.value(QStringLiteral("schemaVersion")).toInt() != 1
            || root.value(QStringLiteral("engineId")).toString()
                   != QString::fromLatin1(PaddleOcrRuntimeLocator::engineId())
            || root.value(QStringLiteral("engineVersion")).toString()
                   != QString::fromLatin1(PaddleOcrRuntimeLocator::engineVersion())
            || !root.value(QStringLiteral("manifestVerified")).toBool()) {
            fail(QStringLiteral("PaddleOCR returned an invalid batch response."), operation);
            return;
        }
        const QJsonObject telemetry = root.value(QStringLiteral("telemetry")).toObject();
        const double cpuSeconds = telemetry.value(QStringLiteral("cpuSeconds")).toDouble(-1.0);
        const qint64 peakWorkingSet = telemetry.value(QStringLiteral("peakWorkingSetBytes")).toVariant().toLongLong();
        if (telemetry.isEmpty() || !std::isfinite(cpuSeconds) || cpuSeconds < 0.0 || peakWorkingSet < 0) {
            fail(QStringLiteral("PaddleOCR batch response is missing valid runtime telemetry."), operation);
            return;
        }
        m_paddleCpuSeconds += cpuSeconds;
        m_paddlePeakWorkingSetBytes = qMax(m_paddlePeakWorkingSetBytes, peakWorkingSet);
        QHash<QByteArray, SubtitleOcrObservation> results;
        for (const QJsonValue &value : root.value(QStringLiteral("results")).toArray()) {
            const QJsonObject item = value.toObject();
            const QByteArray hash = QByteArray::fromHex(item.value(QStringLiteral("hash")).toString().toLatin1());
            const QString text = item.value(QStringLiteral("text")).toString().trimmed();
            const double confidence = item.value(QStringLiteral("confidence")).toDouble(-1.0);
            if (hash.isEmpty() || results.contains(hash) || !std::isfinite(confidence)
                || confidence < 0.0 || confidence > 1.0) {
                fail(QStringLiteral("PaddleOCR returned an invalid recognition result."), operation);
                return;
            }
            results.insert(hash, {0, text, confidence});
        }
        for (const QSharedPointer<RecognitionItem> &item : m_paddleChunkItems) {
            const auto result = results.constFind(item->frameHash);
            if (result == results.cend()) {
                fail(QStringLiteral("PaddleOCR did not return every requested cropped frame."), operation);
                return;
            }
            item->completed = true;
            item->recognizedText = result->text;
            item->recognizedConfidence = result->confidence;
            ++m_ocrSuccessCount;
            ++m_recognizedFrameCount;
            for (const int index : item->sampleIndexes) {
                appendObservation({m_samples.at(index), item->recognizedText, item->recognizedConfidence});
                ++m_completedSampleCount;
            }
            QFile::remove(item->framePath);
        }
        appendDiagnostic(QStringLiteral("paddle-batch-complete"),
                         QStringLiteral("frames=%1; engine=%2; version=%3")
                             .arg(m_paddleChunkItems.size()).arg(PaddleOcrRuntimeLocator::engineId(),
                                 PaddleOcrRuntimeLocator::engineVersion()));
        m_paddleChunkItems.clear();
        updateForwardProgress();
        m_chunkStartIndex = m_chunkEndIndex;
        beginNextChunk();
        return;
    }
    if (operation == Operation::Probe) {
        completeProbe(output);
        return;
    }
    if (operation == Operation::CropPreview) {
        if (!QFileInfo(m_cropPreviewPath).isFile() || QFileInfo(m_cropPreviewPath).size() <= 0) {
            fail(QStringLiteral("Subtitle OCR crop preview did not produce an image."), operation);
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
                 .arg(missingLanguages.join(QStringLiteral(", "))), operation);
            return;
        }
        beginOcrSamples();
        return;
    }
    if (operation == Operation::ExtractFrame) {
        QByteArray hash;
        QString validationError;
        if (!validateCurrentFrame(&hash, &validationError)) {
            fail(QStringLiteral("Subtitle OCR frame extraction did not produce a readable PNG crop: %1. "
                                "Use Retry frame extraction or Open diagnostics.")
                     .arg(validationError), operation);
            return;
        }
        ++m_readableCropCount;
        emit runStatisticsChanged();
        if (!m_previousFrameHash.isEmpty() && hash == m_previousFrameHash) {
            appendObservation({m_samples.at(m_sampleIndex), m_previousText, m_previousConfidence});
            ++m_sampleIndex;
            setProgress(qRound(100.0 * m_sampleIndex / m_samples.size()), true);
            beginNextSample();
            return;
        }
        m_previousFrameHash = hash;
        beginRecognition();
        return;
    }
    if (operation == Operation::ExtractChunk) {
        updateForwardProgress();
        queueChunkFrames();
        return;
    }
    if (operation == Operation::RecognizeFrame) {
        const SubtitleOcrObservation observation = SubtitleOcrPipeline::parseTesseractTsv(
            output, m_samples.at(m_sampleIndex));
        ++m_ocrSuccessCount;
        m_previousText = observation.text;
        m_previousConfidence = observation.confidence;
        appendObservation(observation);
        ++m_sampleIndex;
        setProgress(qRound(100.0 * m_sampleIndex / m_samples.size()), true);
        beginNextSample();
    }
}

void SubtitleOcrController::completeRun()
{
    m_forwardProgressTimer.stop();
    QVector<SubtitleOcrSegment> merged = SubtitleOcrPipeline::mergeObservations(
        m_observations, m_sampleIntervalMs, m_minimumConfidence);
    for (SubtitleOcrSegment &segment : merged) {
        segment.endMs = qMin(segment.endMs, m_durationMs);
    }
    QString validationError;
    const bool requireHanText = m_ocrLanguage.compare(QStringLiteral("chi_sim"), Qt::CaseInsensitive) == 0;
    if (!SubtitleOcrPipeline::validatePublishableSegments(merged, requireHanText, &validationError)) {
        m_segments.clear();
        m_publishedSegmentCount = 0;
        m_exportedSegmentCount = 0;
        emit segmentsChanged();
        emit runStatisticsChanged();
        appendDiagnostic(QStringLiteral("result-validation"),
                         QStringLiteral("status=no_text_detected; scheduled=%1; readableCrops=%2; "
                                        "ocrSuccesses=%3; rawNonEmpty=%4; filterCandidates=%5; "
                                        "publishedSegments=0; detail=%6")
                             .arg(m_scheduledSampleCount).arg(m_readableCropCount)
                             .arg(m_ocrSuccessCount).arg(m_nonEmptyRawResultCount)
                             .arg(m_filterCandidateCount).arg(validationError));
        fail(QStringLiteral("no_text_detected: %1").arg(validationError),
             Operation::RecognizeFrame, QStringLiteral("no_text_detected"));
        return;
    }
    m_segments = segmentsToVariant(merged);
    m_publishedSegmentCount = m_segments.size();
    m_exportedSegmentCount = 0;
    emit runStatisticsChanged();
    if (!storeCachedResult()) {
        appendDiagnostic(QStringLiteral("result-cache-write-failed"),
                         QStringLiteral("key=%1").arg(m_cacheKey));
    }
    releaseActiveCacheKey();
    releaseRecognitionWorkers();
    cleanWorkspace();
    setProcessing(false);
    setProgress(100, true);
    setPhase(QStringLiteral("completed"));
    setResultStatus(QStringLiteral("completed"));
    setError({});
    emit segmentsChanged();
}

void SubtitleOcrController::completeCancellation()
{
    m_frameExtractionTimeout.stop();
    m_forwardProgressTimer.stop();
    m_frameExtractionTimedOut = false;
    m_cancelRequested = false;
    m_operation = Operation::None;
    m_pendingSourcePath.clear();
    releaseRecognitionWorkers();
    releaseActiveCacheKey();
    cleanWorkspace();
    setProcessing(false);
    setProgress(0, false);
    setPhase(QStringLiteral("canceled"));
    setResultStatus(QStringLiteral("canceled"));
    setError({});
}

void SubtitleOcrController::fail(const QString &message, Operation failedOperation,
                                 const QString &resultStatus)
{
    const Operation recordedOperation = failedOperation == Operation::None
        ? m_operation : failedOperation;
    m_frameExtractionTimeout.stop();
    m_forwardProgressTimer.stop();
    m_frameExtractionTimedOut = false;
    if (m_process.state() != QProcess::NotRunning) m_process.kill();
    releaseRecognitionWorkers();
    releaseActiveCacheKey();
    if (m_operation == Operation::RecognizeColabFrame && m_colabRunner)
        QMetaObject::invokeMethod(m_colabRunner, "cancel", Qt::QueuedConnection);
    m_operation = Operation::None;
    m_cancelRequested = false;
    m_pendingSourcePath.clear();
    m_lastFailedOperation = recordedOperation;
    if (!m_segments.isEmpty()) {
        m_segments.clear();
        emit segmentsChanged();
    }
    m_publishedSegmentCount = 0;
    m_exportedSegmentCount = 0;
    emit runStatisticsChanged();
    cleanWorkspace(recordedOperation == Operation::ExtractFrame
                   || recordedOperation == Operation::ExtractChunk);
    setProcessing(false);
    setProgress(0, false);
    setPhase(QStringLiteral("error"));
    setResultStatus(resultStatus);
    setError(message);
    emit frameRetryChanged();
}

void SubtitleOcrController::cancel()
{
    if (!m_processing) return;
    m_cancelRequested = true;
    const bool primaryProcessRunning = m_process.state() != QProcess::NotRunning;
    const Operation operation = m_operation;
    if (primaryProcessRunning) m_process.kill();
    releaseRecognitionWorkers();
    if (primaryProcessRunning) return;
    if (operation == Operation::RecognizeColabFrame && m_colabRunner)
        QMetaObject::invokeMethod(m_colabRunner, "cancel", Qt::QueuedConnection);
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

bool SubtitleOcrController::exportSrt(const QString &path)
{
    QString error;
    const QVector<SubtitleOcrSegment> parsed = segmentsFromVariant(m_segments, &error);
    if (m_resultStatus != QStringLiteral("completed") || parsed.isEmpty()) {
        if (m_error.isEmpty()) {
            setError(QStringLiteral("Subtitle OCR has no published transcript segments to export."));
        }
        return false;
    }
    if (!writeTextFile(path, SubtitleOcrPipeline::toSrt(parsed))) {
        setError(QStringLiteral("Cannot write the Subtitle OCR SRT export."));
        return false;
    }
    m_exportedSegmentCount = parsed.size();
    emit runStatisticsChanged();
    return true;
}

bool SubtitleOcrController::exportText(const QString &path)
{
    QString error;
    const QVector<SubtitleOcrSegment> parsed = segmentsFromVariant(m_segments, &error);
    if (m_resultStatus != QStringLiteral("completed") || parsed.isEmpty()) {
        if (m_error.isEmpty()) {
            setError(QStringLiteral("Subtitle OCR has no published transcript segments to export."));
        }
        return false;
    }
    QStringList lines;
    for (const SubtitleOcrSegment &segment : parsed) lines.append(segment.text);
    if (lines.isEmpty() || !writeTextFile(path, lines.join(QStringLiteral("\n\n")) + QLatin1Char('\n'))) {
        setError(QStringLiteral("Cannot write the Subtitle OCR text export."));
        return false;
    }
    m_exportedSegmentCount = parsed.size();
    emit runStatisticsChanged();
    return true;
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
                        {QStringLiteral("frameWidth"), m_frameWidth},
                        {QStringLiteral("frameHeight"), m_frameHeight},
                        {QStringLiteral("rotationDegrees"), m_rotationDegrees},
                        {QStringLiteral("sampleAspectRatio"), m_sampleAspectRatio},
                        {QStringLiteral("displayAspectRatio"), m_displayAspectRatio},
                        {QStringLiteral("durationMs"), static_cast<double>(m_durationMs)},
                        {QStringLiteral("roi"), QJsonObject{{QStringLiteral("x"), m_roi.x},
                                                            {QStringLiteral("y"), m_roi.y},
                                                            {QStringLiteral("width"), m_roi.width},
                                                            {QStringLiteral("height"), m_roi.height}}},
                        {QStringLiteral("ocrLanguage"), m_ocrLanguage},
                        {QStringLiteral("executionRoute"), m_executionRoute},
                        {QStringLiteral("localEngineId"), m_localEngineId},
                        {QStringLiteral("localEngineVersion"), localEngineVersion()},
                        {QStringLiteral("colabModelId"), m_colabModelId},
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
    const int frameWidth = project.value(QStringLiteral("frameWidth"), width).toInt();
    const int frameHeight = project.value(QStringLiteral("frameHeight"), height).toInt();
    const int rotation = normalizedRotation(project.value(QStringLiteral("rotationDegrees")).toInt());
    const QString sampleAspectRatio = project.value(QStringLiteral("sampleAspectRatio")).toString();
    const QString displayAspectRatio = project.value(QStringLiteral("displayAspectRatio")).toString();
    const qint64 duration = project.value(QStringLiteral("durationMs")).toLongLong();
    const QString language = project.value(QStringLiteral("ocrLanguage")).toString().trimmed();
    const QString executionRoute = project.value(QStringLiteral("executionRoute"),
        QStringLiteral("local-cpu")).toString().trimmed().toLower();
    // Version 1 projects predate PaddleOCR.  Preserve their existing local
    // behaviour by migrating them explicitly to the named Tesseract baseline,
    // never by guessing a model or silently changing an old transcript route.
    const QString localEngine = version == 1 ? QStringLiteral("tesseract-baseline")
        : normalizedLocalEngineId(project.value(QStringLiteral("localEngineId")).toString());
    const QString localEngineVersion = project.value(QStringLiteral("localEngineVersion")).toString().trimmed();
    const QString colabModelId = project.value(QStringLiteral("colabModelId"),
        kColabSubtitleOcrModel).toString().trimmed().toLower();
    const qint64 interval = project.value(QStringLiteral("sampleIntervalMs")).toLongLong();
    const double confidence = project.value(QStringLiteral("minimumConfidence")).toDouble();
    QString segmentsError;
    const QVector<SubtitleOcrSegment> parsed = segmentsFromVariant(
        project.value(QStringLiteral("segments")).toList(), &segmentsError);
    if ((version != 1 && version != kSubtitleOcrProjectVersion) || source.isEmpty() || !QFileInfo(source).isFile()
        || width <= 0 || height <= 0 || frameWidth <= 0 || frameHeight <= 0 || duration <= 0 || !roi.isValid() || language.isEmpty()
        || interval < 100 || interval > 30000 || confidence < 0.0 || confidence > 1.0
        || (executionRoute != QStringLiteral("local-cpu") && executionRoute != QStringLiteral("colab-gpu"))
        || localEngine.isEmpty()
        || (version == kSubtitleOcrProjectVersion && localEngineVersion != QStringLiteral("5.5.1")
            && localEngineVersion != QString::fromLatin1(PaddleOcrRuntimeLocator::engineVersion()))
        || colabModelId != kColabSubtitleOcrModel
        || (!project.value(QStringLiteral("segments")).toList().isEmpty() && parsed.isEmpty())) {
        setError(segmentsError.isEmpty() ? QStringLiteral("Invalid or incompatible Subtitle OCR project.") : segmentsError);
        return false;
    }
    cleanWorkspace();
    m_sourcePath = QFileInfo(source).absoluteFilePath();
    m_sourceWidth = width;
    m_sourceHeight = height;
    m_frameWidth = frameWidth;
    m_frameHeight = frameHeight;
    m_rotationDegrees = rotation;
    m_sampleAspectRatio = sampleAspectRatio;
    m_displayAspectRatio = displayAspectRatio;
    m_durationMs = duration;
    m_roi = roi;
    m_ocrLanguage = language;
    m_executionRoute = executionRoute;
    m_localEngineId = localEngine;
    m_colabModelId = colabModelId;
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
    emit colabRouteChanged();
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
