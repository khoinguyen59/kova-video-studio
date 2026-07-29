#include "AppUpdateService.h"

#include "core/DownloadManager.h"
#include "core/Logger.h"
#include "core/PathUtils.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QVersionNumber>

#include <cmath>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace LAStudio {
namespace {

constexpr auto ReleasesApiUrl = "https://api.github.com/repos/dduongtrandai/LA-Studio/releases?per_page=30";
constexpr auto UpdateMetadataType = "app-update";

QString cleanVersion(QString version)
{
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        version.remove(0, 1);
    }
    return version;
}

bool isInstallerAssetName(const QString &name)
{
    const QString lower = name.toLower();
    return lower.endsWith(QStringLiteral(".exe"))
        && lower.contains(QStringLiteral("setup"))
        && lower.contains(QStringLiteral("windows"))
        && lower.contains(QStringLiteral("x64"));
}

QString parsePublishedSha256(const QByteArray &contents, const QString &expectedFileName)
{
    const QRegularExpression linePattern(
        QStringLiteral(R"(^\s*([A-Fa-f0-9]{64})\s+\*?(.+?)\s*$)"));
    const QStringList lines = QString::fromUtf8(contents).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QRegularExpressionMatch match = linePattern.match(line);
        if (!match.hasMatch()) {
            continue;
        }
        const QString publishedName = QFileInfo(match.captured(2).trimmed()).fileName();
        if (publishedName == expectedFileName) {
            return match.captured(1).toLower();
        }
    }
    return QString();
}

struct ParsedUpdateVersion {
    QVersionNumber core;
    QStringList prerelease;
    bool valid = false;
};

ParsedUpdateVersion parseUpdateVersion(const QString &version)
{
    const QRegularExpression pattern(
        QStringLiteral(R"(^(\d+\.\d+\.\d+\.\d+)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$)"));
    const QRegularExpressionMatch match = pattern.match(cleanVersion(version));
    if (!match.hasMatch()) {
        return {};
    }

    const QStringList coreParts = match.captured(1).split(QLatin1Char('.'));
    for (const QString &part : coreParts) {
        if (part.size() > 1 && part.startsWith(QLatin1Char('0'))) {
            return {};
        }
    }

    ParsedUpdateVersion parsed;
    parsed.core = QVersionNumber::fromString(match.captured(1));
    parsed.prerelease = match.captured(2).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (const QString &identifier : parsed.prerelease) {
        const bool numeric = QRegularExpression(QStringLiteral("^[0-9]+$")).match(identifier).hasMatch();
        if (numeric && identifier.size() > 1 && identifier.startsWith(QLatin1Char('0'))) {
            return {};
        }
    }
    parsed.valid = !parsed.core.isNull();
    return parsed;
}

int comparePrereleaseIdentifier(const QString &left, const QString &right)
{
    const QRegularExpression numericPattern(QStringLiteral("^[0-9]+$"));
    const bool leftNumeric = numericPattern.match(left).hasMatch();
    const bool rightNumeric = numericPattern.match(right).hasMatch();
    if (leftNumeric && rightNumeric) {
        if (left.size() != right.size()) {
            return left.size() < right.size() ? -1 : 1;
        }
        return QString::compare(left, right, Qt::CaseSensitive);
    }
    if (leftNumeric != rightNumeric) {
        return leftNumeric ? -1 : 1;
    }
    return QString::compare(left, right, Qt::CaseSensitive);
}

int compareUpdateVersions(const ParsedUpdateVersion &left, const ParsedUpdateVersion &right)
{
    const int coreComparison = QVersionNumber::compare(left.core, right.core);
    if (coreComparison != 0) {
        return coreComparison;
    }
    if (left.prerelease.isEmpty() != right.prerelease.isEmpty()) {
        return left.prerelease.isEmpty() ? 1 : -1;
    }
    const int commonCount = qMin(left.prerelease.size(), right.prerelease.size());
    for (int index = 0; index < commonCount; ++index) {
        const int comparison = comparePrereleaseIdentifier(left.prerelease.at(index), right.prerelease.at(index));
        if (comparison != 0) {
            return comparison;
        }
    }
    if (left.prerelease.size() == right.prerelease.size()) {
        return 0;
    }
    return left.prerelease.size() < right.prerelease.size() ? -1 : 1;
}

