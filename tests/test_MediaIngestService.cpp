#include "test_MediaIngestService.h"

#include "core/MediaRuntimeLocator.h"
#include "controllers/dubbing/DubbingController.h"
#include "audio/WavIO.h"
#include "dubbing/media/MediaIngestService.h"
#include "dubbing/media/RemoteMediaImportService.h"
#include "dubbing/media/DouyinBrowserSessionService.h"

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
    // The portable package stages the pinned public-video adapter next to the
    // app executable, not under media-tools with FFmpeg/FFprobe.
    const QString ytDlp = temporaryDirectory.filePath(QStringLiteral("yt-dlp.exe"));
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

void TestMediaIngestService::resolverArgumentsUseExplicitCookiesOnlyWhenProvided()
{
    const QUrl source(QStringLiteral("https://www.douyin.com/video/fixture"));
    const QString cookiePath = QStringLiteral("C:/selected/douyin-cookies.txt");
    const QStringList withoutCookies = RemoteMediaImportService::publicVideoResolverArguments(source);
    QVERIFY(withoutCookies.contains(QStringLiteral("--no-cookies")));
    QVERIFY(!withoutCookies.contains(QStringLiteral("--cookies")));

    const QStringList withCookies = RemoteMediaImportService::publicVideoResolverArguments(source, cookiePath);
    QVERIFY(withCookies.contains(QStringLiteral("--cookies")));
    QVERIFY(withCookies.contains(cookiePath));
    QVERIFY(!withCookies.contains(QStringLiteral("--no-cookies")));
    QCOMPARE(withCookies.last(), source.toString(QUrl::FullyEncoded));
}

void TestMediaIngestService::douyinBrowserArgumentsUseDedicatedProfileOnly()
{
    const QStringList arguments = DouyinBrowserSessionService::helperArguments(
        QStringLiteral("C:/app/douyin-browser/douyin_browser_session.py"),
        QStringLiteral("C:/app-data/douyin-browser-profile"), QStringLiteral("download"),
        QUrl(QStringLiteral("https://v.douyin.com/fixture/")),
        QStringLiteral("C:/app-data/staging/video.mp4"));
    QVERIFY(arguments.contains(QStringLiteral("--mode")));
    QCOMPARE(arguments.at(arguments.indexOf(QStringLiteral("--mode")) + 1), QStringLiteral("download"));
    QVERIFY(arguments.contains(QStringLiteral("--profile")));
    QVERIFY(arguments.contains(QStringLiteral("C:/app-data/douyin-browser-profile")));
    QVERIFY(arguments.contains(QStringLiteral("--url")));
    QVERIFY(arguments.contains(QStringLiteral("https://v.douyin.com/fixture/")));
    QVERIFY(arguments.contains(QStringLiteral("--output")));
    QVERIFY(arguments.contains(QStringLiteral("C:/app-data/staging/video.mp4")));
    QVERIFY(!arguments.contains(QStringLiteral("--cookies")));
    QVERIFY(!arguments.contains(QStringLiteral("--cookies-from-browser")));
}

void TestMediaIngestService::douyinBrowserDoesNotUseBrowserCookieImportFlags()
{
    const QStringList arguments = DouyinBrowserSessionService::helperArguments(
        QStringLiteral("helper.py"), QStringLiteral("profile"), QStringLiteral("check"),
        QUrl(QStringLiteral("https://www.douyin.com/")));
    const QString joined = arguments.join(QLatin1Char(' '));
    QVERIFY(!joined.contains(QStringLiteral("cookies"), Qt::CaseInsensitive));
    QVERIFY(!joined.contains(QStringLiteral("chrome"), Qt::CaseInsensitive));
    QVERIFY(!joined.contains(QStringLiteral("edge"), Qt::CaseInsensitive));
}

