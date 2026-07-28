#include "test_RemoteExecution.h"

#include <QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>

#include "core/PathUtils.h"
#include "core/Settings.h"
#include "controllers/app/AppController.h"
#include "controllers/tts/ColabVoiceCloneController.h"
#include "controllers/tts/ColabVoiceDesignController.h"
#include "controllers/separation/ColabVoiceIsolatorController.h"
#include "controllers/alignment/ColabAlignmentController.h"
#include "controllers/tts/ColabTtsController.h"
#include "controllers/tts/GatewayTtsController.h"
#include "controllers/translation/TranslationController.h"
#include "controllers/llm/LlmChatController.h"
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
        : CatalogMock(QList<QByteArray>{std::move(response)})
    {
    }

    explicit CatalogMock(QList<QByteArray> responses)
        : m_responses(std::move(responses))
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                m_socket = socket;
                m_pending.clear();
                connect(socket, &QTcpSocket::readyRead, this, [this] { consume(); });
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    QByteArray request() const { return m_request; }
    QList<QByteArray> requests() const { return m_requests; }

private:
    void consume()
    {
        if (!m_socket) return;
        m_pending += m_socket->readAll();
        const int headerEnd = m_pending.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        m_request = m_pending.left(headerEnd + 4);
        const QByteArray response = m_responses.value(m_requests.size());
        m_requests.append(m_request);
        const QByteArray wireResponse = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
            + QByteArray::number(response.size())
            + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + response;
        m_socket->write(wireResponse);
        m_socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QPointer<QTcpSocket> m_socket;
    QList<QByteArray> m_responses;
    QByteArray m_pending;
    QByteArray m_request;
    QList<QByteArray> m_requests;
};

