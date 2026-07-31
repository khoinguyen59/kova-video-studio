#include "test_SubtitleOcrController.h"

#include "controllers/dubbing/DubbingController.h"
#include "controllers/subtitles/SubtitleOcrController.h"
#include "controllers/tts/SubtitleVoiceController.h"
#include "core/PathUtils.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace LAStudio {
namespace {

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Text)
        && file.write(contents) == contents.size();
}

struct OcrRuntimeEnvironment {
    QByteArray ffmpeg = qgetenv("LASTUDIO_FFMPEG");
    QByteArray ffprobe = qgetenv("LASTUDIO_FFPROBE");
    QByteArray tesseract = qgetenv("LASTUDIO_TESSERACT");
    bool hadFfmpeg = qEnvironmentVariableIsSet("LASTUDIO_FFMPEG");
    bool hadFfprobe = qEnvironmentVariableIsSet("LASTUDIO_FFPROBE");
    bool hadTesseract = qEnvironmentVariableIsSet("LASTUDIO_TESSERACT");

    ~OcrRuntimeEnvironment()
    {
        if (hadFfmpeg) qputenv("LASTUDIO_FFMPEG", ffmpeg); else qunsetenv("LASTUDIO_FFMPEG");
        if (hadFfprobe) qputenv("LASTUDIO_FFPROBE", ffprobe); else qunsetenv("LASTUDIO_FFPROBE");
        if (hadTesseract) qputenv("LASTUDIO_TESSERACT", tesseract); else qunsetenv("LASTUDIO_TESSERACT");
    }
};

struct OcrFixtures {
    explicit OcrFixtures(QTemporaryDir &directory)
        : source(directory.filePath(QStringLiteral("source video.mp4")))
        , ffprobe(directory.filePath(QStringLiteral("ffprobe.cmd")))
        , ffmpeg(directory.filePath(QStringLiteral("ffmpeg.cmd")))
        , slowFfmpeg(directory.filePath(QStringLiteral("slow-ffmpeg.cmd")))
        , tesseract(directory.filePath(QStringLiteral("tesseract.cmd")))
    {
    }

    bool create() const
    {
        return writeFile(source, QByteArrayLiteral("not-a-real-video-fixture"))
            && writeFile(ffprobe, QByteArrayLiteral("@echo off\r\necho {\"streams\":[{\"width\":1920,\"height\":1080}],\"format\":{\"duration\":\"4.0\"}}\r\n"))
            && writeFile(ffmpeg, QByteArrayLiteral("@echo off\r\nset \"last=\"\r\n:next\r\nif \"%~1\"==\"\" goto done\r\nset \"last=%~1\"\r\nshift\r\ngoto next\r\n:done\r\n> \"%last%\" echo same-cropped-frame\r\n"))
            && writeFile(slowFfmpeg, QByteArrayLiteral("@echo off\r\nping 127.0.0.1 -n 4 > nul\r\nset \"last=\"\r\n:next\r\nif \"%~1\"==\"\" goto done\r\nset \"last=%~1\"\r\nshift\r\ngoto next\r\n:done\r\n> \"%last%\" echo delayed-cropped-frame\r\n"))
            && writeFile(tesseract, QByteArrayLiteral("@echo off\r\nif /I \"%~1\"==\"--list-langs\" (\r\n  echo List of available languages in C:\\fixture\\tessdata ^(1^):\r\n  echo eng\r\n  exit /b 0\r\n)\r\necho level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\r\necho 5\t1\t1\t1\t1\t1\t0\t0\t10\t10\t96\tReviewed\r\necho 5\t1\t1\t1\t1\t2\t10\t0\t10\t10\t94\tsubtitle\r\n"));
    }

    QString source;
    QString ffprobe;
    QString ffmpeg;
    QString slowFfmpeg;
    QString tesseract;
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
    QCOMPARE(controller.phase(), QStringLiteral("completed"));
    QCOMPARE(controller.progress(), 100);
    QVERIFY(controller.progressAvailable());
    QCOMPARE(controller.segments().size(), 1);
    const QVariantMap segment = controller.segments().constFirst().toMap();
    QCOMPARE(segment.value(QStringLiteral("text")).toString(), QStringLiteral("Reviewed subtitle"));
    QCOMPARE(segment.value(QStringLiteral("startMs")).toLongLong(), qint64(0));
    QCOMPARE(segment.value(QStringLiteral("endMs")).toLongLong(), qint64(5000));
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

void TestSubtitleOcrController::importsSharedStagedMediaWithoutRedownloadAndPreservesSourceOnProbeFailure()
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

    SharedMediaServer server;
    QVERIFY(server.start());
    QVERIFY(controller.importSourceLink(server.url().toString()));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing() && !controller.sourceImporting(), 10000);
    QVERIFY2(controller.error().isEmpty(), qPrintable(controller.error()));
    QVERIFY(controller.sourcePath() != originalSource);
    QVERIFY(QFileInfo(controller.sourcePath()).isFile());
    QCOMPARE(server.requestCount(), 1);

    const QString stagedSource = controller.sourcePath();
    QVERIFY(writeFile(fixtures.ffprobe, QByteArrayLiteral("@echo off\r\necho not-json\r\n")));
    QVERIFY(controller.importSourceLink(server.url().toString()));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing() && !controller.sourceImporting(), 10000);
    QCOMPARE(controller.sourcePath(), stagedSource);
    QVERIFY(controller.error().contains(QStringLiteral("readable video stream"), Qt::CaseInsensitive));
    QCOMPARE(server.requestCount(), 2);

    QVERIFY(writeFile(fixtures.ffprobe, QByteArrayLiteral("@echo off\r\necho {\"streams\":[{\"width\":1920,\"height\":1080}],\"format\":{\"duration\":\"4.0\"}}\r\n")));
    SharedMediaServer delayedServer(250);
    QVERIFY(delayedServer.start());
    QVERIFY(controller.importSourceLink(delayedServer.url().toString()));
    QTRY_VERIFY_WITH_TIMEOUT(controller.sourceImporting(), 2000);
    // Wait for the transfer to have actually reached the shared backend.
    // sourceImporting becomes true synchronously while the request is still
    // being scheduled, so canceling before this point would not exercise the
    // active-transfer cancellation and retry contract.
    QTRY_COMPARE_WITH_TIMEOUT(delayedServer.requestCount(), 1, 2000);
    controller.cancelSourceImport();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.sourceImporting(), 5000);
    QCOMPARE(controller.sourcePath(), stagedSource);
    QVERIFY(controller.sourceImportError().contains(QStringLiteral("canceled"), Qt::CaseInsensitive));

    QVERIFY(controller.retrySourceImport());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing() && !controller.sourceImporting(), 10000);
    QVERIFY2(controller.error().isEmpty(), qPrintable(controller.error()));
    QVERIFY(controller.sourcePath() != stagedSource);
    QCOMPARE(delayedServer.requestCount(), 2);
}

void TestSubtitleOcrController::importsSharedMediaWithAnUnknownContentLength()
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

    QVERIFY(controller.importSourceLink(server.url().toString()));
    QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.sourceImporting(), 2000);
    QCOMPARE(controller.sourceImportTotalBytes(), qint64(-1));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.processing() && !controller.sourceImporting(), 10000);
    QVERIFY2(controller.error().isEmpty(), qPrintable(controller.error()));
    QVERIFY(QFileInfo(controller.sourcePath()).isFile());
}

} // namespace LAStudio