#ifdef Q_OS_WIN
QString windowsShellExecuteErrorText(DWORD errorCode)
{
    switch (errorCode) {
    case ERROR_CANCELLED:
        return QObject::tr("Windows elevation was cancelled.");
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return QObject::tr("Downloaded installer was not found.");
    case ERROR_ACCESS_DENIED:
        return QObject::tr("Windows denied permission to start the installer.");
    default:
        return QObject::tr("Windows failed to start the installer. Error code: %1").arg(errorCode);
    }
}

bool startWindowsInstallerElevated(const QString &installerPath,
                                   const QString &arguments,
                                   QString *errorMessage)
{
    const QString nativeInstaller = QDir::toNativeSeparators(installerPath);
    const QString nativeWorkDir = QDir::toNativeSeparators(QFileInfo(installerPath).absolutePath());

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = reinterpret_cast<LPCWSTR>(nativeInstaller.utf16());
    info.lpParameters = reinterpret_cast<LPCWSTR>(arguments.utf16());
    info.lpDirectory = reinterpret_cast<LPCWSTR>(nativeWorkDir.utf16());
    info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&info)) {
        if (errorMessage) {
            *errorMessage = windowsShellExecuteErrorText(GetLastError());
        }
        return false;
    }

    if (info.hProcess) {
        CloseHandle(info.hProcess);
    }
    return true;
}
#endif

} // namespace

AppUpdateService::AppUpdateService(DownloadManager *downloads, QObject *parent)
    : QObject(parent)
    , m_downloads(downloads)
    , m_network(new QNetworkAccessManager(this))
{
    if (m_downloads) {
        connect(m_downloads, &DownloadManager::finished,
                this, &AppUpdateService::onDownloadFinished);
        connect(m_downloads, &DownloadManager::error,
                this, &AppUpdateService::onDownloadError);
        connect(m_downloads, &DownloadManager::activeDownloadsChanged,
                this, &AppUpdateService::updateDownloadProgress);
        connect(m_downloads, &DownloadManager::allDownloadsChanged,
                this, &AppUpdateService::updateDownloadProgress);
    }

}

bool AppUpdateService::isUpdateVersionNewer(const QString &candidate, const QString &installed)
{
    const ParsedUpdateVersion candidateVersion = parseUpdateVersion(candidate);
    const ParsedUpdateVersion installedVersion = parseUpdateVersion(installed);
    return candidateVersion.valid && installedVersion.valid
        && compareUpdateVersions(candidateVersion, installedVersion) > 0;
}

QString AppUpdateService::currentVersion() const
{
    return QCoreApplication::applicationVersion();
}

void AppUpdateService::checkForUpdates(const QString &channel)
{
    if (m_checking) return;

    resetUpdateInfo();
    clearError();
    setChecking(true);
    setStatusMessage(tr("Checking GitHub releases..."));

    QNetworkRequest request(QUrl(QString::fromLatin1(ReleasesApiUrl)));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LA-Studio-Updater"));
    request.setRawHeader("Accept", "application/vnd.github+json");

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, channel]() {
        handleReleaseReply(reply, channel);
        reply->deleteLater();
    });
}

void AppUpdateService::downloadUpdate()
{
    if (!m_downloads || m_installerUrl.isEmpty() || m_installerFileName.isEmpty()) {
        setErrorMessage(tr("No update installer is available to download."));
        return;
    }

    if (m_downloading || downloaded()) return;

    QVariantMap metadata;
    metadata.insert(QStringLiteral("type"), QString::fromLatin1(UpdateMetadataType));
    metadata.insert(QStringLiteral("version"), m_latestVersion);
    metadata.insert(QStringLiteral("releaseUrl"), m_releaseUrl.toString());

    m_downloading = true;
    m_downloadProgress = 0.0;
    m_downloadedInstallerPath.clear();
    emit downloadStateChanged();

    QDir().mkpath(installerDownloadDir());
    setStatusMessage(tr("Downloading LA Studio v%1...").arg(m_latestVersion));
    m_downloads->enqueueUrl(m_installerUrl.toString(), m_installerFileName, installerDownloadDir(), metadata);
}

