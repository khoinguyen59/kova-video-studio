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
    QCOMPARE(session.expectedVariant(), QStringLiteral("fixed"));
    QVERIFY(!session.verifiedAt().isEmpty());
    QCOMPARE(session.reportedGpu(), QStringLiteral("Test GPU"));
    QVERIFY(session.verificationMessage().contains(QStringLiteral("tts / kokoro")));
    QString routeError;
    QVERIFY2(session.hasVerifiedRoute(QStringLiteral("tts"), QStringLiteral("kokoro"),
                                      &routeError), qPrintable(routeError));
    QVERIFY(!session.hasVerifiedRoute(QStringLiteral("tts"), QStringLiteral("vibevoice-0.5b"),
                                      &routeError));
    QVERIFY(routeError.contains(QStringLiteral("Wrong Colab worker")));
    QVERIFY(!session.hasVerifiedRoute(QStringLiteral("translation"), QStringLiteral("kokoro"),
                                      &routeError));
    QVERIFY(routeError.contains(QStringLiteral("Wrong Colab worker")));

    QVERIFY(session.checkConnection());
    QVERIFY(session.isChecking());
    QTRY_COMPARE(finished.size(), 2);
    QCOMPARE(finished.constLast().at(0).toBool(), true);
    QVERIFY(session.isActive());

    const QList<QByteArray> requests = server.requests();
    QCOMPARE(requests.size(), 4);
    QCOMPARE(requests.at(0).left(requests.at(0).indexOf("\r\n")),
             QByteArrayLiteral("GET /health HTTP/1.1"));
    QCOMPARE(requests.at(1).left(requests.at(1).indexOf("\r\n")),
             QByteArrayLiteral("GET /v1/capabilities HTTP/1.1"));
    for (const QByteArray &request : requests)
        QVERIFY(request.toLower().contains("authorization: bearer verified-token"));
}

void TestRemoteExecution::temporaryColabWorkerRejectsWrongVariant()
{
    CatalogMock server({
        QByteArrayLiteral(
            R"({"status":"ready","ready":true,"device":"cuda","gpu":"Test GPU","model":"kokoro","variant":"fp16","cpu_fallback":false})"),
    });
    QVERIFY(server.start());

    ColabSession session;
    QSignalSpy finished(&session, &ColabSession::verificationFinished);
    QString error;
    QVERIFY2(session.beginVerifiedSession(
                 server.baseUrl(), QStringLiteral("variant-token"),
                 QStringLiteral("tts"), QStringLiteral("kokoro"), &error, true,
                 QStringLiteral("fixed")),
             qPrintable(error));
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(!finished.constFirst().at(0).toBool());
    QVERIFY(!session.isActive());
    QVERIFY(session.lastError().contains(QStringLiteral("Wrong Colab configuration")));
    QVERIFY(!session.lastError().contains(QStringLiteral("variant-token")));
}

void TestRemoteExecution::staleTranslationPatchContractIsRejected()
{
    const QByteArray health = QByteArrayLiteral(
        R"({"status":"ready","ready":true,"device":"cuda","model":"m2m100-418m","worker_revision":"translation-2026-07-30.3","cpu_fallback":false})");
    CatalogMock stale({
        health,
        QByteArrayLiteral(
            R"({"contract_version":1,"device":"cuda","worker_revision":"translation-2026-07-30.3","capabilities":[{"id":"translation","models":[{"id":"m2m100-418m","device":"cuda","loaded":true}]}]})"),
    });
    QVERIFY(stale.start());
    ColabSession staleSession;
    QSignalSpy staleFinished(&staleSession, &ColabSession::verificationFinished);
    QString error;
    QVERIFY2(staleSession.beginVerifiedSession(
                 stale.baseUrl(), QStringLiteral("stale-translation-token"),
                 QStringLiteral("translation"), QStringLiteral("m2m100-418m"),
                 &error, true), qPrintable(error));
    QTRY_COMPARE(staleFinished.count(), 1);
    QVERIFY(!staleFinished.constFirst().at(0).toBool());
    QVERIFY(staleSession.lastError().contains(QStringLiteral("outdated response contract")));

    CatalogMock current({
        health,
        QByteArrayLiteral(
            R"({"contract_version":1,"device":"cuda","worker_revision":"translation-2026-07-30.3","capabilities":[{"id":"translation","models":[{"id":"m2m100-418m","device":"cuda","loaded":true,"response_contract":"translation-patches-v3"}]}]})"),
    });
    QVERIFY(current.start());
    ColabSession currentSession;
    QSignalSpy currentFinished(&currentSession, &ColabSession::verificationFinished);
    QVERIFY2(currentSession.beginVerifiedSession(
                 current.baseUrl(), QStringLiteral("current-translation-token"),
                 QStringLiteral("translation"), QStringLiteral("m2m100-418m"),
                 &error, true), qPrintable(error));
    QTRY_COMPARE(currentFinished.count(), 1);
    QVERIFY(currentFinished.constFirst().at(0).toBool());
    QVERIFY(currentSession.isActive());
}

