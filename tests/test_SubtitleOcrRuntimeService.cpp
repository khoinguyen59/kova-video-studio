#include "test_SubtitleOcrRuntimeService.h"

#include "core/DownloadManager.h"
#include "core/HFHubClient.h"
#include "subtitles/SubtitleOcrRuntimeLocator.h"
#include "subtitles/SubtitleOcrRuntimeService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

namespace LAStudio {
namespace {

bool writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

class EnvironmentScope final
{
public:
    explicit EnvironmentScope(const QString &dataDirectory)
        : m_data(qgetenv("LASTUDIO_DATA_DIR"))
        , m_runtime(qgetenv("LASTUDIO_TESSERACT"))
        , m_hadData(qEnvironmentVariableIsSet("LASTUDIO_DATA_DIR"))
        , m_hadRuntime(qEnvironmentVariableIsSet("LASTUDIO_TESSERACT"))
    {
        qputenv("LASTUDIO_DATA_DIR", dataDirectory.toUtf8());
        qunsetenv("LASTUDIO_TESSERACT");
    }

    ~EnvironmentScope()
    {
        if (m_hadData) qputenv("LASTUDIO_DATA_DIR", m_data); else qunsetenv("LASTUDIO_DATA_DIR");
        if (m_hadRuntime) qputenv("LASTUDIO_TESSERACT", m_runtime); else qunsetenv("LASTUDIO_TESSERACT");
    }

private:
    QByteArray m_data;
    QByteArray m_runtime;
    bool m_hadData = false;
    bool m_hadRuntime = false;
};

bool validSha256(const QString &value)
{
    return QRegularExpression(QStringLiteral("^[a-f0-9]{64}$")).match(value).hasMatch();
}

} // namespace

void TestSubtitleOcrRuntimeService::manifestPinsRuntimeAndAllRequiredLanguagePacks()
{
    const QVariantMap runtime = SubtitleOcrRuntimeService::runtimeDescriptor();
    QVERIFY(runtime.value(QStringLiteral("url")).toString().startsWith(QStringLiteral("https://")));
    QVERIFY(validSha256(runtime.value(QStringLiteral("sha256")).toString()));
    QVERIFY(runtime.value(QStringLiteral("bytes")).toLongLong() > 20 * 1024 * 1024);
    QCOMPARE(runtime.value(QStringLiteral("license")).toString(), QStringLiteral("Apache-2.0"));

    const QVariantList packs = SubtitleOcrRuntimeService::languageDescriptors();
    QCOMPARE(packs.size(), 6);
    QSet<QString> codes;
    for (const QVariant &value : packs) {
        const QVariantMap pack = value.toMap();
        codes.insert(pack.value(QStringLiteral("code")).toString());
        QVERIFY(pack.value(QStringLiteral("url")).toString().startsWith(QStringLiteral("https://")));
        QVERIFY(validSha256(pack.value(QStringLiteral("sha256")).toString()));
        QCOMPARE(pack.value(QStringLiteral("compatibleRuntimeVersion")).toString(),
                 runtime.value(QStringLiteral("version")).toString());
    }
    QVERIFY(codes == QSet<QString>({QStringLiteral("eng"), QStringLiteral("vie"),
                                    QStringLiteral("chi_sim"), QStringLiteral("chi_tra"),
                                    QStringLiteral("jpn"), QStringLiteral("kor")}));

    QFile packagedManifest(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR)).filePath(
        QStringLiteral("resources/subtitle-ocr-runtime-manifest.json")));
    QVERIFY(packagedManifest.open(QIODevice::ReadOnly));
    const QJsonObject manifest = QJsonDocument::fromJson(packagedManifest.readAll()).object();
    QCOMPARE(manifest.value(QStringLiteral("automaticDownload")).toBool(), false);
    QCOMPARE(manifest.value(QStringLiteral("userInitiatedDownload")).toBool(), true);
    const QJsonObject packagedRuntime = manifest.value(QStringLiteral("runtime")).toObject();
    QCOMPARE(packagedRuntime.value(QStringLiteral("url")).toString(), runtime.value(QStringLiteral("url")).toString());
    QCOMPARE(packagedRuntime.value(QStringLiteral("sha256")).toString(), runtime.value(QStringLiteral("sha256")).toString());
    const QJsonArray packagedPacks = manifest.value(QStringLiteral("languageData")).toObject()
        .value(QStringLiteral("packages")).toArray();
    QCOMPARE(packagedPacks.size(), packs.size());
    for (const QVariant &value : packs) {
        const QVariantMap expected = value.toMap();
        bool found = false;
        for (const QJsonValue &candidate : packagedPacks) {
            const QJsonObject actual = candidate.toObject();
            if (actual.value(QStringLiteral("code")).toString() != expected.value(QStringLiteral("code")).toString()) continue;
            found = true;
            QCOMPARE(actual.value(QStringLiteral("url")).toString(), expected.value(QStringLiteral("url")).toString());
            QCOMPARE(actual.value(QStringLiteral("sha256")).toString(), expected.value(QStringLiteral("sha256")).toString());
        }
        QVERIFY(found);
    }
}

