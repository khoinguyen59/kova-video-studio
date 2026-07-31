#include "test_MediaIngestService.h"

#include "core/MediaRuntimeLocator.h"
#include "controllers/dubbing/DubbingController.h"
#include "audio/WavIO.h"
#include "dubbing/media/MediaIngestService.h"
#include "dubbing/media/RemoteMediaImportService.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtTest>

namespace LAStudio {
namespace {

class DirectMediaServer final : public QObject
{
public:
    explicit DirectMediaServer(QByteArray body = QByteArray("RIFFmock-wave-data"),
                               int bodyDelayMs = 0,
                               qint64 announcedBytes = -1)
        : m_body(std::move(body))
        , m_bodyDelayMs(bodyDelayMs)
        , m_announcedBytes(announcedBytes)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    if (!socket->property("requestHandled").toBool()
                        && socket->bytesAvailable() > 0) {
                        socket->setProperty("requestHandled", true);
                        ++m_requestCount;
                        const qint64 contentLength = m_announcedBytes >= 0
                            ? m_announcedBytes : m_body.size();
                        socket->write("HTTP/1.1 200 OK\r\n"
                                      "Content-Type: audio/wav\r\n"
                                      "Content-Length: " + QByteArray::number(contentLength) + "\r\n"
                                      "Connection: close\r\n\r\n");
                        const auto writeBody = [socket, body = m_body]() {
                            if (socket->state() == QAbstractSocket::ConnectedState) {
                                socket->write(body);
                                socket->disconnectFromHost();
                            }
                        };
                        if (m_bodyDelayMs > 0)
                            QTimer::singleShot(m_bodyDelayMs, socket, writeBody);
                        else
                            writeBody();
                    }
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QUrl url() const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/ti%E1%BA%BFng%20Vi%E1%BB%87t.wav?temporary=not-persisted")
                        .arg(m_server.serverPort()));
    }
    int requestCount() const { return m_requestCount; }

private:
    QTcpServer m_server;
    QByteArray m_body;
    int m_bodyDelayMs = 0;
    qint64 m_announcedBytes = -1;
    int m_requestCount = 0;
};

} // namespace

void TestMediaIngestService::rejectsMissingInputExactlyOnce()
{
    MediaIngestService service;
    QSignalSpy finished(&service, &MediaIngestService::finished);
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    service.ingest(temporaryDirectory.filePath(QStringLiteral("missing-input.wav")));

    QCOMPARE(finished.count(), 1);
    const QList<QVariant> result = finished.takeFirst();
    QCOMPARE(result.at(0).toBool(), false);
    QVERIFY(result.at(2).toString().contains(QStringLiteral("does not exist"), Qt::CaseInsensitive));
}

void TestMediaIngestService::prefersBundledMediaToolsOverExternalConfiguration()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString mediaToolsDirectory = temporaryDirectory.filePath(QStringLiteral("media-tools"));
    QVERIFY(QDir().mkpath(mediaToolsDirectory));
    const QString ffmpeg = mediaToolsDirectory + QStringLiteral("/ffmpeg.exe");
    const QString ffprobe = mediaToolsDirectory + QStringLiteral("/ffprobe.exe");
    const QString ytDlp = mediaToolsDirectory + QStringLiteral("/yt-dlp.exe");
    QVERIFY(QFile(ffmpeg).open(QIODevice::WriteOnly));
    QVERIFY(QFile(ffprobe).open(QIODevice::WriteOnly));
    QVERIFY(QFile(ytDlp).open(QIODevice::WriteOnly));

    const QByteArray originalFfmpeg = qgetenv("LASTUDIO_FFMPEG");
    const QByteArray originalFfprobe = qgetenv("LASTUDIO_FFPROBE");
    qputenv("LASTUDIO_FFMPEG", QByteArray("C:/not-used/ffmpeg.exe"));
    qputenv("LASTUDIO_FFPROBE", QByteArray("C:/not-used/ffprobe.exe"));
    const MediaRuntimePaths runtime = MediaRuntimeLocator::resolveForApplicationDirectory(temporaryDirectory.path());
    if (originalFfmpeg.isEmpty()) qunsetenv("LASTUDIO_FFMPEG");
    else qputenv("LASTUDIO_FFMPEG", originalFfmpeg);
    if (originalFfprobe.isEmpty()) qunsetenv("LASTUDIO_FFPROBE");
    else qputenv("LASTUDIO_FFPROBE", originalFfprobe);

    QCOMPARE(QDir::cleanPath(runtime.ffmpeg), QDir::cleanPath(ffmpeg));
    QCOMPARE(QDir::cleanPath(runtime.ffprobe), QDir::cleanPath(ffprobe));
    QCOMPARE(QDir::cleanPath(runtime.ytDlp), QDir::cleanPath(ytDlp));
    QVERIFY(runtime.hasYtDlp());
    QVERIFY(runtime.isComplete());
}

