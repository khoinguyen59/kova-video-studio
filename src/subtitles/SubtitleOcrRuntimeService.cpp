#include "subtitles/SubtitleOcrRuntimeService.h"

#include "core/DownloadManager.h"
#include "core/Logger.h"
#include "core/PathUtils.h"
#include "subtitles/SubtitleOcrRuntimeLocator.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QUuid>

#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#include <softpub.h>
#include <wintrust.h>
#pragma comment(lib, "wintrust.lib")
#endif

namespace LAStudio {
namespace {

// The portable package builds this pinned CPU runtime from source through
// vcpkg.  It deliberately does not execute the current upstream Windows
// installer: that installer is hash-pinned but its Authenticode certificate
// cannot be validated on supported current Windows systems.
const QString kTesseractVersion = QStringLiteral("5.5.1");
const QString kTessdataCommit = QStringLiteral("87416418657359cb625c412a48b6e1d6d41c29bd");
#ifdef Q_OS_WIN
GUID kWintrustActionGenericVerifyV2 = WINTRUST_ACTION_GENERIC_VERIFY_V2;
#endif

QString normalizedSha(QString value)
{
    return value.trimmed().toLower();
}

QString processErrorName(QProcess::ProcessError error)
{
    switch (error) {
    case QProcess::FailedToStart: return QStringLiteral("FailedToStart");
    case QProcess::Crashed: return QStringLiteral("Crashed");
    case QProcess::Timedout: return QStringLiteral("Timedout");
    case QProcess::WriteError: return QStringLiteral("WriteError");
    case QProcess::ReadError: return QStringLiteral("ReadError");
    case QProcess::UnknownError:
    default: return QStringLiteral("UnknownError");
    }
}

QString bundledRuntimeManifestPath(const QString &applicationDirectory)
{
    return QDir(applicationDirectory).filePath(
        QStringLiteral("subtitle-ocr/runtime-manifest.json"));
}

QString signatureDiagnostic(const QString &path)
{
#ifdef Q_OS_WIN
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
    fileInfo.pcwszFilePath = nativePath.c_str();

    WINTRUST_DATA trustData{};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    // This is local diagnostics only.  Never issue a hidden network request
    // just to inspect a certificate revocation list during installation.
    trustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    const LONG status = WinVerifyTrust(nullptr, &kWintrustActionGenericVerifyV2, &trustData);
    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &kWintrustActionGenericVerifyV2, &trustData);
    return QStringLiteral("Authenticode=%1 (0x%2)")
        .arg(status == ERROR_SUCCESS ? QStringLiteral("verified") : QStringLiteral("not-verified"))
        .arg(QString::number(static_cast<qulonglong>(static_cast<unsigned long>(status)), 16));
#else
    Q_UNUSED(path);
    return QStringLiteral("Authenticode=not-applicable");
#endif
}

} // namespace

SubtitleOcrRuntimeService::SubtitleOcrRuntimeService(DownloadManager *downloads, QObject *parent)
    : QObject(parent), m_downloads(downloads)
{
    if (m_downloads) {
        connect(m_downloads, &DownloadManager::finished,
                this, &SubtitleOcrRuntimeService::onDownloadFinished);
        connect(m_downloads, &DownloadManager::error,
                this, &SubtitleOcrRuntimeService::onDownloadError);
        connect(m_downloads, &DownloadManager::activeDownloadsChanged,
                this, &SubtitleOcrRuntimeService::updateTransferProgress);
    }
    connect(&m_installer, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &SubtitleOcrRuntimeService::onInstallerFinished);
    connect(&m_installer, &QProcess::errorOccurred,
            this, &SubtitleOcrRuntimeService::onInstallerError);
    connect(&m_languagePreflight, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &SubtitleOcrRuntimeService::onLanguagePreflightFinished);
    connect(&m_languagePreflight, &QProcess::errorOccurred,
            this, &SubtitleOcrRuntimeService::onLanguagePreflightError);
    refresh();
}

SubtitleOcrRuntimeService::~SubtitleOcrRuntimeService()
{
    if (m_installer.state() != QProcess::NotRunning) m_installer.kill();
    if (m_languagePreflight.state() != QProcess::NotRunning) m_languagePreflight.kill();
    if (!m_stagingPath.isEmpty()) QDir(m_stagingPath).removeRecursively();
}

QString SubtitleOcrRuntimeService::stateName() const
{
    switch (m_installState) {
    case Downloading: return QStringLiteral("Downloading");
    case Installing: return QStringLiteral("Installing");
    case Installed: return QStringLiteral("Ready");
    case Invalid: return QStringLiteral("Invalid");
    case Failed: return QStringLiteral("Failed");
    case Missing:
    default: return QStringLiteral("Missing");
    }
}

QList<SubtitleOcrRuntimeService::Asset> SubtitleOcrRuntimeService::languageAssets()
{
    const QString base = QStringLiteral("https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/")
        + kTessdataCommit + QLatin1Char('/');
    return {
        {QStringLiteral("eng"), QStringLiteral("English"), QStringLiteral("eng.traineddata"),
         base + QStringLiteral("eng.traineddata"),
         QStringLiteral("7d4322bd2a7749724879683fc3912cb542f19906c83bcc1a52132556427170b2"), 4113088},
        {QStringLiteral("vie"), QStringLiteral("Vietnamese"), QStringLiteral("vie.traineddata"),
         base + QStringLiteral("vie.traineddata"),
         QStringLiteral("79df64caf7bcfb2a27df5042ecb6121e196eada34da774956995747636d5bfa1"), 531275},
        {QStringLiteral("chi_sim"), QStringLiteral("Chinese (Simplified)"), QStringLiteral("chi_sim.traineddata"),
         base + QStringLiteral("chi_sim.traineddata"),
         QStringLiteral("a5fcb6f0db1e1d6d8522f39db4e848f05984669172e584e8d76b6b3141e1f730"), 2469156},
        {QStringLiteral("chi_tra"), QStringLiteral("Chinese (Traditional)"), QStringLiteral("chi_tra.traineddata"),
         base + QStringLiteral("chi_tra.traineddata"),
         QStringLiteral("529c5b5797d64b126065cd55f2bb4c7fd7b15790798091b1ff259941a829330b"), 2366642},
        {QStringLiteral("jpn"), QStringLiteral("Japanese"), QStringLiteral("jpn.traineddata"),
         base + QStringLiteral("jpn.traineddata"),
         QStringLiteral("1f5de9236d2e85f5fdf4b3c500f2d4926f8d9449f28f5394472d9e8d83b91b4d"), 2471260},
        {QStringLiteral("kor"), QStringLiteral("Korean"), QStringLiteral("kor.traineddata"),
         base + QStringLiteral("kor.traineddata"),
         QStringLiteral("6b85e11d9bbf07863b97b3523b1b112844c43e713df8b66418a081fd1060b3b2"), 1677415},
    };
}

