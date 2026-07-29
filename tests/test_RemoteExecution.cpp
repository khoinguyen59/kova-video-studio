#include "test_RemoteExecution.h"

#include <QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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
    QVERIFY(session.isChecking());
    QVERIFY(!session.isActive());

    QSettings settings(PathUtils::dataDir() + QStringLiteral("/settings.ini"), QSettings::IniFormat);
    QVERIFY(!settings.contains(QStringLiteral("remote/colabWorkerUrl")));
    QVERIFY(!settings.contains(QStringLiteral("secrets/colab-worker")));

    session.disconnectTemporaryWorker();
    QVERIFY(!session.isActive());
    QVERIFY(!session.isChecking());
    QCOMPARE(session.verificationState(), QStringLiteral("disconnected"));
}

void TestRemoteExecution::temporaryColabWorkerVerifiesCudaCapabilityAndExactModel()
{
    CatalogMock server({
        QByteArrayLiteral(
            R"({"status":"ready","ready":true,"device":"cuda","gpu":"Test GPU","model":"kokoro","cpu_fallback":false})"),
        QByteArrayLiteral(
            R"({"contract_version":1,"device":"cuda","capabilities":[{"id":"tts","models":[{"id":"kokoro","device":"cuda","loaded":true}]}]})"),
    });
    QVERIFY(server.start());

    ColabSession session;
    QString error;
    QSignalSpy finished(&session, &ColabSession::verificationFinished);
    QVERIFY2(session.beginVerifiedSession(
                 server.baseUrl(), QStringLiteral("verified-token"),
                 QStringLiteral("tts"), QStringLiteral("kokoro"), &error, true),
             qPrintable(error));
    QVERIFY(session.isChecking());
    QVERIFY(!session.isActive());
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(finished.constFirst().at(0).toBool(), true);
    QVERIFY(session.isActive());
    QVERIFY(session.isVerified());
    QCOMPARE(session.verificationState(), QStringLiteral("ready"));
    QCOMPARE(session.expectedCapability(), QStringLiteral("tts"));
    QCOMPARE(session.expectedModel(), QStringLiteral("kokoro"));
    QCOMPARE(session.reportedGpu(), QStringLiteral("Test GPU"));
    QVERIFY(session.verificationMessage().contains(QStringLiteral("tts / kokoro")));

    const QList<QByteArray> requests = server.requests();
    QCOMPARE(requests.size(), 2);
    QCOMPARE(requests.at(0).left(requests.at(0).indexOf("\r\n")),
             QByteArrayLiteral("GET /health HTTP/1.1"));
    QCOMPARE(requests.at(1).left(requests.at(1).indexOf("\r\n")),
             QByteArrayLiteral("GET /v1/capabilities HTTP/1.1"));
    for (const QByteArray &request : requests)
        QVERIFY(request.toLower().contains("authorization: bearer verified-token"));
}

void TestRemoteExecution::temporaryColabWorkerRejectsCpuWrongModelAndWrongCapability()
{
    {
        CatalogMock cpu(QByteArrayLiteral(
            R"({"status":"ready","ready":true,"device":"cpu","model":"kokoro","cpu_fallback":true})"));
        QVERIFY(cpu.start());
        ColabSession session;
        QString error;
        QVERIFY(session.beginVerifiedSession(
            cpu.baseUrl(), QStringLiteral("cpu-token"), QStringLiteral("tts"),
            QStringLiteral("kokoro"), &error, true));
        QTRY_VERIFY(!session.isChecking());
        QVERIFY(!session.isActive());
        QVERIFY(session.lastError().contains(QStringLiteral("CUDA")));
        QVERIFY(session.bearerTokenForRequest().isEmpty());
    }
    {
        CatalogMock wrongModel(QByteArrayLiteral(
            R"({"status":"ready","ready":true,"device":"cuda","model":"vibevoice-0.5b","cpu_fallback":false})"));
        QVERIFY(wrongModel.start());
        ColabSession session;
        QString error;
        QVERIFY(session.beginVerifiedSession(
            wrongModel.baseUrl(), QStringLiteral("wrong-model-token"),
            QStringLiteral("tts"), QStringLiteral("kokoro"), &error, true));
        QTRY_VERIFY(!session.isChecking());
        QVERIFY(!session.isActive());
        QVERIFY(session.lastError().contains(QStringLiteral("Wrong Colab model")));
        QVERIFY(!session.lastError().contains(QStringLiteral("wrong-model-token")));
    }
    {
        CatalogMock wrongCapability({
            QByteArrayLiteral(
                R"({"status":"ready","ready":true,"device":"cuda","model":"kokoro","cpu_fallback":false})"),
            QByteArrayLiteral(
                R"({"contract_version":1,"device":"cuda","capabilities":[{"id":"stt","models":[{"id":"kokoro","device":"cuda","loaded":true}]}]})"),
        });
        QVERIFY(wrongCapability.start());
        ColabSession session;
        QString error;
        QVERIFY(session.beginVerifiedSession(
            wrongCapability.baseUrl(), QStringLiteral("wrong-capability-token"),
            QStringLiteral("tts"), QStringLiteral("kokoro"), &error, true));
        QTRY_VERIFY(!session.isChecking());
        QVERIFY(!session.isActive());
        QVERIFY(session.lastError().contains(QStringLiteral("capability 'tts' is missing")));
    }
}

