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
    QVERIFY(QFile(ffmpeg).open(QIODevice::WriteOnly));
    QVERIFY(QFile(ffprobe).open(QIODevice::WriteOnly));

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

} // namespace LAStudio
