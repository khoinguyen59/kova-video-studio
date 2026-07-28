#include "test_StudioCapabilities.h"

#include <QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSettings>
#include <QTemporaryDir>

#include "api/ApiServerService.h"
#include "core/StudioCapabilityRegistry.h"
#include "core/Settings.h"
#include "core/LocalizationManager.h"
#include "core/SecureCredentialStore.h"
#include "controllers/app/AppController.h"
#include "controllers/shared/AppUpdateService.h"
#include "controllers/models/ModelSessionRegistry.h"

namespace LAStudio {

void TestStudioCapabilities::testForcedAlignmentDescriptor()
{
    StudioCapabilityRegistry *registry = StudioCapabilityRegistry::instance();

    QVERIFY(registry->hasCapability(QStringLiteral("forced-alignment")));
    const StudioCapabilityDescriptor descriptor = registry->getCapability(QStringLiteral("forced-alignment"));
    QCOMPARE(descriptor.displayName, QStringLiteral("Alignment"));
    QCOMPARE(descriptor.routeId, QStringLiteral("studio-alignment"));
    QCOMPARE(descriptor.pageTitle, QStringLiteral("Alignment Studio"));
    QCOMPARE(descriptor.sharedEngineGroup, QStringLiteral("alignment"));
    QCOMPARE(registry->familyDomain(QStringLiteral("forced-alignment")), QStringLiteral("stt"));
}

void TestStudioCapabilities::testForcedAlignmentFamilyMatching()
{
    StudioCapabilityRegistry *registry = StudioCapabilityRegistry::instance();
    QVariantMap family;
    family.insert(QStringLiteral("capabilities"), QVariantList{QStringLiteral("forced-alignment")});

    QVERIFY(registry->familySupportsCapability(family, QStringLiteral("forced-alignment")));
    QVERIFY(!registry->familySupportsCapability(family, QStringLiteral("tts")));
}

void TestStudioCapabilities::testVoiceIsolationSessionRegistered()
{
    StudioCapabilityRegistry *capabilities = StudioCapabilityRegistry::instance();
    const StudioCapabilityDescriptor descriptor =
        capabilities->getCapability(QStringLiteral("voice-isolation"));

    QCOMPARE(descriptor.sharedEngineGroup, QStringLiteral("voice-isolation"));
    QCOMPARE(capabilities->familyDomain(QStringLiteral("voice-isolation")), QStringLiteral("stt"));

    AppController *app = AppController::instance();
    QVERIFY(app);
    QVERIFY(app->sessionRegistry());
    QVERIFY2(app->sessionRegistry()->sessionForCapability(QStringLiteral("voice-isolation")),
             "Voice Isolation must have a model session so Studio load actions have a target");
}

void TestStudioCapabilities::testTranslationDescriptorAndSession()
{
    StudioCapabilityRegistry *capabilities = StudioCapabilityRegistry::instance();
    const StudioCapabilityDescriptor descriptor = capabilities->getCapability(QStringLiteral("translation"));
    QCOMPARE(descriptor.displayName, QStringLiteral("Translation"));
    QCOMPARE(descriptor.routeId, QStringLiteral("studio-translation"));
    QCOMPARE(descriptor.sharedEngineGroup, QStringLiteral("translation"));
    QCOMPARE(capabilities->familyDomain(QStringLiteral("translation")), QStringLiteral("stt"));
    QVariantMap family{{QStringLiteral("capabilities"), QVariantList{QStringLiteral("translation")}}, {QStringLiteral("supportsTranslation"), true}};
    QVERIFY(capabilities->familySupportsCapability(family, QStringLiteral("translation")));
    QVERIFY(AppController::instance()->sessionRegistry()->sessionForCapability(QStringLiteral("translation")));

    bool m2m100Visible = false;
    for (const QVariant &entry : AppController::instance()->registry()->sttFamilies()) {
        const QVariantMap candidate = entry.toMap();
        if (candidate.value(QStringLiteral("id")).toString() == QStringLiteral("m2m100-418m")) {
            m2m100Visible = candidate.value(QStringLiteral("capabilities")).toList().contains(QStringLiteral("translation"));
            break;
        }
    }
    QVERIFY2(m2m100Visible, "Translation families must be included in the speech catalog view used by Translation Studio.");
}

void TestStudioCapabilities::testLocalApiRequiresBearerAuthentication()
{
    QTcpServer portReservation;
    QVERIFY(portReservation.listen(QHostAddress::LocalHost, 0));
    const int port = portReservation.serverPort();
    portReservation.close();

    Settings settings;
    settings.setApiServerEnabled(false);
    settings.setApiServerAllowLan(false);
    settings.setApiServerPort(port);
    settings.setApiServerApiKey(QStringLiteral("test-api-key"));
    ApiServerService server(&settings, nullptr, nullptr);
    server.setEnabled(true);
    QVERIFY2(server.running(), qPrintable(server.lastError()));

    const auto request = [port](const QByteArray &target, const QByteArray &headers) {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(port));
        if (!socket.waitForConnected(3000)) return QByteArray();
        socket.write("GET " + target + " HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port)
                     + "\r\n" + headers + "\r\n");
        // The service returns worker-thread results through the server thread.
        // Keep the test's event loop alive while waiting so it exercises the
        // same delivery path as the GUI application.
        QElapsedTimer elapsed;
        elapsed.start();
        while (socket.bytesAvailable() == 0 && elapsed.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            socket.waitForReadyRead(50);
        }
        if (socket.bytesAvailable() == 0) return QByteArray();
        return socket.readAll();
    };

