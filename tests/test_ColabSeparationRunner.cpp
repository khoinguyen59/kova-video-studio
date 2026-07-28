#include <QtTest>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <cstring>

#include "separation/ColabSeparationRunner.h"
#include "test_ColabSeparationRunner.h"

namespace LAStudio {
namespace {

QByteArray tinyWav()
{
    QByteArray wav(48, '\0');
    std::memcpy(wav.data(), "RIFF", 4);
    const quint32 size = 40, fmtSize = 16, rate = 16000, byteRate = 32000, dataSize = 4;
    const quint16 format = 1, channels = 1, align = 2, bits = 16;
    std::memcpy(wav.data() + 4, &size, 4); std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    std::memcpy(wav.data() + 16, &fmtSize, 4); std::memcpy(wav.data() + 20, &format, 2);
    std::memcpy(wav.data() + 22, &channels, 2); std::memcpy(wav.data() + 24, &rate, 4);
    std::memcpy(wav.data() + 28, &byteRate, 4); std::memcpy(wav.data() + 32, &align, 2);
    std::memcpy(wav.data() + 34, &bits, 2); std::memcpy(wav.data() + 36, "data", 4);
    std::memcpy(wav.data() + 40, &dataSize, 4);
    return wav;
}

class SeparationMock final : public QObject
{
public:
    explicit SeparationMock(bool permanentlyQueued = false) : m_permanentlyQueued(permanentlyQueued)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { consume(socket); });
            }
        });
    }
    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    QByteArray requests() const { return m_requests; }
    bool sawStatus() const { return m_requests.contains("GET /v1/audio/separations/job-direct HTTP/1.1"); }

private:
    void consume(QTcpSocket *socket)
    {
        QByteArray &pending = m_pending[socket];
        pending += socket->readAll();
        const int headerEnd = pending.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const auto match = QRegularExpression(QStringLiteral("Content-Length: (\\d+)"), QRegularExpression::CaseInsensitiveOption)
                               .match(QString::fromLatin1(pending.left(headerEnd)));
        const int contentLength = match.hasMatch() ? match.captured(1).toInt() : 0;
        if (pending.size() < headerEnd + 4 + contentLength) return;
        const QByteArray request = pending.left(headerEnd + 4 + contentLength);
        m_requests += request + "\n---\n";
        QByteArray body;
        QByteArray contentType = "application/json";
        if (request.startsWith("POST /v1/audio/separations ")) {
            body = R"({"job_id":"job-direct","status":"queued","progress":10})";
        } else if (request.startsWith("GET /v1/audio/separations/job-direct ")) {
            body = m_permanentlyQueued ? R"({"job_id":"job-direct","status":"running","progress":30})"
                                       : R"({"job_id":"job-direct","status":"ready","progress":100})";
        } else if (request.startsWith("GET /v1/audio/separations/job-direct/artifacts/vocals ")
                   || request.startsWith("GET /v1/audio/separations/job-direct/artifacts/background ")) {
            body = tinyWav(); contentType = "audio/wav";
        } else if (request.startsWith("DELETE /v1/audio/separations/job-direct ")) {
            body = R"({"status":"cancelled"})";
        } else {
            body = R"({"detail":"unexpected request"})";
        }
        const QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: ") + contentType
            + QByteArrayLiteral("\r\nContent-Length: ") + QByteArray::number(body.size())
            + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
        socket->write(response);
        socket->disconnectFromHost();
        m_pending.remove(socket);
    }
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_pending;
    QByteArray m_requests;
    bool m_permanentlyQueued = false;
};

QString sourceFile(QTemporaryDir *directory)
{
    const QString path = directory->filePath(QStringLiteral("source.wav"));
    QFile file(path); if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(tinyWav()); file.close(); return path;
}

ColabSeparationRequest makeRequest(const QString &url, const QString &source, const QString &output,
                                   const std::shared_ptr<std::atomic_bool> &flag = {})
{
    ColabSeparationRequest request;
    request.workerUrl = QUrl(url); request.bearerToken = QStringLiteral("colab-separation-token");
    request.audioPath = source; request.outputRoot = output; request.allowInsecureLocalhost = true;
    request.cancellation = InferenceCancellationToken(flag);
    return request;
}

} // namespace

