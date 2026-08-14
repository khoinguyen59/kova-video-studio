#include "test_SubtitleOcrController.h"

#include "controllers/dubbing/DubbingController.h"
#include "controllers/subtitles/SubtitleOcrController.h"
#include "controllers/tts/SubtitleVoiceController.h"
#include "core/DownloadManager.h"
#include "core/HFHubClient.h"
#include "core/PathUtils.h"
#include "remote/ColabSession.h"
#include "subtitles/PaddleOcrRuntimeLocator.h"
#include "subtitles/SubtitleOcrRuntimeService.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QImageReader>
#include <QJsonDocument>
#include <QProcess>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace LAStudio {
namespace {

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Text)
        && file.write(contents) == contents.size();
}

QByteArray batchFrameFfmpegScript(bool slow = false)
{
    QByteArray script = QByteArrayLiteral("@echo off\r\n");
    if (slow) script += QByteArrayLiteral("ping 127.0.0.1 -n 4 > nul\r\n");
    script += QByteArrayLiteral(
        "set \"last=\"\r\n"
        ":next\r\n"
        "if \"%~1\"==\"\" goto done\r\n"
        "set \"last=%~1\"\r\n"
        "shift\r\n"
        "goto next\r\n"
        ":done\r\n"
        "set \"LASTUDIO_TEST_FRAME=%last%\"\r\n"
        "for %%A in (\"%last%\") do set \"LASTUDIO_TEST_FRAME_DIR=%%~dpA\"\r\n"
        "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"$bytes=[Convert]::FromBase64String('iVBORw0KGgoAAAANSUhEUgAAAUAAAAASCAIAAAClw5C1AAAACXBIWXMAAAABAAAAAQBPJcTWAAAAV0lEQVR4nO3TsQnAMBDAwDx4/5Gf7ODGCO4mUKOZmQ9oOrv7ugG4dF4HAPcMDGEGhjADQ5iBIczAEGZgCDMwhBkYwgwMYQaGMANDmIEhzMAQZmAIMzCE/UUWA0OS8G1mAAAAAElFTkSuQmCC'); [IO.File]::WriteAllBytes($env:LASTUDIO_TEST_FRAME, $bytes); 0..100 | ForEach-Object { [IO.File]::WriteAllBytes((Join-Path $env:LASTUDIO_TEST_FRAME_DIR ('frame-{0:d6}.png' -f $_)), $bytes) }\"\r\n");
    return script;
}

struct OcrRuntimeEnvironment {
    QByteArray ffmpeg = qgetenv("LASTUDIO_FFMPEG");
    QByteArray ffprobe = qgetenv("LASTUDIO_FFPROBE");
    QByteArray tesseract = qgetenv("LASTUDIO_TESSERACT");
    QByteArray data = qgetenv("LASTUDIO_DATA_DIR");
    QByteArray tessdataPrefix = qgetenv("TESSDATA_PREFIX");
    QByteArray expectedTessdata = qgetenv("LASTUDIO_EXPECTED_TESSDATA");
    QByteArray localEngine = qgetenv("LASTUDIO_SUBTITLE_OCR_ENGINE");
    QByteArray paddlePython = qgetenv("LASTUDIO_PADDLE_PYTHON");
    QByteArray paddleWorker = qgetenv("LASTUDIO_PADDLE_WORKER");
    QByteArray paddleCache = qgetenv("LASTUDIO_PADDLE_CACHE");
    QByteArray paddleManifest = qgetenv("LASTUDIO_PADDLE_MANIFEST");
    QByteArray frameTimeout = qgetenv("LASTUDIO_TEST_SUBTITLE_OCR_FRAME_TIMEOUT_MS");
    bool hadFfmpeg = qEnvironmentVariableIsSet("LASTUDIO_FFMPEG");
    bool hadFfprobe = qEnvironmentVariableIsSet("LASTUDIO_FFPROBE");
    bool hadTesseract = qEnvironmentVariableIsSet("LASTUDIO_TESSERACT");
    bool hadData = qEnvironmentVariableIsSet("LASTUDIO_DATA_DIR");
    bool hadTessdataPrefix = qEnvironmentVariableIsSet("TESSDATA_PREFIX");
    bool hadExpectedTessdata = qEnvironmentVariableIsSet("LASTUDIO_EXPECTED_TESSDATA");
    bool hadLocalEngine = qEnvironmentVariableIsSet("LASTUDIO_SUBTITLE_OCR_ENGINE");
    bool hadPaddlePython = qEnvironmentVariableIsSet("LASTUDIO_PADDLE_PYTHON");
    bool hadPaddleWorker = qEnvironmentVariableIsSet("LASTUDIO_PADDLE_WORKER");
    bool hadPaddleCache = qEnvironmentVariableIsSet("LASTUDIO_PADDLE_CACHE");
    bool hadPaddleManifest = qEnvironmentVariableIsSet("LASTUDIO_PADDLE_MANIFEST");
    bool hadFrameTimeout = qEnvironmentVariableIsSet("LASTUDIO_TEST_SUBTITLE_OCR_FRAME_TIMEOUT_MS");

    OcrRuntimeEnvironment()
    {
        // Fixture suites exercise the named compatibility baseline.  Paddle's
        // real worker contract is covered separately and must be selected
        // explicitly so a cmd fixture can never masquerade as the upstream.
        qputenv("LASTUDIO_SUBTITLE_OCR_ENGINE", QByteArrayLiteral("tesseract-baseline"));
    }

    ~OcrRuntimeEnvironment()
    {
        if (hadFfmpeg) qputenv("LASTUDIO_FFMPEG", ffmpeg); else qunsetenv("LASTUDIO_FFMPEG");
        if (hadFfprobe) qputenv("LASTUDIO_FFPROBE", ffprobe); else qunsetenv("LASTUDIO_FFPROBE");
        if (hadTesseract) qputenv("LASTUDIO_TESSERACT", tesseract); else qunsetenv("LASTUDIO_TESSERACT");
        if (hadData) qputenv("LASTUDIO_DATA_DIR", data); else qunsetenv("LASTUDIO_DATA_DIR");
        if (hadTessdataPrefix) qputenv("TESSDATA_PREFIX", tessdataPrefix); else qunsetenv("TESSDATA_PREFIX");
        if (hadExpectedTessdata) qputenv("LASTUDIO_EXPECTED_TESSDATA", expectedTessdata); else qunsetenv("LASTUDIO_EXPECTED_TESSDATA");
        if (hadLocalEngine) qputenv("LASTUDIO_SUBTITLE_OCR_ENGINE", localEngine); else qunsetenv("LASTUDIO_SUBTITLE_OCR_ENGINE");
        if (hadPaddlePython) qputenv("LASTUDIO_PADDLE_PYTHON", paddlePython); else qunsetenv("LASTUDIO_PADDLE_PYTHON");
        if (hadPaddleWorker) qputenv("LASTUDIO_PADDLE_WORKER", paddleWorker); else qunsetenv("LASTUDIO_PADDLE_WORKER");
        if (hadPaddleCache) qputenv("LASTUDIO_PADDLE_CACHE", paddleCache); else qunsetenv("LASTUDIO_PADDLE_CACHE");
        if (hadPaddleManifest) qputenv("LASTUDIO_PADDLE_MANIFEST", paddleManifest); else qunsetenv("LASTUDIO_PADDLE_MANIFEST");
        if (hadFrameTimeout) qputenv("LASTUDIO_TEST_SUBTITLE_OCR_FRAME_TIMEOUT_MS", frameTimeout); else qunsetenv("LASTUDIO_TEST_SUBTITLE_OCR_FRAME_TIMEOUT_MS");
    }
};

