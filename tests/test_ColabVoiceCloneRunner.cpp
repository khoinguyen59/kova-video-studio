#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>

#include <cstring>

#include "tts/ColabVoiceCloneRunner.h"
#include "test_ColabVoiceCloneRunner.h"

namespace LAStudio {
namespace {

QByteArray pcm16Wav()
{
    QByteArray wav(48, '\0');
    std::memcpy(wav.data(), "RIFF", 4);
    const quint32 size = 40;
    std::memcpy(wav.data() + 4, &size, sizeof(size));
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    const quint32 fmtSize = 16;
    const quint16 format = 1;
    const quint16 channels = 1;
    const quint32 rate = 24000;
    const quint32 byteRate = 48000;
    const quint16 align = 2;
    const quint16 bits = 16;
    const quint32 dataSize = 4;
    std::memcpy(wav.data() + 16, &fmtSize, sizeof(fmtSize));
    std::memcpy(wav.data() + 20, &format, sizeof(format));
    std::memcpy(wav.data() + 22, &channels, sizeof(channels));
    std::memcpy(wav.data() + 24, &rate, sizeof(rate));
    std::memcpy(wav.data() + 28, &byteRate, sizeof(byteRate));
    std::memcpy(wav.data() + 32, &align, sizeof(align));
    std::memcpy(wav.data() + 34, &bits, sizeof(bits));
    std::memcpy(wav.data() + 36, "data", 4);
    std::memcpy(wav.data() + 40, &dataSize, sizeof(dataSize));
    const qint16 samples[] = {4096, -4096};
    std::memcpy(wav.data() + 44, samples, sizeof(samples));
    return wav;
}

QByteArray referenceWav()
{
    constexpr quint32 frameCount = 3 * 24000;
    constexpr quint32 dataSize = frameCount * sizeof(qint16);
    QByteArray wav(static_cast<int>(44 + dataSize), '\0');
    std::memcpy(wav.data(), "RIFF", 4);
    const quint32 size = 36 + dataSize;
    std::memcpy(wav.data() + 4, &size, sizeof(size));
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    const quint32 fmtSize = 16;
    const quint16 format = 1;
    const quint16 channels = 1;
    const quint32 rate = 24000;
    const quint32 byteRate = 48000;
    const quint16 align = 2;
    const quint16 bits = 16;
    std::memcpy(wav.data() + 16, &fmtSize, sizeof(fmtSize));
    std::memcpy(wav.data() + 20, &format, sizeof(format));
    std::memcpy(wav.data() + 22, &channels, sizeof(channels));
    std::memcpy(wav.data() + 24, &rate, sizeof(rate));
    std::memcpy(wav.data() + 28, &byteRate, sizeof(byteRate));
    std::memcpy(wav.data() + 32, &align, sizeof(align));
    std::memcpy(wav.data() + 34, &bits, sizeof(bits));
    std::memcpy(wav.data() + 36, "data", 4);
    std::memcpy(wav.data() + 40, &dataSize, sizeof(dataSize));
    return wav;
}

class VoiceCloneMock final : public QObject
{
public:
    VoiceCloneMock()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { consume(socket); });
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    QList<QByteArray> requests() const { return m_requests; }

private:
    static QByteArray jsonResponse(int status, const QByteArray &body)
    {
        return QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status)
            + QByteArrayLiteral(" OK\r\nContent-Type: application/json\r\nContent-Length: ")
            + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
    }