void TestSubtitleOcrRuntimeService::verifiedLanguageReplacementIsAtomicAndChecksumProtected()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("downloaded.traineddata"));
    const QString destination = directory.filePath(QStringLiteral("runtime/tessdata/eng.traineddata"));
    QVERIFY(writeFile(source, QByteArrayLiteral("verified language data")));
    QVERIFY(writeFile(destination, QByteArrayLiteral("known-good previous language data")));
    const QString expected = SubtitleOcrRuntimeService::sha256File(source);
    QVERIFY(validSha256(expected));

    QString error;
    QVERIFY(SubtitleOcrRuntimeService::replaceFileAtomically(source, destination, expected, &error));
    QCOMPARE(SubtitleOcrRuntimeService::sha256File(destination), expected);
    QVERIFY(QDir(QFileInfo(destination).absolutePath()).entryList(
        {QStringLiteral("*.pending-*"), QStringLiteral("*.backup-*")}, QDir::Files).isEmpty());

    QVERIFY(writeFile(source, QByteArrayLiteral("corrupt replacement")));
    QVERIFY(!SubtitleOcrRuntimeService::replaceFileAtomically(
        source, destination, QString(64, QLatin1Char('0')), &error));
    QCOMPARE(SubtitleOcrRuntimeService::sha256File(destination), expected);
}

void TestSubtitleOcrRuntimeService::runtimeActivationIsAtomicAndRestartDiscoveryUsesAppOwnedPath()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    EnvironmentScope environment(directory.path());
    const QString runtimeRoot = SubtitleOcrRuntimeLocator::managedRuntimeRoot();
    const QString staging = runtimeRoot + QStringLiteral(".staging-fixture");
    QVERIFY(writeFile(QDir(runtimeRoot).filePath(QStringLiteral("tesseract.exe")), QByteArrayLiteral("old-runtime")));
    QVERIFY(writeFile(QDir(staging).filePath(QStringLiteral("tesseract.exe")), QByteArrayLiteral("new-runtime")));
    QVERIFY(writeFile(QDir(staging).filePath(QStringLiteral("runtime-manifest.json")), QByteArrayLiteral("{}")));

    QString error;
    QVERIFY(SubtitleOcrRuntimeService::replaceRuntimeAtomically(staging, runtimeRoot, &error));
    QFile activated(QDir(runtimeRoot).filePath(QStringLiteral("tesseract.exe")));
    QVERIFY(activated.open(QIODevice::ReadOnly));
    QCOMPARE(activated.readAll(), QByteArrayLiteral("new-runtime"));
    QVERIFY(!QFileInfo::exists(staging));

    HFHubClient hub;
    DownloadManager downloads(&hub);
    SubtitleOcrRuntimeService initial(&downloads);
    QCOMPARE(initial.installState(), SubtitleOcrRuntimeService::Invalid);
    QVERIFY(!initial.runtimeAvailable());

    QString manifestError;
    QVERIFY(initial.writeInstallationManifest(runtimeRoot, &manifestError));
    initial.refresh();
    QCOMPARE(initial.installState(), SubtitleOcrRuntimeService::Installed);
    QVERIFY(initial.runtimeAvailable());
    QCOMPARE(initial.runtimeSource(), QStringLiteral("managed"));
    QCOMPARE(initial.runtimePath(), SubtitleOcrRuntimeLocator::managedTesseractPath());

    SubtitleOcrRuntimeService reopened(&downloads);
    QCOMPARE(reopened.installState(), SubtitleOcrRuntimeService::Installed);
    QVERIFY(reopened.runtimeAvailable());
    QCOMPARE(reopened.runtimePath(), initial.runtimePath());
}

