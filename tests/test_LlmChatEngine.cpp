#include "test_LlmChatEngine.h"

#include "llm/LlmChatEngine.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

namespace LAStudio {
namespace {

class ScopedEnvironmentVariable final
{
public:
    ScopedEnvironmentVariable(const char *name, const QByteArray &value)
        : m_name(name)
        , m_wasSet(qEnvironmentVariableIsSet(name))
        , m_previous(qgetenv(name))
    {
        qputenv(m_name.constData(), value);
    }

    ~ScopedEnvironmentVariable()
    {
        if (m_wasSet) {
            qputenv(m_name.constData(), m_previous);
        } else {
            qunsetenv(m_name.constData());
        }
    }

private:
    QByteArray m_name;
    bool m_wasSet = false;
    QByteArray m_previous;
};

class MockGateway final : public QObject
{
public:
    explicit MockGateway(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                m_socket = socket;
                connect(socket, &QTcpSocket::readyRead, this, &MockGateway::consumeRequest);
            }
        });
    }

    bool start()
    {
        return m_server.listen(QHostAddress::LocalHost);
    }

    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1/v1").arg(m_server.serverPort());
    }

    QByteArray request() const { return m_request; }

private:
    void consumeRequest()
    {
        if (!m_socket) return;
        m_pending += m_socket->readAll();
        const int headerEnd = m_pending.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;

        const QByteArray headers = m_pending.left(headerEnd);
        const QRegularExpression contentLengthPattern(QStringLiteral("Content-Length: (\\d+)"),
                                                       QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = contentLengthPattern.match(QString::fromLatin1(headers));
        if (!match.hasMatch()) return;
        const int contentLength = match.captured(1).toInt();
        const int requestLength = headerEnd + 4 + contentLength;
        if (m_pending.size() < requestLength) return;

        m_request = m_pending.left(requestLength);
        const QByteArray response = QByteArrayLiteral(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: close\r\n\r\n"
            "data: {\"choices\":[{\"delta\":{\"content\":\"Hello \"}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"content\":\"gateway\"}}]}\n\n"
            "data: [DONE]\n\n");
        m_socket->write(response);
        m_socket->disconnectFromHost();
    }

private:
    QTcpServer m_server;
    QPointer<QTcpSocket> m_socket;
    QByteArray m_pending;
    QByteArray m_request;
};

} // namespace

void TestLlmChatEngine::rejectsMissingLocalRuntime()
{
    ScopedEnvironmentVariable hostOverride("LASTUDIO_RUNTIME_HOST", "0");
    LlmChatEngine engine;
    QSignalSpy errors(&engine, &LlmChatEngine::errorOccurred);

    engine.load(QStringLiteral("missing-runtime.dll"),
                QStringLiteral("missing-model.gguf"), false);

    QVERIFY2(errors.wait(5000), "Loading a missing local runtime must fail promptly.");
    QCOMPARE(errors.count(), 1);
    QCOMPARE(engine.state(), LlmChatEngine::Error);
    QVERIFY(!engine.isModelLoaded());
    QVERIFY(!engine.isProcessing());
    QVERIFY(!errors.takeFirst().at(0).toString().isEmpty());
}

void TestLlmChatEngine::gatewayChatUsesOpenAiCompatibleEndpoint()
{
    MockGateway gateway;
    QVERIFY2(gateway.start(), qPrintable(gateway.property("errorString").toString()));

    LlmChatEngine engine;
    QSignalSpy finished(&engine, &LlmChatEngine::generationFinished);
    QSignalSpy errors(&engine, &LlmChatEngine::errorOccurred);
    engine.loadGateway(gateway.baseUrl(), QStringLiteral("test-gateway-key"),
                       QStringLiteral("test-chat-model"), true);
    QTRY_COMPARE_WITH_TIMEOUT(engine.state(), LlmChatEngine::Ready, 5000);
    QVERIFY(engine.isModelLoaded());
    QVERIFY(engine.isGatewayActive());

    engine.generate({QVariantMap{{QStringLiteral("role"), QStringLiteral("user")},
                                  {QStringLiteral("content"), QStringLiteral("Hello")}}},
                    4096, 128, 0.2F, 0.9F, 20, 1.05F, QStringLiteral("gateway-request"));
    QVERIFY2(finished.wait(5000), "Gateway chat response did not finish.");
    QCOMPARE(errors.count(), 0);
    QCOMPARE(finished.takeFirst().at(1).toString(), QStringLiteral("Hello gateway"));

    const QByteArray request = gateway.request();
    QVERIFY(request.startsWith("POST /v1/chat/completions HTTP/1.1\r\n"));
    QVERIFY(request.toLower().contains("authorization: bearer test-gateway-key"));
    const int bodyStart = request.indexOf("\r\n\r\n");
    QVERIFY(bodyStart >= 0);
    const QJsonDocument body = QJsonDocument::fromJson(request.mid(bodyStart + 4));
    QVERIFY(body.isObject());
    QCOMPARE(body.object().value(QStringLiteral("model")).toString(), QStringLiteral("test-chat-model"));
    QVERIFY(body.object().value(QStringLiteral("stream")).toBool());
    QCOMPARE(body.object().value(QStringLiteral("messages")).toArray().first().toObject()
                 .value(QStringLiteral("content")).toString(),
             QStringLiteral("Hello"));
}

void TestLlmChatEngine::gatewayChatRejectsIncompleteConfiguration()
{
    LlmChatEngine engine;
    QSignalSpy errors(&engine, &LlmChatEngine::errorOccurred);
    engine.loadGateway(QStringLiteral("https://gateway.example.test/v1"), {},
                       QStringLiteral("test-chat-model"));

    QVERIFY2(errors.wait(5000), "An empty gateway key must be rejected.");
    QCOMPARE(engine.state(), LlmChatEngine::Error);
    QVERIFY(!engine.isModelLoaded());
    QVERIFY(!engine.isGatewayActive());
    QVERIFY(errors.takeFirst().at(0).toString().contains(QStringLiteral("key"), Qt::CaseInsensitive));
}

} // namespace LAStudio