struct OcrFixtures {
    explicit OcrFixtures(QTemporaryDir &directory)
        : source(directory.filePath(QStringLiteral("source video.mp4")))
        , ffprobe(directory.filePath(QStringLiteral("ffprobe.cmd")))
        , ffmpeg(directory.filePath(QStringLiteral("ffmpeg.cmd")))
        , slowFfmpeg(directory.filePath(QStringLiteral("slow-ffmpeg.cmd")))
        , noOutputFfmpeg(directory.filePath(QStringLiteral("no-output-ffmpeg.cmd")))
        , tesseract(directory.filePath(QStringLiteral("tesseract.cmd")))
        , noTextTesseract(directory.filePath(QStringLiteral("no-text-tesseract.cmd")))
    {
    }

    bool create() const
    {
        return writeFile(source, QByteArrayLiteral("not-a-real-video-fixture"))
            && writeFile(ffprobe, QByteArrayLiteral("@echo off\r\necho {\"streams\":[{\"width\":1920,\"height\":1080}],\"format\":{\"duration\":\"4.0\"}}\r\n"))
            && writeFile(ffmpeg, batchFrameFfmpegScript())
            && writeFile(slowFfmpeg, batchFrameFfmpegScript(true))
            && writeFile(noOutputFfmpeg, QByteArrayLiteral("@echo off\r\nexit /b 0\r\n"))
            && writeFile(tesseract, QByteArrayLiteral("@echo off\r\nif /I \"%~1\"==\"--list-langs\" (\r\n  echo List of available languages in C:\\fixture\\tessdata ^(2^):\r\n  echo eng\r\n  echo chi_sim\r\n  echo chi_tra\r\n  exit /b 0\r\n)\r\necho level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\r\necho 5\t1\t1\t1\t1\t1\t0\t0\t10\t10\t96\tReviewed\r\necho 5\t1\t1\t1\t1\t2\t10\t0\t10\t10\t94\tsubtitle\r\n"))
            && writeFile(noTextTesseract, QByteArrayLiteral("@echo off\r\nif /I \"%~1\"==\"--list-langs\" (\r\n  echo List of available languages in C:\\fixture\\tessdata ^(1^):\r\n  echo eng\r\n  exit /b 0\r\n)\r\necho level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\r\n"));
    }

    QString source;
    QString ffprobe;
    QString ffmpeg;
    QString slowFfmpeg;
    QString noOutputFfmpeg;
    QString tesseract;
    QString noTextTesseract;
};

struct PaddleFixture {
    explicit PaddleFixture(QTemporaryDir &directory)
        : python(directory.filePath(QStringLiteral("paddle-python.cmd")))
        , worker(directory.filePath(QStringLiteral("paddle worker.py")))
        , cache(directory.filePath(QStringLiteral("paddle cache")))
        , manifest(directory.filePath(QStringLiteral("paddle-runtime-manifest.json")))
    {
    }

    bool create() const
    {
        const QString models = QDir(cache).filePath(QStringLiteral("official_models"));
        if (!QDir().mkpath(QDir(models).filePath(QStringLiteral("PP-OCRv6_tiny_det")))
            || !QDir().mkpath(QDir(models).filePath(QStringLiteral("PP-OCRv6_tiny_rec")))) return false;
        const QByteArray script = QByteArrayLiteral(
            "@echo off\r\n"
            "set \"request=\"\r\n"
            "set \"response=\"\r\n"
            ":next\r\n"
            "if \"%~1\"==\"\" goto run\r\n"
            "if /I \"%~1\"==\"--health\" goto health\r\n"
            "if /I \"%~1\"==\"--request\" (set \"request=%~2\" & shift & shift & goto next)\r\n"
            "if /I \"%~1\"==\"--response\" (set \"response=%~2\" & shift & shift & goto next)\r\n"
            "shift\r\n"
            "goto next\r\n"
            ":health\r\n"
            "echo {\"ok\":true,\"engineId\":\"paddleocr-ppocrv6-tiny\",\"engineVersion\":\"3.7.0\",\"manifestVerified\":true}\r\n"
            "exit /b 0\r\n"
            ":run\r\n"
            "set \"LASTUDIO_TEST_PADDLE_REQUEST=%request%\"\r\n"
            "set \"LASTUDIO_TEST_PADDLE_RESPONSE=%response%\"\r\n"
            "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"$request=Get-Content -Raw -LiteralPath $env:LASTUDIO_TEST_PADDLE_REQUEST | ConvertFrom-Json; $rows=@($request.frames | ForEach-Object { @{hash=$_.hash;text=(([char]0x4f60)+([char]0x597d));confidence=0.95} }); $output=@{schemaVersion=1;engineId='paddleocr-ppocrv6-tiny';engineVersion='3.7.0';manifestVerified=$true;results=$rows;telemetry=@{elapsedMs=25;cpuSeconds=0.1;peakWorkingSetBytes=1048576}} | ConvertTo-Json -Depth 5 -Compress; [IO.File]::WriteAllText($env:LASTUDIO_TEST_PADDLE_RESPONSE,$output,[Text.UTF8Encoding]::new($false))\"\r\n"
            "exit /b %ERRORLEVEL%\r\n");
        if (!(writeFile(worker, QByteArrayLiteral("# fixture worker\n"))
              && writeFile(QDir(models).filePath(QStringLiteral("PP-OCRv6_tiny_det/inference.yml")), QByteArrayLiteral("fixture\n"))
              && writeFile(QDir(models).filePath(QStringLiteral("PP-OCRv6_tiny_det/inference.pdiparams")), QByteArrayLiteral("fixture\n"))
              && writeFile(QDir(models).filePath(QStringLiteral("PP-OCRv6_tiny_rec/inference.yml")), QByteArrayLiteral("fixture\n"))
              && writeFile(QDir(models).filePath(QStringLiteral("PP-OCRv6_tiny_rec/inference.pdiparams")), QByteArrayLiteral("fixture\n"))
              && writeFile(python, script))) return false;
        const QJsonObject manifestObject{
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("engine"), QJsonObject{
                {QStringLiteral("id"), QStringLiteral("paddleocr-ppocrv6-tiny")},
                {QStringLiteral("version"), QStringLiteral("3.7.0")},
                {QStringLiteral("upstreamRepository"), QStringLiteral("https://github.com/PaddlePaddle/PaddleOCR")},
                {QStringLiteral("upstreamCommit"), QStringLiteral("2661c7c0ef5c613e8f93c6e93b2e052399f0f854")},
                {QStringLiteral("license"), QStringLiteral("Apache-2.0")},
            }},
            {QStringLiteral("models"), QJsonObject{
                {QStringLiteral("detection"), QStringLiteral("PP-OCRv6_tiny_det")},
                {QStringLiteral("recognition"), QStringLiteral("PP-OCRv6_tiny_rec")},
                {QStringLiteral("cacheLayout"), QStringLiteral("paddle cache/official_models")},
                {QStringLiteral("treeSha256"), PaddleOcrRuntimeLocator::modelTreeSha256(cache)},
            }},
            {QStringLiteral("runtime"), QJsonObject{
                {QStringLiteral("delivery"), QStringLiteral("bundled-isolated-python")},
                {QStringLiteral("automaticDownload"), false},
                {QStringLiteral("pythonRelativePath"), QStringLiteral("paddle-python.cmd")},
                {QStringLiteral("pythonSha256"), PaddleOcrRuntimeLocator::sha256File(python)},
            }},
            {QStringLiteral("worker"), QJsonObject{
                {QStringLiteral("relativePath"), QStringLiteral("paddle_ocr_worker.py")},
                {QStringLiteral("sha256"), PaddleOcrRuntimeLocator::sha256File(worker)},
            }},
        };
        return writeFile(manifest, QJsonDocument(manifestObject).toJson(QJsonDocument::Compact));
    }

    QString python;
    QString worker;
    QString cache;
    QString manifest;
};

