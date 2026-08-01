#include "OcrE2ERunner.h"

#include "controllers/dubbing/DubbingController.h"
#include "controllers/subtitles/SubtitleOcrController.h"
#include "dubbing/DubbingTranscriptFusionService.h"
#include "subtitles/PaddleOcrRuntimeLocator.h"
#include "subtitles/SrtTimelineParser.h"
#include "subtitles/SubtitleOcrPipeline.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>

#include <functional>
#include <iostream>

namespace LAStudio {
namespace {

constexpr qint64 kOcrTimeoutMs = 15LL * 60LL * 1000LL;
const QString kPaddleUpstreamRepository = QStringLiteral("https://github.com/PaddlePaddle/PaddleOCR");
const QString kPaddleUpstreamCommit = QStringLiteral("2661c7c0ef5c613e8f93c6e93b2e052399f0f854");
const QString kPaddleModel = QStringLiteral("PP-OCRv6_tiny_det+PP-OCRv6_tiny_rec");

struct Arguments {
    QString inputPath;
    QString outputRoot;
    QString paddlePythonPath;
    QString paddleWorkerPath;
    QString paddleCachePath;
    QString paddleManifestPath;
    QString ffmpegPath;
    QString ffprobePath;
    int benchmarkSamples = 0;
    QString error;
};

struct TranscriptCheck {
    int segmentCount = 0;
    QString error;
};

struct ArtifactArguments {
    QString inputPath;
    QString outputRoot;
    qint64 elapsedMs = 0;
    QString error;
};

QString sha256File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash digest(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFile::NoError) return {};
        digest.addData(bytes);
    }
    return QString::fromLatin1(digest.result().toHex());
}

QString optionValue(int argc, char *argv[], const QString &name)
{
    for (int index = 1; index + 1 < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == name)
            return QString::fromLocal8Bit(argv[index + 1]).trimmed();
    }
    return {};
}

Arguments parseArguments(int argc, char *argv[])
{
    Arguments arguments;
    arguments.inputPath = QFileInfo(optionValue(argc, argv, QStringLiteral("--input"))).absoluteFilePath();
    arguments.outputRoot = QFileInfo(optionValue(argc, argv, QStringLiteral("--output-root"))).absoluteFilePath();
    arguments.paddlePythonPath = QFileInfo(optionValue(argc, argv, QStringLiteral("--paddle-python"))).absoluteFilePath();
    arguments.paddleWorkerPath = QFileInfo(optionValue(argc, argv, QStringLiteral("--paddle-worker"))).absoluteFilePath();
    arguments.paddleCachePath = QFileInfo(optionValue(argc, argv, QStringLiteral("--paddle-cache"))).absoluteFilePath();
    arguments.paddleManifestPath = QFileInfo(optionValue(argc, argv, QStringLiteral("--paddle-manifest"))).absoluteFilePath();
    arguments.ffmpegPath = QFileInfo(optionValue(argc, argv, QStringLiteral("--ffmpeg"))).absoluteFilePath();
    arguments.ffprobePath = QFileInfo(optionValue(argc, argv, QStringLiteral("--ffprobe"))).absoluteFilePath();
    bool benchmarkOk = false;
    const QString benchmarkValue = optionValue(argc, argv, QStringLiteral("--benchmark-samples"));
    if (!benchmarkValue.isEmpty()) {
        arguments.benchmarkSamples = benchmarkValue.toInt(&benchmarkOk);
        if (!benchmarkOk || arguments.benchmarkSamples < 1 || arguments.benchmarkSamples > 100)
            arguments.error = QStringLiteral("--benchmark-samples must be between 1 and 100.");
    }
    if (arguments.inputPath.isEmpty() || !QFileInfo(arguments.inputPath).isFile()) {
        arguments.error = QStringLiteral("--input must name a readable source video.");
    } else if (arguments.outputRoot.isEmpty()
               || QFileInfo(arguments.outputRoot).fileName() != QStringLiteral("ocr-e2e-new")) {
        arguments.error = QStringLiteral("--output-root must be the stable out/ocr-e2e-new directory.");
    } else if (!QFileInfo(arguments.paddlePythonPath).isFile()
               || !QFileInfo(arguments.paddleWorkerPath).isFile()
               || !QFileInfo(arguments.paddleCachePath).isDir()
               || !QFileInfo(arguments.paddleManifestPath).isFile()
               || !QFileInfo(arguments.ffmpegPath).isFile()
               || !QFileInfo(arguments.ffprobePath).isFile()) {
        arguments.error = QStringLiteral("--paddle-python, --paddle-worker, --paddle-cache, --paddle-manifest, --ffmpeg and --ffprobe must name readable production runtime paths.");
    }
    return arguments;
}