QVariantMap SubtitleOcrRuntimeService::runtimeDescriptor()
{
    return {
        {QStringLiteral("id"), QStringLiteral("tesseract-runtime")},
        {QStringLiteral("label"), QStringLiteral("Bundled Tesseract OCR for Windows (x64)")},
        {QStringLiteral("version"), kTesseractVersion},
        {QStringLiteral("delivery"), QStringLiteral("bundled-vcpkg")},
        {QStringLiteral("fileName"), QStringLiteral("tesseract.exe")},
        {QStringLiteral("userDownloadRequired"), false},
        {QStringLiteral("license"), QStringLiteral("Apache-2.0")},
        {QStringLiteral("licenseUrl"), QStringLiteral("https://www.apache.org/licenses/LICENSE-2.0")},
        {QStringLiteral("source"), QStringLiteral("https://github.com/tesseract-ocr/tesseract")},
        {QStringLiteral("architecture"), QStringLiteral("windows-x64")},
    };
}

QVariantList SubtitleOcrRuntimeService::languageDescriptors()
{
    QVariantList result;
    for (const Asset &asset : languageAssets()) {
        result.append(QVariantMap{
            {QStringLiteral("code"), asset.code},
            {QStringLiteral("label"), asset.label},
            {QStringLiteral("fileName"), asset.fileName},
            {QStringLiteral("url"), asset.url},
            {QStringLiteral("sha256"), asset.sha256},
            {QStringLiteral("bytes"), asset.bytes},
            {QStringLiteral("tessdataCommit"), kTessdataCommit},
            {QStringLiteral("compatibleRuntimeVersion"), kTesseractVersion},
            {QStringLiteral("license"), QStringLiteral("Apache-2.0")},
        });
    }
    return result;
}

QString SubtitleOcrRuntimeService::sha256File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray data = file.read(1024 * 1024);
        if (data.isEmpty() && file.error() != QFile::NoError) return {};
        hash.addData(data);
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool SubtitleOcrRuntimeService::replaceFileAtomically(const QString &sourcePath,
                                                       const QString &destinationPath,
                                                       const QString &expectedSha256,
                                                       QString *errorMessage)
{
    if (!QFileInfo(sourcePath).isFile()) {
        if (errorMessage) *errorMessage = QStringLiteral("Downloaded language file is missing.");
        return false;
    }
    if (normalizedSha(sha256File(sourcePath)) != normalizedSha(expectedSha256)) {
        if (errorMessage) *errorMessage = QStringLiteral("SHA-256 verification failed for downloaded language data.");
        return false;
    }

    const QFileInfo destinationInfo(destinationPath);
    if (!QDir().mkpath(destinationInfo.absolutePath())) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot create app-owned tessdata directory.");
        return false;
    }
    const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString stagedPath = destinationPath + QStringLiteral(".pending-") + suffix;
    const QString backupPath = destinationPath + QStringLiteral(".backup-") + suffix;
    QFile::remove(stagedPath);
    if (!QFile::copy(sourcePath, stagedPath) ||
        normalizedSha(sha256File(stagedPath)) != normalizedSha(expectedSha256)) {
        QFile::remove(stagedPath);
        if (errorMessage) *errorMessage = QStringLiteral("Cannot stage verified language data for atomic installation.");
        return false;
    }

    const bool hadExisting = QFileInfo::exists(destinationPath);
    if (hadExisting && !QFile::rename(destinationPath, backupPath)) {
        QFile::remove(stagedPath);
        if (errorMessage) *errorMessage = QStringLiteral("Cannot preserve the existing language data before replacement.");
        return false;
    }
    if (!QFile::rename(stagedPath, destinationPath)) {
        if (hadExisting) QFile::rename(backupPath, destinationPath);
        QFile::remove(stagedPath);
        if (errorMessage) *errorMessage = QStringLiteral("Cannot atomically install language data.");
        return false;
    }
    if (hadExisting) QFile::remove(backupPath);
    return true;
}

bool SubtitleOcrRuntimeService::replaceRuntimeAtomically(const QString &stagingPath,
                                                          const QString &runtimeRoot,
                                                          QString *errorMessage)
{
    if (!QFileInfo(stagingPath).isDir()) {
        if (errorMessage) *errorMessage = QStringLiteral("Runtime installer did not create its staging directory.");
        return false;
    }
    const QFileInfo rootInfo(runtimeRoot);
    if (!QDir().mkpath(rootInfo.dir().absolutePath())) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot create app-owned Subtitle OCR runtime directory.");
        return false;
    }
    const QString backupPath = runtimeRoot + QStringLiteral(".backup-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const bool hadExisting = QFileInfo::exists(runtimeRoot);
    if (hadExisting && !QDir().rename(runtimeRoot, backupPath)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot preserve the existing OCR runtime before replacement.");
        return false;
    }
    if (!QDir().rename(stagingPath, runtimeRoot)) {
        if (hadExisting) QDir().rename(backupPath, runtimeRoot);
        if (errorMessage) *errorMessage = QStringLiteral("Cannot atomically install the OCR runtime.");
        return false;
    }
    if (hadExisting) QDir(backupPath).removeRecursively();
    return true;
}

QString SubtitleOcrRuntimeService::runtimeRoot() const
{
    return SubtitleOcrRuntimeLocator::managedRuntimeRoot();
}

QString SubtitleOcrRuntimeService::downloadRoot() const
{
    return QDir(PathUtils::cacheDir()).filePath(QStringLiteral("subtitle-ocr/downloads"));
}