void TestMediaIngestService::downloadsDirectLoopbackMediaIntoOwnedStaging()
{
    QTemporaryDir staging;
    QVERIFY(staging.isValid());
    DirectMediaServer server;
    QVERIFY(server.start());

    RemoteMediaImportService service(staging.path());
    QSignalSpy finished(&service, &RemoteMediaImportService::finished);
    QVERIFY(service.download(server.url()));
    QTRY_COMPARE(finished.count(), 1);

    const QList<QVariant> result = finished.takeFirst();
    QVERIFY(result.at(0).toBool());
    const QString path = result.at(1).toString();
    QVERIFY(QFileInfo(path).isFile());
    QVERIFY(QDir::cleanPath(path).startsWith(QDir::cleanPath(staging.path()) + QLatin1Char('/')));
    QVERIFY(!path.contains(QStringLiteral("temporary")));
    QVERIFY(QFileInfo(path).fileName().contains(QStringLiteral("tiếng Việt")));
    QFile downloaded(path);
    QVERIFY(downloaded.open(QIODevice::ReadOnly));
    QCOMPARE(downloaded.readAll(), QByteArray("RIFFmock-wave-data"));
}

void TestMediaIngestService::reportsByteProgressForDirectMediaDownload()
{
    QTemporaryDir staging;
    QVERIFY(staging.isValid());
    const QByteArray body(32 * 1024, 'a');
    DirectMediaServer server(body);
    QVERIFY(server.start());

    RemoteMediaImportService service(staging.path());
    QSignalSpy progress(&service, &RemoteMediaImportService::transferProgress);
    QSignalSpy finished(&service, &RemoteMediaImportService::finished);
    QVERIFY(service.download(server.url()));
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(finished.constFirst().at(0).toBool());
    QVERIFY(!progress.isEmpty());
    const QList<QVariant> lastUpdate = progress.constLast();
    QCOMPARE(lastUpdate.at(0).toLongLong(), qint64(body.size()));
    QCOMPARE(lastUpdate.at(1).toLongLong(), qint64(body.size()));
}

void TestMediaIngestService::cancelRemovesPartialStagedMedia()
{
    QTemporaryDir staging;
    QVERIFY(staging.isValid());
    DirectMediaServer server(QByteArray(64 * 1024, 'b'), 300);
    QVERIFY(server.start());

    RemoteMediaImportService service(staging.path());
    QSignalSpy finished(&service, &RemoteMediaImportService::finished);
    QVERIFY(service.download(server.url()));
    QTRY_VERIFY(server.requestCount() == 1);
    service.cancel();
    QTRY_COMPARE(finished.count(), 1);
    const QList<QVariant> result = finished.constFirst();
    QVERIFY(!result.at(0).toBool());
    QVERIFY(result.at(2).toString().contains(QStringLiteral("canceled"), Qt::CaseInsensitive));
    QVERIFY(QDir(staging.path()).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty());
}

void TestMediaIngestService::rejectsOversizedMediaBeforeStaging()
{
    QTemporaryDir staging;
    QVERIFY(staging.isValid());
    DirectMediaServer server(QByteArray("small-body"), 300, 2LL * 1024 * 1024 * 1024 + 1);
    QVERIFY(server.start());

    RemoteMediaImportService service(staging.path());
    QSignalSpy finished(&service, &RemoteMediaImportService::finished);
    QVERIFY(service.download(server.url()));
    QTRY_COMPARE(finished.count(), 1);
    const QList<QVariant> result = finished.constFirst();
    QVERIFY(!result.at(0).toBool());
    QVERIFY(result.at(2).toString().contains(QStringLiteral("2 GiB")));
    QVERIFY(QDir(staging.path()).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty());
}