ArtifactArguments parseArtifactArguments(int argc, char *argv[])
{
    ArtifactArguments arguments;
    arguments.inputPath = QFileInfo(optionValue(argc, argv, QStringLiteral("--input"))).absoluteFilePath();
    arguments.outputRoot = QFileInfo(optionValue(argc, argv, QStringLiteral("--output-root"))).absoluteFilePath();
    bool elapsedOk = false;
    arguments.elapsedMs = optionValue(argc, argv, QStringLiteral("--elapsed-ms")).toLongLong(&elapsedOk);
    if (!QFileInfo(arguments.inputPath).isFile()) {
        arguments.error = QStringLiteral("--input must name the already-recognized source video.");
    } else if (QFileInfo(arguments.outputRoot).fileName() != QStringLiteral("ocr-e2e-new")) {
        arguments.error = QStringLiteral("--output-root must be the stable out/ocr-e2e-new directory.");
    } else if (!elapsedOk || arguments.elapsedMs <= 0) {
        arguments.error = QStringLiteral("--elapsed-ms must preserve the positive elapsed time from the completed OCR E2E JSON.");
    }
    return arguments;
}

bool waitFor(const std::function<bool()> &condition, qint64 timeoutMs, QString *error,
             const QString &description)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() >= timeoutMs) {
            if (error) *error = QStringLiteral("Timed out while waiting for %1 after %2 ms.")
                                    .arg(description).arg(timeoutMs);
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return true;
}

bool configureChineseOcr(SubtitleOcrController &controller, QString *error)
{
    if (!controller.setExecutionRoute(QStringLiteral("local-cpu"))
        || !controller.setLocalEngine(QString::fromLatin1(PaddleOcrRuntimeLocator::engineId()))
        || !controller.setOcrLanguage(QStringLiteral("chi_sim"))
        || !controller.setRoi(0.009, 0.883, 0.976, 0.096)
        || !controller.setSampleIntervalMs(800)
        || !controller.setMinimumConfidence(0.50)) {
        if (error) *error = controller.error();
        return false;
    }
    return true;
}

TranscriptCheck validateTranscriptFile(const QString &path, int expectedCount = -1)
{
    TranscriptCheck check;
    const QFileInfo info(path);
    if (!info.isFile() || info.size() <= 0) {
        check.error = QStringLiteral("Transcript file is missing or empty: %1").arg(path);
        return check;
    }
    const SubtitleParseResult parsed = SrtTimelineParser::parseFile(path);
    if (!parsed.ok || parsed.cues.isEmpty()) {
        check.error = QStringLiteral("Production SRT parser rejected %1: %2").arg(path, parsed.error);
        return check;
    }
    if (expectedCount >= 0 && parsed.cues.size() != expectedCount) {
        check.error = QStringLiteral("SRT cue count %1 does not match published segment count %2.")
                          .arg(parsed.cues.size()).arg(expectedCount);
        return check;
    }
    qint64 previousStartMs = -1;
    bool containsHan = false;
    for (const TimedTextCue &cue : parsed.cues) {
        if (cue.text.trimmed().isEmpty() || cue.endMs <= cue.startMs || cue.startMs <= previousStartMs) {
            check.error = QStringLiteral("SRT contains a blank or non-increasing timestamped cue.");
            return check;
        }
        previousStartMs = cue.startMs;
        containsHan = containsHan || SubtitleOcrPipeline::containsHanText(cue.text);
    }
    if (!containsHan) {
        check.error = QStringLiteral("chi_sim transcript did not contain a Unicode Han character.");
        return check;
    }
    check.segmentCount = parsed.cues.size();
    return check;
}

QString transcriptTimestamp(qint64 milliseconds)
{
    const qint64 hours = milliseconds / 3600000;
    const qint64 minutes = (milliseconds / 60000) % 60;
    const qint64 seconds = (milliseconds / 1000) % 60;
    const qint64 remainder = milliseconds % 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(remainder, 3, 10, QLatin1Char('0'));
}