class SharedMediaServer final : public QObject
{
public:
    explicit SharedMediaServer(int bodyDelayMs = 0, bool includeContentLength = true)
        : m_bodyDelayMs(bodyDelayMs)
        , m_includeContentLength(includeContentLength)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    if (socket->property("handled").toBool() || socket->bytesAvailable() == 0) return;
                    socket->setProperty("handled", true);
                    ++m_requestCount;
                    const QByteArray body("subtitle-ocr-shared-media");
                    QByteArray headers("HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\n");
                    if (m_includeContentLength)
                        headers += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
                    headers += "Connection: close\r\n\r\n";
                    socket->write(headers);
                    const auto sendBody = [socket, body] {
                        if (socket->state() == QAbstractSocket::ConnectedState) {
                            socket->write(body);
                            socket->disconnectFromHost();
                        }
                    };
                    if (m_bodyDelayMs > 0) QTimer::singleShot(m_bodyDelayMs, socket, sendBody);
                    else sendBody();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/subtitle-source.mp4?temporary=query")
                                       .arg(m_server.serverPort())); }
    int requestCount() const { return m_requestCount; }

private:
    QTcpServer m_server;
    int m_bodyDelayMs = 0;
    bool m_includeContentLength = true;
    int m_requestCount = 0;
};

void configure(const OcrFixtures &fixtures, bool includeTesseract = true)
{
    // Existing fixture rows exercise the explicit compatibility baseline.
    // Production defaults to PaddleOCR and is covered through the real worker
    // contract/E2E path rather than pretending this cmd fixture is Paddle.
    qputenv("LASTUDIO_SUBTITLE_OCR_ENGINE", QByteArrayLiteral("tesseract-baseline"));
    qputenv("LASTUDIO_FFMPEG", fixtures.ffmpeg.toUtf8());
    qputenv("LASTUDIO_FFPROBE", fixtures.ffprobe.toUtf8());
    if (includeTesseract) qputenv("LASTUDIO_TESSERACT", fixtures.tesseract.toUtf8());
    else qunsetenv("LASTUDIO_TESSERACT");
}

void loadFixture(SubtitleOcrController &controller, const QString &source)
{
    QVERIFY(controller.loadSource(source));
    QTRY_COMPARE_WITH_TIMEOUT(controller.sourceWidth(), 1920, 5000);
    QCOMPARE(controller.sourceHeight(), 1080);
    QCOMPARE(controller.durationMs(), qint64(4000));
    QVERIFY(!controller.processing());
}

} // namespace

void TestSubtitleOcrController::blocksMissingManagedRuntimeWithoutSilentDownload()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    QVERIFY(fixtures.create());
    configure(fixtures, false);

    SubtitleOcrController controller(nullptr, nullptr);
    loadFixture(controller, fixtures.source);
    QVERIFY(!controller.runtimeAvailable());
    QVERIFY(!controller.run());
    QVERIFY(controller.error().contains(QStringLiteral("Install runtime"), Qt::CaseInsensitive));
}

void TestSubtitleOcrController::rejectsInvalidVideoProbeWithoutReplacingCurrentSource()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    QVERIFY(fixtures.create());
    configure(fixtures);

    SubtitleOcrController controller(nullptr, nullptr);
    loadFixture(controller, fixtures.source);
    const QString priorSource = controller.sourcePath();
    QVERIFY(writeFile(fixtures.ffprobe, QByteArrayLiteral("@echo off\r\necho not-json\r\n")));
    QVERIFY(controller.loadSource(fixtures.source));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 5000);
    QCOMPARE(controller.phase(), QStringLiteral("error"));
    QCOMPARE(controller.sourcePath(), priorSource);
    QVERIFY(controller.error().contains(QStringLiteral("readable video stream"), Qt::CaseInsensitive));
}