void TestMediaIngestService::rejectsUnsafeRemoteMediaUrl()
{
    QTemporaryDir staging;
    QVERIFY(staging.isValid());
    RemoteMediaImportService service(staging.path());
    QSignalSpy finished(&service, &RemoteMediaImportService::finished);

    QVERIFY(!service.download(QUrl(QStringLiteral("ftp://example.invalid/media.wav"))));
    QCOMPARE(finished.count(), 1);
    const QList<QVariant> result = finished.takeFirst();
    QVERIFY(!result.at(0).toBool());
    QVERIFY(result.at(2).toString().contains(QStringLiteral("HTTPS")));

    QSignalSpy userInfoFinished(&service, &RemoteMediaImportService::finished);
    QVERIFY(!service.download(QUrl(QStringLiteral("https://user:secret@example.invalid/media.wav"))));
    QCOMPARE(userInfoFinished.count(), 1);
    QVERIFY(!userInfoFinished.constFirst().at(0).toBool());

    QSignalSpy privateAddressFinished(&service, &RemoteMediaImportService::finished);
    QVERIFY(!service.download(QUrl(QStringLiteral("https://192.168.1.99/private.wav"))));
    QCOMPARE(privateAddressFinished.count(), 1);
    QVERIFY(privateAddressFinished.constFirst().at(2).toString().contains(QStringLiteral("HTTPS")));
}

