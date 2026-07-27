#include <QtTest>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <cstring>

#include "tts/ColabTtsRunner.h"
#include "test_ColabTtsRunner.h"

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

class ColabTtsMock final : public QObject
{
public:
    ColabTtsMock()
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

void TestColabTtsRunner::testPostsDirectWorkerSpeechRequest()
{
    ColabTtsMock server;
    QVERIFY(server.start());
    qRegisterMetaType<ColabTtsRequest>("ColabTtsRequest");
    qRegisterMetaType<QVector<float>>("QVector<float>");
    QThread workerThread;
    auto *runner = new ColabTtsRunner;
    runner->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, runner, &QObject::deleteLater);
    workerThread.start();
    QSignalSpy finished(runner, &ColabTtsRunner::finished);
    QSignalSpy failures(runner, &ColabTtsRunner::failed);

    ColabTtsRequest request;
    request.workerUrl = QUrl(server.baseUrl());
    request.bearerToken = QStringLiteral("colab-tts-token");
    request.model = QStringLiteral("kokoro-vn");
    request.text = QStringLiteral("Xin chào Việt Nam");
    request.voice = QStringLiteral("female-01");
    request.language = QStringLiteral("vi");
    request.speed = 1.15F;
    request.settings.insert(QStringLiteral("seed"), 1234);
    request.allowInsecureLocalhost = true;
    QVERIFY(QMetaObject::invokeMethod(runner, "synthesize", Qt::QueuedConnection,
                                      Q_ARG(ColabTtsRequest, request)));

    QVERIFY2(finished.wait(5000), "Colab TTS worker did not finish.");
    QCOMPARE(failures.count(), 0);
    const QList<QVariant> result = finished.takeFirst();
    QCOMPARE(result.at(0).toByteArray().size(), 4);
    QCOMPARE(result.at(1).value<QVector<float>>().size(), 2);
    QCOMPARE(result.at(2).toInt(), 24000);
    const QByteArray body = server.request();
    QVERIFY(body.startsWith("POST /v1/audio/speech HTTP/1.1\r\n"));
    QVERIFY(body.toLower().contains("authorization: bearer colab-tts-token"));
    QVERIFY(body.contains("\"model\":\"kokoro-vn\""));
    QVERIFY(body.contains("Xin chào Việt Nam"));
    QVERIFY(body.contains("\"voice\":\"female-01\""));
    QVERIFY(body.contains("\"language\":\"vi\""));
    QVERIFY(body.contains("\"seed\":1234"));
    workerThread.quit();
    QVERIFY(workerThread.wait(5000));
}

} // namespace LAStudio
