#include "test_ColabChatRunner.h"

#include "llm/ColabChatRunner.h"

#include <QPointer>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QtTest>

namespace LAStudio {
namespace {

class ColabChatMock final : public QObject
{
public:
    explicit ColabChatMock(bool holdResponse = false)
        : m_holdResponse(holdResponse)
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
        if (m_holdResponse) {
            m_socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: keep-alive\r\n\r\n"));
            return;
        }
        const QByteArray stream = QByteArrayLiteral(
            "data: {\"choices\":[{\"delta\":{\"content\":\"Xin \"}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"content\":\"chao\"}}]}\n\n"
            "data: [DONE]\n\n");
        m_socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: ")
                        + QByteArray::number(stream.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + stream);
        m_socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QPointer<QTcpSocket> m_socket;
    QByteArray m_pending;
    QByteArray m_request;
    bool m_holdResponse = false;
};

} // namespace

void TestColabChatRunner::streamsDirectColabChatOnly()
{
    ColabChatMock worker;
    QVERIFY(worker.start());
    qRegisterMetaType<ColabChatRequest>("ColabChatRequest");
    QThread thread;
    auto *runner = new ColabChatRunner;
    runner->moveToThread(&thread);
    connect(&thread, &QThread::finished, runner, &QObject::deleteLater);
    thread.start();
    QSignalSpy tokens(runner, &ColabChatRunner::tokenGenerated);
    QSignalSpy finished(runner, &ColabChatRunner::finished);
    QSignalSpy failures(runner, &ColabChatRunner::failed);

    ColabChatRequest request;
    request.workerUrl = QUrl(worker.baseUrl());
    request.bearerToken = QStringLiteral("colab-chat-token");
    request.model = QStringLiteral("qwen2.5-3b-instruct");
    request.messages = {QVariantMap{{QStringLiteral("role"), QStringLiteral("user")},
                                    {QStringLiteral("content"), QStringLiteral("Hello")}}};
    request.maxTokens = 320;
    request.temperature = 0.25F;
    request.topP = 0.9F;
    request.requestId = QStringLiteral("direct-request");
    request.allowInsecureLocalhost = true;
    QVERIFY(QMetaObject::invokeMethod(runner, "generate", Qt::QueuedConnection,
                                      Q_ARG(ColabChatRequest, request)));

    QVERIFY2(finished.wait(5000), "Colab chat worker did not finish.");
    QCOMPARE(failures.count(), 0);
    QCOMPARE(tokens.count(), 2);
    QCOMPARE(finished.takeFirst().at(1).toString(), QStringLiteral("Xin chao"));
    const QByteArray sent = worker.request();
    QVERIFY(sent.startsWith("POST /v1/chat/completions HTTP/1.1\r\n"));
    QVERIFY(sent.toLower().contains("authorization: bearer colab-chat-token"));
    QVERIFY(sent.contains("\"model\":\"qwen2.5-3b-instruct\""));
    QVERIFY(sent.contains("\"content\":\"Hello\""));
    QVERIFY(sent.contains("\"stream\":true"));
    QVERIFY(!sent.contains("gateway"));
    thread.quit();
    QVERIFY(thread.wait(5000));
}

void TestColabChatRunner::cancellationAbortsDirectStream()
{
    ColabChatMock worker(true);
    QVERIFY(worker.start());
    qRegisterMetaType<ColabChatRequest>("ColabChatRequest");
    QThread thread;
    auto *runner = new ColabChatRunner;
    runner->moveToThread(&thread);
    connect(&thread, &QThread::finished, runner, &QObject::deleteLater);
    thread.start();
    QSignalSpy cancelled(runner, &ColabChatRunner::cancelled);
    QSignalSpy failures(runner, &ColabChatRunner::failed);

    ColabChatRequest request;
    request.workerUrl = QUrl(worker.baseUrl());
    request.bearerToken = QStringLiteral("cancel-token");
    request.model = QStringLiteral("qwen2.5-3b-instruct");
    request.messages = {QVariantMap{{QStringLiteral("role"), QStringLiteral("user")},
                                    {QStringLiteral("content"), QStringLiteral("Cancel me")}}};
    request.requestId = QStringLiteral("cancel-request");
    request.allowInsecureLocalhost = true;
    QVERIFY(QMetaObject::invokeMethod(runner, "generate", Qt::QueuedConnection,
                                      Q_ARG(ColabChatRequest, request)));
    QTRY_VERIFY_WITH_TIMEOUT(!worker.request().isEmpty(), 3000);
    QVERIFY(QMetaObject::invokeMethod(runner, "cancel", Qt::QueuedConnection));
    QVERIFY2(cancelled.wait(5000), "Colab chat cancellation did not complete.");
    QCOMPARE(failures.count(), 0);
    QCOMPARE(cancelled.takeFirst().at(0).toString(), QStringLiteral("cancel-request"));
    thread.quit();
    QVERIFY(thread.wait(5000));
}

} // namespace LAStudio