void TestMediaIngestService::rejectsUnsafePublicAdapterResults()
{
    QTemporaryDir staging;
    QVERIFY(staging.isValid());
    const QByteArray previous = qgetenv("LASTUDIO_YTDLP");
    const bool hadPrevious = qEnvironmentVariableIsSet("LASTUDIO_YTDLP");
    const auto restoreAdapter = [&] {
        if (hadPrevious) qputenv("LASTUDIO_YTDLP", previous);
        else qunsetenv("LASTUDIO_YTDLP");
    };

    const auto expectRejected = [&](const QString &script, const QString &expectedError) {
        const QString adapterPath = staging.filePath(
            QStringLiteral("fake-yt-dlp-%1.cmd").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QFile adapter(adapterPath);
        QVERIFY(adapter.open(QIODevice::WriteOnly | QIODevice::Text));
        adapter.write(script.toUtf8());
        adapter.close();
        qputenv("LASTUDIO_YTDLP", adapterPath.toUtf8());

        RemoteMediaImportService service(staging.path());
        QSignalSpy finished(&service, &RemoteMediaImportService::finished);
        QVERIFY(service.download(QUrl(QStringLiteral("https://www.youtube.com/watch?v=fixture"))));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(!finished.constFirst().at(0).toBool());
        QVERIFY2(finished.constFirst().at(2).toString().contains(expectedError),
                 qPrintable(finished.constFirst().at(2).toString()));
    };

    expectRejected(QStringLiteral("@echo off\r\nexit /b 2\r\n"), QStringLiteral("could not resolve"));
    expectRejected(QStringLiteral("@echo off\r\necho https://example.com/a.mp4\r\necho https://example.com/b.mp4\r\n"),
                   QStringLiteral("zero or multiple"));
    expectRejected(QStringLiteral("@echo off\r\necho https://192.168.1.99/private.mp4\r\n"),
                   QStringLiteral("unsafe"));
    restoreAdapter();
}

void TestMediaIngestService::resolvesPublicVideoPageThroughManagedAdapter()
{
    QTemporaryDir staging;
    QVERIFY(staging.isValid());
    DirectMediaServer server;
    QVERIFY(server.start());

    const QString adapterPath = staging.filePath(QStringLiteral("fake-yt-dlp.cmd"));
    QFile adapter(adapterPath);
    QVERIFY(adapter.open(QIODevice::WriteOnly | QIODevice::Text));
    adapter.write("@echo off\r\necho " + server.url().toString(QUrl::FullyEncoded).toUtf8() + "\r\n");
    adapter.close();
    const QByteArray previous = qgetenv("LASTUDIO_YTDLP");
    const bool hadPrevious = qEnvironmentVariableIsSet("LASTUDIO_YTDLP");
    qputenv("LASTUDIO_YTDLP", adapterPath.toUtf8());

    const QStringList publicUrls{
        QStringLiteral("https://www.youtube.com/watch?v=fixture"),
        QStringLiteral("https://www.tiktok.com/@fixture/video/123"),
        QStringLiteral("https://www.douyin.com/video/123"),
        QStringLiteral("https://v.douyin.com/fixture/")
    };
    for (const QString &publicUrl : publicUrls) {
        RemoteMediaImportService service(staging.path());
        QSignalSpy finished(&service, &RemoteMediaImportService::finished);
        QVERIFY(service.download(QUrl(publicUrl)));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        const QList<QVariant> result = finished.constFirst();
        QVERIFY2(result.at(0).toBool(), qPrintable(result.at(2).toString()));
        QVERIFY(QFileInfo(result.at(1).toString()).isFile());
    }
    if (hadPrevious) qputenv("LASTUDIO_YTDLP", previous);
    else qunsetenv("LASTUDIO_YTDLP");
    QCOMPARE(server.requestCount(), publicUrls.size());
}

void TestMediaIngestService::controllerCommitsDirectLinkOnlyAfterRealProbeAndNormalization()
{
    MediaIngestService mediaRuntimeCheck;
    if (!mediaRuntimeCheck.available()) {
        QSKIP("This integration regression requires the managed FFmpeg/FFprobe runtime. Set LASTUDIO_FFMPEG and LASTUDIO_FFPROBE in the test environment.");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fixturePath = dir.filePath(QStringLiteral("fixture.wav"));
    const QVector<float> samples(16000, 0.02F);
    QVERIFY(WavIO::saveFloat(fixturePath, samples.constData(), samples.size(), 16000));
    QFile fixture(fixturePath);
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    DirectMediaServer server(fixture.readAll());
    QVERIFY(server.start());

    DubbingController controller(nullptr, nullptr);
    const QString projectPath = dir.filePath(QStringLiteral("direct-link.ladub.json"));
    QVERIFY(controller.newProject(projectPath));
    const QString originalProjectPath = controller.sourceMediaPath();
    QVERIFY(originalProjectPath.isEmpty());

    QVERIFY(controller.importMediaFromLink(server.url().toString()));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.linkImporting(), 15000);
    QVERIFY2(controller.lastError().isEmpty(), qPrintable(controller.lastError()));
    QVERIFY(QFileInfo(controller.sourceMediaPath()).isFile());
    QVERIFY(QFileInfo(controller.normalizedAudioPath()).isFile());
    QVERIFY(QFileInfo(controller.vocalsPath()).isFile());
    QVERIFY(!controller.sourceMediaPath().contains(QStringLiteral("temporary")));
    QVERIFY(!controller.sourceMediaPath().contains(server.url().query()));
    QVERIFY(controller.saveProject());

    QFile project(projectPath);
    QVERIFY(project.open(QIODevice::ReadOnly));
    const QByteArray serialized = project.readAll();
    QVERIFY(!serialized.contains("temporary=not-persisted"));
    QVERIFY(!serialized.contains("127.0.0.1"));
}

void TestMediaIngestService::standaloneDownloadHandsOffOwnedMediaWithoutSecondDownload()
{
    MediaIngestService mediaRuntimeCheck;
    if (!mediaRuntimeCheck.available()) {
        QSKIP("This integration regression requires the managed FFmpeg/FFprobe runtime. Set LASTUDIO_FFMPEG and LASTUDIO_FFPROBE in the test environment.");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fixturePath = dir.filePath(QStringLiteral("handoff.wav"));
    const QVector<float> samples(16000, 0.02F);
    QVERIFY(WavIO::saveFloat(fixturePath, samples.constData(), samples.size(), 16000));
    QFile fixture(fixturePath);
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    DirectMediaServer server(fixture.readAll());
    QVERIFY(server.start());

    DubbingController controller(nullptr, nullptr);
    QVERIFY(controller.downloadMediaFromLink(server.url().toString()));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.linkImporting(), 15000);
    QVERIFY2(controller.lastError().isEmpty(), qPrintable(controller.lastError()));
    QVERIFY(controller.downloadedMediaReady());
    const QString stagedPath = controller.downloadedMediaPath();
    QVERIFY(QFileInfo(stagedPath).isFile());
    QCOMPARE(server.requestCount(), 1);

    QVERIFY(controller.handoffDownloadedMediaToDubbing());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.linkImporting(), 15000);
    QVERIFY2(controller.lastError().isEmpty(), qPrintable(controller.lastError()));
    QCOMPARE(server.requestCount(), 1);
    QVERIFY(QFileInfo(controller.sourceMediaPath()).isFile());
    QVERIFY(QFileInfo(controller.normalizedAudioPath()).isFile());
    QVERIFY(!controller.downloadedMediaReady());
}

