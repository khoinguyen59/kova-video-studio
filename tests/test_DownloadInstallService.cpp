#include "test_DownloadInstallService.h"
#include <QtTest>
#include <QSignalSpy>
#include <QFile>
#include <QThreadPool>

#include "controllers/models/DownloadInstallService.h"
#include "core/HFHubClient.h"
#include "core/DownloadManager.h"
#include "core/ModelManager.h"
#include "core/Settings.h"
#include "core/RuntimeManager.h"

namespace LAStudio {

void TestDownloadInstallService::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
}

void TestDownloadInstallService::cleanupTestCase()
{
    QThreadPool::globalInstance()->waitForDone();
}

void TestDownloadInstallService::testDownloadInstallService()
{
    qDebug() << "--- START: testDownloadInstallService ---";
    HFHubClient hub;
    DownloadManager downloads(&hub);
    ModelManager models;
    Settings settings;
    RuntimeManager runtimes(nullptr, &settings);

    DownloadInstallService service(&downloads, &models, &runtimes, &settings);

    // Create a dummy model zip with incorrect signature to verify rejection
    QString dummyZip = m_tempDir.filePath(QStringLiteral("bad.zip"));
    QFile file(dummyZip);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("NOT-A-ZIP-HEADER");
    file.close();

    QSignalSpy spyError(&service, &DownloadInstallService::errorOccurred);
    
    // Simulate finished download event
    emit downloads.finished(QStringLiteral("test-model"), QStringLiteral("bad.zip"), dummyZip, {});

    QCOMPARE(spyError.size(), 1);
    QVERIFY(!QFile::exists(dummyZip));
}

void TestDownloadInstallService::testQuickInstallSelectsLatestCatalogRuntime()
{
    QVariantMap oldRuntime{
        {QStringLiteral("id"), QStringLiteral("example-cpu")},
        {QStringLiteral("version"), QStringLiteral("v1.9.0")},
        {QStringLiteral("asset"), QStringLiteral("old.zip")}
    };
    QVariantMap latestRuntime{
        {QStringLiteral("id"), QStringLiteral("example-cpu")},
        {QStringLiteral("version"), QStringLiteral("v1.10.0")},
        {QStringLiteral("asset"), QStringLiteral("latest.zip")}
    };
    QVariantMap option = oldRuntime;
    option.insert(QStringLiteral("latestVersion"), QStringLiteral("v1.10.0"));
    option.insert(QStringLiteral("versionOptions"), QVariantList{latestRuntime, oldRuntime});

    const QVariantMap selected = DownloadInstallService::latestSupportedRuntime(option);
    QCOMPARE(selected.value(QStringLiteral("version")).toString(), QStringLiteral("v1.10.0"));
    QCOMPARE(selected.value(QStringLiteral("asset")).toString(), QStringLiteral("latest.zip"));
}

void TestDownloadInstallService::remoteFirstModeBlocksLocalDownloads()
{
    HFHubClient hub;
    DownloadManager downloads(&hub);
    ModelManager models;
    Settings settings;
    RuntimeManager runtimes(nullptr, &settings);
    DownloadInstallService service(&downloads, &models, &runtimes, &settings);
    const bool original = settings.remoteFirstMode();
    settings.setRemoteFirstMode(true);

    QSignalSpy errors(&service, &DownloadInstallService::errorOccurred);
    const QVariantMap family{{QStringLiteral("modelId"), QStringLiteral("org/example")}};
    const QVariantMap requirement{{QStringLiteral("modelId"), QStringLiteral("org/example")},
                                  {QStringLiteral("selectedFile"), QStringLiteral("model.bin")}};
    QVERIFY(!service.enqueueModelFile(family, requirement));
    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.constFirst().constFirst().toString().contains(QStringLiteral("Remote-first mode")));

    settings.setRemoteFirstMode(original);
}

void TestDownloadInstallService::testArchiveMemberPathsCannotEscapeExtractionDir()
{
    QVERIFY(DownloadInstallService::isSafeArchiveMemberPath(QStringLiteral("bin/runtime.dll")));
    QVERIFY(DownloadInstallService::isSafeArchiveMemberPath(QStringLiteral("package/../bin/runtime.dll")));
    QVERIFY(!DownloadInstallService::isSafeArchiveMemberPath(QStringLiteral("../evil.dll")));
    QVERIFY(!DownloadInstallService::isSafeArchiveMemberPath(QStringLiteral("C:/evil.dll")));
    QVERIFY(!DownloadInstallService::isSafeArchiveMemberPath(QStringLiteral("\\\\server\\share\\evil.dll")));
}

} // namespace LAStudio
