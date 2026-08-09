#include "test_MediaIngestService.h"

#include "core/MediaRuntimeLocator.h"
#include "controllers/dubbing/DubbingController.h"
#include "audio/WavIO.h"
#include "dubbing/media/MediaIngestService.h"
#include "dubbing/media/RemoteMediaImportService.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QScopeGuard>
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

class RedirectMediaServer final : public QObject
{
public:
    explicit RedirectMediaServer(QUrl target)
        : m_target(std::move(target))
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    if (socket->property("requestHandled").toBool() || socket->bytesAvailable() == 0) return;
                    socket->setProperty("requestHandled", true);
                    ++m_requestCount;
                    socket->write("HTTP/1.1 302 Found\r\nLocation: "
                                  + m_target.toString(QUrl::FullyEncoded).toUtf8()
                                  + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/redirect").arg(m_server.serverPort()));
    }
    int requestCount() const { return m_requestCount; }

private:
    QTcpServer m_server;
    QUrl m_target;
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

void TestMediaIngestService::rejectsPrivateRedirectBeforeAnyStaging()
{
    QTemporaryDir staging;
    QVERIFY(staging.isValid());
    RedirectMediaServer redirector(QUrl(QStringLiteral("http://192.168.1.99/private.wav")));
    QVERIFY(redirector.start());

    RemoteMediaImportService service(staging.path());
    QSignalSpy finished(&service, &RemoteMediaImportService::finished);
    QVERIFY(service.download(redirector.url()));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    const QList<QVariant> result = finished.constFirst();
    QVERIFY(!result.at(0).toBool());
    QVERIFY(result.at(2).toString().contains(QStringLiteral("redirected to an unsafe")));
    QCOMPARE(redirector.requestCount(), 1);
    QVERIFY(QDir(staging.path()).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty());
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

void TestMediaIngestService::resolverTimeoutCanRetry()
{
    QTemporaryDir staging;
    QVERIFY(staging.isValid());
    DirectMediaServer server;
    QVERIFY(server.start());
    const QString adapterPath = staging.filePath(QStringLiteral("slow-yt-dlp.cmd"));
    const QByteArray previous = qgetenv("LASTUDIO_YTDLP");
    const bool hadPrevious = qEnvironmentVariableIsSet("LASTUDIO_YTDLP");
    qputenv("LASTUDIO_YTDLP", adapterPath.toUtf8());
    const auto restoreAdapter = qScopeGuard([previous, hadPrevious] {
        if (hadPrevious) qputenv("LASTUDIO_YTDLP", previous);
        else qunsetenv("LASTUDIO_YTDLP");
    });

    QFile adapter(adapterPath);
    QVERIFY(adapter.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(adapter.write("@echo off\r\nping 127.0.0.1 -n 5 > nul\r\n") > 0);
    adapter.close();

    // One second is deliberately above normal Windows process-start latency
    // for the successful retry while still making the intentionally stalled
    // adapter fail quickly and deterministically.
    RemoteMediaImportService service(staging.path(), nullptr, 1000);
    QSignalSpy finished(&service, &RemoteMediaImportService::finished);
    QVERIFY(service.download(QUrl(QStringLiteral("https://www.youtube.com/watch?v=fixture"))));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY(!finished.constFirst().at(0).toBool());
    QVERIFY(finished.constFirst().at(2).toString().contains(QStringLiteral("timed out")));
    QTRY_VERIFY_WITH_TIMEOUT(!service.active(), 1000);
    QTest::qWait(100);

    QVERIFY(adapter.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
    QVERIFY(adapter.write("@echo off\r\necho "
                          + server.url().toString(QUrl::FullyEncoded).toUtf8() + "\r\n") > 0);
    adapter.close();
    QVERIFY(service.download(QUrl(QStringLiteral("https://www.youtube.com/watch?v=retry"))));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 10000);
    QVERIFY(finished.constLast().at(0).toBool());
    QCOMPARE(server.requestCount(), 1);
}

void TestMediaIngestService::resolverArgumentsKeepUntrustedUrlPositional()
{
    const QUrl source(QStringLiteral("https://www.youtube.com/watch?v=fixture&--output=evil"));
    const QStringList arguments = RemoteMediaImportService::publicVideoResolverArguments(source);
    QCOMPARE(arguments, QStringList({QStringLiteral("--no-playlist"), QStringLiteral("--no-warnings"),
                                     QStringLiteral("--no-cookies"), QStringLiteral("--get-url"),
                                     QStringLiteral("--"), source.toString(QUrl::FullyEncoded)}));
    QCOMPARE(arguments.count(source.toString(QUrl::FullyEncoded)), 1);
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

void TestMediaIngestService::controllerQueuesMultipleDirectDownloadsWithoutPersistingUrls()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fixturePath = dir.filePath(QStringLiteral("batch-fixture.wav"));
    const QVector<float> samples(8000, 0.02F);
    QVERIFY(WavIO::saveFloat(fixturePath, samples.constData(), samples.size(), 16000));
    QFile fixture(fixturePath);
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const QByteArray body = fixture.readAll();
    DirectMediaServer first(body);
    DirectMediaServer second(body);
    QVERIFY(first.start());
    QVERIFY(second.start());

    DubbingController controller(nullptr, nullptr);
    QCOMPARE(controller.enqueueMediaLinks(
                 first.url().toString() + QStringLiteral("\n") + second.url().toString()), 2);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.mediaQueueDownloading(), 15000);
    QCOMPARE(first.requestCount(), 1);
    QCOMPARE(second.requestCount(), 1);
    const QVariantList items = controller.mediaQueueItems();
    QCOMPARE(items.size(), 2);
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        QCOMPARE(item.value(QStringLiteral("downloadState")).toString(), QStringLiteral("downloaded"));
        QCOMPARE(item.value(QStringLiteral("processState")).toString(), QStringLiteral("ready"));
        QVERIFY(item.value(QStringLiteral("selected")).toBool());
        QVERIFY(QFileInfo(item.value(QStringLiteral("localPath")).toString()).isFile());
        QVERIFY(!item.contains(QStringLiteral("sourceUrl")));
        QVERIFY(!item.value(QStringLiteral("localPath")).toString().contains(QStringLiteral("temporary=")));
    }
}

void TestMediaIngestService::mediaBatchContinuesAfterARealWorkerFailure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fixturePath = dir.filePath(QStringLiteral("batch-worker-failure.wav"));
    const QVector<float> samples(8000, 0.02F);
    QVERIFY(WavIO::saveFloat(fixturePath, samples.constData(), samples.size(), 16000));

