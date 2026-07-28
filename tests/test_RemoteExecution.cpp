#include "test_RemoteExecution.h"

#include <QtTest>
#include <QPointer>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>

#include "core/PathUtils.h"
#include "core/Settings.h"
#include "controllers/tts/ColabVoiceCloneController.h"
#include "controllers/tts/ColabVoiceDesignController.h"
#include "controllers/separation/ColabVoiceIsolatorController.h"
#include "controllers/models/RemoteModelCatalogController.h"
#include "remote/ColabCapabilityCatalog.h"
#include "remote/ColabSession.h"
#include "remote/ExecutionProvider.h"
#include "remote/GatewayModelCatalog.h"

namespace LAStudio {
namespace {

class CatalogMock final : public QObject
{
public:
    explicit CatalogMock(QByteArray response)
        : m_response(std::move(response))
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
        m_request = m_pending.left(headerEnd + 4);
        const QByteArray wireResponse = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
            + QByteArray::number(m_response.size())
            + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + m_response;
        m_socket->write(wireResponse);
        m_socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QPointer<QTcpSocket> m_socket;
    QByteArray m_response;
    QByteArray m_pending;
    QByteArray m_request;
};

} // namespace

void TestRemoteExecution::executionProvidersHaveStableIds()
{
    QCOMPARE(executionProviderId(ExecutionProvider::LocalDev), QStringLiteral("local-dev"));
    QCOMPARE(executionProviderId(ExecutionProvider::ApiGateway), QStringLiteral("api-gateway"));
    QCOMPARE(executionProviderId(ExecutionProvider::ColabDirect), QStringLiteral("colab-direct"));

    ExecutionProvider provider = ExecutionProvider::LocalDev;
    QVERIFY(executionProviderFromId(QStringLiteral("colab-direct"), &provider));
    QCOMPARE(provider, ExecutionProvider::ColabDirect);
    QVERIFY(!executionProviderFromId(QStringLiteral("unknown-provider"), &provider));
}

void TestRemoteExecution::remoteEndpointsRequireHttpsByDefault()
{
    const RemoteEndpointValidation insecureGateway = validateRemoteEndpoint(
        QStringLiteral("http://127.0.0.1:20128/v1"), RemoteEndpointKind::ApiGateway);
    QVERIFY(!insecureGateway.isValid());
    QVERIFY(insecureGateway.error.contains(QStringLiteral("HTTPS")));

    const RemoteEndpointValidation testGateway = validateRemoteEndpoint(
        QStringLiteral("http://127.0.0.1:20128/v1"), RemoteEndpointKind::ApiGateway, true);
    QVERIFY2(testGateway.isValid(), qPrintable(testGateway.error));

    const RemoteEndpointValidation withCredentials = validateRemoteEndpoint(
        QStringLiteral("https://secret@example.test/v1"), RemoteEndpointKind::ApiGateway);
    QVERIFY(!withCredentials.isValid());

    const RemoteEndpointValidation withQuery = validateRemoteEndpoint(
        QStringLiteral("https://gateway.example.test/v1?api_key=not-allowed"), RemoteEndpointKind::ApiGateway);
    QVERIFY(!withQuery.isValid());
}

void TestRemoteExecution::apiGatewayEndpointNormalizesV1Url()
{
    const RemoteEndpointValidation valid = validateRemoteEndpoint(
        QStringLiteral("https://gateway.example.test/v1/"), RemoteEndpointKind::ApiGateway);
    QVERIFY2(valid.isValid(), qPrintable(valid.error));
    QCOMPARE(valid.normalizedUrl.toString(), QStringLiteral("https://gateway.example.test/v1"));
    QCOMPARE(appendRemotePath(valid.normalizedUrl, QStringLiteral("models")).toString(),
             QStringLiteral("https://gateway.example.test/v1/models"));

    const RemoteEndpointValidation invalidPath = validateRemoteEndpoint(
        QStringLiteral("https://gateway.example.test/api"), RemoteEndpointKind::ApiGateway);
    QVERIFY(!invalidPath.isValid());
}

void TestRemoteExecution::colabSessionIsMemoryOnlyAndCanBeCleared()
{
    ColabSession session;
    QString error;
    QVERIFY(session.setSession(QStringLiteral("https://worker.example.test/"),
                               QStringLiteral("temporary-colab-token"), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(session.isActive());
    QCOMPARE(session.workerUrl(), QStringLiteral("https://worker.example.test"));
    QCOMPARE(session.bearerTokenForRequest(), QStringLiteral("temporary-colab-token"));

    QSettings settings(PathUtils::dataDir() + QStringLiteral("/settings.ini"), QSettings::IniFormat);
    QVERIFY(!settings.contains(QStringLiteral("remote/colabWorkerUrl")));
    QVERIFY(!settings.contains(QStringLiteral("secrets/colab-worker")));

    session.clear();
    QVERIFY(!session.isActive());
    QVERIFY(session.bearerTokenForRequest().isEmpty());
}

void TestRemoteExecution::gatewayCredentialUsesDedicatedSecureStoreEntry()
{
#ifdef Q_OS_WIN
    Settings settings;
    settings.setGatewayUrl(QStringLiteral("https://gateway.example.test/v1"));
    settings.setGatewayApiKey(QStringLiteral("gateway-secret-for-test"));
    QCOMPARE(settings.gatewayUrl(), QStringLiteral("https://gateway.example.test/v1"));
    QCOMPARE(settings.gatewayApiKey(), QStringLiteral("gateway-secret-for-test"));

    QSettings raw(PathUtils::dataDir() + QStringLiteral("/settings.ini"), QSettings::IniFormat);
    QVERIFY(!raw.contains(QStringLiteral("remote/gatewayApiKey")));
    const QString ciphertext = raw.value(QStringLiteral("secrets/remote-gateway")).toString();
    QVERIFY(ciphertext.startsWith(QStringLiteral("dpapi-v1:")));
    QVERIFY(!ciphertext.contains(QStringLiteral("gateway-secret-for-test")));

    settings.setGatewayApiKey(QString());
#else
    QSKIP("Secure credential persistence is implemented with Windows DPAPI.");
#endif
}

void TestRemoteExecution::remoteFirstModeIsExplicitAndPersistent()
{
    Settings settings;
    const bool original = settings.remoteFirstMode();

    settings.setRemoteFirstMode(false);
    QVERIFY(!settings.remoteFirstMode());
    Settings reloaded;
    QVERIFY(!reloaded.remoteFirstMode());

    reloaded.setRemoteFirstMode(original);
    QCOMPARE(reloaded.remoteFirstMode(), original);
}

void TestRemoteExecution::remoteFirstVoiceCloneStaysDirectWhenAColabSessionIsAvailable()
{
    Settings settings;
    const bool original = settings.remoteFirstMode();
    settings.setRemoteFirstMode(false);
    ColabSession session;
    QString error;
    QVERIFY(session.setSession(QStringLiteral("https://worker.example.test"),
                               QStringLiteral("temporary-colab-token"), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    ColabVoiceCloneController controller(&session, &settings, nullptr, nullptr, nullptr);
    QVERIFY(!controller.colabActive());
    settings.setRemoteFirstMode(true);
    QVERIFY(controller.colabActive());
    QSignalSpy errors(&controller, &ColabVoiceCloneController::errorOccurred);
    controller.useLocal();
    QVERIFY(controller.colabActive());
    QCOMPARE(errors.count(), 1);
    QVERIFY(errors.takeFirst().at(0).toString().contains(QStringLiteral("Remote-first")));

    settings.setRemoteFirstMode(original);
}

void TestRemoteExecution::remoteFirstVoiceDesignStaysDirectWhenAColabSessionIsAvailable()
{
    Settings settings;
    const bool original = settings.remoteFirstMode();
    settings.setRemoteFirstMode(false);
    ColabSession session;
    QString error;
    QVERIFY(session.setSession(QStringLiteral("https://worker.example.test"),
                               QStringLiteral("temporary-colab-token"), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    ColabVoiceDesignController controller(&session, &settings, nullptr, nullptr, nullptr);
    QVERIFY(!controller.colabActive());
    settings.setRemoteFirstMode(true);
    QVERIFY(controller.colabActive());
    QSignalSpy errors(&controller, &ColabVoiceDesignController::errorOccurred);
    controller.useLocal();
    QVERIFY(controller.colabActive());
    QCOMPARE(errors.count(), 1);
    QVERIFY(errors.takeFirst().at(0).toString().contains(QStringLiteral("Remote-first")));

    settings.setRemoteFirstMode(original);
}

void TestRemoteExecution::remoteFirstVoiceIsolationStaysDirectWhenAColabSessionIsAvailable()
{
    Settings settings;
    const bool original = settings.remoteFirstMode();
    settings.setRemoteFirstMode(false);
    ColabSession session;
    QString error;
    QVERIFY(session.setSession(QStringLiteral("https://worker.example.test"),
                               QStringLiteral("temporary-colab-token"), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    ColabVoiceIsolatorController controller(&session, &settings);
    QVERIFY(!controller.colabActive());
    settings.setRemoteFirstMode(true);
    QVERIFY(controller.colabActive());
    QSignalSpy errors(&controller, &ColabVoiceIsolatorController::errorOccurred);
    controller.useLocal();
    QVERIFY(controller.colabActive());
    QCOMPARE(errors.count(), 1);
    QVERIFY(errors.takeFirst().at(0).toString().contains(QStringLiteral("Remote-first")));

    settings.setRemoteFirstMode(original);
}

void TestRemoteExecution::gatewayAndColabFailuresRemainIndependent()
{
    Settings settings;
    settings.setGatewayUrl(QStringLiteral("not-a-valid-gateway-url"));
    settings.setGatewayApiKey(QStringLiteral("gateway-test-token"));
    ColabSession session;
    QString error;
    QVERIFY(session.setSession(QStringLiteral("https://worker.example.test"),
                               QStringLiteral("temporary-colab-token"), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    RemoteModelCatalogController controller(&settings, &session);

    controller.refreshGateway();
    QTRY_VERIFY(!controller.gatewayRefreshing());
    QVERIFY(!controller.gatewayAvailable());
    QCOMPARE(session.workerUrl(), QStringLiteral("https://worker.example.test"));
    QCOMPARE(session.bearerTokenForRequest(), QStringLiteral("temporary-colab-token"));

    session.clear();
    controller.refreshColab();
    QVERIFY(!controller.colabAvailable());
    QVERIFY(!controller.colabError().isEmpty());
    QCOMPARE(settings.gatewayUrl(), QStringLiteral("not-a-valid-gateway-url"));
    QCOMPARE(settings.gatewayApiKey(), QStringLiteral("gateway-test-token"));
    settings.setGatewayApiKey(QString());
}

void TestRemoteExecution::gatewayModelCatalogUsesGatewayOnly()
{
    CatalogMock server(QByteArrayLiteral(
        R"({"object":"list","data":[{"id":"router/chat-pro","owned_by":"9router"},{"id":"router/tts","name":"Speech API"}]})"));
    QVERIFY(server.start());

    const GatewayModelCatalog::Result result = GatewayModelCatalog::fetch(
        server.baseUrl() + QStringLiteral("/v1"), QStringLiteral("gateway-catalog-token"), true);
    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QCOMPARE(result.models.size(), 2);
    const QVariantMap first = result.models.at(0).toMap();
    QCOMPARE(first.value(QStringLiteral("provider")).toString(), QStringLiteral("api-gateway"));
    QCOMPARE(first.value(QStringLiteral("modelId")).toString(), QStringLiteral("router/chat-pro"));
    QVERIFY(first.value(QStringLiteral("selectable")).toBool());
    QCOMPARE(server.request().left(server.request().indexOf("\r\n")),
             QByteArrayLiteral("GET /v1/models HTTP/1.1"));
    QVERIFY(server.request().toLower().contains("authorization: bearer gateway-catalog-token"));
}

void TestRemoteExecution::colabCapabilityCatalogUsesDirectWorkerOnly()
{
    CatalogMock server(QByteArrayLiteral(
        R"({"device":"cuda","available_vram_gb":8,"capabilities":[{"id":"tts","models":[{"id":"kokoro","source":"hexgrad/Kokoro-82M","revision":"abc123","license":"Apache-2.0","device":"cuda","required_vram_gb":4}]},{"id":"translation","models":[{"id":"large-mt","loaded":true,"device":"cuda","required_vram_gb":16}]}]})"));
    QVERIFY(server.start());

    const ColabCapabilityCatalog::Result result = ColabCapabilityCatalog::fetch(
        QUrl(server.baseUrl()), QStringLiteral("temporary-colab-catalog-token"), true);
    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QCOMPARE(result.models.size(), 2);
    const QVariantMap tts = result.models.at(0).toMap();
    QCOMPARE(tts.value(QStringLiteral("provider")).toString(), QStringLiteral("colab-direct"));
    QCOMPARE(tts.value(QStringLiteral("capability")).toString(), QStringLiteral("tts"));
    QCOMPARE(tts.value(QStringLiteral("modelId")).toString(), QStringLiteral("kokoro"));
    QVERIFY(tts.value(QStringLiteral("selectable")).toBool());
    const QVariantMap overBudget = result.models.at(1).toMap();
    QVERIFY(!overBudget.value(QStringLiteral("selectable")).toBool());
    QCOMPARE(server.request().left(server.request().indexOf("\r\n")),
             QByteArrayLiteral("GET /v1/capabilities HTTP/1.1"));
    QVERIFY(server.request().toLower().contains("authorization: bearer temporary-colab-catalog-token"));
}

} // namespace LAStudio