void TestMediaIngestService::explicitCookieFileIsCopiedTemporarilyAndRemovedAfterResolver()
{
    QTemporaryDir staging;
    QVERIFY(staging.isValid());
    DirectMediaServer server;
    QVERIFY(server.start());

    const QString cookiePath = staging.filePath(QStringLiteral("douyin-cookies.txt"));
    QFile cookie(cookiePath);
    QVERIFY(cookie.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(cookie.write("# Netscape HTTP Cookie File\n.douyin.com\tTRUE\t/\tFALSE\t0\ts_v_web_id\tfixture\n") > 0);
    cookie.close();

    const QString argumentLog = staging.filePath(QStringLiteral("resolver-arguments.txt"));
    const QString adapterPath = staging.filePath(QStringLiteral("fake-yt-dlp-cookies.cmd"));
    QFile adapter(adapterPath);
    QVERIFY(adapter.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(adapter.write("@echo off\r\necho %* > \"%LASTUDIO_COOKIE_TEST_LOG%\"\r\necho "
                          + server.url().toString(QUrl::FullyEncoded).toUtf8() + "\r\n") > 0);
    adapter.close();

    const QByteArray previousAdapter = qgetenv("LASTUDIO_YTDLP");
    const bool hadPreviousAdapter = qEnvironmentVariableIsSet("LASTUDIO_YTDLP");
    const QByteArray previousLog = qgetenv("LASTUDIO_COOKIE_TEST_LOG");
    const bool hadPreviousLog = qEnvironmentVariableIsSet("LASTUDIO_COOKIE_TEST_LOG");
    const auto restoreEnvironment = qScopeGuard([&] {
        if (hadPreviousAdapter) qputenv("LASTUDIO_YTDLP", previousAdapter);
        else qunsetenv("LASTUDIO_YTDLP");
        if (hadPreviousLog) qputenv("LASTUDIO_COOKIE_TEST_LOG", previousLog);
        else qunsetenv("LASTUDIO_COOKIE_TEST_LOG");
    });
    qputenv("LASTUDIO_YTDLP", adapterPath.toUtf8());
    qputenv("LASTUDIO_COOKIE_TEST_LOG", argumentLog.toUtf8());

    const QDir tempDirectory(QDir::tempPath());
    const QStringList before = tempDirectory.entryList(
        QStringList{QStringLiteral("LA-Studio-douyin-cookies-*")}, QDir::Files);
    RemoteMediaImportService service(staging.path());
    QString error;
    QVERIFY2(service.setCookieFilePath(cookiePath, &error), qPrintable(error));
    QSignalSpy finished(&service, &RemoteMediaImportService::finished);
    QVERIFY(service.download(QUrl(QStringLiteral("https://www.douyin.com/video/fixture"))));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QVERIFY2(finished.constFirst().at(0).toBool(),
             qPrintable(finished.constFirst().at(2).toString()));
    QVERIFY(QFileInfo(argumentLog).isFile());
    QFile log(argumentLog);
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QString arguments = QString::fromLocal8Bit(log.readAll());
    QVERIFY(arguments.contains(QStringLiteral("--cookies")));
    QVERIFY(!arguments.contains(cookiePath));
    QVERIFY(!arguments.contains(QStringLiteral("--no-cookies")));
    QVERIFY(QFileInfo(cookiePath).isFile());
    QCOMPARE(tempDirectory.entryList(QStringList{QStringLiteral("LA-Studio-douyin-cookies-*")}, QDir::Files), before);
}

void TestMediaIngestService::resolverFreshCookieDiagnosticIsActionable()
{
    QTemporaryDir staging;
    QVERIFY(staging.isValid());
    const QString adapterPath = staging.filePath(QStringLiteral("fresh-cookie-error.cmd"));
    QFile adapter(adapterPath);
    QVERIFY(adapter.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(adapter.write("@echo off\r\necho ERROR: [Douyin] Fresh cookies (not necessarily logged in) are needed 1>&2\r\nexit /b 1\r\n") > 0);
    adapter.close();

    const QByteArray previous = qgetenv("LASTUDIO_YTDLP");
    const bool hadPrevious = qEnvironmentVariableIsSet("LASTUDIO_YTDLP");
    const auto restoreAdapter = qScopeGuard([previous, hadPrevious] {
        if (hadPrevious) qputenv("LASTUDIO_YTDLP", previous);
        else qunsetenv("LASTUDIO_YTDLP");
    });
    qputenv("LASTUDIO_YTDLP", adapterPath.toUtf8());

    RemoteMediaImportService service(staging.path());
    QSignalSpy finished(&service, &RemoteMediaImportService::finished);
    QVERIFY(service.download(QUrl(QStringLiteral("https://www.douyin.com/video/fresh-cookie-fixture"))));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QVERIFY(!finished.constFirst().at(0).toBool());
    const QString error = finished.constFirst().at(2).toString();
    QVERIFY(error.contains(QStringLiteral("fresh cookies"), Qt::CaseInsensitive));
    QVERIFY(error.contains(QStringLiteral("Choose a Netscape cookie file"), Qt::CaseInsensitive));
}

void TestMediaIngestService::controllerRetainsDouyinLinkForCookieRetry()
{
    QTemporaryDir staging;
    QVERIFY(staging.isValid());
    DirectMediaServer server;
    QVERIFY(server.start());

    const QString adapterPath = staging.filePath(QStringLiteral("retryable-yt-dlp.cmd"));
    const auto writeAdapter = [&](const QByteArray &contents) {
        QFile adapter(adapterPath);
        if (!adapter.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
        return adapter.write(contents) == contents.size();
    };
    QVERIFY(writeAdapter("@echo off\r\necho ERROR: [Douyin] Fresh cookies (not necessarily logged in) are needed 1>&2\r\nexit /b 1\r\n"));

    const QByteArray previous = qgetenv("LASTUDIO_YTDLP");
    const bool hadPrevious = qEnvironmentVariableIsSet("LASTUDIO_YTDLP");
    const auto restoreAdapter = qScopeGuard([previous, hadPrevious] {
        if (hadPrevious) qputenv("LASTUDIO_YTDLP", previous);
        else qunsetenv("LASTUDIO_YTDLP");
    });
    qputenv("LASTUDIO_YTDLP", adapterPath.toUtf8());

    DubbingController controller(nullptr, nullptr);
    QCOMPARE(controller.enqueueMediaLinks(QStringLiteral("https://v.douyin.com/retry-fixture/")), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.mediaQueueDownloading(), 10000);
    const QVariantList failedItems = controller.mediaQueueItems();
    QCOMPARE(failedItems.size(), 1);
    const QVariantMap failed = failedItems.constFirst().toMap();
    QCOMPARE(failed.value(QStringLiteral("downloadState")).toString(), QStringLiteral("needs-auth"));
    const QString itemId = failed.value(QStringLiteral("id")).toString();
    QVERIFY(!failed.value(QStringLiteral("sourceUrl")).toString().isEmpty());

    const QString cookiePath = staging.filePath(QStringLiteral("douyin-cookies.txt"));
    QFile cookie(cookiePath);
    QVERIFY(cookie.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(cookie.write("# Netscape HTTP Cookie File\n.douyin.com\tTRUE\t/\tFALSE\t0\ts_v_web_id\tfixture\n") > 0);
    cookie.close();
    QVERIFY2(controller.setDouyinCookieFile(cookiePath), qPrintable(controller.lastError()));
    QVERIFY(writeAdapter("@echo off\r\necho "
                         + server.url().toString(QUrl::FullyEncoded).toUtf8() + "\r\n"));
    QVERIFY(controller.retryMediaQueueItem(itemId));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.mediaQueueDownloading(), 10000);
    const QVariantMap downloaded = controller.mediaQueueItems().constFirst().toMap();
    QCOMPARE(downloaded.value(QStringLiteral("downloadState")).toString(), QStringLiteral("downloaded"));
    QVERIFY(QFileInfo(downloaded.value(QStringLiteral("localPath")).toString()).isFile());
    QVERIFY(!downloaded.contains(QStringLiteral("sourceUrl")));
    // Queue advancement is deliberately posted after the resolver's finished
    // signal so the next queued URL cannot re-enter that signal handler.  The
    // selected cookie is nevertheless memory-only and must be cleared before
    // the retry is observable as complete to the caller.
    QTRY_VERIFY_WITH_TIMEOUT(!controller.douyinCookieConfigured(), 1000);
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

void TestMediaIngestService::sharedVideoTextQueuesOnlyEmbeddedPublicUrls()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fixturePath = dir.filePath(QStringLiteral("shared-link-fixture.wav"));
    const QVector<float> samples(8000, 0.02F);
    QVERIFY(WavIO::saveFloat(fixturePath, samples.constData(), samples.size(), 16000));
    QFile fixture(fixturePath);
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    DirectMediaServer server(fixture.readAll());
    QVERIFY(server.start());

    DubbingController controller(nullptr, nullptr);
    const QString sharedText = QStringLiteral(
        "3.58 XMj:/ 06/23 :9pm f@o.qE shared caption %1 复制此链接，打开应用直接观看视频！")
                                   .arg(server.url().toString());
    QCOMPARE(controller.enqueueMediaLinks(sharedText), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.mediaQueueDownloading(), 15000);
    QCOMPARE(server.requestCount(), 1);
    const QVariantList items = controller.mediaQueueItems();
    QCOMPARE(items.size(), 1);
    const QVariantMap item = items.constFirst().toMap();
    QCOMPARE(item.value(QStringLiteral("downloadState")).toString(), QStringLiteral("downloaded"));
    QVERIFY(item.value(QStringLiteral("selected")).toBool());
    QVERIFY(QFileInfo(item.value(QStringLiteral("localPath")).toString()).isFile());
}

void TestMediaIngestService::mediaLibraryRunsOnlyTheLaterSelectedActionSubset()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fixturePath = dir.filePath(QStringLiteral("independent-action-fixture.wav"));
    const QVector<float> samples(8000, 0.02F);
    QVERIFY(WavIO::saveFloat(fixturePath, samples.constData(), samples.size(), 16000));

    DubbingController controller(nullptr, nullptr);
    QCOMPARE(controller.enqueueMediaFiles({fixturePath, fixturePath}), 2);
    QVERIFY(controller.startMediaQueue({{QStringLiteral("operation"), QStringLiteral("import")}}));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.mediaQueueProcessing(), 20000);

    QVariantList imported = controller.mediaQueueItems();
    QCOMPARE(imported.size(), 2);
    for (const QVariant &value : imported) {
        const QVariantMap item = value.toMap();
        QCOMPARE(item.value(QStringLiteral("processState")).toString(), QStringLiteral("completed"));
        QVERIFY(QFileInfo(item.value(QStringLiteral("outputs")).toMap()
                          .value(QStringLiteral("project")).toString()).isFile());
    }

    const QString firstId = imported.at(0).toMap().value(QStringLiteral("id")).toString();
    const QString secondId = imported.at(1).toMap().value(QStringLiteral("id")).toString();
    QVERIFY(controller.setMediaQueueItemSelected(firstId, true));
    QVERIFY(controller.setMediaQueueItemSelected(secondId, false));
    // Translate is deliberately attempted without a transcript session.  The
    // real controller rejects that selected item, while the unselected item
    // must retain its completed Import result and must not be re-enqueued.
    QVERIFY(controller.startMediaQueue({{QStringLiteral("operation"), QStringLiteral("translate")}}));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.mediaQueueProcessing(), 10000);

    const QVariantList afterTranslate = controller.mediaQueueItems();
    QCOMPARE(afterTranslate.at(0).toMap().value(QStringLiteral("processState")).toString(),
             QStringLiteral("failed"));
    const QVariantMap untouched = afterTranslate.at(1).toMap();
    QCOMPARE(untouched.value(QStringLiteral("processState")).toString(), QStringLiteral("completed"));
    QVERIFY(QFileInfo(untouched.value(QStringLiteral("outputs")).toMap()
                      .value(QStringLiteral("project")).toString()).isFile());
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
    const QString dubbingHeader = readSource(QStringLiteral("qml/components/dubbing/DubbingWorkflowHeader.qml"));
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
    QVERIFY(page.contains(QStringLiteral("objectName: \"mediaDownloadButton\"")));
    QVERIFY(page.contains(QStringLiteral("text: qsTr(\"Download\")")));
    QVERIFY(page.contains(QStringLiteral("objectName: \"mediaDownloadBrowserSetupButton\"")));
    QVERIFY(page.contains(QStringLiteral("text: qsTr(\"Set up Chromium\")")));
    QVERIFY(page.contains(QStringLiteral("openDouyinBrowserSession()")));
    QVERIFY(page.contains(QStringLiteral("checkDouyinBrowserSession()")));
    QVERIFY(!page.contains(QStringLiteral("startMediaQueue({")));
    QVERIFY(!page.contains(QStringLiteral("objectName: \"dubbingQueueIsolateTask\"")));
    QVERIFY(!page.contains(QStringLiteral("objectName: \"dubbingQueueTranscribeTask\"")));
    QVERIFY(!page.contains(QStringLiteral("objectName: \"dubbingQueueTranslateTask\"")));
    QVERIFY(!page.contains(QStringLiteral("objectName: \"dubbingQueueVoiceTask\"")));
    QVERIFY(!page.contains(QStringLiteral("Isolate audio")));
    QVERIFY(!page.contains(QStringLiteral("STT to source.srt")));
    QVERIFY(!page.contains(QStringLiteral("Translate to translated.srt")));
    QVERIFY(!page.contains(QStringLiteral("Voice / cloned voice to WAV")));
    QVERIFY(page.contains(QStringLiteral("mediaQueueItems")));
    QVERIFY(page.contains(QStringLiteral("public YouTube, TikTok, and Douyin pages")));
    QVERIFY(page.contains(QStringLiteral("Netscape cookies are optional")));
    QVERIFY(page.contains(QStringLiteral("Browser cookies are never imported automatically")));
    QVERIFY(page.contains(QStringLiteral("Douyin Chromium session")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Managed Chromium session for Douyin")));
    QVERIFY(dubbingSource.contains(QStringLiteral("openDouyinBrowserSession()")));
    QVERIFY(dubbingSource.contains(QStringLiteral("checkDouyinBrowserSession()")));
    QVERIFY(dubbingSource.contains(QStringLiteral("objectName: \"dubbingDouyinChromiumSetupButton\"")));
    QVERIFY(dubbingSource.contains(QStringLiteral("mediaQueueRequested")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Queue direct media, YouTube, TikTok, or Douyin links")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Add link(s) to download queue")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Choose Douyin cookies")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Netscape cookie file")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("retryMediaQueueItem")));
    QVERIFY(page.contains(QStringLiteral("text: qsTr(\"Retry\")")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Downloaded media & actions")));
    QVERIFY(dubbingSource.contains(QStringLiteral("root.mediaQueueRequested(directMediaLink.text)\n"
                                                   "                        directMediaLink.clear()\n"
                                                   "                    }")));
    QVERIFY(dubbingSource.contains(QStringLiteral("DubbingMediaQueueDialog")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Show source setup by default only until a source exists")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Change / download source")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("Downloaded media")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("Import / Normalize")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("Export / Output")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("Run selected action")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("\"operation\": root.selectedAction")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("exportedMedia")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("Complete one video, then next")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("Complete each step for all videos")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("executionMode\": root.batchExecutionMode")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("source.srt")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("translated.srt")));
    QVERIFY(dubbingQueueDialog.contains(QStringLiteral("Voice WAV")));
    QVERIFY(dubbingPage.contains(QStringLiteral("dubbingHistoryResizeHandle")));
    QVERIFY(dubbingPage.contains(QStringLiteral("dubbingWorkspaceResizeHandle")));
    QVERIFY(dubbingPage.contains(QStringLiteral("dubbingTaskShelfResizeHandle")));
    QVERIFY(dubbingPage.contains(QStringLiteral("dubbingStepReviewPanel")));
    QVERIFY(dubbingPage.contains(QStringLiteral("dubbingTimelineResizeHandle")));
    QVERIFY(dubbingPage.contains(QStringLiteral("dubbingTaskShelf")));
    QVERIFY(dubbingPage.contains(QStringLiteral("compactDubbingControls")));
    QVERIFY(dubbingPage.contains(QStringLiteral("visible: root.compactDubbingControls && node !== null")));
    QVERIFY(dubbingPage.contains(QStringLiteral("isAdvancedNodeInspectorOpen")));
    QVERIFY(dubbingPage.contains(QStringLiteral("Dubbing workbench shelf or full-width timeline is unavailable")));
    QVERIFY(dubbingPage.contains(QStringLiteral("Layout.minimumWidth: root.compactDubbingControls ? 240 : 320")));
    QVERIFY(dubbingPage.contains(QStringLiteral("Drag to resize Dubbing History")));
    QVERIFY(dubbingPage.contains(QStringLiteral("Drag to resize Dubbing Preview")));
    QVERIFY(dubbingPage.contains(QStringLiteral("Drag to resize task controls")));
    QVERIFY(dubbingPage.contains(QStringLiteral("Drag to resize Dubbing timeline")));
    QVERIFY(dubbingPage.contains(QStringLiteral("DragHandler")));
    QVERIFY(dubbingPage.contains(QStringLiteral("not a horizontally flicked canvas")));
    QVERIFY(dubbingPage.contains(QStringLiteral("video workspace overlays the task review panel")));
    QVERIFY(dubbingPage.contains(QStringLiteral("task review panel extends outside the Dubbing workspace")));
    QVERIFY(dubbingPage.contains(QStringLiteral("Dubbing header clips an action or overlays its workflow rail")));
    QVERIFY(dubbingPage.contains(QStringLiteral("property bool previewFocusMode")));
    QVERIFY(dubbingSource.contains(QStringLiteral("dubbingPreviewFocusToggle")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Focus video")));
    QVERIFY(dubbingSource.contains(QStringLiteral("dubbingOpenVideoButton")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Replace video")));
    QVERIFY(dubbingSource.contains(QStringLiteral("dubbingPreviewFrameModeSelector")));
    QVERIFY(dubbingSource.contains(QStringLiteral("previewFrameAspectRatio")));
    QVERIFY(dubbingSource.contains(QStringLiteral("id: previewFrame")));
    QVERIFY(dubbingSource.contains(QStringLiteral("VideoOutput.PreserveAspectFit")));
    QVERIFY(dubbingSource.contains(QStringLiteral("qmlSmokeLoadedSourceLayoutCheck")));
    QVERIFY(dubbingSource.contains(QStringLiteral("collapseSourceSetupAfterSelection")));
    QVERIFY(dubbingSource.contains(QStringLiteral("sourceSetupMaximumHeight")));
    QVERIFY(dubbingSource.contains(QStringLiteral("dubbingSourceSetupScrollView")));
    QVERIFY(dubbingSource.contains(QStringLiteral("id: previewToolbar")));
    QVERIFY(dubbingSource.contains(QStringLiteral("objectName: \"dubbingPreviewToolbar\"")));
    QVERIFY(dubbingSource.contains(QStringLiteral("All preview controls share one horizontal editor toolbar")));
    QVERIFY(dubbingSource.contains(QStringLiteral("dubbingPreviewModeSelector")));
    QVERIFY(dubbingSource.contains(QStringLiteral("Layout.minimumHeight: root.isVideoSource ? 440 : 300")));
    QVERIFY(dubbingHeader.contains(QStringLiteral("id: workflowStepsFlickable")));
    QVERIFY(dubbingHeader.contains(QStringLiteral("id: headerActionCluster")));
    QVERIFY(dubbingHeader.contains(QStringLiteral("function qmlSmokeLayoutCheck()")));
    QVERIFY(dubbingHeader.contains(QStringLiteral("id: workflowAction")));
    QVERIFY(dubbingHeader.contains(QStringLiteral("action cluster never scrolls")));
    QVERIFY(!dubbingHeader.contains(QStringLiteral("headerUtilitiesFlickable")));
    // At narrower widths the label is intentionally replaced by an icon and
    // tooltip so the action cluster never truncates into a misleading "Wor".
    QVERIFY(dubbingHeader.contains(
        QStringLiteral("text: root.compactActionCluster ? \"\" : qsTr(\"Workflow\")")));
    QVERIFY(dubbingHeader.contains(QStringLiteral("toolTip: qsTr(\"Open workflow\")")));
    QVERIFY(dubbingPage.contains(QStringLiteral("dubbingWorkspaceScroller.width < 1450")));
    QVERIFY(dubbingPage.contains(QStringLiteral("dubbingWorkspaceScroller.width < 1080")));
    QVERIFY(dubbingPage.contains(QStringLiteral("actual non-overlapping minima")));
    QVERIFY(dubbingPage.contains(QStringLiteral("readonly property bool compactDubbingHistory")));
    QVERIFY(dubbingPage.contains(QStringLiteral("A real 28 px hit target")));
    QVERIFY(main.contains(QStringLiteral("visible: stack.currentIndex === 13")));
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