void TestRemoteExecution::newerColabVerificationSupersedesStaleRequest()
{
    SilentCatalogMock staleServer;
    CatalogMock currentServer({
        QByteArrayLiteral(
            R"({"status":"ready","ready":true,"device":"cuda","model":"qwen3.5-2b","cpu_fallback":false})"),
        QByteArrayLiteral(
            R"({"contract_version":1,"device":"cuda","capabilities":[{"id":"llm-chat","models":[{"id":"qwen3.5-2b","device":"cuda","loaded":true}]}]})"),
    });
    QVERIFY(staleServer.start());
    QVERIFY(currentServer.start());

    ColabSession session;
    QString error;
    QVERIFY(session.beginVerifiedSession(
        staleServer.baseUrl(), QStringLiteral("stale-token"),
        QStringLiteral("tts"), QStringLiteral("kokoro"), &error, true));
    QVERIFY(session.beginVerifiedSession(
        currentServer.baseUrl(), QStringLiteral("current-token"),
        QStringLiteral("llm-chat"), QStringLiteral("qwen3.5-2b"), &error, true));
    QTRY_VERIFY(session.isActive());
    QCOMPARE(session.expectedCapability(), QStringLiteral("llm-chat"));
    QCOMPARE(session.expectedModel(), QStringLiteral("qwen3.5-2b"));
    QCOMPARE(session.bearerTokenForRequest(), QStringLiteral("current-token"));
}

void TestRemoteExecution::everyGpuFeatureSurfacesVerifiedColabSessionState()
{
    const QDir sourceRoot(QStringLiteral(LASTUDIO_SOURCE_DIR));
    const QStringList featurePanels{
        QStringLiteral("qml/components/stt/SttSettingsPanel.qml"),
        QStringLiteral("qml/components/tts/TtsSettingsPanel.qml"),
        QStringLiteral("qml/components/voicecloning/VoiceSettingsPanel.qml"),
        QStringLiteral("qml/components/voicedesign/VoiceDesignSettingsPanel.qml"),
        QStringLiteral("qml/components/voiceisolator/VoiceIsolatorStudioView.qml"),
        QStringLiteral("qml/components/alignment/AlignmentSetupPanel.qml"),
        QStringLiteral("qml/components/translation/TranslationStudioView.qml"),
        QStringLiteral("qml/components/llm/LlmChatStudioView.qml"),
        QStringLiteral("qml/components/dubbing/DubbingNodeSettingsPanel.qml"),
        QStringLiteral("qml/components/dubbing/DubbingNodeInspector.qml"),
    };
    for (const QString &relativePath : featurePanels) {
        QFile file(sourceRoot.filePath(relativePath));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(relativePath));
        const QString source = QString::fromUtf8(file.readAll());
        QVERIFY2(source.contains(QStringLiteral("ColabSessionStatus")),
                 qPrintable(relativePath + QStringLiteral(
                     " does not show Colab verification state in the feature UI")));
    }

    QFile sessionSource(sourceRoot.filePath(QStringLiteral("src/remote/ColabSession.cpp")));
    QVERIFY(sessionSource.open(QIODevice::ReadOnly));
    const QString source = QString::fromUtf8(sessionSource.readAll());
    QVERIFY(source.contains(QStringLiteral("v1/capabilities")));
    QVERIFY(source.contains(QStringLiteral("cpu_fallback")));
    QVERIFY(source.contains(QStringLiteral("Wrong Colab model")));
    QVERIFY(source.contains(QStringLiteral("contract_version")));
}