QString SubtitleOcrRuntimeService::manifestPath() const
{
    return QDir(runtimeRoot()).filePath(QStringLiteral("runtime-manifest.json"));
}

QString SubtitleOcrRuntimeService::languagePath(const QString &languageCode) const
{
    return QDir(runtimeRoot()).filePath(QStringLiteral("tessdata/%1.traineddata").arg(languageCode));
}

bool SubtitleOcrRuntimeService::hasValidManagedRuntime(QString *errorMessage) const
{
    if (!QFileInfo(SubtitleOcrRuntimeLocator::managedTesseractPath()).isFile()) {
        if (errorMessage) *errorMessage = QStringLiteral("Managed Tesseract executable is missing.");
        return false;
    }
    QFile file(manifestPath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Managed runtime manifest is missing.");
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = document.object();
    const QJsonObject runtime = root.value(QStringLiteral("runtime")).toObject();
    const QString executable = SubtitleOcrRuntimeLocator::managedTesseractPath();
    const QString expectedHash = normalizedSha(runtime.value(QStringLiteral("binarySha256")).toString());
    if (!document.isObject() || root.value(QStringLiteral("schemaVersion")).toInt() != 2 ||
        runtime.value(QStringLiteral("delivery")).toString() != QStringLiteral("legacy-app-data") ||
        runtime.value(QStringLiteral("version")).toString() != kTesseractVersion ||
        runtime.value(QStringLiteral("binaryRelativePath")).toString() != QStringLiteral("tesseract.exe") ||
        expectedHash.size() != 64 || normalizedSha(sha256File(executable)) != expectedHash) {
        if (errorMessage) *errorMessage = QStringLiteral("Managed runtime manifest is invalid or incompatible.");
        return false;
    }
    return true;
}

bool SubtitleOcrRuntimeService::hasValidBundledRuntime(const QString &applicationDirectory,
                                                        QString *errorMessage)
{
    const QString executable = QDir(applicationDirectory).filePath(
        QStringLiteral("subtitle-ocr/tesseract.exe"));
    if (!QFileInfo(executable).isFile()) {
        if (errorMessage) *errorMessage = QStringLiteral("Bundled Tesseract executable is missing.");
        return false;
    }
    QFile manifestFile(bundledRuntimeManifestPath(applicationDirectory));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Bundled Tesseract runtime manifest is missing.");
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll());
    const QJsonObject manifest = document.object();
    const QJsonObject runtime = manifest.value(QStringLiteral("runtime")).toObject();
    if (!document.isObject() || manifest.value(QStringLiteral("schemaVersion")).toInt() != 2 ||
        runtime.value(QStringLiteral("delivery")).toString() != QStringLiteral("bundled-vcpkg") ||
        runtime.value(QStringLiteral("version")).toString() != kTesseractVersion ||
        runtime.value(QStringLiteral("healthCheckPassed")).toBool() != true) {
        if (errorMessage) *errorMessage = QStringLiteral("Bundled Tesseract runtime manifest is invalid.");
        return false;
    }
    const QString expectedHash = normalizedSha(runtime.value(QStringLiteral("binarySha256")).toString());
    if (expectedHash.size() != 64 || normalizedSha(sha256File(executable)) != expectedHash) {
        if (errorMessage) *errorMessage = QStringLiteral("Bundled Tesseract executable does not match its package manifest.");
        return false;
    }
    return true;
}

bool SubtitleOcrRuntimeService::hasUsablePackagedRuntime(QString *errorMessage) const
{
    if (m_runtimeSource == QStringLiteral("managed")) return hasValidManagedRuntime(errorMessage);
    if (m_runtimeSource == QStringLiteral("bundled")) {
        return hasValidBundledRuntime(QCoreApplication::applicationDirPath(), errorMessage);
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("Install a package-provisioned Tesseract runtime before adding language packs.");
    }
    return false;
}

bool SubtitleOcrRuntimeService::writeInstallationManifest(const QString &installationRoot,
                                                          QString *errorMessage) const
{
    const QString executable = QDir(installationRoot).filePath(QStringLiteral("tesseract.exe"));
    const QString executableHash = normalizedSha(sha256File(executable));
    if (!QFileInfo(executable).isFile() || executableHash.size() != 64) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot hash the app-managed Tesseract executable.");
        return false;
    }

    QJsonObject root{{QStringLiteral("schemaVersion"), 2},
                     {QStringLiteral("component"), QStringLiteral("Tesseract OCR")},
                     {QStringLiteral("runtime"), QJsonObject{
                         {QStringLiteral("delivery"), QStringLiteral("legacy-app-data")},
                         {QStringLiteral("version"), kTesseractVersion},
                         {QStringLiteral("binaryRelativePath"), QStringLiteral("tesseract.exe")},
                         {QStringLiteral("binarySha256"), executableHash},
                     }},
                     {QStringLiteral("license"), QStringLiteral("Apache-2.0")},
                     {QStringLiteral("tessdataCommit"), kTessdataCommit}};
    QJsonArray languages;
    for (const Asset &asset : languageAssets()) {
        const QString path = QDir(installationRoot).filePath(
            QStringLiteral("tessdata/%1.traineddata").arg(asset.code));
        if (normalizedSha(sha256File(path)) == normalizedSha(asset.sha256)) {
            languages.append(QJsonObject{{QStringLiteral("code"), asset.code},
                                         {QStringLiteral("sha256"), asset.sha256},
                                         {QStringLiteral("fileName"), asset.fileName}});
        }
    }
    root.insert(QStringLiteral("installedLanguages"), languages);
    QSaveFile file(QDir(installationRoot).filePath(QStringLiteral("runtime-manifest.json")));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0 || !file.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot atomically write the OCR runtime manifest.");
        return false;
    }
    return true;
}

void SubtitleOcrRuntimeService::setInstallState(int state)
{
    if (m_installState == state) return;
    m_installState = state;
    emit stateChanged();
}

void SubtitleOcrRuntimeService::setError(const QString &message)
{
    if (m_error == message) return;
    m_error = message;
    emit errorChanged();
}

SubtitleOcrRuntimeService::Asset SubtitleOcrRuntimeService::languageAsset(const QString &languageCode) const
{
    const QString normalized = languageCode.trimmed().toLower();
    for (const Asset &asset : languageAssets()) {
        if (asset.code == normalized) return asset;
    }
    return {};
}

