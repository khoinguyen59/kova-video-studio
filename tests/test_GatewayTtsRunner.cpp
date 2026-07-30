#include <QtTest>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <cstring>

#include "tts/GatewayTtsRunner.h"
#include "test_GatewayTtsRunner.h"

namespace LAStudio {
namespace {

QByteArray pcm16Wav()
{
    QByteArray wav(50, '\0');
    std::memcpy(wav.data(), "RIFF", 4);
    const quint32 size = 42;
    std::memcpy(wav.data() + 4, &size, sizeof(size));
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    const quint32 fmtSize = 16;
    const quint16 format = 1;
    const quint16 channels = 1;
    const quint32 rate = 16000;
    const quint32 byteRate = 32000;
    const quint16 align = 2;
    const quint16 bits = 16;
    const quint32 dataSize = 6;
    std::memcpy(wav.data() + 16, &fmtSize, sizeof(fmtSize));
    std::memcpy(wav.data() + 20, &format, sizeof(format));
    std::memcpy(wav.data() + 22, &channels, sizeof(channels));
    std::memcpy(wav.data() + 24, &rate, sizeof(rate));
    std::memcpy(wav.data() + 28, &byteRate, sizeof(byteRate));
    std::memcpy(wav.data() + 32, &align, sizeof(align));
    std::memcpy(wav.data() + 34, &bits, sizeof(bits));
    std::memcpy(wav.data() + 36, "data", 4);
    std::memcpy(wav.data() + 40, &dataSize, sizeof(dataSize));
    const qint16 samples[] = {0, 8192, -8192};
    std::memcpy(wav.data() + 44, samples, sizeof(samples));
    return wav;
}

class GatewayTtsMock final : public QObject
{
public:
    GatewayTtsMock()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                m_socket = socket;
                connect(socket, &QTcpSocket::readyRead, this, [this] { consume(); });
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    QByteArray request() const { return m_request; }

private:
    void consume()
    {
        if (!m_socket) return;
        m_pending += m_socket->readAll();
        const int headerEnd = m_pending.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const auto match = QRegularExpression(QStringLiteral("Content-Length: (\\d+)"),
                                              QRegularExpression::CaseInsensitiveOption)
                               .match(QString::fromLatin1(m_pending.left(headerEnd)));
        if (!match.hasMatch()) return;
        const int requestLength = headerEnd + 4 + match.captured(1).toInt();
        if (m_pending.size() < requestLength) return;
        m_request = m_pending.left(requestLength);
        const QByteArray audio = pcm16Wav();
        const QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: ")
            + QByteArray::number(audio.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + audio;
        m_socket->write(response);
        m_socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QPointer<QTcpSocket> m_socket;
    QByteArray m_pending;
    QByteArray m_request;
};

} // namespace

void TestGatewayTtsRunner::testPostsOpenAiCompatibleSpeechRequest()
{
    GatewayTtsMock server;
    QVERIFY(server.start());
    qRegisterMetaType<GatewayTtsRequest>("GatewayTtsRequest");
    qRegisterMetaType<QVector<float>>("QVector<float>");
    QThread workerThread;
    auto *runner = new GatewayTtsRunner;
    runner->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, runner, &QObject::deleteLater);
    workerThread.start();
    QSignalSpy finished(runner, &GatewayTtsRunner::finished);
    QSignalSpy failures(runner, &GatewayTtsRunner::failed);
    QSignalSpy progress(runner, &GatewayTtsRunner::progress);

    GatewayTtsRequest request;
    request.gatewayUrl = server.baseUrl() + QStringLiteral("/v1");
    request.apiKey = QStringLiteral("gateway-tts-test-key");
    request.model = QStringLiteral("gateway-tts-model");
    request.text = QStringLiteral("Hello from Gateway");
    request.voice = QStringLiteral("alloy");
    request.speed = 1.25F;
    request.allowInsecureLocalhost = true;
    QVERIFY(QMetaObject::invokeMethod(runner, "synthesize", Qt::QueuedConnection,
                                      Q_ARG(GatewayTtsRequest, request)));

    QVERIFY2(finished.wait(5000), "Gateway TTS worker did not finish.");
    QCOMPARE(failures.count(), 0);
    const QList<QVariant> result = finished.takeFirst();
    QCOMPARE(result.at(0).toByteArray().size(), 6);
    QCOMPARE(result.at(1).value<QVector<float>>().size(), 3);
    QCOMPARE(result.at(2).toInt(), 16000);
    QCOMPARE(progress.count(), 1);
    QCOMPARE(progress.constFirst().at(0).toInt(), 100);
    const QByteArray body = server.request();
    QVERIFY(body.startsWith("POST /v1/audio/speech HTTP/1.1\r\n"));
    QVERIFY(body.toLower().contains("authorization: bearer gateway-tts-test-key"));
    QVERIFY(body.contains("\"model\":\"gateway-tts-model\""));
    QVERIFY(body.contains("\"input\":\"Hello from Gateway\""));
    QVERIFY(body.contains("\"voice\":\"alloy\""));
    QVERIFY(body.contains("\"response_format\":\"wav\""));
    workerThread.quit();
    QVERIFY(workerThread.wait(5000));
}

} // namespace LAStudio