void TestRemoteExecution::everyGpuControllerUsesExactVerifiedColabRoute()
{
    const QDir sourceRoot(QStringLiteral(LASTUDIO_SOURCE_DIR));
    const QList<QPair<QString, QString>> controllerRoutes{
        {QStringLiteral("src/controllers/stt/SttSessionController.cpp"),
         QStringLiteral("QStringLiteral(\"stt\")")},
        {QStringLiteral("src/controllers/tts/ColabTtsController.cpp"),
         QStringLiteral("QStringLiteral(\"tts\")")},
        {QStringLiteral("src/controllers/tts/ColabVoiceCloneController.cpp"),
         QStringLiteral("QStringLiteral(\"voice-cloning\")")},
        {QStringLiteral("src/controllers/tts/ColabVoiceDesignController.cpp"),
         QStringLiteral("QStringLiteral(\"voice-design\")")},
        {QStringLiteral("src/controllers/alignment/ColabAlignmentController.cpp"),
         QStringLiteral("QStringLiteral(\"forced-alignment\")")},
        {QStringLiteral("src/controllers/separation/ColabVoiceIsolatorController.cpp"),
         QStringLiteral("QStringLiteral(\"voice-isolation\")")},
        {QStringLiteral("src/controllers/translation/TranslationController.cpp"),
         QStringLiteral("QStringLiteral(\"translation\")")},
        {QStringLiteral("src/controllers/llm/LlmChatController.cpp"),
         QStringLiteral("QStringLiteral(\"llm-chat\")")},
    };
    for (const auto &[relativePath, capability] : controllerRoutes) {
        QFile file(sourceRoot.filePath(relativePath));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(relativePath));
        const QString source = QString::fromUtf8(file.readAll());
        QVERIFY2(source.contains(QStringLiteral("beginVerifiedSession")),
                 qPrintable(relativePath + QStringLiteral(
                     " must use verified Colab pairing")));
        QVERIFY2(source.contains(capability),
                 qPrintable(relativePath + QStringLiteral(
                     " must bind the worker to its exact capability")));
        QVERIFY2(!source.contains(QStringLiteral("->setSession(workerUrl, bearerToken")),
                 qPrintable(relativePath + QStringLiteral(
                     " must not activate a feature from URL/token syntax alone")));
    }

    const QList<QPair<QString, QString>> dubbingRoutes{
        {QStringLiteral("qml/components/dubbing/DubbingNodeSettingsPanel.qml"),
         QStringLiteral("colabCapabilityForNode")},
        {QStringLiteral("qml/components/dubbing/DubbingNodeInspector.qml"),
         QStringLiteral("\"voice-cloning\"")},
        {QStringLiteral("qml/components/dubbing/DubbingNodeInspector.qml"),
         QStringLiteral("\"forced-alignment\"")},
    };
    for (const auto &[relativePath, expected] : dubbingRoutes) {
        QFile file(sourceRoot.filePath(relativePath));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(relativePath));
        const QString source = QString::fromUtf8(file.readAll());
        QVERIFY2(source.contains(QStringLiteral("connectTemporaryWorker(")),
                 qPrintable(relativePath));
        QVERIFY2(source.contains(expected), qPrintable(relativePath));
    }
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
    struct NotebookContract {
        QString file;
        QString capability;
        QString model;
        QString endpoint;
    };
    const QList<NotebookContract> notebooks{
        {QStringLiteral("LA_STUDIO_STT_NEMOTRON_3_5_0_6B_GPU.ipynb"), QStringLiteral("stt"), QStringLiteral("nemotron-3.5-asr-streaming-0.6b"), QStringLiteral("/v1/audio/transcriptions")},
        {QStringLiteral("LA_STUDIO_STT_WHISPER_GPU.ipynb"), QStringLiteral("stt"), QStringLiteral("whisper.cpp"), QStringLiteral("/v1/audio/transcriptions")},
        {QStringLiteral("LA_STUDIO_STT_QWEN3_ASR_0_6B_GPU.ipynb"), QStringLiteral("stt"), QStringLiteral("qwen3-asr-0.6b"), QStringLiteral("/v1/audio/transcriptions")},
        {QStringLiteral("LA_STUDIO_STT_QWEN3_ASR_1_7B_GPU.ipynb"), QStringLiteral("stt"), QStringLiteral("qwen3-asr-1.7b"), QStringLiteral("/v1/audio/transcriptions")},
        {QStringLiteral("LA_STUDIO_TTS_KOKORO_GPU.ipynb"), QStringLiteral("tts"), QStringLiteral("kokoro"), QStringLiteral("/v1/audio/speech")},
        {QStringLiteral("LA_STUDIO_TTS_KOKORO_VIETNAMESE_GPU.ipynb"), QStringLiteral("tts"), QStringLiteral("kokoro-vietnamese"), QStringLiteral("/v1/audio/speech")},
        {QStringLiteral("LA_STUDIO_TTS_OMNIVOICE_GPU.ipynb"), QStringLiteral("tts"), QStringLiteral("omnivoice"), QStringLiteral("/v1/audio/speech")},
        {QStringLiteral("LA_STUDIO_TTS_QWEN3_CUSTOMVOICE_1_7B_GPU.ipynb"), QStringLiteral("tts"), QStringLiteral("qwen3-tts-1.7b-customvoice"), QStringLiteral("/v1/audio/speech")},
        {QStringLiteral("LA_STUDIO_TTS_VIBEVOICE_0_5B_GPU.ipynb"), QStringLiteral("tts"), QStringLiteral("vibevoice"), QStringLiteral("/v1/audio/speech")},
        {QStringLiteral("LA_STUDIO_TTS_VIENEU_V2_TURBO_GPU.ipynb"), QStringLiteral("tts"), QStringLiteral("vieneu-tts-v2-turbo"), QStringLiteral("/v1/audio/speech")},
        {QStringLiteral("LA_STUDIO_TTS_VIENEU_V3_TURBO_GPU.ipynb"), QStringLiteral("tts"), QStringLiteral("vieneu-tts-v3-turbo"), QStringLiteral("/v1/audio/speech")},
        {QStringLiteral("LA_STUDIO_TTS_VOXCPM2_GPU.ipynb"), QStringLiteral("tts"), QStringLiteral("voxcpm2"), QStringLiteral("/v1/audio/speech")},
        {QStringLiteral("LA_STUDIO_VOICE_CLONE_OMNIVOICE_GPU.ipynb"), QStringLiteral("voice-cloning"), QStringLiteral("omnivoice"), QStringLiteral("/v2/jobs/profile")},
        {QStringLiteral("LA_STUDIO_VOICE_CLONE_QWEN3_BASE_0_6B_GPU.ipynb"), QStringLiteral("voice-cloning"), QStringLiteral("qwen3-tts-0.6b-base"), QStringLiteral("/v2/jobs/profile")},
        {QStringLiteral("LA_STUDIO_VOICE_CLONE_QWEN3_BASE_1_7B_GPU.ipynb"), QStringLiteral("voice-cloning"), QStringLiteral("qwen3-tts-1.7b-base"), QStringLiteral("/v2/jobs/profile")},
        {QStringLiteral("LA_STUDIO_VOICE_CLONE_VIENEU_V2_TURBO_GPU.ipynb"), QStringLiteral("voice-cloning"), QStringLiteral("vieneu-tts-v2-turbo"), QStringLiteral("/v2/jobs/profile")},
        {QStringLiteral("LA_STUDIO_VOICE_CLONE_VIENEU_V3_TURBO_GPU.ipynb"), QStringLiteral("voice-cloning"), QStringLiteral("vieneu-tts-v3-turbo"), QStringLiteral("/v2/jobs/profile")},
        {QStringLiteral("LA_STUDIO_VOICE_CLONE_VOXCPM2_GPU.ipynb"), QStringLiteral("voice-cloning"), QStringLiteral("voxcpm2"), QStringLiteral("/v2/jobs/profile")},
        {QStringLiteral("LA_STUDIO_VOICE_DESIGN_OMNIVOICE_GPU.ipynb"), QStringLiteral("voice-design"), QStringLiteral("omnivoice"), QStringLiteral("/v1/audio/voice_designs")},
        {QStringLiteral("LA_STUDIO_VOICE_DESIGN_QWEN3_1_7B_GPU.ipynb"), QStringLiteral("voice-design"), QStringLiteral("qwen3-tts-1.7b-voicedesign"), QStringLiteral("/v1/audio/voice_designs")},
        {QStringLiteral("LA_STUDIO_VOICE_DESIGN_VOXCPM2_GPU.ipynb"), QStringLiteral("voice-design"), QStringLiteral("voxcpm2"), QStringLiteral("/v1/audio/voice_designs")},
        {QStringLiteral("LA_STUDIO_ALIGNMENT_WAV2VEC2_ZH_GPU.ipynb"), QStringLiteral("forced-alignment"), QStringLiteral("wav2vec2-aligner-zh"), QStringLiteral("/v1/audio/alignments")},
        {QStringLiteral("LA_STUDIO_ALIGNMENT_CANARY_CTC_GPU.ipynb"), QStringLiteral("forced-alignment"), QStringLiteral("canary-ctc-aligner"), QStringLiteral("/v1/audio/alignments")},
        {QStringLiteral("LA_STUDIO_ALIGNMENT_MMS_ONNX_GPU.ipynb"), QStringLiteral("forced-alignment"), QStringLiteral("mms-forced-aligner-onnx"), QStringLiteral("/v1/audio/alignments")},
        {QStringLiteral("LA_STUDIO_ALIGNMENT_QWEN3_0_6B_GPU.ipynb"), QStringLiteral("forced-alignment"), QStringLiteral("qwen3-forced-aligner-0.6b"), QStringLiteral("/v1/audio/alignments")},
        {QStringLiteral("LA_STUDIO_SEPARATION_SPLEETER_2STEMS_GPU.ipynb"), QStringLiteral("voice-isolation"), QStringLiteral("sherpa-onnx-spleeter-2stems-fp16"), QStringLiteral("/v1/audio/separations")},
        {QStringLiteral("LA_STUDIO_SEPARATION_UVR_VOCALS_GPU.ipynb"), QStringLiteral("voice-isolation"), QStringLiteral("sherpa-onnx-uvr-vocals-ft"), QStringLiteral("/v1/audio/separations")},
        {QStringLiteral("LA_STUDIO_TRANSLATION_M2M100_418M_GPU.ipynb"), QStringLiteral("translation"), QStringLiteral("m2m100-418m"), QStringLiteral("/v1/translations")},
        {QStringLiteral("LA_STUDIO_TRANSLATION_MADLAD400_3B_GPU.ipynb"), QStringLiteral("translation"), QStringLiteral("madlad400-3b-mt"), QStringLiteral("/v1/translations")},
        {QStringLiteral("LA_STUDIO_TRANSLATION_HY_MT2_1_8B_GPU.ipynb"), QStringLiteral("translation"), QStringLiteral("hy-mt2-1.8b"), QStringLiteral("/v1/translations")},
        {QStringLiteral("LA_STUDIO_LLM_QWEN3_5_2B_GPU.ipynb"), QStringLiteral("llm-chat"), QStringLiteral("qwen3.5-2b"), QStringLiteral("/v1/chat/completions")},
    };
    for (const NotebookContract &notebook : notebooks) {
        QFile file(sourceRoot.filePath(QStringLiteral("notebooks/") + notebook.file));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(notebook.file));
        const QByteArray source = file.readAll();
        const QJsonDocument notebookDocument = QJsonDocument::fromJson(source);
        QVERIFY2(notebookDocument.isObject(), qPrintable(notebook.file));
        const QJsonObject laStudioMetadata = notebookDocument.object()
                                                 .value(QStringLiteral("metadata"))
                                                 .toObject()
                                                 .value(QStringLiteral("la_studio"))
                                                 .toObject();
        QCOMPARE(laStudioMetadata.value(QStringLiteral("capability")).toString(),
                 notebook.capability);
        QCOMPARE(laStudioMetadata.value(QStringLiteral("family_id")).toString(),
                 notebook.model);
        QCOMPARE(laStudioMetadata.value(QStringLiteral("contract_version")).toInt(), 1);
        QCOMPARE(laStudioMetadata.value(QStringLiteral("device")).toString(),
                 QStringLiteral("cuda"));
        QCOMPARE(laStudioMetadata.value(QStringLiteral("cpu_fallback")).toBool(), false);
        const bool hasHealth = source.contains("@app.get('/health')")
            || source.contains("@app.get(\\\"/health\\\")");
        const bool hasCapabilities = source.contains("@app.get('/v1/capabilities')")
            || source.contains("@app.get(\\\"/v1/capabilities\\\")");
        const bool hasContractVersion = source.contains("'contract_version': 1")
            || source.contains("\\\"contract_version\\\": 1");
        const bool hasReady = source.contains("'ready': True")
            || source.contains("\\\"ready\\\": True");
        const bool hasCuda = source.contains("'device': 'cuda'")
            || source.contains("\\\"device\\\": \\\"cuda\\\"");
        const bool hasNoCpuFallback = source.contains("'cpu_fallback': False")
            || source.contains("\\\"cpu_fallback\\\": False");
        QVERIFY2(hasHealth, qPrintable(notebook.file));
        QVERIFY2(hasCapabilities, qPrintable(notebook.file));
        QVERIFY2(hasContractVersion, qPrintable(notebook.file));
        QVERIFY2(hasReady, qPrintable(notebook.file));
        QVERIFY2(hasCuda, qPrintable(notebook.file));
        QVERIFY2(hasNoCpuFallback, qPrintable(notebook.file));
        QVERIFY2(source.contains(notebook.capability.toUtf8()), qPrintable(notebook.file));
        QVERIFY2(source.contains(notebook.model.toUtf8()), qPrintable(notebook.file));
        QVERIFY2(source.contains(notebook.endpoint.toUtf8()), qPrintable(notebook.file));
    }
}

} // namespace LAStudio