bool SubtitleOcrRuntimeService::isLanguageInstalled(const QString &languageCode) const
{
    const Asset asset = languageAsset(languageCode);
    if (asset.code.isEmpty() ||
        (m_runtimeSource != QStringLiteral("managed") && m_runtimeSource != QStringLiteral("bundled"))) {
        return false;
    }
    return m_languagePreflightFinished && m_workerLanguages.contains(asset.code) &&
        normalizedSha(sha256File(languagePath(asset.code))) == normalizedSha(asset.sha256);
}

bool SubtitleOcrRuntimeService::canCleanFailedDownload() const
{
    return isManagedDownloadPath(m_failedDownloadPath) && QFileInfo(m_failedDownloadPath).isFile();
}

QString SubtitleOcrRuntimeService::managedRuntimePath() const
{
    return SubtitleOcrRuntimeLocator::managedRuntimeRoot();
}

QString SubtitleOcrRuntimeService::managedTessdataPath() const
{
    return QDir(managedRuntimePath()).filePath(QStringLiteral("tessdata"));
}

QStringList SubtitleOcrRuntimeService::tesseractDataArguments() const
{
    if (m_runtimeSource != QStringLiteral("managed") &&
        m_runtimeSource != QStringLiteral("bundled")) {
        return {};
    }
    return {QStringLiteral("--tessdata-dir"), managedTessdataPath()};
}

QProcessEnvironment SubtitleOcrRuntimeService::tesseractProcessEnvironment() const
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (m_runtimeSource == QStringLiteral("managed") ||
        m_runtimeSource == QStringLiteral("bundled")) {
        environment.remove(QStringLiteral("TESSDATA_PREFIX"));
    }
    return environment;
}

QSet<QString> SubtitleOcrRuntimeService::parseTesseractLanguages(const QByteArray &output)
{
    QSet<QString> languages;
    const QStringList lines = QString::fromLocal8Bit(output).split(QRegularExpression("[\\r\\n]+"),
                                                                     Qt::SkipEmptyParts);
    static const QRegularExpression languageCode(QStringLiteral("^[A-Za-z0-9_]+$"));
    for (const QString &line : lines) {
        const QString code = line.trimmed();
        if (languageCode.match(code).hasMatch()) languages.insert(code);
    }
    return languages;
}

bool SubtitleOcrRuntimeService::isManagedDownloadPath(const QString &path) const
{
    if (path.isEmpty()) return false;
    const QString root = QDir::cleanPath(QFileInfo(downloadRoot()).absoluteFilePath());
    const QString candidate = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString relative = QDir(root).relativeFilePath(candidate);
    return relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../")) &&
        !QDir::isAbsolutePath(relative);
}

void SubtitleOcrRuntimeService::appendDiagnostics(const QString &phase, const QString &message)
{
    const QString entry = QStringLiteral("[%1] %2")
        .arg(phase, Logger::sanitizeDiagnostics(message.trimmed()));
    constexpr qsizetype maxDiagnosticsCharacters = 12 * 1024;
    m_diagnostics = (m_diagnostics.isEmpty() ? entry : m_diagnostics + QLatin1Char('\n') + entry);
    if (m_diagnostics.size() > maxDiagnosticsCharacters) {
        m_diagnostics = QStringLiteral("[Earlier diagnostics omitted]\n")
            + m_diagnostics.right(maxDiagnosticsCharacters);
    }
    Logger::info(QStringLiteral("Subtitle OCR runtime"), entry);
    emit diagnosticsChanged();
}

QString SubtitleOcrRuntimeService::processDiagnostics(const QString &phase,
                                                      QProcess::ProcessError error) const
{
    const QFileInfo fileInfo(m_installer.program());
    QStringList arguments = m_installer.arguments();
    for (QString &argument : arguments) argument = QDir::toNativeSeparators(argument);
    QString nativeError = QStringLiteral("unavailable");
#ifdef Q_OS_WIN
    // QProcess does not expose CreateProcessW's native error code. Capture the
    // callback thread value and label it explicitly instead of pretending it
    // is an authoritative process error.
    nativeError = QStringLiteral("callback-GetLastError=%1")
        .arg(static_cast<qulonglong>(GetLastError()));
#endif
    return QStringLiteral("phase=%1; program=%2; workingDirectory=%3; arguments=%4; "
                          "exists=%5; file=%6; bytes=%7; executable=%8; sha256=%9; %10; "
                          "processError=%11; processErrorString=%12; nativeError=%13")
        .arg(phase,
             QDir::cleanPath(fileInfo.absoluteFilePath()),
             QDir::cleanPath(m_installer.workingDirectory()),
             arguments.join(QStringLiteral(" | ")),
             fileInfo.exists() ? QStringLiteral("true") : QStringLiteral("false"),
             fileInfo.isFile() ? QStringLiteral("true") : QStringLiteral("false"),
             QString::number(fileInfo.size()),
             fileInfo.isExecutable() ? QStringLiteral("true") : QStringLiteral("false"),
             sha256File(fileInfo.absoluteFilePath()),
             signatureDiagnostic(fileInfo.absoluteFilePath()),
             processErrorName(error),
             m_installer.errorString(),
             nativeError);
}

