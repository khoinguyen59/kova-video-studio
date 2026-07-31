#include "subtitles/SubtitleOcrRuntimeService.h"

#include "core/DownloadManager.h"
#include "core/PathUtils.h"
#include "subtitles/SubtitleOcrRuntimeLocator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace LAStudio {
namespace {

const QString kTesseractVersion = QStringLiteral("5.5.3.20260724");
const QString kTessdataCommit = QStringLiteral("87416418657359cb625c412a48b6e1d6d41c29bd");

QString normalizedSha(QString value)
{
    return value.trimmed().toLower();
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
    refresh();
}

SubtitleOcrRuntimeService::~SubtitleOcrRuntimeService()
{
    if (m_installer.state() != QProcess::NotRunning) m_installer.kill();
    if (!m_stagingPath.isEmpty()) QDir(m_stagingPath).removeRecursively();
}

QString SubtitleOcrRuntimeService::stateName() const
{
    switch (m_installState) {
    case Downloading: return QStringLiteral("Downloading");
    case Installing: return QStringLiteral("Installing");
    case Installed: return QStringLiteral("Installed");
    case Invalid: return QStringLiteral("Invalid");
    case Failed: return QStringLiteral("Failed");
    case Missing:
    default: return QStringLiteral("Missing");
    }
}

SubtitleOcrRuntimeService::Asset SubtitleOcrRuntimeService::runtimeAsset()
{
    return {
        QStringLiteral("tesseract-runtime"),
        QStringLiteral("Tesseract OCR for Windows (x64)"),
        QStringLiteral("tesseract-ocr-w64-setup-5.5.3.20260724.exe"),
        QStringLiteral("https://github.com/tesseract-ocr/tesseract/releases/download/5.5.3/"
                       "tesseract-ocr-w64-setup-5.5.3.20260724.exe"),
        QStringLiteral("bee9e3434bd94fd65387d9be28cd467a41f61b1275383b55b0f59a1331270ae4"),
        26573224,
    };
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
    const Asset asset = runtimeAsset();
    return {
        {QStringLiteral("id"), asset.code},
        {QStringLiteral("label"), asset.label},
        {QStringLiteral("version"), kTesseractVersion},
        {QStringLiteral("fileName"), asset.fileName},
        {QStringLiteral("url"), asset.url},
        {QStringLiteral("sha256"), asset.sha256},
        {QStringLiteral("bytes"), asset.bytes},
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
    if (!document.isObject() || root.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
        root.value(QStringLiteral("runtimeVersion")).toString() != kTesseractVersion) {
        if (errorMessage) *errorMessage = QStringLiteral("Managed runtime manifest is invalid or incompatible.");
        return false;
    }
    return true;
}

bool SubtitleOcrRuntimeService::writeInstallationManifest(const QString &installationRoot,
                                                          QString *errorMessage) const
{
    QJsonObject root{{QStringLiteral("schemaVersion"), 1},
                     {QStringLiteral("component"), QStringLiteral("Tesseract OCR")},
                     {QStringLiteral("runtimeVersion"), kTesseractVersion},
                     {QStringLiteral("runtimeInstallerSha256"), runtimeAsset().sha256},
                     {QStringLiteral("runtimeInstallerUrl"), runtimeAsset().url},
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
    if (asset.code.isEmpty() || m_runtimeSource != QStringLiteral("managed")) return false;
    return normalizedSha(sha256File(languagePath(asset.code))) == normalizedSha(asset.sha256);
}

void SubtitleOcrRuntimeService::rebuildLanguagePacks()
{
    QVariantList packs;
    const bool managed = m_runtimeSource == QStringLiteral("managed");
    for (const Asset &asset : languageAssets()) {
        const QString path = languagePath(asset.code);
        const bool exists = QFileInfo(path).isFile();
        const bool installed = managed && normalizedSha(sha256File(path)) == normalizedSha(asset.sha256);
        QString state = installed ? QStringLiteral("Installed")
            : exists && managed ? QStringLiteral("Invalid") : QStringLiteral("Missing");
        QString detail = installed ? QStringLiteral("Verified SHA-256; compatible with managed Tesseract %1.")
                                     .arg(kTesseractVersion)
            : managed ? QStringLiteral("Install this language pack before OCR uses it.")
            : QStringLiteral("Language packs are managed only for the app-owned runtime. External runtime languages are preflighted by Tesseract.");
        packs.append(QVariantMap{{QStringLiteral("code"), asset.code},
                                 {QStringLiteral("label"), asset.label},
                                 {QStringLiteral("fileName"), asset.fileName},
                                 {QStringLiteral("sha256"), asset.sha256},
                                 {QStringLiteral("url"), asset.url},
                                 {QStringLiteral("bytes"), asset.bytes},
                                 {QStringLiteral("installed"), installed},
                                 {QStringLiteral("state"), state},
                                 {QStringLiteral("detail"), detail},
                                 {QStringLiteral("compatibleRuntimeVersion"), kTesseractVersion}});
    }
    if (m_languagePacks == packs) return;
    m_languagePacks = packs;
    emit languagePacksChanged();
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
        } else {
            detectedState = Installed;
            version = QStringLiteral("external");
        }
    }
    const bool valid = detectedState == Installed;
    const bool changed = m_runtimePath != resolution.path || m_runtimeSource != resolution.source ||
        m_runtimeVersion != version || m_runtimeValid != valid;
    m_runtimePath = resolution.path;
    m_runtimeSource = resolution.source;
    m_runtimeVersion = version;
    m_runtimeValid = valid;
    if (m_pendingKind == PendingKind::None) setInstallState(detectedState);
    rebuildLanguagePacks();
    if (changed) emit runtimeChanged();
}

bool SubtitleOcrRuntimeService::beginDownload(PendingKind kind, const Asset &asset)
{
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
    m_bytesReceived = 0;
    m_bytesTotal = 0;
    setError({});
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
    return beginDownload(PendingKind::Runtime, runtimeAsset());
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
    if (m_runtimeSource != QStringLiteral("managed") || !hasValidManagedRuntime()) {
        setError(QStringLiteral("Install the app-managed Tesseract runtime before adding language packs. LASTUDIO_TESSERACT is an advanced external override and is not modified."));
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
        beginInstaller(localPath);
        return;
    }

    QString errorMessage;
    if (!replaceFileAtomically(localPath, languagePath(m_pendingAsset.code), m_pendingAsset.sha256, &errorMessage) ||
        !writeInstallationManifest(runtimeRoot(), &errorMessage)) {
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
    const QString parentRoot = QFileInfo(runtimeRoot()).dir().absolutePath();
    m_stagingPath = QDir(parentRoot).filePath(QStringLiteral("runtime.staging-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(m_stagingPath)) {
        QFile::remove(installerPath);
        fail(QStringLiteral("Cannot create staging storage for the verified OCR runtime."));
        return;
    }
    setInstallState(Installing);
    m_installer.setProgram(installerPath);
    // NSIS accepts /S and a final absolute /D= path.  The destination is
    // app-owned, so the installation never requests administrator privileges.
    m_installer.setArguments({QStringLiteral("/S"), QStringLiteral("/D=") + QDir::toNativeSeparators(m_stagingPath)});
    m_installer.start();
}

void SubtitleOcrRuntimeService::onInstallerFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_pendingKind != PendingKind::Runtime) return;
    const QString installerPath = m_installer.program();
    const QByteArray installerError = m_installer.readAllStandardError();
    if (m_cancelRequested) {
        QFile::remove(installerPath);
        completeCancelled();
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0 ||
        !QFileInfo(QDir(m_stagingPath).filePath(
#ifdef Q_OS_WIN
            QStringLiteral("tesseract.exe")
#else
            QStringLiteral("tesseract")
#endif
        )).isFile()) {
        QFile::remove(installerPath);
        fail(QStringLiteral("Tesseract installer did not create a usable app-owned runtime%1")
             .arg(installerError.isEmpty() ? QStringLiteral(".")
                  : QStringLiteral(": %1").arg(QString::fromLocal8Bit(installerError).trimmed())));
        return;
    }
    if (!QDir().mkpath(QDir(m_stagingPath).filePath(QStringLiteral("tessdata")))) {
        QFile::remove(installerPath);
        fail(QStringLiteral("Tesseract installer did not provide a writable tessdata directory."));
        return;
    }
    QString errorMessage;
    if (!writeInstallationManifest(m_stagingPath, &errorMessage) ||
        !replaceRuntimeAtomically(m_stagingPath, runtimeRoot(), &errorMessage)) {
        QFile::remove(installerPath);
        fail(errorMessage.isEmpty() ? QStringLiteral("Cannot atomically activate the OCR runtime.") : errorMessage);
        return;
    }
    m_stagingPath.clear();
    QFile::remove(installerPath);
    completePending();
    refresh();
}

void SubtitleOcrRuntimeService::onInstallerError(QProcess::ProcessError error)
{
    if (m_pendingKind != PendingKind::Runtime || error != QProcess::FailedToStart) return;
    if (m_cancelRequested) {
        completeCancelled();
    } else {
        fail(QStringLiteral("Verified Tesseract installer could not be started."));
    }
}

void SubtitleOcrRuntimeService::completePending()
{
    m_pendingKind = PendingKind::None;
    m_pendingAsset = {};
    m_cancelRequested = false;
    m_bytesReceived = 0;
    m_bytesTotal = 0;
    emit progressChanged();
}

void SubtitleOcrRuntimeService::completeCancelled()
{
    if (!m_stagingPath.isEmpty()) QDir(m_stagingPath).removeRecursively();
    m_stagingPath.clear();
    completePending();
    setError(QStringLiteral("OCR installation was cancelled. The existing runtime was kept unchanged."));
    refresh();
}

void SubtitleOcrRuntimeService::fail(const QString &message)
{
    if (!m_stagingPath.isEmpty()) QDir(m_stagingPath).removeRecursively();
    m_stagingPath.clear();
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