    QVERIFY(request("/health", QByteArray()).startsWith("HTTP/1.1 401"));
    QVERIFY(request("/health?api_key=test-api-key", QByteArray()).startsWith("HTTP/1.1 401"));
    const QByteArray health = request("/health", "Authorization: Bearer test-api-key\r\n");
    QVERIFY(health.startsWith("HTTP/1.1 200"));
    QVERIFY(health.contains("/source"));
    const QByteArray source = request("/source", "Authorization: Bearer test-api-key\r\n");
    QVERIFY(source.startsWith("HTTP/1.1 200"));
    QVERIFY(source.contains("AGPL-3.0-only"));
    QVERIFY(request("/health", "Authorization: Bearer test-api-key\r\nOrigin: http://example.invalid\r\n")
                .startsWith("HTTP/1.1 403"));
    QVERIFY(request("/health", "Content-Length: malformed\r\n").startsWith("HTTP/1.1 400"));
    server.setEnabled(false);
}

void TestStudioCapabilities::testCredentialStoreMigratesPlaintext()
{
#ifdef Q_OS_WIN
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QSettings settings(tempDir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("api/serverApiKey"), QStringLiteral("plaintext-test-token"));
    settings.sync();

    QString error;
    const QString migrated = SecureCredentialStore::migrateLegacy(
        settings, QStringLiteral("test-api"), QStringLiteral("api/serverApiKey"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(migrated, QStringLiteral("plaintext-test-token"));
    QVERIFY(!settings.contains(QStringLiteral("api/serverApiKey")));
    const QString ciphertext = settings.value(QStringLiteral("secrets/test-api")).toString();
    QVERIFY(ciphertext.startsWith(QStringLiteral("dpapi-v1:")));
    QVERIFY(!ciphertext.contains(QStringLiteral("plaintext-test-token")));
    QCOMPARE(SecureCredentialStore::read(settings, QStringLiteral("test-api"), &error), migrated);
    QVERIFY2(error.isEmpty(), qPrintable(error));
#else
    QSKIP("Secure credential persistence is implemented with Windows DPAPI.");
#endif
}

void TestStudioCapabilities::testUpdateVersionPrecedence()
{
    QVERIFY(AppUpdateService::isUpdateVersionNewer(
        QStringLiteral("0.0.0.1-beta.2"), QStringLiteral("0.0.0.1-beta.1")));
    QVERIFY(!AppUpdateService::isUpdateVersionNewer(
        QStringLiteral("0.0.0.1-beta.1"), QStringLiteral("0.0.0.1-beta.2")));
    QVERIFY(AppUpdateService::isUpdateVersionNewer(
        QStringLiteral("0.0.0.1"), QStringLiteral("0.0.0.1-beta.2")));
    QVERIFY(!AppUpdateService::isUpdateVersionNewer(
        QStringLiteral("0.0.0.1-beta.2"), QStringLiteral("0.0.0.1")));
    QVERIFY(AppUpdateService::isUpdateVersionNewer(
        QStringLiteral("0.0.1.0-alpha.1"), QStringLiteral("0.0.0.9")));
    QVERIFY(AppUpdateService::isUpdateVersionNewer(
        QStringLiteral("0.0.0.10"), QStringLiteral("0.0.0.9")));
    QVERIFY(!AppUpdateService::isUpdateVersionNewer(
        QStringLiteral("0.0.0.1-beta.01"), QStringLiteral("0.0.0.1-beta.1")));
    QVERIFY(!AppUpdateService::isUpdateVersionNewer(
        QStringLiteral("0.0.1"), QStringLiteral("0.0.0.1")));
}

void TestStudioCapabilities::testUnsupportedLocalizationFallsBackAndPersists()
{
    Settings settings;
    settings.setUiLanguage(QStringLiteral("en"));
    LocalizationManager localization(&settings);
    QSignalSpy languageChanged(&localization, &LocalizationManager::currentLanguageChanged);
    QSignalSpy revisionChanged(&localization, &LocalizationManager::revisionChanged);

    localization.setCurrentLanguage(QStringLiteral("unsupported-locale"));

    QCOMPARE(localization.currentLanguage(), QStringLiteral("en"));
    QCOMPARE(settings.uiLanguage(), QStringLiteral("en"));
    QCOMPARE(localization.revision(), 1);
    QCOMPARE(languageChanged.count(), 1);
    QCOMPARE(revisionChanged.count(), 1);
}

} // namespace LAStudio