void SubtitleOcrRuntimeService::rebuildLanguagePacks()
{
    QVariantList packs;
    const bool managed = m_runtimeSource == QStringLiteral("managed") ||
        m_runtimeSource == QStringLiteral("bundled");
    for (const Asset &asset : languageAssets()) {
        const QString path = languagePath(asset.code);
        const bool exists = QFileInfo(path).isFile();
        const bool payloadVerified = managed && normalizedSha(sha256File(path)) == normalizedSha(asset.sha256);
        const bool workerVerified = payloadVerified && m_languagePreflightFinished &&
            m_workerLanguages.contains(asset.code);
        QString state = workerVerified ? QStringLiteral("Ready")
            : payloadVerified && m_languagePreflightRunning ? QStringLiteral("Verifying")
            : payloadVerified ? QStringLiteral("Invalid")
            : exists && managed ? QStringLiteral("Invalid") : QStringLiteral("Missing");
        const QString workerDiagnostic = QStringLiteral("Tesseract did not report %1; binary=%2; tessdata=%3; reported=%4.")
            .arg(asset.code, QDir::cleanPath(m_runtimePath), QDir::cleanPath(managedTessdataPath()),
                 QStringList(m_workerLanguages.values()).join(QLatin1Char(',')));
        QString detail = workerVerified
            ? QStringLiteral("Verified SHA-256 and confirmed by Tesseract using this app's tessdata directory.")
            : payloadVerified && m_languagePreflightRunning
            ? QStringLiteral("Verified SHA-256; checking Tesseract access using the app's exact tessdata directory.")
            : payloadVerified
            ? (m_languagePreflightError.isEmpty() ? workerDiagnostic : m_languagePreflightError)
            : managed ? QStringLiteral("Install this language pack before OCR uses it.")
            : QStringLiteral("Language packs are managed only for the app-owned runtime. External runtime languages are preflighted by Tesseract.");
        packs.append(QVariantMap{{QStringLiteral("code"), asset.code},
                                 {QStringLiteral("label"), asset.label},
                                 {QStringLiteral("fileName"), asset.fileName},
                                 {QStringLiteral("sha256"), asset.sha256},
                                 {QStringLiteral("url"), asset.url},
                                 {QStringLiteral("bytes"), asset.bytes},
                                 {QStringLiteral("installed"), workerVerified},
                                 {QStringLiteral("payloadVerified"), payloadVerified},
                                 {QStringLiteral("workerVerified"), workerVerified},
                                 {QStringLiteral("state"), state},
                                 {QStringLiteral("detail"), detail},
                                 {QStringLiteral("compatibleRuntimeVersion"), kTesseractVersion}});
    }
    if (m_languagePacks == packs) return;
    m_languagePacks = packs;
    emit languagePacksChanged();
}

QString SubtitleOcrRuntimeService::languageFingerprint() const
{
    if (!m_runtimeValid || (m_runtimeSource != QStringLiteral("managed") &&
                            m_runtimeSource != QStringLiteral("bundled"))) {
        return {};
    }
    QStringList parts{QDir::cleanPath(m_runtimePath), QDir::cleanPath(managedTessdataPath())};
    for (const Asset &asset : languageAssets()) {
        const QString hash = normalizedSha(sha256File(languagePath(asset.code)));
        if (hash == normalizedSha(asset.sha256)) parts.append(asset.code + QLatin1Char('=') + hash);
    }
    return parts.join(QLatin1Char('|'));
}

void SubtitleOcrRuntimeService::resetLanguagePreflight(const QString &fingerprint)
{
    if (m_languagePreflightRunning && m_languagePreflight.state() != QProcess::NotRunning) {
        m_languagePreflight.kill();
    }
    m_languagePreflightFingerprint = fingerprint;
    m_workerLanguages.clear();
    m_languagePreflightError.clear();
    m_languagePreflightExpectedCodes.clear();
    m_languagePreflightRunning = false;
    m_languagePreflightFinished = false;
}

void SubtitleOcrRuntimeService::beginLanguagePreflight()
{
    if (m_languagePreflightFingerprint.isEmpty() || m_languagePreflightRunning ||
        m_languagePreflightFinished || !m_runtimeValid) {
        return;
    }
    QStringList verifiedCodes;
    for (const Asset &asset : languageAssets()) {
        if (normalizedSha(sha256File(languagePath(asset.code))) == normalizedSha(asset.sha256)) {
            verifiedCodes.append(asset.code);
        }
    }
    if (verifiedCodes.isEmpty()) return;

    QStringList arguments = tesseractDataArguments();
    arguments.append(QStringLiteral("--list-langs"));
    m_languagePreflight.setProgram(m_runtimePath);
    m_languagePreflight.setArguments(arguments);
    m_languagePreflight.setProcessEnvironment(tesseractProcessEnvironment());
    m_languagePreflightRunning = true;
    m_languagePreflightExpectedCodes = verifiedCodes;
    appendDiagnostics(QStringLiteral("language-preflight"),
                      QStringLiteral("Starting; binary=%1; tessdata=%2; language=%3")
                          .arg(QDir::cleanPath(m_runtimePath), QDir::cleanPath(managedTessdataPath()),
                               verifiedCodes.join(QLatin1Char(','))));
    rebuildLanguagePacks();
    m_languagePreflight.start();
}

void SubtitleOcrRuntimeService::completeLanguagePreflight(const QSet<QString> &languages,
                                                           const QString &errorMessage)
{
    if (!m_languagePreflightRunning) return;
    m_languagePreflightRunning = false;
    m_languagePreflightFinished = true;
    m_workerLanguages = languages;
    m_languagePreflightError = errorMessage;
    if (errorMessage.isEmpty()) {
        appendDiagnostics(QStringLiteral("language-preflight"),
                          QStringLiteral("Passed; binary=%1; tessdata=%2; language=%3")
                              .arg(QDir::cleanPath(m_runtimePath), QDir::cleanPath(managedTessdataPath()),
                                   QStringList(languages.values()).join(QLatin1Char(','))));
    } else {
        appendDiagnostics(QStringLiteral("language-preflight"), errorMessage);
    }
    rebuildLanguagePacks();
}

void SubtitleOcrRuntimeService::onLanguagePreflightFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_languagePreflightRunning) return;
    const QByteArray output = m_languagePreflight.readAllStandardOutput();
    const QString errorOutput = QString::fromLocal8Bit(m_languagePreflight.readAllStandardError()).trimmed();
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString message = QStringLiteral("Tesseract language preflight failed; binary=%1; tessdata=%2; language=%3; exit=%4; stderr=%5")
            .arg(QDir::cleanPath(m_runtimePath))
            .arg(QDir::cleanPath(managedTessdataPath()))
            .arg(m_languagePreflightExpectedCodes.join(QLatin1Char(',')))
            .arg(exitCode)
            .arg(errorOutput);
        completeLanguagePreflight({}, message);
        return;
    }
    completeLanguagePreflight(parseTesseractLanguages(output), {});
}