bool writeUtf8File(const QString &path, const QString &content, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Cannot write %1.").arg(path);
        return false;
    }
    if (file.write(content.toUtf8()) < 0 || !file.commit()) {
        if (error) *error = QStringLiteral("Cannot commit %1.").arg(path);
        return false;
    }
    return true;
}

bool combinedRoutePreservesActualOcr(const QVariantList &ocrSegments, QString *error)
{
    const QVariantList normalized = DubbingTranscriptFusionService::normalizeOcrSegments(ocrSegments);
    if (normalized.isEmpty()) {
        if (error) *error = QStringLiteral("Production Dubbing OCR normalization discarded every OCR segment.");
        return false;
    }
    QVariantList sttSegments;
    for (const QVariant &value : normalized) {
        const QVariantMap ocr = value.toMap();
        sttSegments.append(QVariantMap{{QStringLiteral("id"), ocr.value(QStringLiteral("id"))},
                                       {QStringLiteral("startMs"), ocr.value(QStringLiteral("startMs"))},
                                       {QStringLiteral("endMs"), ocr.value(QStringLiteral("endMs"))},
                                       {QStringLiteral("sourceText"), QStringLiteral("STT evidence")},
                                       {QStringLiteral("sttConfidence"), 0.50}});
    }
    const QVariantList fused = DubbingTranscriptFusionService::fuse(sttSegments, normalized);
    if (fused.size() != normalized.size()) {
        if (error) *error = QStringLiteral("STT+OCR fusion did not retain each actual OCR segment.");
        return false;
    }
    for (int index = 0; index < fused.size(); ++index) {
        const QVariantMap result = fused.at(index).toMap();
        const QString actualOcr = normalized.at(index).toMap().value(QStringLiteral("sourceText")).toString();
        if (result.value(QStringLiteral("fusionStatus")).toString() == QStringLiteral("stt-only")
            || result.value(QStringLiteral("ocrText")).toString() != actualOcr) {
            if (error) *error = QStringLiteral("STT+OCR fusion silently lost an actual OCR segment.");
            return false;
        }
    }
    return true;
}

QJsonObject baseJson(bool ok, const QString &error)
{
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("error"), error}};
}

int finish(const QJsonObject &result, int exitCode)
{
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << "\n";
    return exitCode;
}

} // namespace

bool isOcrE2EInvocation(int argc, char *argv[])
{
    for (int index = 1; index < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == QStringLiteral("--ocr-e2e")) return true;
    }
    return false;
}

bool isOcrE2EArtifactReportInvocation(int argc, char *argv[])
{
    for (int index = 1; index < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == QStringLiteral("--ocr-e2e-artifact-report")) return true;
    }
    return false;
}