void TestMediaIngestService::standaloneDownloadKeepsExistingProjectWhenProbeFails()
{
    MediaIngestService mediaRuntimeCheck;
    if (!mediaRuntimeCheck.available()) {
        QSKIP("This integration regression requires the managed FFmpeg/FFprobe runtime. Set LASTUDIO_FFMPEG and LASTUDIO_FFPROBE in the test environment.");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString existingPath = dir.filePath(QStringLiteral("existing.wav"));
    const QVector<float> samples(16000, 0.02F);
    QVERIFY(WavIO::saveFloat(existingPath, samples.constData(), samples.size(), 16000));
    DirectMediaServer invalidServer(QByteArray("not-a-media-file"));
    QVERIFY(invalidServer.start());

    DubbingController controller(nullptr, nullptr);
    QVERIFY(controller.newProject(dir.filePath(QStringLiteral("existing.ladub.json"))));
    QVERIFY(controller.importMedia(existingPath));
    const QString previousSource = controller.sourceMediaPath();
    QVERIFY(QFileInfo(previousSource).isFile());

    QVERIFY(controller.downloadMediaFromLink(invalidServer.url().toString()));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.linkImporting(), 15000);
    QVERIFY(controller.downloadedMediaReady());
    QVERIFY(controller.handoffDownloadedMediaToDubbing());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.linkImporting(), 15000);
    QCOMPARE(controller.sourceMediaPath(), previousSource);
    QVERIFY(!controller.lastError().isEmpty());
    QVERIFY(controller.downloadedMediaReady());
    QCOMPARE(invalidServer.requestCount(), 1);
}

void TestMediaIngestService::downloadRouteAndDubbingLinkControlAreWired()
{
    const QDir sourceRoot(QStringLiteral(LASTUDIO_SOURCE_DIR));
    const auto readSource = [&sourceRoot](const QString &relativePath) {
        QFile file(sourceRoot.filePath(relativePath));
        if (!file.open(QIODevice::ReadOnly)) return QString();
        return QString::fromUtf8(file.readAll());
    };

    const QString routes = readSource(QStringLiteral("qml/components/shared/StudioRouteRegistry.qml"));
    const QString main = readSource(QStringLiteral("qml/Main.qml"));
    const QString page = readSource(QStringLiteral("qml/pages/MediaDownloadPage.qml"));
    const QString dubbingSource = readSource(QStringLiteral("qml/components/dubbing/DubbingSourceMediaPanel.qml"));
    const QString popup = readSource(QStringLiteral("qml/components/DownloadsPopup.qml"));
    QVERIFY(routes.contains(QStringLiteral("media-download")));
    QVERIFY(routes.contains(QStringLiteral("label: qsTr(\"Download\")")));
    QVERIFY(main.contains(QStringLiteral("id: mediaDownloadLoader")));
    QVERIFY(main.contains(QStringLiteral("MediaDownloadPage")));
    QVERIFY(main.contains(QStringLiteral("case 14: return mediaDownloadLoader.status === Loader.Ready")));
    QVERIFY(page.contains(QStringLiteral("downloadMediaFromLink")));
    QVERIFY(page.contains(QStringLiteral("handoffDownloadedMediaToDubbing")));
    QVERIFY(page.contains(QStringLiteral("downloadedMediaPath")));
    QVERIFY(page.contains(QStringLiteral("Use in Dubbing")));
    QVERIFY(page.contains(QStringLiteral("public YouTube, TikTok, or Douyin video")));
    QVERIFY(page.contains(QStringLiteral("playlists, login/cookies, DRM/paywalls")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Import direct media, YouTube, TikTok, or Douyin link")));
    QCOMPARE(dubbingSource.count(QStringLiteral("Import direct media, YouTube, TikTok, or Douyin link")), 1);
    QVERIFY(dubbingSource.contains(QStringLiteral("Keep the direct-link import action above the fill-height preview")));
    QVERIFY(popup.contains(QStringLiteral("AppController.downloads.allDownloads")));
}

} // namespace LAStudio