void SubtitleOcrRuntimeService::onLanguagePreflightError(QProcess::ProcessError error)
{
    if (!m_languagePreflightRunning) return;
    completeLanguagePreflight({}, QStringLiteral("Tesseract language preflight failed; binary=%1; tessdata=%2; language=%3; processError=%4; detail=%5")
        .arg(QDir::cleanPath(m_runtimePath))
        .arg(QDir::cleanPath(managedTessdataPath()))
        .arg(m_languagePreflightExpectedCodes.join(QLatin1Char(',')))
        .arg(processErrorName(error))
        .arg(m_languagePreflight.errorString()));
}

void SubtitleOcrRuntimeService::refresh()
{
    const SubtitleOcrRuntimeResolution resolution = SubtitleOcrRuntimeLocator::resolve();
    QString version;
    int detectedState = Missing;
    if (!resolution.path.isEmpty()) {
        if (resolution.source == QStringLiteral("managed")) {
            QString manifestError;
            if (hasValidManagedRuntime(&manifestError)) {
                detectedState = Installed;
                version = kTesseractVersion;
            } else {
                detectedState = Invalid;
                setError(manifestError);
            }
        } else if (resolution.source == QStringLiteral("bundled")) {
            QString manifestError;
            if (hasValidBundledRuntime(QCoreApplication::applicationDirPath(), &manifestError)) {
                detectedState = Installed;
                version = kTesseractVersion;
            } else {
                detectedState = Invalid;
                setError(manifestError);
            }
        } else {
            detectedState = Installed;
            version = QStringLiteral("external");
        }
    }
    const bool valid = detectedState == Installed;
    const bool clearResolvedRuntimeError = valid && m_pendingKind == PendingKind::None &&
        (m_installState == Invalid ||
         (m_installState == Failed && m_lastAction == QStringLiteral("runtime")));
    const bool changed = m_runtimePath != resolution.path || m_runtimeSource != resolution.source ||
        m_runtimeVersion != version || m_runtimeValid != valid;
    m_runtimePath = resolution.path;
    m_runtimeSource = resolution.source;
    m_runtimeVersion = version;
    m_runtimeValid = valid;
    if (m_pendingKind == PendingKind::None) setInstallState(detectedState);
    if (clearResolvedRuntimeError) setError({});
    const QString fingerprint = languageFingerprint();
    // An explicit Refresh is a new worker check, not a relabel of an old
    // checksum result.  This also makes a replacement download recover from
    // a transient worker-start failure without ever showing a false Ready.
    if (fingerprint != m_languagePreflightFingerprint ||
        (m_languagePreflightFinished && m_pendingKind == PendingKind::None)) {
        resetLanguagePreflight(fingerprint);
    }
    rebuildLanguagePacks();
    beginLanguagePreflight();
    if (changed) emit runtimeChanged();
}

bool SubtitleOcrRuntimeService::beginDownload(PendingKind kind, const Asset &asset)
{
    if (kind != PendingKind::Language) {
        fail(QStringLiteral("Runtime installers are not supported. Install only a verified package-provisioned OCR runtime."));
        return false;
    }
    if (!m_downloads) {
        fail(QStringLiteral("The managed download service is unavailable."));
        return false;
    }
    if (asset.code.isEmpty() || !asset.url.startsWith(QStringLiteral("https://")) ||
        asset.sha256.size() != 64 || asset.bytes <= 0) {
        fail(QStringLiteral("The OCR runtime manifest is incomplete or unsafe."));
        return false;
    }
    if (m_pendingKind != PendingKind::None) {
        setError(QStringLiteral("An OCR runtime installation is already in progress."));
        return false;
    }
    if (!QDir().mkpath(downloadRoot())) {
        fail(QStringLiteral("Cannot create app-owned OCR download storage."));
        return false;
    }
    m_pendingKind = kind;
    m_pendingAsset = asset;
    m_cancelRequested = false;
    m_runtimeProcessPhase = RuntimeProcessPhase::None;
    m_bytesReceived = 0;
    m_bytesTotal = 0;
    setError({});
    if (!m_diagnostics.isEmpty()) {
        m_diagnostics.clear();
        emit diagnosticsChanged();
    }
    const QString cachedPath = QDir(downloadRoot()).filePath(asset.fileName);
    if (kind == PendingKind::Runtime && QFileInfo(cachedPath).isFile() &&
        normalizedSha(sha256File(cachedPath)) == normalizedSha(asset.sha256)) {
        appendDiagnostics(QStringLiteral("installer-cache"),
                          QStringLiteral("Reusing a previously verified installer cache for Retry."));
        beginInstaller(cachedPath);
        return m_pendingKind == PendingKind::Runtime;
    }
    setInstallState(Downloading);
    emit progressChanged();
    const QVariantMap metadata{{QStringLiteral("kind"), QStringLiteral("subtitleOcr")},
                               {QStringLiteral("component"), kind == PendingKind::Runtime
                                    ? QStringLiteral("runtime") : QStringLiteral("language")},
                               {QStringLiteral("language"), kind == PendingKind::Language ? asset.code : QString()},
                               {QStringLiteral("expectedBytes"), asset.bytes},
                               {QStringLiteral("sha256"), asset.sha256},
                               {QStringLiteral("version"), kTesseractVersion}};
    if (!m_downloads->enqueueUrl(asset.url, asset.fileName, downloadRoot(), metadata)) {
        m_pendingKind = PendingKind::None;
        m_pendingAsset = {};
        fail(QStringLiteral("The OCR download could not be queued. Check storage and retry."));
        return false;
    }
    updateTransferProgress();
    return true;
}

bool SubtitleOcrRuntimeService::installRuntime()
{
    m_lastAction = QStringLiteral("runtime");
    m_lastLanguage.clear();
    refresh();
    if (m_runtimeValid) return true;
    appendDiagnostics(QStringLiteral("runtime-package"),
                      QStringLiteral("No integrity-verified bundled Tesseract runtime was found. "
                                     "This application never executes the deprecated upstream installer."));
    fail(QStringLiteral("This package is missing a verified bundled Tesseract runtime. Repair or replace the package; no external installer was started."));
    return false;
}