void TestSubtitleOcrController::blocksMissingSelectedLanguageBeforeFrameExtraction()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    QVERIFY(fixtures.create());
    configure(fixtures);

    SubtitleOcrController controller(nullptr, nullptr);
    loadFixture(controller, fixtures.source);
    QVERIFY(controller.setOcrLanguage(QStringLiteral("eng+vie")));
    QVERIFY(controller.run());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 5000);
    QCOMPARE(controller.phase(), QStringLiteral("error"));
    QVERIFY(controller.error().contains(QStringLiteral("vie")));
    QVERIFY(controller.error().contains(QStringLiteral("language data"), Qt::CaseInsensitive));
}

void TestSubtitleOcrController::blocksColabRouteWithoutAnExactVerifiedProfile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    QVERIFY(fixtures.create());
    configure(fixtures, false);

    ColabSession session;
    SubtitleOcrController controller(nullptr, nullptr);
    controller.setColabSession(&session);
    loadFixture(controller, fixtures.source);
    QVERIFY(controller.setExecutionRoute(QStringLiteral("colab-gpu")));
    QCOMPARE(controller.colabModelId(), QStringLiteral("pp-ocrv5-multilingual-3.1"));
    QVERIFY(!controller.colabRouteReady());

    QVERIFY(!controller.run());
    QVERIFY(controller.error().contains(QStringLiteral("Connect and check")));
    QVERIFY(!controller.processing());
}

void TestSubtitleOcrController::projectNeverPersistsTemporaryColabCredentials()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    QVERIFY(fixtures.create());
    configure(fixtures, false);

    ColabSession session;
    QString sessionError;
    QVERIFY(session.setSession(QStringLiteral("http://127.0.0.1:8765"),
                               QStringLiteral("temporary-colab-token"),
                               &sessionError, true));

    SubtitleOcrController controller(nullptr, nullptr);
    controller.setColabSession(&session);
    loadFixture(controller, fixtures.source);
    QVERIFY(controller.setExecutionRoute(QStringLiteral("colab-gpu")));
    const QString projectPath = directory.filePath(QStringLiteral("private-route.laocr.json"));
    QVERIFY(controller.saveProject(projectPath));

    QFile project(projectPath);
    QVERIFY(project.open(QIODevice::ReadOnly));
    const QByteArray serialized = project.readAll();
    QVERIFY(serialized.contains("\"executionRoute\": \"colab-gpu\""));
    QVERIFY(serialized.contains("\"colabModelId\": \"pp-ocrv5-multilingual-3.1\""));
    QVERIFY(!serialized.contains("temporary-colab-token"));
    QVERIFY(!serialized.contains("127.0.0.1:8765"));
}

void TestSubtitleOcrController::usesExactManagedTessdataForLanguagePreflightAndRecognition()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    QVERIFY(fixtures.create());
    const QString strictTesseract = directory.filePath(QStringLiteral("strict tesseract.cmd"));
    const QByteArray strictWorker = QByteArrayLiteral(
        "@echo off\r\n"
        "if /I not \"%~1\"==\"--tessdata-dir\" goto invalid-first\r\n"
        "if /I not \"%~2\"==\"%LASTUDIO_EXPECTED_TESSDATA%\" goto invalid-directory\r\n"
        "if not \"%TESSDATA_PREFIX%\"==\"\" goto invalid-prefix\r\n"
        "if /I \"%~3\"==\"--list-langs\" (\r\n"
        "  echo List of available languages in managed tessdata ^(2^):\r\n"
        "  echo eng\r\n"
        "  echo chi_sim\r\n"
        "  exit /b 0\r\n"
        ")\r\n"
        "if /I \"%~4\"==\"stdout\" (\r\n"
        "  echo level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\r\n"
        "  echo 5\t1\t1\t1\t1\t1\t0\t0\t10\t10\t96\tManaged\r\n"
        "  echo 5\t1\t1\t1\t1\t2\t10\t0\t10\t10\t94\tlanguage\r\n"
        "  exit /b 0\r\n"
        ")\r\n"
        ":invalid-first\r\n"
        "echo expected --tessdata-dir, got [%~1] 1>&2\r\n"
        "exit /b 9\r\n"
        ":invalid-directory\r\n"
        "echo expected tessdata [%LASTUDIO_EXPECTED_TESSDATA%], got [%~2] 1>&2\r\n"
        "exit /b 9\r\n"
        ":invalid-prefix\r\n"
        "echo inherited TESSDATA_PREFIX [%TESSDATA_PREFIX%] 1>&2\r\n"
        "exit /b 9\r\n");
    QVERIFY(writeFile(strictTesseract, strictWorker));
    configure(fixtures, false);

    // Keep a Unicode directory in the direct argument regression. cmd.exe
    // uses the active console codepage when a batch fixture compares text, so
    // the process-boundary fixture below uses spaces while this assertion
    // proves Qt retains the exact Unicode path before process creation.
    const QString unicodeDataDirectory = directory.filePath(QString::fromUtf8("unicode-\xC4\x91"));
    QVERIFY(QDir().mkpath(unicodeDataDirectory));
#ifdef Q_OS_WIN
    QVERIFY(SetEnvironmentVariableW(L"LASTUDIO_DATA_DIR",
                                    reinterpret_cast<const wchar_t *>(unicodeDataDirectory.utf16())));
#else
    qputenv("LASTUDIO_DATA_DIR", unicodeDataDirectory.toUtf8());
#endif
    HFHubClient unicodeHub;
    DownloadManager unicodeDownloads(&unicodeHub);
    SubtitleOcrRuntimeService unicodeRuntime(&unicodeDownloads);
    unicodeRuntime.m_runtimeSource = QStringLiteral("bundled");
    unicodeRuntime.m_runtimeValid = true;
    const QString unicodeTessdata = QDir(unicodeDataDirectory).filePath(
        QStringLiteral("subtitle-ocr/runtime/tessdata"));
    QCOMPARE(unicodeRuntime.managedTessdataPath(), QDir::cleanPath(unicodeTessdata));
    QCOMPARE(unicodeRuntime.tesseractDataArguments(),
             QStringList({QStringLiteral("--tessdata-dir"), QDir::cleanPath(unicodeTessdata)}));

    // The worker path and active managed data directory contain spaces. A
    // stale inherited prefix would fail the strict worker rather than
    // accidentally finding a system Tesseract install.
    const QString dataDirectory = directory.filePath(QStringLiteral("data with spaces"));
    QVERIFY(QDir().mkpath(dataDirectory));
    qputenv("LASTUDIO_DATA_DIR", dataDirectory.toUtf8());
    qputenv("TESSDATA_PREFIX", QByteArrayLiteral("C:\\unrelated-system-tessdata"));

    HFHubClient hub;
    DownloadManager downloads(&hub);
    SubtitleOcrRuntimeService runtime(&downloads);
    runtime.m_runtimePath = strictTesseract;
    runtime.m_runtimeSource = QStringLiteral("bundled");
    runtime.m_runtimeVersion = QStringLiteral("5.5.1");
    runtime.m_runtimeValid = true;
    qputenv("LASTUDIO_EXPECTED_TESSDATA", runtime.managedTessdataPath().toUtf8());

    SubtitleOcrController controller(nullptr, nullptr);
    controller.setRuntimeService(&runtime);
    loadFixture(controller, fixtures.source);
    QVERIFY(controller.setOcrLanguage(QStringLiteral("eng+chi_sim")));
    QVERIFY(controller.run());
    QElapsedTimer completionTimer;
    completionTimer.start();
    while (controller.processing() && completionTimer.elapsed() < 10000) QTest::qWait(20);
    QVERIFY2(!controller.processing(), qPrintable(
        QStringLiteral("phase=%1; status=%2; statistics=%3; diagnostics=%4")
            .arg(controller.phase(), controller.resultStatus(),
                 QString::fromUtf8(QJsonDocument::fromVariant(controller.runStatistics()).toJson(
                     QJsonDocument::Compact)), controller.diagnostics())));
    QVERIFY2(controller.phase() == QStringLiteral("completed"), qPrintable(controller.error()));
    QCOMPARE(controller.segments().size(), 1);
    QCOMPARE(controller.segments().constFirst().toMap().value(QStringLiteral("text")).toString(),
             QStringLiteral("Managed language"));
}