    // The controller has no STT session in this regression.  Each item still
    // runs the real ingest worker, then the real transcription worker rejects
    // its unavailable dependency.  This asserts the asynchronous error path
    // terminates that item and advances the serial queue instead of leaving a
    // permanent "running" item.
    DubbingController controller(nullptr, nullptr);
    QCOMPARE(controller.enqueueMediaFiles({fixturePath, fixturePath}), 2);
    QVERIFY(controller.startMediaQueue({{QStringLiteral("transcribe"), true}}));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.mediaQueueProcessing(), 20000);

    const QVariantList items = controller.mediaQueueItems();
    QCOMPARE(items.size(), 2);
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        QCOMPARE(item.value(QStringLiteral("processState")).toString(), QStringLiteral("failed"));
        QVERIFY(!item.value(QStringLiteral("error")).toString().trimmed().isEmpty());
        QVERIFY(item.value(QStringLiteral("progress")).toInt() >= 0);
    }
    QVERIFY(controller.mediaQueueStatus().contains(QStringLiteral("finished"), Qt::CaseInsensitive));
}

void TestMediaIngestService::mediaBatchCanRunEachStageAcrossTheSelectedQueue()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fixturePath = dir.filePath(QStringLiteral("stage-by-stage-fixture.wav"));
    const QVector<float> samples(8000, 0.02F);
    QVERIFY(WavIO::saveFloat(fixturePath, samples.constData(), samples.size(), 16000));

    // This uses the production ingest stage for both files.  STT intentionally
    // cannot start without a session, so the test proves the controller first
    // completes ingest for every selected item, then advances the queue to the
    // transcribe stage and terminates each real worker failure.
    DubbingController controller(nullptr, nullptr);
    QCOMPARE(controller.enqueueMediaFiles({fixturePath, fixturePath}), 2);
    QStringList startedStages;
    QStringList observedRunningKeys;
    connect(&controller, &DubbingController::mediaQueueChanged, &controller, [&] {
        for (const QVariant &value : controller.mediaQueueItems()) {
            const QVariantMap item = value.toMap();
            if (item.value(QStringLiteral("processState")).toString() != QStringLiteral("running")) continue;
            const QString key = item.value(QStringLiteral("id")).toString()
                + QLatin1Char(':') + item.value(QStringLiteral("stage")).toString();
            if (!observedRunningKeys.contains(key)) {
                observedRunningKeys.append(key);
                startedStages.append(item.value(QStringLiteral("stage")).toString());
            }
        }
    });

    QVERIFY(controller.startMediaQueue({{QStringLiteral("transcribe"), true},
                                        {QStringLiteral("executionMode"), QStringLiteral("stage-by-stage")}}));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.mediaQueueProcessing(), 20000);

    QCOMPARE(startedStages.count(QStringLiteral("ingest")), 2);
    const int firstTranscribe = startedStages.indexOf(QStringLiteral("transcribe"));
    QVERIFY(firstTranscribe >= 2);
    for (const QVariant &value : controller.mediaQueueItems()) {
        const QVariantMap item = value.toMap();
        QCOMPARE(item.value(QStringLiteral("executionMode")).toString(), QStringLiteral("stage-by-stage"));
        QCOMPARE(item.value(QStringLiteral("processState")).toString(), QStringLiteral("failed"));
        QVERIFY(item.value(QStringLiteral("completedStages")).toList().contains(QStringLiteral("ingest")));
    }
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
    const QString dubbingQueueDialog = readSource(QStringLiteral("qml/components/dubbing/DubbingMediaQueueDialog.qml"));
    const QString dubbingPage = readSource(QStringLiteral("qml/pages/DubbingPage.qml"));
    const QString popup = readSource(QStringLiteral("qml/components/DownloadsPopup.qml"));
    const QString sidebar = readSource(QStringLiteral("qml/components/Sidebar.qml"));
    const QString studioShell = readSource(QStringLiteral("qml/components/shared/StudioShell.qml"));
    const QString theme = readSource(QStringLiteral("qml/Theme.qml"));
    QVERIFY(routes.contains(QStringLiteral("media-download")));
    QVERIFY(routes.contains(QStringLiteral("label: qsTr(\"Download\")")));
    QVERIFY(main.contains(QStringLiteral("id: mediaDownloadLoader")));
    QVERIFY(main.contains(QStringLiteral("MediaDownloadPage")));
    QVERIFY(main.contains(QStringLiteral("case 14: return mediaDownloadLoader.status === Loader.Ready")));
    QVERIFY(page.contains(QStringLiteral("enqueueMediaLinks(sourceUrl.text)")));
    QVERIFY(page.contains(QStringLiteral("startMediaQueue({")));
    QVERIFY(page.contains(QStringLiteral("Complete one video, then next")));
    QVERIFY(page.contains(QStringLiteral("Complete each step for all videos")));
    QVERIFY(page.contains(QStringLiteral("executionMode\": root.batchExecutionMode")));
    QVERIFY(page.contains(QStringLiteral("mediaQueueItems")));
    QVERIFY(page.contains(QStringLiteral("source.srt")));
    QVERIFY(page.contains(QStringLiteral("translated.srt")));
    QVERIFY(page.contains(QStringLiteral("voice.wav")));
    QVERIFY(page.contains(QStringLiteral("vocals.wav")));
    QVERIFY(page.contains(QStringLiteral("background.wav")));
    QVERIFY(page.contains(QStringLiteral("public YouTube, TikTok, and Douyin pages")));
    QVERIFY(page.contains(QStringLiteral("Playlists, login/cookies, DRM/paywalls")));
    QVERIFY(dubbingSource.contains(QStringLiteral("mediaQueueRequested")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Queue direct media, YouTube, TikTok, or Douyin links")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Add link(s) to media queue")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Queue & batch settings")));
    QVERIFY(dubbingSource.contains(QStringLiteral("DubbingMediaQueueDialog")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Keep the direct-link import action above the fill-height preview")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("Complete one video, then next")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("Complete each step for all videos")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("executionMode\": root.batchExecutionMode")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("Run selected batch")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("source.srt")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("translated.srt")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("voice.wav")));
    QVERIFY(dubbingPage.contains(QStringLiteral("dubbingHistoryResizeHandle")));
    QVERIFY(dubbingPage.contains(QStringLiteral("dubbingWorkspaceResizeHandle")));
    QVERIFY(dubbingPage.contains(QStringLiteral("Drag to resize Dubbing History")));
    QVERIFY(dubbingPage.contains(QStringLiteral("Drag to resize Dubbing Preview")));
    QVERIFY(popup.contains(QStringLiteral("AppController.downloads.allDownloads")));
    QVERIFY(sidebar.contains(QStringLiteral("Expand navigation")));
    QVERIFY(sidebar.contains(QStringLiteral("labelsVisible")));
    QVERIFY(sidebar.contains(QStringLiteral("modelData.label")));
    QVERIFY(studioShell.contains(QStringLiteral("Drag to resize the left panel")));
    QVERIFY(studioShell.contains(QStringLiteral("Drag to resize the settings panel")));
    QVERIFY(studioShell.contains(QStringLiteral("clampedPanelWidth")));
    QVERIFY(theme.contains(QStringLiteral("#f3f1ff")));
    QVERIFY(theme.contains(QStringLiteral("#c7c2dc")));

    const QDir ttsExamples(sourceRoot.filePath(QStringLiteral("examples/tts")));
    const QDir voiceCloneExamples(sourceRoot.filePath(QStringLiteral("examples/voice-cloning")));
    QVERIFY(ttsExamples.exists());
    QVERIFY(voiceCloneExamples.exists());
    const QStringList allTtsExamples = [&] {
        QStringList result;
        const auto entries = ttsExamples.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries)
            result.append(QDir(entry.absoluteFilePath()).entryList({QStringLiteral("*.json")}, QDir::Files));
        return result;
    }();
    const QStringList allVoiceCloneExamples = [&] {
        QStringList result = voiceCloneExamples.entryList({QStringLiteral("*.json")}, QDir::Files);
        const auto entries = voiceCloneExamples.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries)
            result.append(QDir(entry.absoluteFilePath()).entryList({QStringLiteral("*.json")}, QDir::Files));
        return result;
    }();
    QVERIFY(allTtsExamples.size() >= 9);
    QVERIFY(allVoiceCloneExamples.size() >= 5);
}

} // namespace LAStudio