bool SubtitleOcrRuntimeService::installLanguage(const QString &languageCode)
{
    const Asset asset = languageAsset(languageCode);
    if (asset.code.isEmpty()) {
        setError(QStringLiteral("That OCR language pack is not supported by the managed manifest."));
        return false;
    }
    m_lastAction = QStringLiteral("language");
    m_lastLanguage = asset.code;
    QString runtimeError;
    if (!hasUsablePackagedRuntime(&runtimeError)) {
        setError(QStringLiteral("Install a verified package-provisioned Tesseract runtime before adding language packs. LASTUDIO_TESSERACT is an advanced external override and is not modified. %1")
                 .arg(runtimeError));
        return false;
    }
    if (isLanguageInstalled(asset.code)) return true;
    return beginDownload(PendingKind::Language, asset);
}

bool SubtitleOcrRuntimeService::cancelInstallation()
{
    if (m_pendingKind == PendingKind::None) return false;
    m_cancelRequested = true;
    if (m_installState == Downloading) {
        if (!m_downloads || !m_downloads->cancel(m_pendingAsset.url, m_pendingAsset.fileName)) {
            m_cancelRequested = false;
            fail(QStringLiteral("The active OCR download could not be cancelled."));
            return false;
        }
        setError(QStringLiteral("Cancelling OCR download…"));
        return true;
    }
    if (m_installState == Installing) {
        m_installer.kill();
        return true;
    }
    return false;
}

bool SubtitleOcrRuntimeService::retryInstallation()
{
    if (m_pendingKind != PendingKind::None) return false;
    if (m_lastAction == QStringLiteral("runtime")) return installRuntime();
    if (m_lastAction == QStringLiteral("language")) return installLanguage(m_lastLanguage);
    setError(QStringLiteral("There is no OCR runtime action to retry."));
    return false;
}

bool SubtitleOcrRuntimeService::cleanFailedDownload()
{
    if (!canCleanFailedDownload()) {
        setError(QStringLiteral("There is no verified failed OCR installer cache to clean."));
        return false;
    }
    const QString path = m_failedDownloadPath;
    if (!QFile::remove(path)) {
        setError(QStringLiteral("Could not remove the failed OCR installer cache. Check file permissions and retry."));
        appendDiagnostics(QStringLiteral("cleanup"),
                          QStringLiteral("Failed to remove installer cache: %1").arg(path));
        return false;
    }
    m_failedDownloadPath.clear();
    setError(QStringLiteral("The failed OCR installer cache was removed. Retry installs a fresh verified copy."));
    appendDiagnostics(QStringLiteral("cleanup"),
                      QStringLiteral("Removed failed installer cache after an explicit user request: %1").arg(path));
    emit diagnosticsChanged();
    return true;
}

void SubtitleOcrRuntimeService::updateTransferProgress()
{
    if (m_pendingKind == PendingKind::None || !m_downloads) return;
    qint64 received = 0;
    qint64 total = 0;
    for (const QVariant &item : m_downloads->activeDownloads()) {
        const QVariantMap download = item.toMap();
        if (download.value(QStringLiteral("identifier")).toString() == m_pendingAsset.url &&
            download.value(QStringLiteral("filename")).toString() == m_pendingAsset.fileName) {
            received = download.value(QStringLiteral("bytesReceived")).toLongLong();
            total = download.value(QStringLiteral("bytesTotal")).toLongLong();
            break;
        }
    }
    if (m_bytesReceived == received && m_bytesTotal == total) return;
    m_bytesReceived = received;
    m_bytesTotal = total;
    emit progressChanged();
}

void SubtitleOcrRuntimeService::onDownloadFinished(const QString &identifier, const QString &filename,
                                                    const QString &localPath, const QVariantMap &)
{
    if (m_pendingKind == PendingKind::None || identifier != m_pendingAsset.url ||
        filename != m_pendingAsset.fileName) return;
    if (m_cancelRequested) {
        QFile::remove(localPath);
        completeCancelled();
        return;
    }
    if (normalizedSha(sha256File(localPath)) != normalizedSha(m_pendingAsset.sha256)) {
        QFile::remove(localPath);
        fail(QStringLiteral("OCR download SHA-256 verification failed; the existing runtime was not changed."));
        return;
    }
    if (m_pendingKind == PendingKind::Runtime) {
        QFile::remove(localPath);
        fail(QStringLiteral("Runtime installers are not supported. Install only a verified package-provisioned OCR runtime."));
        return;
    }

    QString errorMessage;
    const bool needsLegacyManifest = m_runtimeSource == QStringLiteral("managed");
    if (!replaceFileAtomically(localPath, languagePath(m_pendingAsset.code), m_pendingAsset.sha256, &errorMessage) ||
        (needsLegacyManifest && !writeInstallationManifest(runtimeRoot(), &errorMessage))) {
        QFile::remove(localPath);
        fail(errorMessage.isEmpty() ? QStringLiteral("Cannot install verified OCR language data.") : errorMessage);
        return;
    }
    QFile::remove(localPath);
    completePending();
    refresh();
}

void SubtitleOcrRuntimeService::onDownloadError(const QString &identifier, const QString &filename,
                                                 const QString &errorMessage)
{
    if (m_pendingKind == PendingKind::None || identifier != m_pendingAsset.url ||
        filename != m_pendingAsset.fileName) return;
    if (m_cancelRequested) {
        completeCancelled();
        return;
    }
    const QString component = m_pendingKind == PendingKind::Runtime
        ? QStringLiteral("runtime") : QStringLiteral("language");
    fail(QStringLiteral("OCR %1 download failed: %2").arg(component, errorMessage));
}

void SubtitleOcrRuntimeService::beginInstaller(const QString &installerPath)
{
    Q_UNUSED(installerPath);
    // Defence in depth: no public flow calls this legacy helper, but retain a
    // hard stop here so a future internal caller cannot re-enable execution of
    // the invalidly signed upstream installer by accident.
    appendDiagnostics(QStringLiteral("installer-disabled"),
                      QStringLiteral("Deprecated upstream Tesseract installer execution is disabled."));
    fail(QStringLiteral("Runtime installers are not supported. Install only a verified package-provisioned OCR runtime."));
}