    void respond(QTcpSocket *socket, const QByteArray &request)
    {
        const QByteArray firstLine = request.left(request.indexOf("\r\n"));
        QByteArray response;
        if (firstLine.startsWith("POST /v2/jobs/profile ")) {
            response = jsonResponse(202, QByteArrayLiteral("{\"id\":\"profile-job\",\"status\":\"queued\",\"percent\":0}"));
        } else if (firstLine.startsWith("GET /v2/jobs/profile-job ")) {
            response = jsonResponse(200, QByteArrayLiteral("{\"id\":\"profile-job\",\"status\":\"succeeded\",\"percent\":100,\"result\":{\"id\":\"profile-1\"}}"));
        } else if (firstLine.startsWith("POST /v2/jobs/generation ")) {
            response = jsonResponse(202, QByteArrayLiteral("{\"id\":\"generation-job\",\"status\":\"queued\",\"percent\":0}"));
        } else if (firstLine.startsWith("GET /v2/jobs/generation-job/audio ")) {
            const QByteArray audio = pcm16Wav();
            response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: ")
                + QByteArray::number(audio.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + audio;
        } else if (firstLine.startsWith("GET /v2/jobs/generation-job ")) {
            response = jsonResponse(200, QByteArrayLiteral("{\"id\":\"generation-job\",\"status\":\"succeeded\",\"percent\":100}"));
        } else if (firstLine.startsWith("DELETE /v1/profiles/profile-1 ")) {
            response = jsonResponse(200, QByteArrayLiteral("{\"deleted\":true}"));
        } else {
            response = jsonResponse(404, QByteArrayLiteral("{\"detail\":\"Unexpected request\"}"));
        }
        socket->write(response);
        socket->disconnectFromHost();
    }

    void consume(QTcpSocket *socket)
    {
        QByteArray &pending = m_pending[socket];
        pending += socket->readAll();
        const int headerEnd = pending.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const auto match = QRegularExpression(QStringLiteral("Content-Length: (\\d+)"),
                                              QRegularExpression::CaseInsensitiveOption)
                               .match(QString::fromLatin1(pending.left(headerEnd)));
        const int bodyLength = match.hasMatch() ? match.captured(1).toInt() : 0;
        const int requestLength = headerEnd + 4 + bodyLength;
        if (pending.size() < requestLength) return;
        const QByteArray request = pending.left(requestLength);
        m_requests.append(request);
        pending.clear();
        respond(socket, request);
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_pending;
    QList<QByteArray> m_requests;
};

} // namespace

void TestColabVoiceCloneRunner::testRunsVoiceProfileAndGenerationDirectlyOnColab()
{
    VoiceCloneMock server;
    QVERIFY(server.start());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString referencePath = temporary.filePath(QStringLiteral("reference.wav"));
    QFile reference(referencePath);
    QVERIFY(reference.open(QIODevice::WriteOnly));
    QVERIFY(reference.write(referenceWav()) > 0);
    reference.close();

    qRegisterMetaType<ColabVoiceCloneRequest>("ColabVoiceCloneRequest");
    qRegisterMetaType<QVector<float>>("QVector<float>");
    QThread workerThread;
    auto *runner = new ColabVoiceCloneRunner;
    runner->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, runner, &QObject::deleteLater);
    workerThread.start();
    QSignalSpy profiles(runner, &ColabVoiceCloneRunner::profileReady);
    QSignalSpy deleted(runner, &ColabVoiceCloneRunner::profileDeleted);
    QSignalSpy finished(runner, &ColabVoiceCloneRunner::finished);
    QSignalSpy failures(runner, &ColabVoiceCloneRunner::failed);

    ColabVoiceCloneRequest request;
    request.workerUrl = QUrl(server.baseUrl());
    request.bearerToken = QStringLiteral("colab-voice-token");
    request.referencePath = referencePath;
    request.referenceName = QStringLiteral("Consent test voice");
    request.referenceText = QStringLiteral("This is the exact transcript.");
    request.text = QStringLiteral("Generate this sentence.");
    request.language = QStringLiteral("en");
    request.speed = 1.1F;
    request.steps = 28;
    request.consentConfirmed = true;
    request.allowInsecureLocalhost = true;
    QVERIFY(QMetaObject::invokeMethod(runner, "clone", Qt::QueuedConnection,
                                      Q_ARG(ColabVoiceCloneRequest, request)));

    QVERIFY2(finished.wait(8000), "Colab voice-cloning worker did not finish.");
    QCOMPARE(failures.count(), 0);
    QCOMPARE(profiles.count(), 1);
    QCOMPARE(profiles.takeFirst().at(0).toString(), QStringLiteral("profile-1"));
    const QList<QVariant> result = finished.takeFirst();
    QCOMPARE(result.at(0).toByteArray().size(), 4);
    QCOMPARE(result.at(1).value<QVector<float>>().size(), 2);
    QCOMPARE(result.at(2).toInt(), 24000);