void TestSubtitleOcrRuntimeService::cancelAndRetryKeepExistingRuntimeUntouched()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    EnvironmentScope environment(directory.path());
    HFHubClient hub;
    DownloadManager downloads(&hub);
    SubtitleOcrRuntimeService service(&downloads);
    const QVariantMap runtime = SubtitleOcrRuntimeService::runtimeDescriptor();

    QVERIFY(service.installRuntime());
    QCOMPARE(service.installState(), SubtitleOcrRuntimeService::Downloading);
    QVERIFY(service.busy());
    QVERIFY(service.cancelInstallation());
    emit downloads.error(runtime.value(QStringLiteral("url")).toString(),
                         runtime.value(QStringLiteral("fileName")).toString(),
                         QStringLiteral("Download cancelled."));
    QVERIFY(!service.busy());
    QCOMPARE(service.installState(), SubtitleOcrRuntimeService::Missing);
    QVERIFY(!service.runtimeAvailable());

    QVERIFY(service.installRuntime());
    emit downloads.error(runtime.value(QStringLiteral("url")).toString(),
                         runtime.value(QStringLiteral("fileName")).toString(),
                         QStringLiteral("network unavailable"));
    QCOMPARE(service.installState(), SubtitleOcrRuntimeService::Failed);
    QVERIFY(!service.runtimeAvailable());
    QVERIFY(service.retryInstallation());
    QCOMPARE(service.installState(), SubtitleOcrRuntimeService::Downloading);
}

void TestSubtitleOcrRuntimeService::installerPreflightAndProcessFailureExposeActionableDiagnostics()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    EnvironmentScope environment(directory.path());
    HFHubClient hub;
    DownloadManager downloads(&hub);
    SubtitleOcrRuntimeService service(&downloads);

    service.m_pendingKind = SubtitleOcrRuntimeService::PendingKind::Runtime;
    service.m_pendingAsset = SubtitleOcrRuntimeService::runtimeAsset();
    service.beginInstaller(directory.filePath(QStringLiteral("missing installer.exe")));
    QCOMPARE(service.installState(), SubtitleOcrRuntimeService::Failed);
    QVERIFY(service.error().contains(QStringLiteral("missing"), Qt::CaseInsensitive));
    QVERIFY(service.diagnostics().contains(QStringLiteral("exists=false")));

    const QString invalidInstaller = QDir(service.downloadRoot()).filePath(
        QStringLiteral("installer with spaces ü.exe"));
    QVERIFY(writeFile(invalidInstaller, QByteArrayLiteral("not a Windows executable")));
    service.m_pendingKind = SubtitleOcrRuntimeService::PendingKind::Runtime;
    service.m_pendingAsset = SubtitleOcrRuntimeService::runtimeAsset();
    service.m_pendingAsset.bytes = QFileInfo(invalidInstaller).size();
    service.m_pendingAsset.sha256 = SubtitleOcrRuntimeService::sha256File(invalidInstaller);
    service.beginInstaller(invalidInstaller);
    QTRY_COMPARE_WITH_TIMEOUT(service.installState(), SubtitleOcrRuntimeService::Failed, 5000);
    QVERIFY(service.error().contains(QStringLiteral("could not be started"), Qt::CaseInsensitive));
    QVERIFY(service.diagnostics().contains(QStringLiteral("processError=FailedToStart")));
    QVERIFY(service.diagnostics().contains(QStringLiteral("workingDirectory=")));
    QVERIFY(service.canCleanFailedDownload());
}