void TestRemoteExecution::staleSttWorkerRevisionIsRejected()
{
    CatalogMock stale({
        QByteArrayLiteral(
            R"({"status":"ready","ready":true,"device":"cuda","model":"whisper.cpp","cpu_fallback":false})"),
        QByteArrayLiteral(
            R"({"contract_version":1,"device":"cuda","capabilities":[{"id":"stt","models":[{"id":"whisper.cpp","device":"cuda","loaded":true}]}]})"),
    });
    QVERIFY(stale.start());
    ColabSession staleSession;
    QSignalSpy staleFinished(&staleSession, &ColabSession::verificationFinished);
    QString error;
    QVERIFY2(staleSession.beginVerifiedSession(
                 stale.baseUrl(), QStringLiteral("stale-stt-token"),
                 QStringLiteral("stt"), QStringLiteral("whisper.cpp"),
                 &error, true), qPrintable(error));
    QTRY_COMPARE(staleFinished.count(), 1);
    QVERIFY(!staleFinished.constFirst().at(0).toBool());
    QVERIFY(staleSession.lastError().contains(QStringLiteral("Speech-to-Text notebook is outdated")));

    const QByteArray currentHealth = QByteArrayLiteral(
        R"({"status":"ready","ready":true,"device":"cuda","model":"whisper.cpp","worker_revision":"stt-2026-07-30.2","cpu_fallback":false})");
    CatalogMock current({
        currentHealth,
        QByteArrayLiteral(
            R"({"contract_version":1,"worker_revision":"stt-2026-07-30.2","device":"cuda","capabilities":[{"id":"stt","models":[{"id":"whisper.cpp","device":"cuda","loaded":true}]}]})"),
    });
    QVERIFY(current.start());
    ColabSession currentSession;
    QSignalSpy currentFinished(&currentSession, &ColabSession::verificationFinished);
    QVERIFY2(currentSession.beginVerifiedSession(
                 current.baseUrl(), QStringLiteral("current-stt-token"),
                 QStringLiteral("stt"), QStringLiteral("whisper.cpp"),
                 &error, true), qPrintable(error));
    QTRY_COMPARE(currentFinished.count(), 1);
    QVERIFY(currentFinished.constFirst().at(0).toBool());
    QVERIFY(currentSession.isActive());
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
    {
        CatalogMock unloadedModel({
            QByteArrayLiteral(
                R"({"status":"ready","ready":true,"device":"cuda","model":"kokoro","cpu_fallback":false})"),
            QByteArrayLiteral(
                R"({"contract_version":1,"device":"cuda","capabilities":[{"id":"tts","models":[{"id":"kokoro","device":"cuda"}]}]})"),
        });
        QVERIFY(unloadedModel.start());
        ColabSession session;
        QString error;
        QVERIFY(session.beginVerifiedSession(
            unloadedModel.baseUrl(), QStringLiteral("unloaded-model-token"),
            QStringLiteral("tts"), QStringLiteral("kokoro"), &error, true));
        QTRY_VERIFY(!session.isChecking());
        QVERIFY(!session.isActive());
        QVERIFY(session.lastError().contains(QStringLiteral("not loaded")));
        QVERIFY(session.bearerTokenForRequest().isEmpty());
    }
    {
        CatalogMock modelWithoutCudaProof({
            QByteArrayLiteral(
                R"({"status":"ready","ready":true,"device":"cuda","model":"kokoro","cpu_fallback":false})"),
            QByteArrayLiteral(
                R"({"contract_version":1,"device":"cuda","capabilities":[{"id":"tts","device":"cuda","models":[{"id":"kokoro","loaded":true}]}]})"),
        });
        QVERIFY(modelWithoutCudaProof.start());
        ColabSession session;
        QString error;
        QVERIFY(session.beginVerifiedSession(
            modelWithoutCudaProof.baseUrl(), QStringLiteral("model-device-token"),
            QStringLiteral("tts"), QStringLiteral("kokoro"), &error, true));
        QTRY_VERIFY(!session.isChecking());
        QVERIFY(!session.isActive());
        QVERIFY(session.lastError().contains(QStringLiteral("not advertised on CUDA")));
        QVERIFY(session.bearerTokenForRequest().isEmpty());
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
        QStringLiteral("qml/pages/SubtitleOcrPage.qml"),
        QStringLiteral("qml/components/llm/LlmChatStudioView.qml"),
        QStringLiteral("qml/components/dubbing/DubbingNodeSettingsPanel.qml"),
        QStringLiteral("qml/components/dubbing/DubbingNodeInspector.qml"),
        QStringLiteral("qml/components/dubbing/DubbingColabSetupDialog.qml"),
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
    QVERIFY(source.contains(QStringLiteral("Wrong Colab configuration")));
    QVERIFY(source.contains(QStringLiteral("contract_version")));

    QFile sharedStatus(sourceRoot.filePath(QStringLiteral("qml/components/base/ColabSessionStatus.qml")));
    QVERIFY(sharedStatus.open(QIODevice::ReadOnly));
    const QString sharedStatusSource = QString::fromUtf8(sharedStatus.readAll());
    QVERIFY(sharedStatusSource.contains(QStringLiteral("Check connection")));
    QVERIFY(sharedStatusSource.contains(QStringLiteral("Disconnect")));
    QVERIFY(sharedStatusSource.contains(QStringLiteral("expectedVariant")));
    QVERIFY(sharedStatusSource.contains(QStringLiteral("verifiedAt")));
    QVERIFY(sharedStatusSource.contains(QStringLiteral("checkRequested")));
    QVERIFY(sharedStatusSource.contains(QStringLiteral("disconnectRequested")));
    QVERIFY(sharedStatusSource.contains(QStringLiteral(".arg(root.session.workerUrl)")));
    QVERIFY(sharedStatusSource.contains(QStringLiteral(".arg(root.session.expectedVariant)")));
}

void TestRemoteExecution::voiceCloneUiMakesConsentAndRequiredInputsActionable()
{
    const QDir sourceRoot(QStringLiteral(LASTUDIO_SOURCE_DIR));
    QFile settings(sourceRoot.filePath(QStringLiteral("qml/components/voicecloning/VoiceSettingsPanel.qml")));
    QVERIFY(settings.open(QIODevice::ReadOnly));
    const QString settingsSource = QString::fromUtf8(settings.readAll());
    QVERIFY(settingsSource.contains(QStringLiteral("component ConsentCheckBox: CheckBox")));
    QVERIFY(settingsSource.contains(QStringLiteral("focusPolicy: Qt.StrongFocus")));
    QVERIFY(settingsSource.contains(QStringLiteral("implicitHeight: 46")));
    QVERIFY(settingsSource.contains(QStringLiteral("ScrollView")));
    QVERIFY(settingsSource.contains(QStringLiteral("qmlSmokeConsentLayoutCheck")));
    QVERIFY(settingsSource.contains(QStringLiteral("I have permission to clone this voice (required)")));

    QFile studio(sourceRoot.filePath(QStringLiteral("qml/components/voicecloning/VoiceCloningStudioView.qml")));
    QVERIFY(studio.open(QIODevice::ReadOnly));
    const QString studioSource = QString::fromUtf8(studio.readAll());
    QVERIFY(studioSource.contains(QStringLiteral("function cloneBlockReason()")));
    QVERIFY(studioSource.contains(QStringLiteral("Target Prompt is required.")));
    QVERIFY(studioSource.contains(QStringLiteral("Confirm that you have permission")));
    QVERIFY(studioSource.contains(QStringLiteral("referenceIsolatorSetupOpen")));
    QVERIFY(studioSource.contains(QStringLiteral("colabVoiceCloneReferenceIsolator")));
    QVERIFY(studioSource.contains(QStringLiteral("colabVoiceCloneReferenceIsolatorSession")));
    QVERIFY(studioSource.contains(QStringLiteral("No second file selection or upload is required.")));
    QVERIFY(!studioSource.contains(QStringLiteral("openStudioRoute(\"studio-voice-isolator\")")));
    QVERIFY(studioSource.contains(QStringLiteral("qmlSmokeVoiceCloneLayoutCheck")));

    QFile reference(sourceRoot.filePath(QStringLiteral("qml/components/voicecloning/ReferenceInputBox.qml")));
    QVERIFY(reference.open(QIODevice::ReadOnly));
    const QString referenceSource = QString::fromUtf8(reference.readAll());
    QVERIFY(referenceSource.contains(QStringLiteral("requiresExactTranscript")));
    QVERIFY(referenceSource.contains(QStringLiteral("Reference Transcript (optional)")));
    QVERIFY(referenceSource.contains(QStringLiteral("transcriptHint")));

    QFile isolation(sourceRoot.filePath(QStringLiteral("qml/components/voiceisolator/VoiceIsolatorStudioView.qml")));
    QVERIFY(isolation.open(QIODevice::ReadOnly));
    const QString isolationSource = QString::fromUtf8(isolation.readAll());
    QVERIFY(isolationSource.contains(QStringLiteral("selectedFile")));
    QVERIFY(isolationSource.contains(QStringLiteral("Saved WAV: %1")));

    QFile main(sourceRoot.filePath(QStringLiteral("qml/Main.qml")));
    QVERIFY(main.open(QIODevice::ReadOnly));
    const QString mainSource = QString::fromUtf8(main.readAll());
    QVERIFY(mainSource.contains(QStringLiteral("qmlSmokeVoiceCloneLayoutSizeIndex")));
    QVERIFY(mainSource.contains(QStringLiteral("qmlSmokeVoiceCloneLayoutCheck")));
    QVERIFY(mainSource.contains(QStringLiteral("voiceCloneSizes")));
}

void TestRemoteExecution::voiceCloneOmniVoiceIsReusableInTtsWithoutLocalFallback()
{
    const QDir sourceRoot(QStringLiteral(LASTUDIO_SOURCE_DIR));

    QFile reference(sourceRoot.filePath(QStringLiteral("qml/components/voicecloning/ReferenceInputBox.qml")));
    QVERIFY(reference.open(QIODevice::ReadOnly));
    const QString referenceSource = QString::fromUtf8(reference.readAll());
    QVERIFY(referenceSource.contains(QStringLiteral("Voice name for TTS reuse")));
    QVERIFY(referenceSource.contains(QStringLiteral("reusableVoiceName")));

    QFile cloneStudio(sourceRoot.filePath(QStringLiteral("qml/components/voicecloning/VoiceCloningStudioView.qml")));
    QVERIFY(cloneStudio.open(QIODevice::ReadOnly));
    const QString cloneSource = QString::fromUtf8(cloneStudio.readAll());
    QVERIFY(cloneSource.contains(QStringLiteral("pendingReusableVoiceName")));
    QVERIFY(cloneSource.contains(QStringLiteral("onSynthesisFinished")));
    QVERIFY(cloneSource.contains(QStringLiteral("voiceClonePresets.addPreset")));
    QVERIFY(cloneSource.contains(QStringLiteral("Enter a Voice name for TTS reuse")));

    QFile ttsPage(sourceRoot.filePath(QStringLiteral("qml/pages/TtsPage.qml")));
    QVERIFY(ttsPage.open(QIODevice::ReadOnly));
    const QString pageSource = QString::fromUtf8(ttsPage.readAll());
    QVERIFY(pageSource.contains(QStringLiteral("syncOmniVoiceCloneSelection")));
    QVERIFY(pageSource.contains(QStringLiteral("selectColabModel(\"omnivoice\")")));
    QVERIFY(pageSource.contains(QStringLiteral("saveConfigurationSelection(\"omnivoice\"")));
    QVERIFY(pageSource.contains(QStringLiteral("colabVoiceClone.colabActive")));

    QFile settings(sourceRoot.filePath(QStringLiteral("qml/components/tts/TtsSettingsPanel.qml")));
    QVERIFY(settings.open(QIODevice::ReadOnly));
    const QString settingsSource = QString::fromUtf8(settings.readAll());
    QVERIFY(settingsSource.contains(QStringLiteral("Reuse cloned OmniVoice")));
    QVERIFY(settingsSource.contains(QStringLiteral("reusableCloneVoices")));
    QVERIFY(settingsSource.contains(QStringLiteral("I have permission to use this cloned voice for TTS")));
    QVERIFY(settingsSource.contains(QStringLiteral("Use cloned OmniVoice in TTS")));

    QFile ttsStudio(sourceRoot.filePath(QStringLiteral("qml/components/tts/TtsStudioView.qml")));
    QVERIFY(ttsStudio.open(QIODevice::ReadOnly));
    const QString ttsSource = QString::fromUtf8(ttsStudio.readAll());
    QVERIFY(ttsSource.contains(QStringLiteral("cloneOmniVoiceActive")));
    QVERIFY(ttsSource.contains(QStringLiteral("selectedRemoteProvider === \"clone\"")));
    QVERIFY(ttsSource.contains(QStringLiteral("colabVoiceClone.cloneVoice")));
    QVERIFY(ttsSource.contains(QStringLiteral("OmniVoice Voice Clone Colab")));
}

void TestRemoteExecution::settingsControlsExposeDescriptionsAndKeyboardFocus()
{
    const QDir sourceRoot(QStringLiteral(LASTUDIO_SOURCE_DIR));

    QFile toggle(sourceRoot.filePath(QStringLiteral("qml/components/shared/settings/ToggleRow.qml")));
    QVERIFY(toggle.open(QIODevice::ReadOnly));
    const QString toggleSource = QString::fromUtf8(toggle.readAll());
    QVERIFY(toggleSource.contains(QStringLiteral("property string description")));
    QVERIFY(toggleSource.contains(QStringLiteral("focusPolicy: Qt.StrongFocus")));
    QVERIFY(toggleSource.contains(QStringLiteral("toggle.description")));

    QFile parameters(sourceRoot.filePath(QStringLiteral("qml/components/shared/settings/ModelParameterControls.qml")));
    QVERIFY(parameters.open(QIODevice::ReadOnly));
    const QString parameterSource = QString::fromUtf8(parameters.readAll());
    QVERIFY(parameterSource.contains(QStringLiteral("Range: %1")));
    QVERIFY(parameterSource.contains(QStringLiteral("Default: %1")));

    for (const QString &relativePath : {QStringLiteral("qml/components/voicecloning/VoiceSettingsPanel.qml"),
                                        QStringLiteral("qml/components/voicedesign/VoiceDesignSettingsPanel.qml"),
                                        QStringLiteral("qml/components/tts/TtsSettingsPanel.qml")}) {
        QFile panel(sourceRoot.filePath(relativePath));
        QVERIFY2(panel.open(QIODevice::ReadOnly), qPrintable(relativePath));
        const QString panelSource = QString::fromUtf8(panel.readAll());
        QVERIFY2(panelSource.contains(QStringLiteral("Reduce steady background noise")),
                 qPrintable(relativePath));
        QVERIFY2(panelSource.contains(QStringLiteral("Normalize prompt text")),
                 qPrintable(relativePath));
        QVERIFY2(panelSource.contains(QStringLiteral("Fixed seed (whole number")),
                 qPrintable(relativePath));
    }

    QFile collapsible(sourceRoot.filePath(QStringLiteral("qml/components/shared/settings/CollapsibleSettingsSection.qml")));
    QVERIFY(collapsible.open(QIODevice::ReadOnly));
    const QString collapsibleSource = QString::fromUtf8(collapsible.readAll());
    QVERIFY(collapsibleSource.contains(QStringLiteral("chevron-right")));
    QVERIFY(collapsibleSource.contains(QStringLiteral("Qt.PointingHandCursor")));
}

void TestRemoteExecution::workflowActivityOnlyDisplaysMeasuredProgress()
{
    const QDir sourceRoot(QStringLiteral(LASTUDIO_SOURCE_DIR));
    QFile cmake(sourceRoot.filePath(QStringLiteral("CMakeLists.txt")));
    QVERIFY(cmake.open(QIODevice::ReadOnly));
    const QString cmakeSource = QString::fromUtf8(cmake.readAll());
    QVERIFY(cmakeSource.contains(QStringLiteral("set(LASTUDIO_VERSION \"0.0.6.1\"")));
    QVERIFY(cmakeSource.contains(QStringLiteral("four single digits (0-9)")));

    for (const QString &relativePath : {QStringLiteral("scripts/build.ps1"),
                                        QStringLiteral("scripts/package.ps1")}) {
        QFile versionScript(sourceRoot.filePath(relativePath));
        QVERIFY2(versionScript.open(QIODevice::ReadOnly), qPrintable(relativePath));
        const QString versionScriptSource = QString::fromUtf8(versionScript.readAll());
        QVERIFY2(versionScriptSource.contains(QStringLiteral("^[0-9]\\.[0-9]\\.[0-9]\\.[0-9]$")),
                 qPrintable(relativePath));
        QVERIFY2(versionScriptSource.contains(QStringLiteral("four single digits (0-9)")),
                 qPrintable(relativePath));
    }

    QFile releaseVersionScript(sourceRoot.filePath(QStringLiteral("scripts/verify_release_version.ps1")));
    QVERIFY(releaseVersionScript.open(QIODevice::ReadOnly));
    const QString releaseVersionScriptSource = QString::fromUtf8(releaseVersionScript.readAll());
    QVERIFY(releaseVersionScriptSource.contains(
        QStringLiteral("^v([0-9]\\.[0-9]\\.[0-9]\\.[0-9])")));
    QVERIFY(releaseVersionScriptSource.contains(QStringLiteral("four single digits with carry at 9")));

    QFile manager(sourceRoot.filePath(QStringLiteral("src/controllers/app/WorkflowActivityManager.cpp")));
    QVERIFY(manager.open(QIODevice::ReadOnly));
    const QString managerSource = QString::fromUtf8(manager.readAll());
    for (const QString &workflow : {QStringLiteral("gatewayTtsWorkflow"),
                                    QStringLiteral("colabTtsWorkflow"),
                                    QStringLiteral("voiceCloneWorkflow"),
                                    QStringLiteral("voiceDesignWorkflow"),
                                    QStringLiteral("colabAlignmentWorkflow"),
                                    QStringLiteral("localVoiceIsolationWorkflow"),
                                    QStringLiteral("colabVoiceIsolationWorkflow"),
                                    QStringLiteral("translationWorkflow"),
                                    QStringLiteral("subtitleOcrWorkflow"),
                                    QStringLiteral("llmChatWorkflow")}) {
        QVERIFY2(managerSource.contains(workflow), qPrintable(workflow));
    }
    QVERIFY(managerSource.contains(QStringLiteral("progressAvailable")));
    QVERIFY(managerSource.contains(QStringLiteral("progressScope")));
    QVERIFY(managerSource.contains(QStringLiteral("progressLabel")));
    QVERIFY(managerSource.contains(QStringLiteral("addExecutionDetails")));
    QVERIFY(managerSource.contains(QStringLiteral("Direct Colab GPU")));
    QVERIFY(managerSource.contains(QStringLiteral("API Gateway")));

    QFile popup(sourceRoot.filePath(QStringLiteral("qml/components/WorkflowPopup.qml")));
    QVERIFY(popup.open(QIODevice::ReadOnly));
    const QString popupSource = QString::fromUtf8(popup.readAll());
    QVERIFY(popupSource.contains(QStringLiteral("function executionDetails")));
    QVERIFY(popupSource.contains(QStringLiteral("function progressText")));
    QVERIFY(popupSource.contains(QStringLiteral("workflow.progressScope === \"artifact\"")));
    QVERIFY(popupSource.contains(QStringLiteral("Artifact transfer")));
    QVERIFY(popupSource.contains(QStringLiteral("qsTr(\"Working\")")));
    QVERIFY(popupSource.contains(QStringLiteral("modelData.progressAvailable !== false")));

    QFile page(sourceRoot.filePath(QStringLiteral("qml/pages/DubbingPage.qml")));
    QVERIFY(page.open(QIODevice::ReadOnly));
    const QString pageSource = QString::fromUtf8(page.readAll());
    QVERIFY(pageSource.contains(QStringLiteral("property bool followRunningStep")));
    QVERIFY(pageSource.contains(QStringLiteral("root.followRunningStep = false")));
    QVERIFY(pageSource.contains(QStringLiteral("function openOcrColabSetup")));
    QVERIFY(pageSource.contains(QStringLiteral("function ocrSetupEditable")));
    QVERIFY(pageSource.contains(QStringLiteral("subtitle-ocr")));
    QVERIFY(pageSource.contains(QStringLiteral("Set up OCR Colab GPU")));

    QFile nodeSettings(sourceRoot.filePath(
        QStringLiteral("qml/components/dubbing/DubbingNodeSettingsPanel.qml")));
    QVERIFY(nodeSettings.open(QIODevice::ReadOnly));
    const QString nodeSettingsSource = QString::fromUtf8(nodeSettings.readAll());
    QVERIFY(nodeSettingsSource.contains(QStringLiteral("function startColabSetup")));
    QVERIFY(nodeSettingsSource.contains(QStringLiteral("qsTr(\"Colab setup\")")));
    QVERIFY(nodeSettingsSource.contains(QStringLiteral("!root.dubbing.settingsLocked && !root.isCurrentRunningNode()")));

    QFile sourcePanel(sourceRoot.filePath(
        QStringLiteral("qml/components/dubbing/DubbingSourceMediaPanel.qml")));
    QVERIFY(sourcePanel.open(QIODevice::ReadOnly));
    const QString sourcePanelSource = QString::fromUtf8(sourcePanel.readAll());
    QVERIFY(sourcePanelSource.contains(QStringLiteral("property bool ocrRoiEditMode")));
    QVERIFY(sourcePanelSource.contains(QStringLiteral("property bool sourceSetupExpanded")));
    QVERIFY(sourcePanelSource.contains(QStringLiteral("id: sourceSetupPanel")));
    QVERIFY(sourcePanelSource.contains(QStringLiteral("visible: !root.hasLoadedSource || root.sourceSetupExpanded")));
    QVERIFY(sourcePanelSource.contains(QStringLiteral("Change / download source")));
    QVERIFY(sourcePanelSource.contains(QStringLiteral("Layout.minimumHeight: root.isVideoSource")));
    QVERIFY(sourcePanelSource.contains(QStringLiteral("Edit OCR scan area")));
    QVERIFY(sourcePanelSource.contains(QStringLiteral("OCR scan area locked while running")));
    QVERIFY(sourcePanelSource.contains(QStringLiteral("preventStealing: true")));

    QFile projectStatus(sourceRoot.filePath(
        QStringLiteral("qml/components/dubbing/DubbingProjectStatusPanel.qml")));
    QVERIFY(projectStatus.open(QIODevice::ReadOnly));
    const QString projectStatusSource = QString::fromUtf8(projectStatus.readAll());
    QVERIFY(projectStatusSource.contains(QStringLiteral("Add speaker label")));
    QVERIFY(projectStatusSource.contains(QStringLiteral("Set before starting a job")));
    QVERIFY(projectStatusSource.contains(QStringLiteral("Execution and rewrite policy only")));
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
        QVERIFY2(source.contains(QStringLiteral("hasVerifiedRoute(")),
                 qPrintable(relativePath + QStringLiteral(
                     " must revalidate the capability/model immediately before dispatch")));
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
         QStringLiteral("TTS / Text to Speech")},
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

    const QList<QPair<QString, QString>> dubbingDispatchRoutes{
        {QStringLiteral("src/controllers/dubbing/DubbingJobRunner.cpp"),
         QStringLiteral("voice-isolation")},
        {QStringLiteral("src/controllers/dubbing/DubbingTranscriptionJob.cpp"),
         QStringLiteral("forced-alignment")},
        {QStringLiteral("src/controllers/dubbing/DubbingTranslationJob.cpp"),
         QStringLiteral("translation")},
        {QStringLiteral("src/controllers/dubbing/DubbingSynthesisJob.cpp"),
         QStringLiteral("voice-cloning")},
    };
    for (const auto &[relativePath, capability] : dubbingDispatchRoutes) {
        QFile file(sourceRoot.filePath(relativePath));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(relativePath));
        const QString source = QString::fromUtf8(file.readAll());
        QVERIFY2(source.contains(QStringLiteral("hasVerifiedRoute(")),
                 qPrintable(relativePath + QStringLiteral(
                     " must revalidate an exact Colab session before dispatch")));
        QVERIFY2(source.contains(capability), qPrintable(relativePath));
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
    QVERIFY(app->colabVoiceCloneReferenceIsolatorSession());
    QVERIFY(app->colabVoiceCloneReferenceIsolator());
    QVERIFY(app->colabSttSession() != app->colabTtsSession());
    QVERIFY(app->colabTtsSession() != app->colabTranslationSession());
    QVERIFY(app->colabVoiceCloneSession() != app->colabSeparationSession());
    QVERIFY(app->colabVoiceCloneReferenceIsolatorSession() != app->colabSeparationSession());
    QVERIFY(app->colabVoiceCloneReferenceIsolatorSession() != app->colabVoiceCloneSession());

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
        {QStringLiteral("LA_STUDIO_STT_NEMOTRON_3_5_0_6B_GPU.ipynb"), QStringLiteral("stt"), QStringLiteral("nemotron-3.5-asr-streaming-0.6b"), QStringLiteral("/v2/jobs/transcriptions")},
        {QStringLiteral("LA_STUDIO_STT_WHISPER_GPU.ipynb"), QStringLiteral("stt"), QStringLiteral("whisper.cpp"), QStringLiteral("/v2/jobs/transcriptions")},
        {QStringLiteral("LA_STUDIO_STT_QWEN3_ASR_0_6B_GPU.ipynb"), QStringLiteral("stt"), QStringLiteral("qwen3-asr-0.6b"), QStringLiteral("/v2/jobs/transcriptions")},
        {QStringLiteral("LA_STUDIO_STT_QWEN3_ASR_1_7B_GPU.ipynb"), QStringLiteral("stt"), QStringLiteral("qwen3-asr-1.7b"), QStringLiteral("/v2/jobs/transcriptions")},
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
        QByteArray source = file.readAll();
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
        for (const QJsonValue &workerTemplate : laStudioMetadata
                                                    .value(QStringLiteral("worker_templates"))
                                                    .toArray()) {
            QFile worker(sourceRoot.filePath(QStringLiteral("notebooks/")
                                              + workerTemplate.toString()));
            QVERIFY2(worker.open(QIODevice::ReadOnly), qPrintable(worker.fileName()));
            source += '\n' + worker.readAll();
        }
        const bool hasHealth = source.contains("@app.get('/health')")
            || source.contains("@app.get(\\\"/health\\\")")
            || source.contains("@app.get(\"/health\")");
        const bool hasCapabilities = source.contains("@app.get('/v1/capabilities')")
            || source.contains("@app.get(\\\"/v1/capabilities\\\")")
            || source.contains("@app.get(\"/v1/capabilities\")");
        const bool hasContractVersion = source.contains("'contract_version': 1")
            || source.contains("\\\"contract_version\\\": 1")
            || source.contains("\"contract_version\": 1");
        const bool hasReady = source.contains("'ready': True")
            || source.contains("\\\"ready\\\": True")
            || source.contains("\"ready\": True");
        const bool hasCuda = source.contains("'device': 'cuda'")
            || source.contains("\\\"device\\\": \\\"cuda\\\"")
            || source.contains("\"device\": \"cuda\"");
        const bool hasNoCpuFallback = source.contains("'cpu_fallback': False")
            || source.contains("\\\"cpu_fallback\\\": False")
            || source.contains("\"cpu_fallback\": False");
        const bool hasFixedVariant = source.contains("'variant': 'fixed'")
            || source.contains("\\\"variant\\\": \\\"fixed\\\"")
            || source.contains("\"variant\": \"fixed\"");
        QVERIFY2(hasHealth, qPrintable(notebook.file));
        QVERIFY2(hasCapabilities, qPrintable(notebook.file));
        QVERIFY2(hasContractVersion, qPrintable(notebook.file));
        QVERIFY2(hasReady, qPrintable(notebook.file));
        QVERIFY2(hasCuda, qPrintable(notebook.file));
        QVERIFY2(hasNoCpuFallback, qPrintable(notebook.file));
        QVERIFY2(hasFixedVariant, qPrintable(notebook.file));
        QVERIFY2(source.contains(notebook.capability.toUtf8()), qPrintable(notebook.file));
        QVERIFY2(source.contains(notebook.model.toUtf8()), qPrintable(notebook.file));
        QVERIFY2(source.contains(notebook.endpoint.toUtf8()), qPrintable(notebook.file));
    }

    QFile cmakeFile(sourceRoot.filePath(QStringLiteral("CMakeLists.txt")));
    QVERIFY2(cmakeFile.open(QIODevice::ReadOnly), qPrintable(cmakeFile.fileName()));
    const QByteArray cmakeSource = cmakeFile.readAll();
    QVERIFY(cmakeSource.contains("notebooks/workers/"));
    QVERIFY(cmakeSource.contains("docs/colab-notebooks/workers"));
}

} // namespace LAStudio