void AppUpdateService::installDownloadedUpdate()
{
    if (m_downloadedInstallerPath.isEmpty() || !QFileInfo::exists(m_downloadedInstallerPath)) {
        setErrorMessage(tr("Downloaded installer was not found."));
        return;
    }

    Logger::info(QStringLiteral("Updater"),
                 QStringLiteral("Starting downloaded installer: %1").arg(m_downloadedInstallerPath));

#ifdef Q_OS_WIN
    const QString installerLogPath = QDir::toNativeSeparators(
        installerDownloadDir() + QStringLiteral("/install-%1.log").arg(m_latestVersion));
    const QString arguments = QStringLiteral(
        "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS /LOG=\"%1\"")
        .arg(installerLogPath);
    QString startError;
    const bool started = startWindowsInstallerElevated(m_downloadedInstallerPath, arguments, &startError);
#else
    const bool started = QProcess::startDetached(m_downloadedInstallerPath, {});
#endif

    if (!started) {
#ifdef Q_OS_WIN
        setErrorMessage(startError.isEmpty() ? tr("Failed to start the update installer.") : startError);
#else
        setErrorMessage(tr("Failed to start the update installer."));
#endif
        return;
    }

    setStatusMessage(tr("Installer started. LA Studio will close now."));
    QTimer::singleShot(250, qApp, &QCoreApplication::quit);
}

void AppUpdateService::clearError()
{
    setErrorMessage(QString());
}

void AppUpdateService::onDownloadFinished(const QString &identifier, const QString &filename,
                                          const QString &localPath, const QVariantMap &metadata)
{
    Q_UNUSED(identifier)

    if (metadata.value(QStringLiteral("type")).toString() != QString::fromLatin1(UpdateMetadataType)) return;
    if (filename != m_installerFileName) return;

    QString checksumError;
    if (!verifyInstallerChecksum(localPath, &checksumError)) {
        QFile::remove(localPath);
        m_downloading = false;
        m_downloadProgress = 0.0;
        m_downloadedInstallerPath.clear();
        setStatusMessage(tr("Downloaded update was rejected."));
        setErrorMessage(checksumError);
        emit downloadStateChanged();
        return;
    }

    m_downloading = false;
    m_downloadProgress = 1.0;
    m_downloadedInstallerPath = localPath;
    setStatusMessage(tr("LA Studio v%1 is ready to install.").arg(m_latestVersion));
    emit downloadStateChanged();
}

void AppUpdateService::onDownloadError(const QString &identifier, const QString &filename,
                                       const QString &errorMsg)
{
    Q_UNUSED(identifier)

    if (filename != m_installerFileName) return;

    m_downloading = false;
    m_downloadProgress = 0.0;
    emit downloadStateChanged();
    setErrorMessage(errorMsg);
}

void AppUpdateService::setChecking(bool checking)
{
    if (m_checking == checking) return;
    m_checking = checking;
    emit checkingChanged();
}

void AppUpdateService::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

void AppUpdateService::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) return;
    m_errorMessage = message;
    emit errorMessageChanged();
    if (!message.isEmpty()) {
        emit errorOccurred(message);
    }
}

void AppUpdateService::resetUpdateInfo()
{
    m_latestVersion.clear();
    m_installerFileName.clear();
    m_expectedInstallerSha256.clear();
    m_installerUrl = QUrl();
    m_releaseUrl = QUrl();
    m_downloading = false;
    m_downloadProgress = 0.0;
    m_downloadedInstallerPath.clear();
    emit updateInfoChanged();
    emit downloadStateChanged();
}