void TestSubtitleOcrController::rejectsIncompletePaddleOcrRuntimeManifest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    PaddleFixture paddle(directory);
    QVERIFY(paddle.create());
    qputenv("LASTUDIO_SUBTITLE_OCR_ENGINE", QByteArrayLiteral("paddleocr-ppocrv6-tiny"));
    qputenv("LASTUDIO_PADDLE_PYTHON", paddle.python.toUtf8());
    qputenv("LASTUDIO_PADDLE_WORKER", paddle.worker.toUtf8());
    qputenv("LASTUDIO_PADDLE_CACHE", paddle.cache.toUtf8());
    qputenv("LASTUDIO_PADDLE_MANIFEST", paddle.manifest.toUtf8());

    SubtitleOcrController controller(nullptr, nullptr);
    QVERIFY(controller.runtimeAvailable());
    QFile manifestFile(paddle.manifest);
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    QJsonObject models = manifest.value(QStringLiteral("models")).toObject();
    models.insert(QStringLiteral("treeSha256"), QStringLiteral("invalid"));
    manifest.insert(QStringLiteral("models"), models);
    QVERIFY(writeFile(paddle.manifest, QJsonDocument(manifest).toJson(QJsonDocument::Compact)));
    QVERIFY(!controller.runtimeAvailable());
}

void TestSubtitleOcrController::runsPaddleOcrBatchAdapterWithoutTesseractFallback()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    PaddleFixture paddle(directory);
    QVERIFY(fixtures.create());
    QVERIFY(paddle.create());
    configure(fixtures, false);
    qputenv("LASTUDIO_SUBTITLE_OCR_ENGINE", QByteArrayLiteral("paddleocr-ppocrv6-tiny"));
    qputenv("LASTUDIO_PADDLE_PYTHON", paddle.python.toUtf8());
    qputenv("LASTUDIO_PADDLE_WORKER", paddle.worker.toUtf8());
    qputenv("LASTUDIO_PADDLE_CACHE", paddle.cache.toUtf8());
    qputenv("LASTUDIO_PADDLE_MANIFEST", paddle.manifest.toUtf8());

    SubtitleOcrController controller(nullptr, nullptr);
    loadFixture(controller, fixtures.source);
    QVERIFY(controller.runtimeAvailable());
    QCOMPARE(controller.localEngineId(), QStringLiteral("paddleocr-ppocrv6-tiny"));
    QCOMPARE(controller.localEngineVersion(), QStringLiteral("3.7.0"));
    QVERIFY(controller.setOcrLanguage(QStringLiteral("chi_sim")));
    QVERIFY(controller.run());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 10000);
    QVERIFY2(controller.phase() == QStringLiteral("completed"),
             qPrintable(controller.error() + QStringLiteral("\n") + controller.diagnostics()));
    QCOMPARE(controller.resultStatus(), QStringLiteral("completed"));
    QCOMPARE(controller.segments().size(), 1);
    QCOMPARE(controller.segments().constFirst().toMap().value(QStringLiteral("text")).toString(),
             QString::fromUtf8("\xE4\xBD\xA0\xE5\xA5\xBD"));
    const QVariantMap statistics = controller.runStatistics();
    QCOMPARE(statistics.value(QStringLiteral("ocrEngineId")).toString(),
             QStringLiteral("paddleocr-ppocrv6-tiny"));
    QCOMPARE(statistics.value(QStringLiteral("paddleProcessCount")).toInt(), 1);
    QCOMPARE(statistics.value(QStringLiteral("tesseractProcessCount")).toInt(), 0);
    QVERIFY(statistics.value(QStringLiteral("ocrWorkerCpuSeconds")).toDouble() > 0.0);
    QVERIFY(statistics.value(QStringLiteral("ocrWorkerPeakWorkingSetBytes")).toLongLong() > 0);
}

void TestSubtitleOcrController::localPaddleRouteRejectsUnbundledLanguageBeforeProcessing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    PaddleFixture paddle(directory);
    QVERIFY(fixtures.create());
    QVERIFY(paddle.create());
    configure(fixtures, false);
    qputenv("LASTUDIO_SUBTITLE_OCR_ENGINE", QByteArrayLiteral("paddleocr-ppocrv6-tiny"));
    qputenv("LASTUDIO_PADDLE_PYTHON", paddle.python.toUtf8());
    qputenv("LASTUDIO_PADDLE_WORKER", paddle.worker.toUtf8());
    qputenv("LASTUDIO_PADDLE_CACHE", paddle.cache.toUtf8());
    qputenv("LASTUDIO_PADDLE_MANIFEST", paddle.manifest.toUtf8());

    SubtitleOcrController controller(nullptr, nullptr);
    loadFixture(controller, fixtures.source);
    QVERIFY(controller.setOcrLanguage(QStringLiteral("eng")));
    QVERIFY(controller.runtimeAvailable());
    QVERIFY(!controller.localRouteReady());
    QCOMPARE(controller.localRuntimeState(), QStringLiteral("Unsupported language"));
    QVERIFY(!controller.run());
    QVERIFY(controller.error().contains(QStringLiteral("Simplified Chinese")));
    QVERIFY(!controller.processing());
}