void TestColabSeparationRunner::testUsesDirectJobAndArtifactContract()
{
    SeparationMock server; QVERIFY(server.start());
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString source = sourceFile(&directory); QVERIFY(!source.isEmpty());
    const QString output = directory.filePath(QStringLiteral("output"));
    qRegisterMetaType<ColabSeparationRequest>("ColabSeparationRequest");
    qRegisterMetaType<ColabSeparationResult>("ColabSeparationResult");
    QThread thread; auto *runner = new ColabSeparationRunner; runner->moveToThread(&thread);
    connect(&thread, &QThread::finished, runner, &QObject::deleteLater); thread.start();
    QSignalSpy finished(runner, &ColabSeparationRunner::finished); QSignalSpy failures(runner, &ColabSeparationRunner::failed);
    QVERIFY(QMetaObject::invokeMethod(runner, "separate", Qt::QueuedConnection,
                                      Q_ARG(ColabSeparationRequest, makeRequest(server.baseUrl(), source, output))));
    QVERIFY2(finished.wait(5000), "Colab separation worker did not finish.");
    QCOMPARE(failures.count(), 0);
    const ColabSeparationResult result = finished.takeFirst().at(0).value<ColabSeparationResult>();
    QVERIFY(QFileInfo::exists(result.vocalsPath)); QVERIFY(QFileInfo::exists(result.backgroundPath));
    const QByteArray requests = server.requests();
    QVERIFY(requests.startsWith("POST /v1/audio/separations HTTP/1.1\r\n"));
    QVERIFY(requests.toLower().contains("authorization: bearer colab-separation-token"));
    QVERIFY(requests.contains("name=\"stems\"")); QVERIFY(requests.contains("vocals,background"));
    QVERIFY(requests.contains("name=\"file\"; filename=\"source.wav\""));
    QVERIFY(requests.toLower().contains("content-type: audio/wav"));
    QVERIFY(requests.contains("GET /v1/audio/separations/job-direct/artifacts/vocals HTTP/1.1"));
    QVERIFY(requests.contains("GET /v1/audio/separations/job-direct/artifacts/background HTTP/1.1"));
    QVERIFY(!requests.contains("gateway")); QVERIFY(!requests.contains("chat/completions"));
    thread.quit(); QVERIFY(thread.wait(5000));
}

void TestColabSeparationRunner::testCancellationDiscardsPartialArtifacts()
{
    SeparationMock server(true); QVERIFY(server.start());
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto flag = std::make_shared<std::atomic_bool>(false);
    qRegisterMetaType<ColabSeparationRequest>("ColabSeparationRequest");
    QThread thread; auto *runner = new ColabSeparationRunner; runner->moveToThread(&thread);
    connect(&thread, &QThread::finished, runner, &QObject::deleteLater); thread.start();
    QSignalSpy finished(runner, &ColabSeparationRunner::finished); QSignalSpy failures(runner, &ColabSeparationRunner::failed);
    QVERIFY(QMetaObject::invokeMethod(runner, "separate", Qt::QueuedConnection,
                                      Q_ARG(ColabSeparationRequest, makeRequest(server.baseUrl(), sourceFile(&directory), directory.filePath(QStringLiteral("cancelled")), flag))));
    QTRY_VERIFY(server.sawStatus());
    flag->store(true, std::memory_order_relaxed);
    QVERIFY2(failures.wait(5000), "Cancelled direct separation request did not terminate.");
    QCOMPARE(finished.count(), 0);
    QVERIFY(failures.takeFirst().at(0).toString().contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
    QVERIFY(server.requests().contains("DELETE /v1/audio/separations/job-direct HTTP/1.1"));
    thread.quit(); QVERIFY(thread.wait(5000));
}

void TestColabSeparationRunner::separationNotebookMatchesDirectColabContract()
{
    const QString path = QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
        .filePath(QStringLiteral("notebooks/LA_STUDIO_SEPARATION_GPU.ipynb"));
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();
    QCOMPARE(root.value(QStringLiteral("nbformat")).toInt(), 4);

    QString source;
    const QJsonArray cells = root.value(QStringLiteral("cells")).toArray();
    QVERIFY(cells.size() >= 4);
    for (const QJsonValue &cellValue : cells) {
        const QJsonArray lines = cellValue.toObject().value(QStringLiteral("source")).toArray();
        for (const QJsonValue &line : lines) source += line.toString();
    }
    QVERIFY(source.contains(QStringLiteral("demucs")));
    QVERIFY(source.contains(QStringLiteral("if not torch.cuda.is_available()")));
    QVERIFY(source.contains(QStringLiteral("@app.post('/v1/audio/separations')")));
    QVERIFY(source.contains(QStringLiteral("@app.get('/v1/audio/separations/{job_id}')")));
    QVERIFY(source.contains(QStringLiteral("@app.get('/v1/capabilities')")));
    QVERIFY(source.contains(QStringLiteral("'id': 'voice-isolation'")));
    QVERIFY(source.contains(QStringLiteral("'device': 'cuda'")));
    QVERIFY(source.contains(QStringLiteral("MAX_UPLOAD_BYTES = 512 * 1024 * 1024")));
    QVERIFY(source.contains(QStringLiteral("MAX_AUDIO_SECONDS = 30 * 60")));
    QVERIFY(source.contains(QStringLiteral("ARTIFACT_TTL_SECONDS = 1800")));
    QVERIFY(source.contains(QStringLiteral("ALLOWED_CONTENT_TYPES")));
    QVERIFY(source.contains(QStringLiteral("JOB_SLOTS = threading.BoundedSemaphore(1)")));
    QVERIFY(source.contains(QStringLiteral("media_duration_seconds")));
    QVERIFY(source.contains(QStringLiteral("status_code=429")));
    QVERIFY(source.contains(QStringLiteral("JOB_SLOTS.release()")));
    QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_SEPARATION_URL")));
    QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_SEPARATION_TOKEN")));
    QVERIFY(source.contains(QStringLiteral("cloudflared")));
    QVERIFY(!source.contains(QStringLiteral("API_GATEWAY")));
}

} // namespace LAStudio