void TestSubtitleOcrRuntimeService::healthCheckFailureDoesNotActivateStagingRuntime()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    EnvironmentScope environment(directory.path());
    HFHubClient hub;
    DownloadManager downloads(&hub);
    SubtitleOcrRuntimeService service(&downloads);
    const QString staging = QDir(directory.path()).filePath(QStringLiteral("runtime.staging-health"));
    QVERIFY(writeFile(QDir(staging).filePath(QStringLiteral("tesseract.exe")), QByteArrayLiteral("not runnable")));

    service.m_pendingKind = SubtitleOcrRuntimeService::PendingKind::Runtime;
    service.m_runtimeProcessPhase = SubtitleOcrRuntimeService::RuntimeProcessPhase::HealthCheck;
    service.m_stagingPath = staging;
    service.onInstallerFinished(1, QProcess::NormalExit);

    QCOMPARE(service.installState(), SubtitleOcrRuntimeService::Failed);
    QVERIFY(service.error().contains(QStringLiteral("health check"), Qt::CaseInsensitive));
    QVERIFY(!QFileInfo::exists(staging));
    QVERIFY(!QFileInfo::exists(SubtitleOcrRuntimeLocator::managedTesseractPath()));
}

void TestSubtitleOcrRuntimeService::failedInstallerCacheRequiresExplicitCleanup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    EnvironmentScope environment(directory.path());
    HFHubClient hub;
    DownloadManager downloads(&hub);
    SubtitleOcrRuntimeService service(&downloads);
    const QString cachedInstaller = QDir(service.downloadRoot()).filePath(QStringLiteral("failed-installer.exe"));
    QVERIFY(writeFile(cachedInstaller, QByteArrayLiteral("verified fixture")));
    service.m_failedDownloadPath = cachedInstaller;

    QVERIFY(service.canCleanFailedDownload());
    QVERIFY(service.cleanFailedDownload());
    QVERIFY(!QFileInfo::exists(cachedInstaller));
    QVERIFY(!service.canCleanFailedDownload());
    QVERIFY(service.error().contains(QStringLiteral("removed"), Qt::CaseInsensitive));
}