void TestSubtitleOcrController::keepsLowerRegionPresetSeparateFromFullFrameReset()
{
    SubtitleOcrController controller(nullptr, nullptr);

    QVERIFY(controller.setRoi(0.25, 0.25, 0.50, 0.50));
    controller.setLowerRegionPreset();
    QCOMPARE(controller.roiX(), 0.10);
    QCOMPARE(controller.roiY(), 0.72);
    QCOMPARE(controller.roiWidth(), 0.80);
    QCOMPARE(controller.roiHeight(), 0.22);

    controller.resetRoi();
    QCOMPARE(controller.roiX(), 0.0);
    QCOMPARE(controller.roiY(), 0.0);
    QCOMPARE(controller.roiWidth(), 1.0);
    QCOMPARE(controller.roiHeight(), 1.0);
}

void TestSubtitleOcrController::runsManagedAdapterPersistsReviewedSegmentsAndExports()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    QVERIFY(fixtures.create());
    configure(fixtures);

    SubtitleOcrController controller(nullptr, nullptr);
    loadFixture(controller, fixtures.source);
    QVERIFY(controller.setSampleIntervalMs(1000));
    QVERIFY(controller.setMinimumConfidence(0.90));
    QVERIFY(controller.requestCropPreview(1200));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 5000);
    QVERIFY(!controller.cropPreviewUrl().isEmpty());
    QVERIFY(QFileInfo::exists(controller.cropPreviewUrl().toLocalFile()));

    QVERIFY(controller.run());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 10000);
    QVERIFY2(controller.phase() == QStringLiteral("completed"),
             qPrintable(controller.error() + QStringLiteral("\n") + controller.diagnostics()));
    QCOMPARE(controller.progress(), 100);
    QVERIFY(controller.progressAvailable());
    QCOMPARE(controller.segments().size(), 1);
    const QVariantMap segment = controller.segments().constFirst().toMap();
    QCOMPARE(segment.value(QStringLiteral("text")).toString(), QStringLiteral("Reviewed subtitle"));
    QCOMPARE(segment.value(QStringLiteral("startMs")).toLongLong(), qint64(0));
    QCOMPARE(segment.value(QStringLiteral("endMs")).toLongLong(), qint64(4000));
    QVERIFY(segment.value(QStringLiteral("confidence")).toDouble() > 0.90);

    controller.updateSegment(0, {{QStringLiteral("text"), QStringLiteral("Edited subtitle")}});
    QCOMPARE(controller.segments().constFirst().toMap().value(QStringLiteral("text")).toString(),
             QStringLiteral("Edited subtitle"));
    const QString srtPath = directory.filePath(QStringLiteral("reviewed.srt"));
    const QString textPath = directory.filePath(QStringLiteral("reviewed.txt"));
    const QString projectPath = directory.filePath(QStringLiteral("reviewed.laocr.json"));
    QVERIFY(controller.exportSrt(srtPath));
    QVERIFY(controller.exportText(textPath));
    QVERIFY(controller.saveProject(projectPath));
    QVERIFY(QFileInfo::exists(srtPath));
    QVERIFY(QFileInfo::exists(textPath));

    SubtitleOcrController reopened(nullptr, nullptr);
    QVERIFY(reopened.openProject(projectPath));
    QCOMPARE(reopened.sourcePath(), QFileInfo(fixtures.source).absoluteFilePath());
    QCOMPARE(reopened.segments().size(), 1);
    QCOMPARE(reopened.segments().constFirst().toMap().value(QStringLiteral("text")).toString(),
             QStringLiteral("Edited subtitle"));
}

void TestSubtitleOcrController::transfersReviewedSegmentsToSubtitleVoiceAndDubbing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    QVERIFY(fixtures.create());
    configure(fixtures);

    SubtitleVoiceController subtitleVoice(nullptr, nullptr, nullptr);
    DubbingController dubbing(nullptr, nullptr, static_cast<ModelManager *>(nullptr),
                              static_cast<RuntimeManager *>(nullptr));
    QVERIFY(dubbing.newProject(directory.filePath(QStringLiteral("ocr-transfer.ladub.json"))));
    SubtitleOcrController controller(&subtitleVoice, &dubbing);
    loadFixture(controller, fixtures.source);
    QVERIFY(controller.run());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 10000);
    QVERIFY(controller.sendToSubtitleVoice());
    QCOMPARE(subtitleVoice.cues().size(), 1);
    QCOMPARE(subtitleVoice.cues().constFirst().toMap().value(QStringLiteral("text")).toString(),
             QStringLiteral("Reviewed subtitle"));
    QVERIFY(controller.sendToDubbing());
    QCOMPARE(dubbing.segments().size(), 1);
    const QVariantMap imported = dubbing.segments().constFirst().toMap();
    QCOMPARE(imported.value(QStringLiteral("sourceText")).toString(), QStringLiteral("Reviewed subtitle"));
    QCOMPARE(imported.value(QStringLiteral("timingSource")).toString(), QStringLiteral("subtitle-ocr"));
}

void TestSubtitleOcrController::cancelsAndRetriesWithoutLeavingOcrStaging()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    QVERIFY(fixtures.create());
    configure(fixtures);

    SubtitleOcrController controller(nullptr, nullptr);
    loadFixture(controller, fixtures.source);
    qputenv("LASTUDIO_FFMPEG", fixtures.slowFfmpeg.toUtf8());
    QVERIFY(controller.run());
    QTRY_VERIFY_WITH_TIMEOUT(controller.processing(), 1000);
    controller.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 5000);
    QCOMPARE(controller.phase(), QStringLiteral("canceled"));
    const QDir staging(QDir(PathUtils::cacheDir()).filePath(QStringLiteral("subtitle-ocr")));
    QVERIFY(staging.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot).isEmpty());

    qputenv("LASTUDIO_FFMPEG", fixtures.ffmpeg.toUtf8());
    QVERIFY(controller.retry());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 10000);
    QCOMPARE(controller.phase(), QStringLiteral("completed"));
    QCOMPARE(controller.segments().size(), 1);
}