void AppUpdateService::handleReleaseReply(QNetworkReply *reply, const QString &channel)
{
    if (reply->error() != QNetworkReply::NoError) {
        setChecking(false);
        setStatusMessage(tr("Update check failed."));
        setErrorMessage(reply->errorString());
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        setChecking(false);
        setStatusMessage(tr("Update check failed."));
        setErrorMessage(tr("GitHub returned an invalid releases response."));
        return;
    }

    const bool includePrereleases = channel.compare(QStringLiteral("beta"), Qt::CaseInsensitive) == 0
        || channel.compare(QStringLiteral("Beta"), Qt::CaseInsensitive) == 0;
    QString bestVersionText;
    QJsonObject bestRelease;
    QJsonObject bestAsset;
    QJsonObject bestChecksumAsset;

    const QJsonArray releases = doc.array();
    for (const QJsonValue &releaseValue : releases) {
        const QJsonObject release = releaseValue.toObject();
        if (release.value(QStringLiteral("draft")).toBool()) continue;
        if (!includePrereleases && release.value(QStringLiteral("prerelease")).toBool()) continue;

        const QString versionText = cleanVersion(release.value(QStringLiteral("tag_name")).toString());
        if (!isUpdateVersionNewer(versionText, currentVersion())
            || (!bestVersionText.isEmpty() && !isUpdateVersionNewer(versionText, bestVersionText))) {
            continue;
        }

        QJsonObject installerAsset;
        QJsonObject checksumAsset;
        const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue &assetValue : assets) {
            const QJsonObject asset = assetValue.toObject();
            if (isInstallerAssetName(asset.value(QStringLiteral("name")).toString())) {
                installerAsset = asset;
            }
        }
        if (installerAsset.isEmpty()) continue;

        const QString checksumName = installerAsset.value(QStringLiteral("name")).toString()
            + QStringLiteral(".sha256");
        for (const QJsonValue &assetValue : assets) {
            const QJsonObject asset = assetValue.toObject();
            if (asset.value(QStringLiteral("name")).toString() == checksumName) {
                checksumAsset = asset;
                break;
            }
        }
        if (checksumAsset.isEmpty()) continue;

        bestVersionText = versionText;
        bestRelease = release;
        bestAsset = installerAsset;
        bestChecksumAsset = checksumAsset;
    }

    if (bestRelease.isEmpty()) {
        setChecking(false);
        Logger::info(QStringLiteral("Updater"),
                     QStringLiteral("No app update found. Current version: %1").arg(currentVersion()));
        setStatusMessage(tr("LA Studio is up to date."));
        emit updateInfoChanged();
        return;
    }

    fetchInstallerChecksum(
        QUrl(bestChecksumAsset.value(QStringLiteral("browser_download_url")).toString()),
        bestVersionText,
        QUrl(bestRelease.value(QStringLiteral("html_url")).toString()),
        QUrl(bestAsset.value(QStringLiteral("browser_download_url")).toString()),
        bestAsset.value(QStringLiteral("name")).toString());
}

void AppUpdateService::fetchInstallerChecksum(const QUrl &checksumUrl, const QString &version,
                                              const QUrl &releaseUrl, const QUrl &installerUrl,
                                              const QString &installerFileName)
{
    if (!checksumUrl.isValid() || checksumUrl.scheme() != QStringLiteral("https")
        || !installerUrl.isValid() || installerUrl.scheme() != QStringLiteral("https")
        || installerFileName.isEmpty()) {
        setChecking(false);
        setStatusMessage(tr("Update check failed."));
        setErrorMessage(tr("Published update metadata contains an invalid URL or installer name."));
        return;
    }

    QNetworkRequest request(checksumUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LA-Studio-Updater"));
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, version, releaseUrl, installerUrl, installerFileName]() {
        setChecking(false);
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(tr("Update check failed."));
            setErrorMessage(tr("Could not retrieve the published update checksum: %1")
                                .arg(reply->errorString()));
            reply->deleteLater();
            return;
        }

        const QString checksum = parsePublishedSha256(reply->readAll(), installerFileName);
        reply->deleteLater();
        if (checksum.isEmpty()) {
            setStatusMessage(tr("Update check failed."));
            setErrorMessage(tr("Published update checksum is invalid or does not match the installer name."));
            return;
        }

        m_latestVersion = version;
        m_installerFileName = installerFileName;
        m_installerUrl = installerUrl;
        m_releaseUrl = releaseUrl;
        m_expectedInstallerSha256 = checksum;

        Logger::info(QStringLiteral("Updater"),
                     QStringLiteral("Verified update metadata for: %1 (%2)")
                         .arg(m_latestVersion, m_installerUrl.toString()));

        const QString existingInstaller = existingDownloadedInstallerPath(m_latestVersion, m_installerFileName);
        QString checksumError;
        if (!existingInstaller.isEmpty() && verifyInstallerChecksum(existingInstaller, &checksumError)) {
            m_downloadedInstallerPath = existingInstaller;
            m_downloadProgress = 1.0;
            setStatusMessage(tr("LA Studio v%1 is ready to install.").arg(m_latestVersion));
            emit downloadStateChanged();
        } else {
            if (!existingInstaller.isEmpty()) {
                Logger::warning(QStringLiteral("Updater"),
                                QStringLiteral("Discarding cached installer with invalid checksum: %1")
                                    .arg(checksumError));
                QFile::remove(existingInstaller);
            }
            setStatusMessage(tr("LA Studio v%1 is available.").arg(m_latestVersion));
        }
        emit updateInfoChanged();
    });
}