void TestSubtitleOcrRuntimeService::qmlRouteRoiAndManagedRuntimeControlsAreWired()
{
    const QDir sourceRoot(QStringLiteral(LASTUDIO_SOURCE_DIR));
    QFile main(sourceRoot.filePath(QStringLiteral("qml/Main.qml")));
    QFile routes(sourceRoot.filePath(QStringLiteral("qml/components/shared/StudioRouteRegistry.qml")));
    QFile page(sourceRoot.filePath(QStringLiteral("qml/pages/SubtitleOcrPage.qml")));
    QFile runtimeService(sourceRoot.filePath(QStringLiteral("src/subtitles/SubtitleOcrRuntimeService.cpp")));
    QFile controller(sourceRoot.filePath(
        QStringLiteral("src/controllers/subtitles/SubtitleOcrController.cpp")));
    QVERIFY(main.open(QIODevice::ReadOnly));
    QVERIFY(routes.open(QIODevice::ReadOnly));
    QVERIFY(page.open(QIODevice::ReadOnly));
    QVERIFY(runtimeService.open(QIODevice::ReadOnly));
    QVERIFY(controller.open(QIODevice::ReadOnly));
    const QString mainSource = QString::fromUtf8(main.readAll());
    const QString routeSource = QString::fromUtf8(routes.readAll());
    const QString pageSource = QString::fromUtf8(page.readAll());
    const QString runtimeServiceSource = QString::fromUtf8(runtimeService.readAll());
    const QString controllerSource = QString::fromUtf8(controller.readAll());

    QVERIFY(routeSource.contains(QStringLiteral("\"subtitle-ocr\": 15")));
    QVERIFY(routeSource.contains(QStringLiteral("id: \"subtitle-ocr\"")));
    QVERIFY(mainSource.contains(QStringLiteral("sourceComponent: SubtitleOcrPage")));
    QVERIFY(pageSource.contains(QStringLiteral("AppController.subtitleOcrRuntime")));
    QVERIFY(pageSource.contains(QStringLiteral("runtime.installRuntime()")));
    QVERIFY(pageSource.contains(QStringLiteral("runtime.installLanguage(modelData.code)")));
    QVERIFY(pageSource.contains(QStringLiteral("runtime.cancelInstallation()")));
    QVERIFY(pageSource.contains(QStringLiteral("runtime.cleanFailedDownload()")));
    QVERIFY(pageSource.contains(QStringLiteral("subtitleOcrOpenRuntimeDiagnosticsButton")));
    QVERIFY(pageSource.contains(QStringLiteral("subtitleOcrCleanFailedRuntimeDownloadButton")));
    QVERIFY(pageSource.contains(QStringLiteral("No GPU or Colab required")));
    QVERIFY(pageSource.contains(QStringLiteral("runtime.managedRuntimePath")));
    QVERIFY(runtimeServiceSource.contains(QStringLiteral("chi_tra")));
    QVERIFY(runtimeServiceSource.contains(QStringLiteral("beginRuntimeHealthCheck")));
    QVERIFY(runtimeServiceSource.contains(QStringLiteral("processError=")));
    QVERIFY(runtimeServiceSource.contains(QStringLiteral("signatureDiagnostic")));
    QVERIFY(runtimeServiceSource.contains(QStringLiteral("PathUtils::toNativeShortPath")));
    QVERIFY(pageSource.contains(QStringLiteral("displayedWidth")));
    QVERIFY(pageSource.contains(QStringLiteral("displayedHeight")));
    QVERIFY(pageSource.contains(QStringLiteral("ocr.setRoi")));
    QVERIFY(pageSource.contains(QStringLiteral("ocr.setLowerRegionPreset()")));
    QVERIFY(pageSource.contains(QStringLiteral("ocr.resetRoi()")));
    QVERIFY(pageSource.contains(QStringLiteral("Preview crop")));
    QVERIFY(pageSource.contains(QStringLiteral("Reset region")));
    QCOMPARE(pageSource.count(QStringLiteral("RoiHandle { objectName:")), 8);
    QVERIFY(controllerSource.contains(QStringLiteral("setRuntimeService")));
    QVERIFY(controllerSource.contains(QStringLiteral("Install runtime in Subtitle OCR")));
}

