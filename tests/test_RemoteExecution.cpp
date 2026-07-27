#include "test_RemoteExecution.h"

#include <QtTest>
#include <QSettings>

#include "core/PathUtils.h"
#include "core/Settings.h"
#include "remote/ColabSession.h"
#include "remote/ExecutionProvider.h"

namespace LAStudio {

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

} // namespace LAStudio