class SilentCatalogMock final : public QObject
{
public:
    SilentCatalogMock()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                socket->setParent(this);
                m_sockets.append(socket);
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

private:
    QTcpServer m_server;
    QList<QPointer<QTcpSocket>> m_sockets;
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

void TestRemoteExecution::temporaryColabWorkerWrapperValidatesAndRemainsEphemeral()
{
    ColabSession session;
    QVERIFY(!session.connectTemporaryWorker(QStringLiteral("not-a-worker-url"),
                                            QStringLiteral("temporary-colab-token")));
    QVERIFY(!session.lastError().isEmpty());
    QVERIFY(!session.isActive());

    QVERIFY(session.connectTemporaryWorker(QStringLiteral("https://worker.example.test"),
                                           QStringLiteral("temporary-colab-token")));
    QVERIFY(session.lastError().isEmpty());
    QVERIFY(session.isActive());

    QSettings settings(PathUtils::dataDir() + QStringLiteral("/settings.ini"), QSettings::IniFormat);
    QVERIFY(!settings.contains(QStringLiteral("remote/colabWorkerUrl")));
    QVERIFY(!settings.contains(QStringLiteral("secrets/colab-worker")));

    session.disconnectTemporaryWorker();
    QVERIFY(!session.isActive());
}

void TestRemoteExecution::appControllerScopesColabSessionsPerCapability()
{
    AppController *app = AppController::instance();
    QVERIFY(app);
    QVERIFY(app->colabSttSession());
    QVERIFY(app->colabTtsSession());
    QVERIFY(app->colabTranslationSession());
    QVERIFY(app->colabVoiceCloneSession());
    QVERIFY(app->colabSeparationSession());
    QVERIFY(app->colabSttSession() != app->colabTtsSession());
    QVERIFY(app->colabTtsSession() != app->colabTranslationSession());
    QVERIFY(app->colabVoiceCloneSession() != app->colabSeparationSession());

    ColabSession *tts = app->colabTtsSession();
    ColabSession *translation = app->colabTranslationSession();
    tts->clear();
    translation->clear();

    QString error;
    QVERIFY(tts->setSession(QStringLiteral("https://tts-worker.example.test"),
                            QStringLiteral("tts-session-token"), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(translation->setSession(QStringLiteral("https://translation-worker.example.test"),
                                    QStringLiteral("translation-session-token"), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    tts->clear();
    QVERIFY(!tts->isActive());
    QVERIFY(translation->isActive());
    QCOMPARE(translation->workerUrl(), QStringLiteral("https://translation-worker.example.test"));
    QCOMPARE(translation->bearerTokenForRequest(), QStringLiteral("translation-session-token"));
    translation->clear();
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

void TestRemoteExecution::remoteFirstAlignmentStaysDirectWhenAColabSessionIsAvailable()
{
    Settings settings;
    const bool original = settings.remoteFirstMode();
    settings.setRemoteFirstMode(false);
    ColabSession session;
    QString error;
    QVERIFY(session.setSession(QStringLiteral("https://worker.example.test"),
                               QStringLiteral("temporary-colab-token"), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    ColabAlignmentController controller(&session, &settings);
    QVERIFY(!controller.colabActive());
    settings.setRemoteFirstMode(true);
    QVERIFY(controller.colabActive());
    QSignalSpy failures(&controller, &ColabAlignmentController::failed);
    controller.useLocal();
    QVERIFY(controller.colabActive());
    QCOMPARE(failures.count(), 1);
    QVERIFY(failures.takeFirst().at(0).toString().contains(QStringLiteral("Remote-first")));

    settings.setRemoteFirstMode(original);
}

void TestRemoteExecution::remoteFirstTtsBlocksLocalButPreservesIndependentRoutes()
{
    Settings settings;
    const bool original = settings.remoteFirstMode();
    settings.setRemoteFirstMode(false);
    ColabSession session;
    QString error;
    QVERIFY(session.setSession(QStringLiteral("https://worker.example.test"),
                               QStringLiteral("temporary-colab-token"), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    ColabTtsController controller(&session, &settings, nullptr, nullptr, nullptr);
    controller.useColab();
    QVERIFY(controller.colabActive());
    settings.setRemoteFirstMode(true);
    QSignalSpy errors(&controller, &ColabTtsController::errorOccurred);
    controller.useLocal();
    QVERIFY(controller.colabActive());
    QCOMPARE(errors.count(), 1);
    QVERIFY(errors.takeFirst().at(0).toString().contains(QStringLiteral("Remote-first")));
    controller.deactivateColab();
    QVERIFY(!controller.colabActive());
    QCOMPARE(session.workerUrl(), QStringLiteral("https://worker.example.test"));
    QCOMPARE(session.bearerTokenForRequest(), QStringLiteral("temporary-colab-token"));

    settings.setRemoteFirstMode(original);
}

void TestRemoteExecution::gatewayAndColabTtsControllersStayIndependent()
{
    Settings settings;
    const QString originalGatewayUrl = settings.gatewayUrl();
    const QString originalGatewayKey = settings.gatewayApiKey();
    const QString originalGatewayModel = settings.gatewayTtsModel();
    const QString originalGatewayVoice = settings.gatewayTtsVoice();
    const bool originalRemoteFirst = settings.remoteFirstMode();
    settings.setRemoteFirstMode(true);
    settings.setGatewayUrl(QStringLiteral("https://gateway.example.test/v1"));
    settings.setGatewayApiKey(QStringLiteral("gateway-test-token"));
    settings.setGatewayTtsModel(QStringLiteral("gateway-tts-model"));
    settings.setGatewayTtsVoice(QStringLiteral("alloy"));

    ColabSession session;
    QString error;
    QVERIFY(session.setSession(QStringLiteral("https://worker.example.test"),
                               QStringLiteral("temporary-colab-token"), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    {
        GatewayTtsController gateway(&settings, nullptr, nullptr, nullptr);
        ColabTtsController colab(&session, &settings, nullptr, nullptr, nullptr);
        gateway.useGateway();
        colab.useColab();
        QVERIFY(gateway.gatewayActive());
        QVERIFY(colab.colabActive());

        // Selecting or disconnecting one direct route does not clear the
        // other route's state or its temporary Colab credentials.
        gateway.disconnectGateway();
        QVERIFY(!gateway.gatewayActive());
        QVERIFY(colab.colabActive());
        QCOMPARE(session.workerUrl(), QStringLiteral("https://worker.example.test"));
        QCOMPARE(session.bearerTokenForRequest(), QStringLiteral("temporary-colab-token"));

        gateway.useGateway();
        colab.deactivateColab();
        QVERIFY(gateway.gatewayActive());
        QVERIFY(!colab.colabActive());
        QCOMPARE(settings.gatewayApiKey(), QStringLiteral("gateway-test-token"));
    }

    settings.setGatewayUrl(originalGatewayUrl);
    settings.setGatewayApiKey(originalGatewayKey);
    settings.setGatewayTtsModel(originalGatewayModel);
    settings.setGatewayTtsVoice(originalGatewayVoice);
    settings.setRemoteFirstMode(originalRemoteFirst);
}

void TestRemoteExecution::remoteFirstTranslationBlocksLocalExecution()
{
    Settings settings;
    const bool original = settings.remoteFirstMode();
    settings.setRemoteFirstMode(false);
    ColabSession session;
    QString error;
    QVERIFY(session.setSession(QStringLiteral("https://worker.example.test"),
                               QStringLiteral("temporary-colab-token"), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    TranslationController controller(nullptr, nullptr, &settings, &session);
    controller.useColab();
    QVERIFY(controller.colabActive());
    settings.setRemoteFirstMode(true);
    controller.useLocal();
    QVERIFY(controller.colabActive());
    QVERIFY(controller.errorText().contains(QStringLiteral("Remote-first")));

    settings.setRemoteFirstMode(false);
    controller.useLocal();
    QVERIFY(!controller.colabActive());
    QVERIFY(controller.importText(QStringLiteral("Remote execution only.")));
    settings.setRemoteFirstMode(true);
    controller.translateAll();
    QVERIFY(controller.errorText().contains(QStringLiteral("Remote-first")));
    QVERIFY(!controller.processing());
    settings.setRemoteFirstMode(original);
}

void TestRemoteExecution::remoteFirstChatBlocksLocalExecution()
{
    Settings settings;
    const bool original = settings.remoteFirstMode();
    settings.setRemoteFirstMode(false);
    ColabSession session;
    QString error;
    QVERIFY(session.setSession(QStringLiteral("https://worker.example.test"),
                               QStringLiteral("temporary-colab-token"), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    LlmChatController controller(nullptr, nullptr, &settings, &session);
    controller.useColab();
    QVERIFY(controller.colabActive());
    settings.setRemoteFirstMode(true);
    controller.useLocal();
    QVERIFY(controller.colabActive());
    QVERIFY(controller.errorText().contains(QStringLiteral("Remote-first")));

    settings.setRemoteFirstMode(false);
    controller.useLocal();
    QVERIFY(!controller.colabActive());
    settings.setRemoteFirstMode(true);
    controller.sendMessage(QStringLiteral("Remote execution only."));
    QVERIFY(controller.errorText().contains(QStringLiteral("Remote-first")));
    QVERIFY(!controller.generating());
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
    CatalogMock server({
        QByteArrayLiteral(R"({"object":"list","data":[{"id":"router/chat-pro","owned_by":"9router"},{"id":"router/translate","name":"Translation API"}]})"),
        QByteArrayLiteral(R"({"object":"list","data":[{"id":"router/stt-pro","name":"Speech-to-Text API"}]})"),
        QByteArrayLiteral(R"({"object":"list","data":[{"id":"router/tts-pro","name":"Text-to-Speech API"}]})"),
    });
    QVERIFY(server.start());

    const GatewayModelCatalog::Result result = GatewayModelCatalog::fetch(
        server.baseUrl() + QStringLiteral("/v1"), QStringLiteral("gateway-catalog-token"), true);
    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QCOMPARE(result.models.size(), 4);
    const QVariantMap first = result.models.at(0).toMap();
    QCOMPARE(first.value(QStringLiteral("provider")).toString(), QStringLiteral("api-gateway"));
    QCOMPARE(first.value(QStringLiteral("capability")).toString(), QStringLiteral("llm"));
    QCOMPARE(first.value(QStringLiteral("modelId")).toString(), QStringLiteral("router/chat-pro"));
    QVERIFY(first.value(QStringLiteral("selectable")).toBool());
    const QVariantMap stt = result.models.at(2).toMap();
    QCOMPARE(stt.value(QStringLiteral("capability")).toString(), QStringLiteral("stt"));
    QCOMPARE(stt.value(QStringLiteral("modelId")).toString(), QStringLiteral("router/stt-pro"));
    const QVariantMap tts = result.models.at(3).toMap();
    QCOMPARE(tts.value(QStringLiteral("capability")).toString(), QStringLiteral("tts"));
    QCOMPARE(tts.value(QStringLiteral("modelId")).toString(), QStringLiteral("router/tts-pro"));

    const QList<QByteArray> requests = server.requests();
    QCOMPARE(requests.size(), 3);
    QCOMPARE(requests.at(0).left(requests.at(0).indexOf("\r\n")),
             QByteArrayLiteral("GET /v1/models HTTP/1.1"));
    QCOMPARE(requests.at(1).left(requests.at(1).indexOf("\r\n")),
             QByteArrayLiteral("GET /v1/models/stt HTTP/1.1"));
    QCOMPARE(requests.at(2).left(requests.at(2).indexOf("\r\n")),
             QByteArrayLiteral("GET /v1/models/tts HTTP/1.1"));
    for (const QByteArray &request : requests)
        QVERIFY(request.toLower().contains("authorization: bearer gateway-catalog-token"));
}

void TestRemoteExecution::colabCapabilityCatalogUsesDirectWorkerOnly()
{
    CatalogMock server(QByteArrayLiteral(
        R"({"contract_version":1,"device":"cuda","available_vram_gb":8,"capabilities":[{"id":"tts","models":[{"id":"kokoro","source":"hexgrad/Kokoro-82M","revision":"abc123","license":"Apache-2.0","device":"cuda","required_vram_gb":4}]},{"id":"translation","models":[{"id":"large-mt","loaded":true,"device":"cuda","required_vram_gb":16}]}]})"));
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

void TestRemoteExecution::colabCapabilityCatalogRequiresSupportedContractVersion()
{
    CatalogMock missingVersion(QByteArrayLiteral(
        R"({"device":"cuda","capabilities":[{"id":"tts","models":[{"id":"kokoro","device":"cuda"}]}]})"));
    QVERIFY(missingVersion.start());

    const ColabCapabilityCatalog::Result result = ColabCapabilityCatalog::fetch(
        QUrl(missingVersion.baseUrl()), QStringLiteral("contract-version-token"), true);
    QVERIFY(!result.isSuccess());
    QVERIFY(result.error.contains(QStringLiteral("contract_version must be 1")));
    QVERIFY(!result.error.contains(QStringLiteral("contract-version-token")));
}

void TestRemoteExecution::remoteCatalogRequestsTimeOut()
{
    SilentCatalogMock server;
    QVERIFY(server.start());

    QElapsedTimer elapsed;
    elapsed.start();
    const GatewayModelCatalog::Result gateway = GatewayModelCatalog::fetch(
        server.baseUrl() + QStringLiteral("/v1"), QStringLiteral("gateway-timeout-token"), true, 120);
    QVERIFY(!gateway.isSuccess());
    QVERIFY(!gateway.error.contains(QStringLiteral("gateway-timeout-token")));
    QVERIFY2(elapsed.elapsed() < 5'000, "Gateway catalog timeout exceeded its bounded test window");

    elapsed.restart();
    const ColabCapabilityCatalog::Result colab = ColabCapabilityCatalog::fetch(
        QUrl(server.baseUrl()), QStringLiteral("colab-timeout-token"), true, 120);
    QVERIFY(!colab.isSuccess());
    QVERIFY(!colab.error.contains(QStringLiteral("colab-timeout-token")));
    QVERIFY2(elapsed.elapsed() < 5'000, "Colab catalog timeout exceeded its bounded test window");
}

void TestRemoteExecution::remoteModelCatalogAggregatesIndependentColabSessions()
{
    CatalogMock sttServer(QByteArrayLiteral(
        R"({"contract_version":1,"device":"cuda","capabilities":[{"id":"stt","models":[{"id":"whisper-contract","device":"cuda"}]}]})"));
    CatalogMock ttsServer(QByteArrayLiteral(
        R"({"contract_version":1,"device":"cuda","capabilities":[{"id":"tts","models":[{"id":"kokoro-contract","device":"cuda"}]}]})"));
    QVERIFY(sttServer.start());
    QVERIFY(ttsServer.start());

    Settings settings;
    ColabSession sttSession;
    ColabSession ttsSession;
    QString error;
    QVERIFY2(sttSession.setSession(sttServer.baseUrl(), QStringLiteral("stt-catalog-token"),
                                   &error, true), qPrintable(error));
    QVERIFY2(ttsSession.setSession(ttsServer.baseUrl(), QStringLiteral("tts-catalog-token"),
                                   &error, true), qPrintable(error));
    RemoteModelCatalogController controller(&settings, {
        {QStringLiteral("stt"), &sttSession},
        {QStringLiteral("tts"), &ttsSession},
    }, nullptr, true);

    controller.refreshColab();
    QTRY_VERIFY(!controller.colabRefreshing());
    QVERIFY(controller.colabAvailable());
    QCOMPARE(controller.colabModels().size(), 2);
    QVERIFY(controller.isModelSelectable(QStringLiteral("colab-direct"),
                                          QStringLiteral("whisper-contract"),
                                          QStringLiteral("stt")));
    QVERIFY(controller.isModelSelectable(QStringLiteral("colab-direct"),
                                          QStringLiteral("kokoro-contract"),
                                          QStringLiteral("tts")));
    const QVariantMap sttModel = controller.colabModels().at(0).toMap();
    QCOMPARE(sttModel.value(QStringLiteral("workerCapability")).toString(), QStringLiteral("stt"));
    const QVariantMap ttsModel = controller.colabModels().at(1).toMap();
    QCOMPARE(ttsModel.value(QStringLiteral("workerCapability")).toString(), QStringLiteral("tts"));
    QVERIFY(sttServer.request().toLower().contains("authorization: bearer stt-catalog-token"));
    QVERIFY(ttsServer.request().toLower().contains("authorization: bearer tts-catalog-token"));
}

void TestRemoteExecution::remoteModelCatalogRetainsHealthyWorkerWhenAnotherFails()
{
    CatalogMock ttsServer(QByteArrayLiteral(
        R"({"contract_version":1,"device":"cuda","capabilities":[{"id":"tts","models":[{"id":"kokoro-survives-reset","device":"cuda"}]}]})"));
    QVERIFY(ttsServer.start());

    // Reserve then release a loopback port so the STT request fails immediately
    // without making the test depend on an external network endpoint.
    QTcpServer unavailableServer;
    QVERIFY(unavailableServer.listen(QHostAddress::LocalHost));
    const QString unavailableUrl = QStringLiteral("http://127.0.0.1:%1")
        .arg(unavailableServer.serverPort());
    unavailableServer.close();

    Settings settings;
    ColabSession sttSession;
    ColabSession ttsSession;
    QString error;
    QVERIFY2(sttSession.setSession(unavailableUrl, QStringLiteral("stt-reset-token"), &error, true),
             qPrintable(error));
    QVERIFY2(ttsSession.setSession(ttsServer.baseUrl(), QStringLiteral("tts-survives-token"),
                                   &error, true), qPrintable(error));
    RemoteModelCatalogController controller(&settings, {
        {QStringLiteral("stt"), &sttSession},
        {QStringLiteral("tts"), &ttsSession},
    }, nullptr, true);

    controller.refreshColab();
    QTRY_VERIFY(!controller.colabRefreshing());
    QVERIFY(controller.colabAvailable());
    QCOMPARE(controller.colabModels().size(), 1);
    QCOMPARE(controller.colabModels().constFirst().toMap()
                 .value(QStringLiteral("modelId")).toString(),
             QStringLiteral("kokoro-survives-reset"));
    QVERIFY(controller.colabError().contains(QStringLiteral("stt worker")));
    QVERIFY(ttsServer.request().toLower().contains("authorization: bearer tts-survives-token"));
    QVERIFY(!controller.colabError().contains(QStringLiteral("tts-survives-token")));
}

void TestRemoteExecution::colabNotebooksAdvertiseCapabilityContractVersion()
{
    const QDir sourceRoot(QStringLiteral(LASTUDIO_SOURCE_DIR));
    const QStringList notebooks{
        QStringLiteral("LA_STUDIO_SPEECH_GPU.ipynb"),
        QStringLiteral("LA_STUDIO_VOICE_GPU.ipynb"),
        QStringLiteral("LA_STUDIO_VOICE_CLONE_OMNIVOICE_GPU.ipynb"),
        QStringLiteral("LA_STUDIO_VOICE_CLONE_QWEN3_BASE_0_6B_GPU.ipynb"),
        QStringLiteral("LA_STUDIO_VOICE_CLONE_QWEN3_BASE_1_7B_GPU.ipynb"),
        QStringLiteral("LA_STUDIO_VOICE_CLONE_VIENEU_V2_TURBO_GPU.ipynb"),
        QStringLiteral("LA_STUDIO_VOICE_CLONE_VIENEU_V3_TURBO_GPU.ipynb"),
        QStringLiteral("LA_STUDIO_VOICE_CLONE_VOXCPM2_GPU.ipynb"),
        QStringLiteral("LA_STUDIO_VOICE_DESIGN_OMNIVOICE_GPU.ipynb"),
        QStringLiteral("LA_STUDIO_VOICE_DESIGN_QWEN3_1_7B_GPU.ipynb"),
        QStringLiteral("LA_STUDIO_VOICE_DESIGN_VOXCPM2_GPU.ipynb"),
        QStringLiteral("LA_STUDIO_ALIGNMENT_GPU.ipynb"),
        QStringLiteral("LA_STUDIO_SEPARATION_GPU.ipynb"),
        QStringLiteral("LA_STUDIO_LANGUAGE_GPU.ipynb"),
    };
    for (const QString &notebook : notebooks) {
        QFile file(sourceRoot.filePath(QStringLiteral("notebooks/") + notebook));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
        const QByteArray source = file.readAll();
        const bool hasCapabilities = source.contains("@app.get('/v1/capabilities')")
            || source.contains("@app.get(\\\"/v1/capabilities\\\")");
        const bool hasContractVersion = source.contains("'contract_version': 1")
            || source.contains("\\\"contract_version\\\": 1");
        QVERIFY2(hasCapabilities, qPrintable(notebook));
        QVERIFY2(hasContractVersion, qPrintable(notebook));
    }
}

} // namespace LAStudio