int runOcrE2EArtifactReport(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LAStudioOcrE2EArtifactReport"));
    const ArtifactArguments arguments = parseArtifactArguments(argc, argv);
    if (!arguments.error.isEmpty()) return finish(baseJson(false, arguments.error), 2);

    const QString inputSha256 = sha256File(arguments.inputPath);
    if (inputSha256.isEmpty()) return finish(baseJson(false, QStringLiteral("Cannot hash OCR input.")), 1);
    const QString standaloneSrt = QDir(arguments.outputRoot).filePath(QStringLiteral("standalone-zh-Hans.srt"));
    const QString dubbingSrt = QDir(arguments.outputRoot).filePath(QStringLiteral("dubbing-zh-Hans.srt"));
    const QString transcriptPath = QDir(arguments.outputRoot).filePath(QStringLiteral("transcript-zh-Hans.txt"));
    const QString resultPath = QDir(arguments.outputRoot).filePath(QStringLiteral("OCR_TEST_RESULT.md"));
    const TranscriptCheck standalone = validateTranscriptFile(standaloneSrt);
    const TranscriptCheck dubbing = validateTranscriptFile(dubbingSrt);
    if (!standalone.error.isEmpty() || !dubbing.error.isEmpty()) {
        return finish(baseJson(false, !standalone.error.isEmpty() ? standalone.error : dubbing.error), 1);
    }
    if (standalone.segmentCount != dubbing.segmentCount) {
        return finish(baseJson(false, QStringLiteral("Standalone and Dubbing transcript cue counts differ.")), 1);
    }
    const SubtitleParseResult parsed = SrtTimelineParser::parseFile(standaloneSrt);
    QStringList transcriptLines;
    transcriptLines.reserve(parsed.cues.size());
    for (const TimedTextCue &cue : parsed.cues) {
        const QString text = cue.text.simplified();
        if (text.isEmpty()) return finish(baseJson(false, QStringLiteral("Standalone SRT contains blank cue text.")), 1);
        transcriptLines.append(QStringLiteral("[%1 --> %2] %3")
                                   .arg(transcriptTimestamp(cue.startMs), transcriptTimestamp(cue.endMs), text));
    }
    QString error;
    if (!writeUtf8File(transcriptPath, transcriptLines.join(QLatin1Char('\n')) + QLatin1Char('\n'), &error)) {
        return finish(baseJson(false, error), 1);
    }
    QFile transcriptFile(transcriptPath);
    if (!transcriptFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return finish(baseJson(false, QStringLiteral("Cannot read generated transcript TXT.")), 1);
    }
    const QString transcriptText = QString::fromUtf8(transcriptFile.readAll());
    if (transcriptText.trimmed().isEmpty() || !SubtitleOcrPipeline::containsHanText(transcriptText)
        || !transcriptText.contains(QRegularExpression(
               QStringLiteral("\\[\\d{2}:\\d{2}:\\d{2}\\.\\d{3} --> \\d{2}:\\d{2}:\\d{2}\\.\\d{3}\\]")))) {
        return finish(baseJson(false, QStringLiteral("Generated transcript TXT is missing Han text or a valid timestamp.")), 1);
    }
    const QString report = QStringLiteral(
        "# OCR E2E result\n\n"
        "- Status: PASS\n"
        "- Input: %1\n"
        "- Input SHA-256: `%2`\n"
        "- Engine/model: PaddleOCR 3.7.0 · PP-OCRv6 tiny · Simplified Chinese\n"
        "- Upstream commit: `%3`\n"
        "- Elapsed: %4 ms\n"
        "- Segments: %5 (Standalone and Dubbing match; Dubbing reused the completed OCR artifact.)\n"
        "- Standalone SRT: %6\n"
        "- Dubbing SRT: %7\n"
        "- Transcript TXT: %8\n")
        .arg(arguments.inputPath, inputSha256, kPaddleUpstreamCommit)
        .arg(arguments.elapsedMs).arg(standalone.segmentCount)
        .arg(standaloneSrt, dubbingSrt, transcriptPath);
    if (!writeUtf8File(resultPath, report, &error)) return finish(baseJson(false, error), 1);
    const bool artifactsValid = QFileInfo(transcriptPath).size() > 0 && QFileInfo(resultPath).size() > 0;
    QJsonObject result{{QStringLiteral("ok"), artifactsValid},
                       {QStringLiteral("inputSha256"), inputSha256},
                       {QStringLiteral("engineId"), QString::fromLatin1(PaddleOcrRuntimeLocator::engineId())},
                       {QStringLiteral("model"), kPaddleModel},
                       {QStringLiteral("elapsedMs"), arguments.elapsedMs},
                       {QStringLiteral("segmentCount"), standalone.segmentCount},
                       {QStringLiteral("standaloneTranscriptPath"), standaloneSrt},
                       {QStringLiteral("dubbingTranscriptPath"), dubbingSrt},
                       {QStringLiteral("transcriptPath"), transcriptPath},
                       {QStringLiteral("resultPath"), resultPath}};
    if (!artifactsValid) result.insert(QStringLiteral("error"), QStringLiteral("Artifact report output is empty."));
    return finish(result, artifactsValid ? 0 : 1);
}