void TestSubtitleOcrController::rejectsUnreadableFrameAndRetriesTheSameSample()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    QVERIFY(fixtures.create());
    configure(fixtures);

    SubtitleOcrController controller(nullptr, nullptr);
    loadFixture(controller, fixtures.source);
    qputenv("LASTUDIO_FFMPEG", fixtures.noOutputFfmpeg.toUtf8());
    QVERIFY(controller.run());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 5000);
    QCOMPARE(controller.phase(), QStringLiteral("error"));
    QVERIFY(controller.error().contains(QStringLiteral("readable PNG crop"), Qt::CaseInsensitive));
    QVERIFY(controller.canRetryFrameExtraction());
    QVERIFY(controller.diagnostics().contains(QStringLiteral("frame-extraction-exit")));
    QVERIFY(controller.diagnostics().contains(QStringLiteral("exists=false")));
    QVERIFY(controller.diagnostics().contains(QStringLiteral("workspace-cleanup")));
    QCOMPARE(controller.sourcePath(), QFileInfo(fixtures.source).absoluteFilePath());
    QCOMPARE(controller.ocrLanguage(), QStringLiteral("eng"));
    QCOMPARE(controller.roiX(), 0.10);

    qputenv("LASTUDIO_FFMPEG", fixtures.ffmpeg.toUtf8());
    QVERIFY(controller.retryFrameExtraction());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 10000);
    QCOMPARE(controller.phase(), QStringLiteral("completed"));
    QCOMPARE(controller.segments().size(), 1);
}

void TestSubtitleOcrController::timesOutFrameExtractionAndKeepsDiagnosticsForRetry()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    QVERIFY(fixtures.create());
    configure(fixtures);

    SubtitleOcrController controller(nullptr, nullptr);
    loadFixture(controller, fixtures.source);
    qputenv("LASTUDIO_FFMPEG", fixtures.slowFfmpeg.toUtf8());
    qputenv("LASTUDIO_TEST_SUBTITLE_OCR_FRAME_TIMEOUT_MS", "100");
    QVERIFY(controller.run());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 5000);
    QCOMPARE(controller.phase(), QStringLiteral("error"));
    QVERIFY(controller.error().contains(QStringLiteral("timed out"), Qt::CaseInsensitive));
    QVERIFY(controller.canRetryFrameExtraction());
    QVERIFY(controller.diagnostics().contains(QStringLiteral("frame-extraction-timeout")));
    QVERIFY(controller.diagnostics().contains(QStringLiteral("workspace-cleanup")));

    qunsetenv("LASTUDIO_TEST_SUBTITLE_OCR_FRAME_TIMEOUT_MS");
    qputenv("LASTUDIO_FFMPEG", fixtures.ffmpeg.toUtf8());
    QVERIFY(controller.retryFrameExtraction());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 10000);
    QCOMPARE(controller.phase(), QStringLiteral("completed"));
}

void TestSubtitleOcrController::rejectsNoTextCompletionClearsStaleSegmentsAndBlocksExport()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    QVERIFY(fixtures.create());
    configure(fixtures);

    SubtitleOcrController controller(nullptr, nullptr);
    loadFixture(controller, fixtures.source);
    QVERIFY(controller.run());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 10000);
    QCOMPARE(controller.resultStatus(), QStringLiteral("completed"));
    QVERIFY(!controller.segments().isEmpty());

    qputenv("LASTUDIO_TESSERACT", fixtures.noTextTesseract.toUtf8());
    QVERIFY(controller.retry());
    QVERIFY(controller.segments().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 10000);
    QCOMPARE(controller.phase(), QStringLiteral("error"));
    QCOMPARE(controller.resultStatus(), QStringLiteral("no_text_detected"));
    QVERIFY(controller.error().startsWith(QStringLiteral("no_text_detected:")));
    QCOMPARE(controller.segments().size(), 0);
    QCOMPARE(controller.runStatistics().value(QStringLiteral("publishedSegments")).toInt(), 0);
    QVERIFY(controller.runStatistics().value(QStringLiteral("ocrSuccesses")).toInt() > 0);
    QVERIFY(controller.diagnostics().contains(QStringLiteral("result-validation")));
    QVERIFY(!controller.exportSrt(directory.filePath(QStringLiteral("must-not-exist.srt"))));
}