void TestSubtitleOcrRuntimeService::responsiveLayoutSharedMediaAndHomeCardsAreWired()
{
    const QDir sourceRoot(QStringLiteral(LASTUDIO_SOURCE_DIR));
    QFile page(sourceRoot.filePath(QStringLiteral("qml/pages/SubtitleOcrPage.qml")));
    QFile mediaDownload(sourceRoot.filePath(QStringLiteral("qml/pages/MediaDownloadPage.qml")));
    QFile welcome(sourceRoot.filePath(QStringLiteral("qml/pages/WelcomePage.qml")));
    QFile routes(sourceRoot.filePath(QStringLiteral("qml/components/shared/StudioRouteRegistry.qml")));
    QFile main(sourceRoot.filePath(QStringLiteral("qml/Main.qml")));
    QFile mediaControls(sourceRoot.filePath(
        QStringLiteral("qml/components/shared/MediaControlsAutoHide.qml")));
    QFile dubbingSource(sourceRoot.filePath(
        QStringLiteral("qml/components/dubbing/DubbingSourceMediaPanel.qml")));
    QFile controller(sourceRoot.filePath(
        QStringLiteral("src/controllers/subtitles/SubtitleOcrController.cpp")));
    QVERIFY(page.open(QIODevice::ReadOnly));
    QVERIFY(mediaDownload.open(QIODevice::ReadOnly));
    QVERIFY(welcome.open(QIODevice::ReadOnly));
    QVERIFY(routes.open(QIODevice::ReadOnly));
    QVERIFY(main.open(QIODevice::ReadOnly));
    QVERIFY(mediaControls.open(QIODevice::ReadOnly));
    QVERIFY(dubbingSource.open(QIODevice::ReadOnly));
    QVERIFY(controller.open(QIODevice::ReadOnly));

    const QString pageSource = QString::fromUtf8(page.readAll());
    const QString mediaDownloadSource = QString::fromUtf8(mediaDownload.readAll());
    const QString welcomeSource = QString::fromUtf8(welcome.readAll());
    const QString routeSource = QString::fromUtf8(routes.readAll());
    const QString mainSource = QString::fromUtf8(main.readAll());
    const QString mediaControlsSource = QString::fromUtf8(mediaControls.readAll());
    const QString dubbingSourceText = QString::fromUtf8(dubbingSource.readAll());
    const QString controllerSource = QString::fromUtf8(controller.readAll());

    QVERIFY(pageSource.contains(QStringLiteral("id: subtitleOcrScroll")));
    QVERIFY(pageSource.contains(QStringLiteral("id: cardGrid")));
    QVERIFY(pageSource.contains(QStringLiteral("columns: root.wideLayout ? 2 : 1")));
    QVERIFY(pageSource.contains(QStringLiteral("id: sourceMediaCard")));
    QVERIFY(pageSource.contains(QStringLiteral("id: sourceDropZone")));
    QVERIFY(pageSource.contains(QStringLiteral("id: chooseVideoButton")));
    QVERIFY(pageSource.contains(QStringLiteral("id: importLinkButton")));
    QVERIFY(pageSource.contains(QStringLiteral("id: sourceImportIndeterminateIndicator")));
    QVERIFY(pageSource.contains(QStringLiteral("subtitleOcrSourceImportIndeterminateIndicator")));
    QVERIFY(pageSource.contains(
        QStringLiteral("visible: ocr.sourceImporting && ocr.sourceImportTotalBytes <= 0")));
    QVERIFY(pageSource.contains(QStringLiteral("running: visible")));
    QVERIFY(pageSource.contains(QStringLiteral("id: languagePackScroll")));
    QVERIFY(pageSource.contains(QStringLiteral("ocr.importSourceLink")));
    QVERIFY(pageSource.contains(QStringLiteral("ocr.cancelSourceImport")));
    QVERIFY(pageSource.contains(QStringLiteral("ocr.retrySourceImport")));
    QVERIFY(pageSource.contains(QStringLiteral("ocr.runtimeAvailable && root.selectedLanguageReady")));
    QVERIFY(pageSource.contains(QStringLiteral("id: languageSelector")));
    QVERIFY(pageSource.contains(QStringLiteral("enabled: !ocr.processing")));
    QVERIFY(pageSource.contains(QStringLiteral("runtime.runtimeAvailable && !runtime.busy")));
    QCOMPARE(pageSource.count(QStringLiteral("subtitleOcrRoiHandle")), 8);
    QVERIFY(pageSource.contains(QStringLiteral("qmlSmokeLayoutCheck")));
    QVERIFY(pageSource.contains(QStringLiteral("MediaControlsAutoHide")));
    QVERIFY(pageSource.contains(QStringLiteral("subtitleOcrSharedMediaControls")));
    QVERIFY(pageSource.contains(QStringLiteral("qmlSmokeMediaControlsCheck")));
    QVERIFY(mediaControlsSource.contains(QStringLiteral("property int delayMs: 2000")));
    QVERIFY(mediaControlsSource.contains(QStringLiteral("interactionActive")));
    QVERIFY(mediaControlsSource.contains(QStringLiteral("menuOpen")));
    QVERIFY(mediaControlsSource.contains(QStringLiteral("controlsFocused")));
    QVERIFY(mediaControlsSource.contains(QStringLiteral("applyHideDecision")));
    QVERIFY(dubbingSourceText.contains(QStringLiteral("MediaControlsAutoHide")));
    QVERIFY(dubbingSourceText.contains(QStringLiteral("dubbingSharedMediaControls")));
    QVERIFY(dubbingSourceText.contains(QStringLiteral("qmlSmokeMediaControlsCheck")));
    QVERIFY(!dubbingSourceText.contains(QStringLiteral("interval: 2500")));

    QVERIFY(controllerSource.contains(QStringLiteral("m_dubbing->downloadMediaFromLink")));
    QVERIFY(controllerSource.contains(QStringLiteral("m_dubbing->downloadedMediaPath")));
    QVERIFY(controllerSource.contains(QStringLiteral("m_dubbing->cancelMediaLinkImport")));
    QVERIFY(controllerSource.contains(QStringLiteral("m_lastSourceImportUrl")));
    QVERIFY(!controllerSource.contains(QStringLiteral("new RemoteMediaImportService")));

    QVERIFY(mediaDownloadSource.contains(QStringLiteral("Use in Subtitle OCR")));
    QVERIFY(mediaDownloadSource.contains(QStringLiteral("subtitleOcr.useDownloadedMedia")));
    QVERIFY(mediaDownloadSource.contains(QStringLiteral("openSubtitleOcrRequested")));
    QVERIFY(mainSource.contains(QStringLiteral("onOpenSubtitleOcrRequested")));
    QVERIFY(mainSource.contains(QStringLiteral("width: 1024, height: 720")));
    QVERIFY(mainSource.contains(QStringLiteral("width: 1280, height: 800")));
    QVERIFY(mainSource.contains(QStringLiteral("width: 1600, height: 900")));
    QVERIFY(mainSource.contains(QStringLiteral("qmlSmokeHomeLayoutSizeIndex")));
    QVERIFY(mainSource.contains(QStringLiteral("qmlSmokeHomeLayoutResizePending")));

    QVERIFY(welcomeSource.contains(QStringLiteral("StudioRouteRegistry.homeFeatureCards")));
    QVERIFY(welcomeSource.contains(QStringLiteral("qmlSmokeHomeCardsCheck")));
    QVERIFY(welcomeSource.contains(QStringLiteral("id: homeScroll")));
    QVERIFY(welcomeSource.contains(QStringLiteral("id: homeContent")));
    QVERIFY(welcomeSource.contains(
        QStringLiteral("homeScroll.contentHeight < studioCardGrid.y + studioCardGrid.height - 1")));
    QVERIFY(welcomeSource.contains(QStringLiteral("cardNumber: index + 1")));
    QVERIFY(welcomeSource.contains(QStringLiteral("targetRoute: modelData.id")));
    QVERIFY(welcomeSource.contains(QStringLiteral("onClicked: root.pageRequested(card.targetRoute)")));
    QVERIFY(welcomeSource.contains(QStringLiteral("homeFeatureCard-")));
    QVERIFY(welcomeSource.contains(QStringLiteral("Feature cards\"); value: \"10\"")));
    QVERIFY(routeSource.contains(QStringLiteral("readonly property var homeFeatureCards")));
    QCOMPARE(routeSource.count(QStringLiteral("homeCard: true")), 10);
    QCOMPARE(routeSource.count(QStringLiteral("id: \"media-download\"")), 1);
    QCOMPARE(routeSource.count(QStringLiteral("id: \"subtitle-ocr\"")), 1);
}

} // namespace LAStudio