int runOcrE2E(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LAStudioOcrE2E"));
    app.setOrganizationName(QStringLiteral("LAStudio"));

    const Arguments arguments = parseArguments(argc, argv);
    if (!arguments.error.isEmpty()) return finish(baseJson(false, arguments.error), 2);

    if (!QDir().mkpath(arguments.outputRoot)) {
        return finish(baseJson(false, QStringLiteral("Cannot create E2E output directory.")), 1);
    }
    const QString dataRoot = QDir(arguments.outputRoot).filePath(QStringLiteral("runtime-data"));
    if (!QDir().mkpath(dataRoot)) {
        return finish(baseJson(false, QStringLiteral("Cannot create isolated E2E runtime directory.")), 1);
    }
    qputenv("LASTUDIO_DATA_DIR", dataRoot.toUtf8());
    qputenv("LASTUDIO_FFMPEG", arguments.ffmpegPath.toUtf8());
    qputenv("LASTUDIO_FFPROBE", arguments.ffprobePath.toUtf8());
    qputenv("LASTUDIO_SUBTITLE_OCR_ENGINE", PaddleOcrRuntimeLocator::engineId());
    qputenv("LASTUDIO_PADDLE_PYTHON", arguments.paddlePythonPath.toUtf8());
    qputenv("LASTUDIO_PADDLE_WORKER", arguments.paddleWorkerPath.toUtf8());
    qputenv("LASTUDIO_PADDLE_CACHE", arguments.paddleCachePath.toUtf8());
    qputenv("LASTUDIO_PADDLE_MANIFEST", arguments.paddleManifestPath.toUtf8());
    // The acceptance run must prove the upstream adapter rather than inherit
    // a developer's Tesseract installation as an accidental fallback.
    qunsetenv("LASTUDIO_TESSERACT");
    qunsetenv("TESSDATA_PREFIX");

    const QString inputSha256 = sha256File(arguments.inputPath);
    if (inputSha256.isEmpty()) {
        return finish(baseJson(false, QStringLiteral("Cannot calculate SHA-256 for the source video.")), 1);
    }
    const QString standaloneSrt = QDir(arguments.outputRoot).filePath(QStringLiteral("standalone-zh-Hans.srt"));
    const QString dubbingSrt = QDir(arguments.outputRoot).filePath(QStringLiteral("dubbing-zh-Hans.srt"));
    const QString dubbingProject = QDir(arguments.outputRoot).filePath(QStringLiteral("dubbing-zh-Hans.ladub.json"));
    QFile::remove(standaloneSrt);
    QFile::remove(dubbingSrt);
    QFile::remove(dubbingProject);

    int standaloneCount = 0;
    int dubbingCount = 0;
    QVariantMap standaloneStatistics;
    QVariantMap dubbingStatistics;
    int standaloneChildrenAlive = 0;
    int dubbingChildrenAlive = 0;
    QString error;
    bool combinedOcrPreserved = false;
    QElapsedTimer totalElapsed;
    totalElapsed.start();

    {
        SubtitleOcrController standalone(nullptr, nullptr);
        if (!configureChineseOcr(standalone, &error)
            || !standalone.setBenchmarkSampleLimit(arguments.benchmarkSamples)
            || !standalone.loadSource(arguments.inputPath)
            || !waitFor([&standalone, &arguments]() {
                    return !standalone.processing()
                        && QFileInfo(standalone.sourcePath()).absoluteFilePath() == arguments.inputPath
                        && standalone.sourceWidth() > 0 && standalone.sourceHeight() > 0
                        && standalone.durationMs() > 0;
                }, 30000, &error, QStringLiteral("the standalone production metadata probe"))) {
            if (error.isEmpty()) error = standalone.error();
            return finish(baseJson(false, error), 1);
        }
        if (!standalone.run()
            || !waitFor([&standalone]() { return !standalone.processing(); }, kOcrTimeoutMs, &error,
                        QStringLiteral("the standalone production OCR run"))) {
            if (error.isEmpty()) error = standalone.error();
            return finish(baseJson(false, error), 1);
        }
        standaloneStatistics = standalone.runStatistics();
        standaloneChildrenAlive = standalone.activeChildProcessCount();
        if (arguments.benchmarkSamples > 0) {
            const bool tesseractFallbackUsed = standaloneStatistics.value(
                QStringLiteral("tesseractProcessCount")).toInt() != 0;
            QJsonObject result{{QStringLiteral("ok"), true},
                               {QStringLiteral("benchmark"), true},
                               {QStringLiteral("benchmarkSamples"), arguments.benchmarkSamples},
                               {QStringLiteral("inputPath"), arguments.inputPath},
                               {QStringLiteral("inputSha256"), inputSha256},
                               {QStringLiteral("engineId"), QString::fromLatin1(PaddleOcrRuntimeLocator::engineId())},
                               {QStringLiteral("runtimeVersion"), QString::fromLatin1(PaddleOcrRuntimeLocator::engineVersion())},
                               {QStringLiteral("language"), QStringLiteral("zh-Hans")},
                               {QStringLiteral("resultStatus"), standalone.resultStatus()},
                               {QStringLiteral("elapsedMs"), totalElapsed.elapsed()},
                               {QStringLiteral("samplesPerSecond"),
                                totalElapsed.elapsed() > 0
                                    ? double(standaloneStatistics.value(QStringLiteral("scheduledSamples")).toInt())
                                          / (double(totalElapsed.elapsed()) / 1000.0)
                                    : 0.0},
                               {QStringLiteral("upstreamRepository"), kPaddleUpstreamRepository},
                               {QStringLiteral("upstreamCommit"), kPaddleUpstreamCommit},
                               {QStringLiteral("model"), kPaddleModel},
                               {QStringLiteral("ffmpegProcessCount"), standaloneStatistics.value(QStringLiteral("ffmpegProcessCount")).toInt()},
                               {QStringLiteral("paddleProcessCount"), standaloneStatistics.value(QStringLiteral("paddleProcessCount")).toInt()},
                               {QStringLiteral("ocrWorkerCpuSeconds"), standaloneStatistics.value(QStringLiteral("ocrWorkerCpuSeconds")).toDouble()},
                               {QStringLiteral("ocrWorkerPeakWorkingSetBytes"), standaloneStatistics.value(QStringLiteral("ocrWorkerPeakWorkingSetBytes")).toLongLong()},
                               {QStringLiteral("sampledFrameCount"), standaloneStatistics.value(QStringLiteral("scheduledSamples")).toInt()},
                               {QStringLiteral("deduplicatedFrameCount"), standaloneStatistics.value(QStringLiteral("deduplicatedFrames")).toInt()},
                               {QStringLiteral("recognizedFrameCount"), standaloneStatistics.value(QStringLiteral("recognizedFrames")).toInt()},
                               {QStringLiteral("tesseractFallbackUsed"), tesseractFallbackUsed},
                               {QStringLiteral("childProcessesAlive"), standaloneChildrenAlive},
                               {QStringLiteral("counts"), QJsonObject::fromVariantMap(standaloneStatistics)}};
            if (standalone.resultStatus() != QStringLiteral("completed") || standaloneChildrenAlive != 0
                || tesseractFallbackUsed) {
                result.insert(QStringLiteral("ok"), false);
                result.insert(QStringLiteral("error"), QStringLiteral("Benchmark OCR did not publish a usable child-process-clean transcript."));
                result.insert(QStringLiteral("diagnostics"), standalone.diagnostics());
                return finish(result, 1);
            }
            const QString benchmarkSrt = QDir(arguments.outputRoot).filePath(
                QStringLiteral("benchmark-chi_sim.srt"));
            if (!standalone.exportSrt(benchmarkSrt)) {
                result.insert(QStringLiteral("ok"), false);
                result.insert(QStringLiteral("error"), standalone.error());
                return finish(result, 1);
            }
            result.insert(QStringLiteral("benchmarkTranscriptPath"), QFileInfo(benchmarkSrt).absoluteFilePath());
            return finish(result, 0);
        }
        if (standalone.phase() != QStringLiteral("completed")
            || standalone.resultStatus() != QStringLiteral("completed")
            || standalone.segments().isEmpty()) {
            error = QStringLiteral("Standalone OCR did not publish a usable result: %1").arg(standalone.error());
            QJsonObject result = baseJson(false, error);
            result.insert(QStringLiteral("standalonePhase"), standalone.phase());
            result.insert(QStringLiteral("standaloneResultStatus"), standalone.resultStatus());
            result.insert(QStringLiteral("standaloneCounts"),
                          QJsonObject::fromVariantMap(standaloneStatistics));
            result.insert(QStringLiteral("standaloneDiagnostics"), standalone.diagnostics());
            return finish(result, 1);
        }
        standaloneCount = standalone.segments().size();
        if (!standalone.exportSrt(standaloneSrt)) {
            return finish(baseJson(false, QStringLiteral("Standalone SRT export failed: %1").arg(standalone.error())), 1);
        }
        const TranscriptCheck transcript = validateTranscriptFile(standaloneSrt, standaloneCount);
        if (!transcript.error.isEmpty()) return finish(baseJson(false, transcript.error), 1);
    }

    {
        DubbingController dubbing(nullptr, nullptr, static_cast<ModelManager *>(nullptr),
                                  static_cast<RuntimeManager *>(nullptr));
        SubtitleOcrController dubbingOcr(nullptr, &dubbing);
        dubbing.setSubtitleOcrController(&dubbingOcr);
        if (!configureChineseOcr(dubbingOcr, &error)
            || !dubbing.newProject(dubbingProject)
            || !dubbing.importMedia(arguments.inputPath)
            || !dubbing.setWorkflowNodeParameters(QStringLiteral("transcribe"), {
                {QStringLiteral("transcriptSource"), QStringLiteral("ocr")},
                {QStringLiteral("ocrLanguage"), QStringLiteral("chi_sim")},
                {QStringLiteral("ocrExecutionRoute"), QStringLiteral("local-cpu")},
                {QStringLiteral("ocrLocalEngineId"), QString::fromLatin1(PaddleOcrRuntimeLocator::engineId())},
                {QStringLiteral("ocrLocalEngineVersion"), QString::fromLatin1(PaddleOcrRuntimeLocator::engineVersion())},
                {QStringLiteral("ocrRoi"), QVariantMap{{QStringLiteral("x"), 0.009},
                                                          {QStringLiteral("y"), 0.883},
                                                          {QStringLiteral("width"), 0.976},
                                                          {QStringLiteral("height"), 0.096}}},
                {QStringLiteral("ocrSampleIntervalMs"), 800},
                {QStringLiteral("ocrMinimumConfidence"), 0.50}})) {
            if (error.isEmpty()) error = dubbing.lastError().isEmpty() ? dubbingOcr.error() : dubbing.lastError();
            return finish(baseJson(false, QStringLiteral("Dubbing setup failed: %1").arg(error)), 1);
        }
        dubbing.setSourceLanguage(QStringLiteral("zh"));
        dubbing.transcribeSource();
        if (!waitFor([&dubbing]() { return dubbing.processing(); }, 5000, &error,
                     QStringLiteral("the Dubbing production OCR route to start"))
            || !waitFor([&dubbing]() { return !dubbing.processing(); }, kOcrTimeoutMs, &error,
                        QStringLiteral("the Dubbing production OCR run"))) {
            if (error.isEmpty()) error = dubbing.lastError();
            return finish(baseJson(false, error), 1);
        }
        if (!dubbing.lastError().isEmpty() || dubbing.segments().isEmpty()) {
            return finish(baseJson(false, QStringLiteral("Dubbing OCR did not publish a usable result: %1")
                                                 .arg(dubbing.lastError())), 1);
        }
        dubbingStatistics = dubbingOcr.runStatistics();
        dubbingChildrenAlive = dubbingOcr.activeChildProcessCount();
        dubbingCount = dubbing.segments().size();
        if (!dubbing.exportSubtitles(dubbingSrt, false)) {
            return finish(baseJson(false, QStringLiteral("Dubbing SRT export failed: %1").arg(dubbing.lastError())), 1);
        }
        const TranscriptCheck transcript = validateTranscriptFile(dubbingSrt, dubbingCount);
        if (!transcript.error.isEmpty()) return finish(baseJson(false, transcript.error), 1);
        if (!dubbing.saveProject()) {
            return finish(baseJson(false, QStringLiteral("Dubbing project save failed: %1").arg(dubbing.lastError())), 1);
        }
        DubbingController reloaded(nullptr, nullptr, static_cast<ModelManager *>(nullptr),
                                   static_cast<RuntimeManager *>(nullptr));
        if (!reloaded.openProject(dubbingProject) || reloaded.segments().size() != dubbingCount) {
            return finish(baseJson(false, QStringLiteral("Dubbing save/reload did not retain OCR segments: %1")
                                                 .arg(reloaded.lastError())), 1);
        }
        if (!combinedRoutePreservesActualOcr(dubbing.segments(), &error)) {
            return finish(baseJson(false, error), 1);
        }
        combinedOcrPreserved = true;
    }

    const bool cacheReusedByDubbing = dubbingStatistics.value(QStringLiteral("cacheReused")).toBool();
    const int tesseractProcessCount = standaloneStatistics.value(QStringLiteral("tesseractProcessCount")).toInt()
        + dubbingStatistics.value(QStringLiteral("tesseractProcessCount")).toInt();
    const int paddleProcessCount = standaloneStatistics.value(QStringLiteral("paddleProcessCount")).toInt()
        + dubbingStatistics.value(QStringLiteral("paddleProcessCount")).toInt();
    const int childProcessesAlive = standaloneChildrenAlive + dubbingChildrenAlive;
    const bool tesseractFallbackUsed = tesseractProcessCount != 0;
    const double ocrWorkerCpuSeconds = standaloneStatistics.value(QStringLiteral("ocrWorkerCpuSeconds")).toDouble()
        + dubbingStatistics.value(QStringLiteral("ocrWorkerCpuSeconds")).toDouble();
    const qint64 ocrWorkerPeakWorkingSetBytes = qMax(
        standaloneStatistics.value(QStringLiteral("ocrWorkerPeakWorkingSetBytes")).toLongLong(),
        dubbingStatistics.value(QStringLiteral("ocrWorkerPeakWorkingSetBytes")).toLongLong());
    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("inputPath"), arguments.inputPath},
                       {QStringLiteral("inputSha256"), inputSha256},
                       {QStringLiteral("engineId"), QString::fromLatin1(PaddleOcrRuntimeLocator::engineId())},
                       {QStringLiteral("runtimeVersion"), QString::fromLatin1(PaddleOcrRuntimeLocator::engineVersion())},
                       {QStringLiteral("language"), QStringLiteral("zh-Hans")},
                       {QStringLiteral("standaloneTranscriptPath"), QFileInfo(standaloneSrt).absoluteFilePath()},
                       {QStringLiteral("dubbingTranscriptPath"), QFileInfo(dubbingSrt).absoluteFilePath()},
                       {QStringLiteral("standaloneSegmentCount"), standaloneCount},
                       {QStringLiteral("dubbingSegmentCount"), dubbingCount},
                       {QStringLiteral("upstreamRepository"), kPaddleUpstreamRepository},
                       {QStringLiteral("upstreamCommit"), kPaddleUpstreamCommit},
                       {QStringLiteral("model"), kPaddleModel},
                       {QStringLiteral("elapsedMs"), totalElapsed.elapsed()},
                       {QStringLiteral("ffmpegProcessCount"), standaloneStatistics.value(QStringLiteral("ffmpegProcessCount")).toInt()
                                                              + dubbingStatistics.value(QStringLiteral("ffmpegProcessCount")).toInt()},
                       {QStringLiteral("tesseractProcessCount"), tesseractProcessCount},
                       {QStringLiteral("tesseractFallbackUsed"), tesseractFallbackUsed},
                       {QStringLiteral("paddleProcessCount"), paddleProcessCount},
                       {QStringLiteral("childProcessesAlive"), childProcessesAlive},
                       {QStringLiteral("ocrWorkerCpuSeconds"), ocrWorkerCpuSeconds},
                       {QStringLiteral("ocrWorkerPeakWorkingSetBytes"), ocrWorkerPeakWorkingSetBytes},
                       {QStringLiteral("sampledFrameCount"), standaloneStatistics.value(QStringLiteral("scheduledSamples")).toInt()},
                       {QStringLiteral("deduplicatedFrameCount"), standaloneStatistics.value(QStringLiteral("deduplicatedFrames")).toInt()},
                       {QStringLiteral("recognizedFrameCount"), standaloneStatistics.value(QStringLiteral("recognizedFrames")).toInt()},
                       {QStringLiteral("cacheReusedByDubbing"), cacheReusedByDubbing},
                       {QStringLiteral("standaloneCounts"), QJsonObject::fromVariantMap(standaloneStatistics)},
                       {QStringLiteral("dubbingCounts"), QJsonObject::fromVariantMap(dubbingStatistics)},
                       {QStringLiteral("combinedOcrPreserved"), combinedOcrPreserved}};
    if (totalElapsed.elapsed() > 15LL * 60LL * 1000LL) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"), QStringLiteral("Full OCR E2E exceeded the 15-minute performance gate."));
        return finish(result, 1);
    }
    if (!cacheReusedByDubbing || tesseractFallbackUsed || paddleProcessCount < 1
        || childProcessesAlive != 0) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"), QStringLiteral(
            "E2E cache/engine gate failed: Dubbing must reuse the Standalone PaddleOCR artifact without Tesseract fallback."));
        return finish(result, 1);
    }
    return finish(result, 0);
}

} // namespace LAStudio