void TestSubtitleOcrController::extractsBottomRoiWithTheStagedPackagedFfmpegRuntime()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    QVERIFY(fixtures.create());

    const QString runtimeRoot = qEnvironmentVariable("LASTUDIO_TEST_MEDIA_RUNTIME_ROOT");
    const QString ffmpeg = QDir(runtimeRoot).filePath(QStringLiteral("ffmpeg.exe"));
    const QString ffprobe = QDir(runtimeRoot).filePath(QStringLiteral("ffprobe.exe"));
    QVERIFY2(QFileInfo(ffmpeg).isExecutable(), qPrintable(QStringLiteral("Missing staged package FFmpeg: %1").arg(ffmpeg)));
    QVERIFY2(QFileInfo(ffprobe).isExecutable(), qPrintable(QStringLiteral("Missing staged package FFprobe: %1").arg(ffprobe)));

    const QString source = directory.filePath(QString::fromUtf8("subtitle source Unicode-\xC4\x91.mp4"));
    QProcess generator;
    generator.setProgram(ffmpeg);
    generator.setArguments({QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                            QStringLiteral("-f"), QStringLiteral("lavfi"),
                            QStringLiteral("-i"), QStringLiteral("color=c=black:s=320x180:r=25:d=110"),
                            QStringLiteral("-vf"), QStringLiteral("drawbox=x=0:y=159:w=320:h=19:color=white:t=fill"),
                            QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-q:v"),
                            QStringLiteral("5"), QStringLiteral("-y"), source});
    generator.start();
    QVERIFY2(generator.waitForFinished(30000), qPrintable(generator.errorString()));
    QVERIFY2(generator.exitCode() == 0, generator.readAllStandardError().constData());
    QVERIFY(QFileInfo(source).isFile());

    qputenv("LASTUDIO_FFMPEG", ffmpeg.toUtf8());
    qputenv("LASTUDIO_FFPROBE", ffprobe.toUtf8());
    qputenv("LASTUDIO_TESSERACT", fixtures.tesseract.toUtf8());
    SubtitleOcrController controller(nullptr, nullptr);
    QVERIFY(controller.loadSource(source));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 10000);
    QCOMPARE(controller.phase(), QStringLiteral("ready"));
    QCOMPARE(controller.sourceWidth(), 320);
    QCOMPARE(controller.sourceHeight(), 180);
    QVERIFY(controller.durationMs() >= 109000);
    QVERIFY(controller.setOcrLanguage(QStringLiteral("eng")));
    QVERIFY(controller.setRoi(0.0, 0.883, 1.0, 0.105));
    QVERIFY(controller.requestCropPreview(108000));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 10000);
    QImageReader preview(controller.cropPreviewUrl().toLocalFile());
    const QImage previewImage = preview.read();
    QVERIFY2(!previewImage.isNull(), qPrintable(preview.errorString()));
    // The source ROI remains 320x19; the production OCR filter enlarges the
    // cropped strip before Tesseract so small hard subtitles remain readable.
    QCOMPARE(previewImage.width(), 960);
    QCOMPARE(previewImage.height(), 57);

    QVERIFY(controller.setSampleIntervalMs(30000));
    QVERIFY(controller.run());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 30000);
    QVERIFY2(controller.phase() == QStringLiteral("completed"),
             qPrintable(controller.error() + QStringLiteral("\n") + controller.diagnostics()));
    QCOMPARE(controller.progress(), 100);
    QVERIFY(controller.diagnostics().contains(QStringLiteral("ffmpeg=%1").arg(ffmpeg)));
    QVERIFY(controller.diagnostics().contains(QStringLiteral("timestampMs=109000")));
    QVERIFY(controller.diagnostics().contains(QStringLiteral("normalizedRoi=x=0.000000 y=0.883000 w=1.000000 h=0.105000")));
    QVERIFY(controller.diagnostics().contains(QStringLiteral("pixelCrop=x=0 y=159 w=320 h=19")));
    QVERIFY(controller.diagnostics().contains(QStringLiteral("result=readable")));
    QVERIFY(controller.diagnostics().contains(QStringLiteral("workspace-cleanup")));

    QVERIFY(controller.setRoi(0.0, 0.883, 0.01, 0.01));
    QVERIFY(controller.requestCropPreview(108000));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 10000);
    QImageReader smallPreview(controller.cropPreviewUrl().toLocalFile());
    const QImage smallImage = smallPreview.read();
    QVERIFY2(!smallImage.isNull(), qPrintable(smallPreview.errorString()));
    QCOMPARE(smallImage.width(), 9);
    QCOMPARE(smallImage.height(), 6);

    const QString rotatedSource = directory.filePath(QStringLiteral("portrait rotated subtitle.mp4"));
    QProcess rotate;
    rotate.setProgram(ffmpeg);
    rotate.setArguments({QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                         QStringLiteral("-display_rotation:v"), QStringLiteral("90"),
                         QStringLiteral("-i"), source, QStringLiteral("-c"), QStringLiteral("copy"),
                         QStringLiteral("-y"), rotatedSource});
    rotate.start();
    QVERIFY2(rotate.waitForFinished(30000), qPrintable(rotate.errorString()));
    QVERIFY2(rotate.exitCode() == 0, rotate.readAllStandardError().constData());

    SubtitleOcrController rotatedController(nullptr, nullptr);
    QVERIFY(rotatedController.loadSource(rotatedSource));
    QTRY_VERIFY_WITH_TIMEOUT(!rotatedController.processing(), 10000);
    QCOMPARE(rotatedController.sourceWidth(), 180);
    QCOMPARE(rotatedController.sourceHeight(), 320);
    QVERIFY(rotatedController.setRoi(0.0, 0.883, 1.0, 0.105));
    QVERIFY(rotatedController.requestCropPreview(108000));
    QTRY_VERIFY_WITH_TIMEOUT(!rotatedController.processing(), 10000);
    QImageReader rotatedPreview(rotatedController.cropPreviewUrl().toLocalFile());
    const QImage rotatedImage = rotatedPreview.read();
    QVERIFY2(!rotatedImage.isNull(), qPrintable(rotatedPreview.errorString()));
    QCOMPARE(rotatedImage.width(), 540);
    QCOMPARE(rotatedImage.height(), 102);
    QVERIFY(rotatedController.diagnostics().contains(QStringLiteral("rotation=90")));
}

void TestSubtitleOcrController::acceptsLocallyStagedMediaAndPreservesSourceOnProbeFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    QVERIFY(fixtures.create());
    configure(fixtures);

    DubbingController dubbing(nullptr, nullptr, static_cast<ModelManager *>(nullptr),
                              static_cast<RuntimeManager *>(nullptr));
    SubtitleOcrController controller(nullptr, &dubbing);
    loadFixture(controller, fixtures.source);
    const QString originalSource = controller.sourcePath();

    const QString downloadedSource = directory.filePath(QStringLiteral("colab-downloaded-source.mp4"));
    QVERIFY(writeFile(downloadedSource, QByteArrayLiteral("downloaded-by-colab")));
    QVERIFY(controller.useDownloadedMedia(downloadedSource));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 10000);
    QVERIFY2(controller.error().isEmpty(), qPrintable(controller.error()));
    QCOMPARE(controller.sourcePath(), QFileInfo(downloadedSource).absoluteFilePath());
    QVERIFY(QFileInfo(controller.sourcePath()).isFile());

    const QString stagedSource = controller.sourcePath();
    QVERIFY(writeFile(fixtures.ffprobe, QByteArrayLiteral("@echo off\r\necho not-json\r\n")));
    QVERIFY(controller.useDownloadedMedia(originalSource));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 10000);
    QCOMPARE(controller.sourcePath(), stagedSource);
    QVERIFY(controller.error().contains(QStringLiteral("readable video stream"), Qt::CaseInsensitive));

    QVERIFY(writeFile(fixtures.ffprobe, QByteArrayLiteral("@echo off\r\necho {\"streams\":[{\"width\":1920,\"height\":1080}],\"format\":{\"duration\":\"4.0\"}}\r\n")));
    QVERIFY(controller.useDownloadedMedia(originalSource));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing(), 10000);
    QVERIFY2(controller.error().isEmpty(), qPrintable(controller.error()));
    QCOMPARE(controller.sourcePath(), originalSource);
}

void TestSubtitleOcrController::rejectsRemoteMediaLinksBeforeAnyDesktopRequest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OcrRuntimeEnvironment environment;
    OcrFixtures fixtures(directory);
    QVERIFY(fixtures.create());
    configure(fixtures);

    DubbingController dubbing(nullptr, nullptr, static_cast<ModelManager *>(nullptr),
                              static_cast<RuntimeManager *>(nullptr));
    SubtitleOcrController controller(nullptr, &dubbing);
    SharedMediaServer server(350, false);
    QVERIFY(server.start());

    QVERIFY(!controller.importSourceLink(server.url().toString()));
    QVERIFY(!controller.processing());
    QVERIFY(!controller.sourceImporting());
    QCOMPARE(controller.sourceImportTotalBytes(), qint64(-1));
    QVERIFY(controller.sourceImportError().contains(QStringLiteral("download the public link locally"), Qt::CaseInsensitive));
    QTest::qWait(500);
    QCOMPARE(server.requestCount(), 0);
}

} // namespace LAStudio