void SubtitleOcrRuntimeService::onInstallerFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_pendingKind != PendingKind::Runtime) return;
    const QByteArray standardOutput = m_installer.readAllStandardOutput();
    const QByteArray standardError = m_installer.readAllStandardError();
    if (m_cancelRequested) {
        completeCancelled();
        return;
    }
    if (m_runtimeProcessPhase == RuntimeProcessPhase::Installer) {
        appendDiagnostics(QStringLiteral("installer-finished"),
                          QStringLiteral("exitCode=%1; exitStatus=%2; stdout=%3; stderr=%4")
                              .arg(exitCode)
                              .arg(exitStatus == QProcess::NormalExit ? QStringLiteral("NormalExit")
                                                                       : QStringLiteral("CrashExit"))
                              .arg(QString::fromLocal8Bit(standardOutput).trimmed(),
                                   QString::fromLocal8Bit(standardError).trimmed()));
        if (exitStatus != QProcess::NormalExit || exitCode != 0 ||
            !QFileInfo(QDir(m_stagingPath).filePath(
#ifdef Q_OS_WIN
                QStringLiteral("tesseract.exe")
#else
                QStringLiteral("tesseract")
#endif
            )).isFile()) {
            fail(QStringLiteral("Tesseract installer did not create a usable app-owned runtime. Retry or open diagnostics."));
            return;
        }
        if (!QDir().mkpath(QDir(m_stagingPath).filePath(QStringLiteral("tessdata")))) {
            fail(QStringLiteral("Tesseract installer did not provide a writable tessdata directory."));
            return;
        }
        beginRuntimeHealthCheck();
        return;
    }
    if (m_runtimeProcessPhase == RuntimeProcessPhase::HealthCheck) {
        appendDiagnostics(QStringLiteral("health-check-finished"),
                          QStringLiteral("exitCode=%1; exitStatus=%2; stdout=%3; stderr=%4")
                              .arg(exitCode)
                              .arg(exitStatus == QProcess::NormalExit ? QStringLiteral("NormalExit")
                                                                       : QStringLiteral("CrashExit"))
                              .arg(QString::fromLocal8Bit(standardOutput).trimmed(),
                                   QString::fromLocal8Bit(standardError).trimmed()));
        if (exitStatus != QProcess::NormalExit || exitCode != 0 ||
            !QString::fromLocal8Bit(standardOutput).contains(QStringLiteral("tesseract"), Qt::CaseInsensitive)) {
            fail(QStringLiteral("The installed Tesseract runtime failed its health check. Retry or open diagnostics."));
            return;
        }
        activateVerifiedRuntime();
        return;
    }
    fail(QStringLiteral("OCR runtime installer reached an unexpected process state."));
}

void SubtitleOcrRuntimeService::onInstallerError(QProcess::ProcessError error)
{
    if (m_pendingKind != PendingKind::Runtime) return;
    const QString phase = m_runtimeProcessPhase == RuntimeProcessPhase::HealthCheck
        ? QStringLiteral("health-check") : QStringLiteral("installer");
    appendDiagnostics(QStringLiteral("process-error"), processDiagnostics(phase, error));
    if (m_cancelRequested) {
        completeCancelled();
    } else {
        const QString noun = m_runtimeProcessPhase == RuntimeProcessPhase::HealthCheck
            ? QStringLiteral("installed Tesseract health check") : QStringLiteral("verified Tesseract installer");
        fail(QStringLiteral("The %1 could not be started. Retry or open diagnostics.").arg(noun));
    }
}

void SubtitleOcrRuntimeService::beginRuntimeHealthCheck()
{
    const QString executable = QDir(m_stagingPath).filePath(
#ifdef Q_OS_WIN
        QStringLiteral("tesseract.exe")
#else
        QStringLiteral("tesseract")
#endif
    );
    m_runtimeProcessPhase = RuntimeProcessPhase::HealthCheck;
    m_installer.setWorkingDirectory(PathUtils::toNativeShortPath(m_stagingPath));
    m_installer.setProgram(PathUtils::toNativeShortPath(executable));
    m_installer.setArguments({QStringLiteral("--version")});
    appendDiagnostics(QStringLiteral("health-check-start"), processDiagnostics(
        QStringLiteral("health-check"), QProcess::UnknownError));
    m_installer.start();
}

void SubtitleOcrRuntimeService::activateVerifiedRuntime()
{
    QString errorMessage;
    if (!writeInstallationManifest(m_stagingPath, &errorMessage) ||
        !replaceRuntimeAtomically(m_stagingPath, runtimeRoot(), &errorMessage)) {
        fail(errorMessage.isEmpty() ? QStringLiteral("Cannot atomically activate the OCR runtime.") : errorMessage);
        return;
    }
    m_stagingPath.clear();
    m_runtimeProcessPhase = RuntimeProcessPhase::None;
    if (isManagedDownloadPath(m_failedDownloadPath)) QFile::remove(m_failedDownloadPath);
    m_failedDownloadPath.clear();
    completePending();
    appendDiagnostics(QStringLiteral("activation"),
                      QStringLiteral("Activated a health-checked app-managed Tesseract runtime."));
    refresh();
}

void SubtitleOcrRuntimeService::completePending()
{
    m_pendingKind = PendingKind::None;
    m_pendingAsset = {};
    m_cancelRequested = false;
    m_runtimeProcessPhase = RuntimeProcessPhase::None;
    m_bytesReceived = 0;
    m_bytesTotal = 0;
    emit progressChanged();
}

void SubtitleOcrRuntimeService::completeCancelled()
{
    if (!m_stagingPath.isEmpty()) QDir(m_stagingPath).removeRecursively();
    m_stagingPath.clear();
    m_runtimeProcessPhase = RuntimeProcessPhase::None;
    completePending();
    setError(QStringLiteral("OCR installation was cancelled. The existing runtime was kept unchanged."));
    refresh();
}

void SubtitleOcrRuntimeService::fail(const QString &message)
{
    if (!m_stagingPath.isEmpty()) QDir(m_stagingPath).removeRecursively();
    m_stagingPath.clear();
    m_runtimeProcessPhase = RuntimeProcessPhase::None;
    m_pendingKind = PendingKind::None;
    m_pendingAsset = {};
    m_cancelRequested = false;
    m_bytesReceived = 0;
    m_bytesTotal = 0;
    emit progressChanged();
    setInstallState(Failed);
    setError(message);
    rebuildLanguagePacks();
}

} // namespace LAStudio
