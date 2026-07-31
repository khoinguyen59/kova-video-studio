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
    QVERIFY(runtimeServiceSource.contains(QStringLiteral("chi_tra")));
    QVERIFY(pageSource.contains(QStringLiteral("displayedWidth")));
    QVERIFY(pageSource.contains(QStringLiteral("displayedHeight")));
    QVERIFY(pageSource.contains(QStringLiteral("ocr.setRoi")));
    QVERIFY(pageSource.contains(QStringLiteral("Preview cropped frame")));
    QVERIFY(pageSource.contains(QStringLiteral("Reset subtitle region")));
    QCOMPARE(pageSource.count(QStringLiteral("RoiHandle { mode:")), 8);
    QVERIFY(controllerSource.contains(QStringLiteral("setRuntimeService")));
    QVERIFY(controllerSource.contains(QStringLiteral("Install runtime in Subtitle OCR")));
}

} // namespace LAStudio