    const QList<QByteArray> calls = server.requests();
    QCOMPARE(calls.size(), 5);
    QCOMPARE(calls.at(0).left(calls.at(0).indexOf("\r\n")), QByteArrayLiteral("POST /v2/jobs/profile HTTP/1.1"));
    QVERIFY(calls.at(0).toLower().contains("authorization: bearer colab-voice-token"));
    QVERIFY(calls.at(0).toLower().contains("content-type: audio/wav"));
    QVERIFY(calls.at(0).contains("consent_confirmed"));
    QVERIFY(calls.at(0).contains("This is the exact transcript."));
    QCOMPARE(calls.at(2).left(calls.at(2).indexOf("\r\n")), QByteArrayLiteral("POST /v2/jobs/generation HTTP/1.1"));
    QVERIFY(calls.at(2).contains("\"profile_id\":\"profile-1\""));
    QVERIFY(calls.at(2).contains("\"text\":\"Generate this sentence.\""));
    QVERIFY(calls.at(2).contains("\"language\":\"en\""));
    QVERIFY(calls.at(2).contains("\"num_step\":28"));
    QVERIFY(calls.at(4).startsWith("GET /v2/jobs/generation-job/audio HTTP/1.1\r\n"));

    ColabVoiceCloneRequest deleteRequest;
    deleteRequest.workerUrl = QUrl(server.baseUrl());
    deleteRequest.bearerToken = QStringLiteral("colab-voice-token");
    deleteRequest.existingProfileId = QStringLiteral("profile-1");
    deleteRequest.allowInsecureLocalhost = true;
    QVERIFY(QMetaObject::invokeMethod(runner, "deleteProfile", Qt::QueuedConnection,
                                      Q_ARG(ColabVoiceCloneRequest, deleteRequest)));
    QVERIFY2(deleted.wait(5000), "Colab voice profile was not deleted.");
    const QList<QByteArray> callsAfterDelete = server.requests();
    QCOMPARE(callsAfterDelete.size(), 6);
    QVERIFY(callsAfterDelete.at(5).startsWith("DELETE /v1/profiles/profile-1 HTTP/1.1\r\n"));
    QVERIFY(callsAfterDelete.at(5).toLower().contains("authorization: bearer colab-voice-token"));
    workerThread.quit();
    QVERIFY(workerThread.wait(5000));
}

void TestColabVoiceCloneRunner::testRejectsProfileWithoutConsent()
{
    VoiceCloneMock server;
    QVERIFY(server.start());
    qRegisterMetaType<ColabVoiceCloneRequest>("ColabVoiceCloneRequest");
    QThread workerThread;
    auto *runner = new ColabVoiceCloneRunner;
    runner->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, runner, &QObject::deleteLater);
    workerThread.start();
    QSignalSpy failures(runner, &ColabVoiceCloneRunner::failed);

    ColabVoiceCloneRequest request;
    request.workerUrl = QUrl(server.baseUrl());
    request.bearerToken = QStringLiteral("colab-voice-token");
    request.text = QStringLiteral("Blocked before upload");
    request.allowInsecureLocalhost = true;
    QVERIFY(QMetaObject::invokeMethod(runner, "clone", Qt::QueuedConnection,
                                      Q_ARG(ColabVoiceCloneRequest, request)));
    QVERIFY(failures.wait(5000));
    QCOMPARE(failures.takeFirst().at(0).toString(), QStringLiteral("Confirm permission before creating a voice profile"));
    QCOMPARE(server.requests().size(), 0);
    workerThread.quit();
    QVERIFY(workerThread.wait(5000));
}

void TestColabVoiceCloneRunner::voiceCloneNotebookMatchesDirectColabContract()
{
    const QString path = QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
        .filePath(QStringLiteral("notebooks/LA_STUDIO_VOICE_CLONE_GPU.ipynb"));
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
    QVERIFY(source.contains(QStringLiteral("REPO_REF = 'v1.0.2.1'")));
    QVERIFY(source.contains(QStringLiteral("KOVA_VOICE_REQUIRE_CUDA': '1'")));
    QVERIFY(source.contains(QStringLiteral("@app.get('/v1/capabilities')")));
    QVERIFY(source.contains(QStringLiteral("'id': 'voice-cloning'")));
    QVERIFY(source.contains(QStringLiteral("'id': 'omnivoice'")));
    QVERIFY(source.contains(QStringLiteral("'requires_consent': True")));
    QVERIFY(source.contains(QStringLiteral("'device': 'cuda'")));
    QVERIFY(source.contains(QStringLiteral("app.mount('/', worker_app)")));
    QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_VOICE_CLONE_URL")));
    QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_VOICE_CLONE_TOKEN")));
    QVERIFY(source.contains(QStringLiteral("cloudflared")));
    QVERIFY(!source.contains(QStringLiteral("API_GATEWAY")));
    QVERIFY(!source.contains(QStringLiteral("GATEWAY_BASE_URL")));
}

} // namespace LAStudio