bool AppUpdateService::verifyInstallerChecksum(const QString &installerPath, QString *errorMessage) const
{
    if (!QRegularExpression(QStringLiteral("^[a-f0-9]{64}$")).match(m_expectedInstallerSha256).hasMatch()) {
        if (errorMessage) {
            *errorMessage = tr("No valid published checksum is available for this update.");
        }
        return false;
    }

    QFile installer(installerPath);
    if (!installer.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = tr("Downloaded installer could not be opened for verification.");
        }
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!installer.atEnd()) {
        const QByteArray chunk = installer.read(1024 * 1024);
        if (chunk.isEmpty() && installer.error() != QFile::NoError) {
            if (errorMessage) {
                *errorMessage = tr("Downloaded installer could not be read for verification.");
            }
            return false;
        }
        hash.addData(chunk);
    }

    const QString actual = QString::fromLatin1(hash.result().toHex());
    if (actual != m_expectedInstallerSha256) {
        if (errorMessage) {
            *errorMessage = tr("Downloaded installer SHA-256 did not match the published release checksum.");
        }
        return false;
    }
    return true;
}

void AppUpdateService::updateDownloadProgress()
{
    if (!m_downloads || m_installerUrl.isEmpty() || m_installerFileName.isEmpty()) return;

    bool active = false;
    qreal progress = m_downloadProgress;
    const QVariantList downloads = m_downloads->allDownloads();
    for (const QVariant &value : downloads) {
        const QVariantMap item = value.toMap();
        const QVariantMap metadata = item.value(QStringLiteral("metadata")).toMap();
        if (metadata.value(QStringLiteral("type")).toString() != QString::fromLatin1(UpdateMetadataType)) continue;
        if (metadata.value(QStringLiteral("version")).toString() != m_latestVersion) continue;

        const qint64 received = item.value(QStringLiteral("bytesReceived")).toLongLong();
        const qint64 total = item.value(QStringLiteral("bytesTotal")).toLongLong();
        if (total > 0) progress = static_cast<qreal>(received) / static_cast<qreal>(total);
        active = item.value(QStringLiteral("status")).toString() == QStringLiteral("downloading");
        break;
    }

    if (m_downloading != active || std::abs(m_downloadProgress - progress) > 0.0001) {
        m_downloading = active;
        m_downloadProgress = progress;
        emit downloadStateChanged();
    }
}

QString AppUpdateService::installerDownloadDir() const
{
    return PathUtils::dataDir() + QStringLiteral("/updates");
}

QString AppUpdateService::existingDownloadedInstallerPath(const QString &version, const QString &filename) const
{
    if (!m_downloads || version.isEmpty() || filename.isEmpty()) return QString();

    const QVariantList downloads = m_downloads->allDownloads();
    for (const QVariant &value : downloads) {
        const QVariantMap item = value.toMap();
        const QVariantMap metadata = item.value(QStringLiteral("metadata")).toMap();
        if (metadata.value(QStringLiteral("type")).toString() != QString::fromLatin1(UpdateMetadataType)) continue;
        if (metadata.value(QStringLiteral("version")).toString() != version) continue;
        if (item.value(QStringLiteral("filename")).toString() != filename) continue;
        if (item.value(QStringLiteral("status")).toString() != QStringLiteral("completed")) continue;

        const QString localPath = item.value(QStringLiteral("localPath")).toString();
        if (QFileInfo::exists(localPath)) {
            return localPath;
        }
    }

    return QString();
}

} // namespace LAStudio
