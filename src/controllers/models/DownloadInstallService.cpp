#include "DownloadInstallService.h"

#include "core/Settings.h"
#include "core/DownloadManager.h"
#include "core/ModelManager.h"
#include "core/RuntimeManager.h"
#include "core/Logger.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPointer>
#include <QMetaType>
#include <QDateTime>
#include <QThreadPool>
#include <QCryptographicHash>
#include <QVersionNumber>
#include <QCoreApplication>
#include <QDirIterator>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QUrl>

#include <limits>

#include <curl/curl.h>

#ifdef Q_OS_WIN
#include <windows.h>
#include <softpub.h>
#include <wintrust.h>
#endif

namespace LAStudio {

namespace {
QVersionNumber parsedRuntimeVersion(QString version)
{
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        version.remove(0, 1);
    return QVersionNumber::fromString(version);
}

bool runtimeVersionGreater(const QString &left, const QString &right)
{
    if (right.isEmpty())
        return !left.isEmpty();
    const QVersionNumber leftVersion = parsedRuntimeVersion(left);
    const QVersionNumber rightVersion = parsedRuntimeVersion(right);
    if (!leftVersion.isNull() && !rightVersion.isNull())
        return QVersionNumber::compare(leftVersion, rightVersion) > 0;
    return QString::compare(left, right, Qt::CaseInsensitive) > 0;
}

size_t discardBodyCallback(char *ptr, size_t size, size_t nmemb, void *)
{
    Q_UNUSED(ptr);
    return size * nmemb;
}

size_t metadataHeaderCallback(char *buffer, size_t size, size_t nitems, void *userdata)
{
    const size_t total = size * nitems;
    auto *headers = static_cast<QVariantMap *>(userdata);
    const QString line = QString::fromUtf8(buffer, static_cast<qsizetype>(total)).trimmed();
    const int separator = line.indexOf(QLatin1Char(':'));
    if (separator <= 0) {
        return total;
    }

    const QString key = line.left(separator).trimmed().toLower();
    const QString value = line.mid(separator + 1).trimmed();
    if (key == QStringLiteral("x-repo-commit")) {
        headers->insert(QStringLiteral("repoCommit"), value);
    } else if (key == QStringLiteral("etag")) {
        headers->insert(QStringLiteral("etag"), value);
    } else if (key == QStringLiteral("x-linked-etag")) {
        headers->insert(QStringLiteral("linkedEtag"), value);
    } else if (key == QStringLiteral("x-linked-size")) {
        headers->insert(QStringLiteral("linkedSize"), value.toLongLong());
    } else if (key == QStringLiteral("content-length")) {
        headers->insert(QStringLiteral("contentLength"), value.toLongLong());
    } else if (key == QStringLiteral("x-xet-hash")) {
        headers->insert(QStringLiteral("xetHash"), value);
    }
    return total;
}

QString cleanFingerprint(QString value)
{
    value = value.trimmed();
    if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')) && value.size() > 1) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

QString archiveExtractor(const QString &name)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString bundled = QDir(appDir).absoluteFilePath(name);
    return QFileInfo(bundled).isExecutable() ? bundled : QString();
}

QString systemMsiexecPath()
{
#ifdef Q_OS_WIN
    wchar_t systemDirectory[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    const QString path = QDir(QString::fromWCharArray(systemDirectory))
                             .absoluteFilePath(QStringLiteral("msiexec.exe"));
    return QFileInfo::exists(path) ? path : QString();
#else
    return {};
#endif
}

bool hasTrustedAuthenticodeSignature(const QString &path)
{
#ifdef Q_OS_WIN
    WINTRUST_FILE_INFO fileInfo = {};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = reinterpret_cast<LPCWSTR>(path.utf16());

    WINTRUST_DATA trustData = {};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;

    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG verification = WinVerifyTrust(nullptr, &policy, &trustData);
    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &trustData);
    return verification == ERROR_SUCCESS;
#else
    Q_UNUSED(path);
    return false;
#endif
}

QString fileFingerprint(const QVariantMap &metadata)
{
    QString value = metadata.value(QStringLiteral("linkedEtag")).toString();
    if (value.isEmpty()) value = metadata.value(QStringLiteral("etag")).toString();
    if (value.isEmpty()) value = metadata.value(QStringLiteral("xetHash")).toString();
    return cleanFingerprint(value);
}

qint64 remoteSize(const QVariantMap &metadata)
{
    qint64 value = metadata.value(QStringLiteral("linkedSize")).toLongLong();
    if (value <= 0) value = metadata.value(QStringLiteral("contentLength")).toLongLong();
    return value;
}

QVariantMap fetchRemoteFileMetadata(const QString &modelId, const QString &filename)
{
    QVariantMap metadata;
    if (modelId.isEmpty() || filename.isEmpty()) {
        return metadata;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        return metadata;
    }

    const QString url = QStringLiteral("https://huggingface.co/%1/resolve/main/%2").arg(modelId, filename);
    const QByteArray urlBytes = url.toUtf8();
    curl_easy_setopt(curl, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardBodyCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, metadataHeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &metadata);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "LAStudio/0.1");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    const CURLcode res = curl_easy_perform(curl);
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || responseCode >= 400) {
        metadata.clear();
        return metadata;
    }

    metadata.insert(QStringLiteral("provider"), QStringLiteral("huggingface"));
    metadata.insert(QStringLiteral("modelId"), modelId);
    metadata.insert(QStringLiteral("filename"), filename);
    metadata.insert(QStringLiteral("sourceUrl"), url);
    metadata.insert(QStringLiteral("checkedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    metadata.insert(QStringLiteral("fingerprint"), fileFingerprint(metadata));
    return metadata;
}

bool hasExpectedArchiveSignature(const QString &path, const QString &filename)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray header = file.read(4);
    if (filename.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        return header.startsWith("PK");
    }
    if (filename.endsWith(QStringLiteral(".tar.gz"), Qt::CaseInsensitive) ||
        filename.endsWith(QStringLiteral(".tgz"), Qt::CaseInsensitive)) {
        return header.size() >= 2 &&
               static_cast<unsigned char>(header[0]) == 0x1f &&
               static_cast<unsigned char>(header[1]) == 0x8b;
    }
    if (filename.endsWith(QStringLiteral(".tar.bz2"), Qt::CaseInsensitive) ||
        filename.endsWith(QStringLiteral(".tbz2"), Qt::CaseInsensitive)) {
        return header.size() >= 3 &&
               header[0] == 'B' &&
               header[1] == 'Z' &&
               header[2] == 'h';
    }
    return true;
}

QString yamlScalar(const QVariant &value)
{
    if (!value.isValid() || value.isNull()) return QStringLiteral("null");
    if (value.typeId() == QMetaType::Bool) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.canConvert<int>() && value.typeId() != QMetaType::QString) return value.toString();

    QString text = value.toString();
    text.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    text.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(text);
}

void appendYamlValue(QStringList &lines, const QVariant &value, int indent);

void appendYamlMap(QStringList &lines, const QVariantMap &map, int indent)
{
    const QString pad(indent, QLatin1Char(' '));
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        const QVariant value = it.value();
        if (value.typeId() == QMetaType::QVariantMap || value.typeId() == QMetaType::QVariantList) {
            lines << pad + it.key() + QStringLiteral(":");
            appendYamlValue(lines, value, indent + 2);
        } else {
            lines << pad + it.key() + QStringLiteral(": ") + yamlScalar(value);
        }
    }
}

void appendYamlList(QStringList &lines, const QVariantList &list, int indent)
{
    const QString pad(indent, QLatin1Char(' '));
    for (const QVariant &item : list) {
        if (item.typeId() == QMetaType::QVariantMap) {
            lines << pad + QStringLiteral("-");
            appendYamlMap(lines, item.toMap(), indent + 2);
        } else if (item.typeId() == QMetaType::QVariantList) {
            lines << pad + QStringLiteral("-");
            appendYamlList(lines, item.toList(), indent + 2);
        } else {
            lines << pad + QStringLiteral("- ") + yamlScalar(item);
        }
    }
}

void appendYamlValue(QStringList &lines, const QVariant &value, int indent)
{
    if (value.typeId() == QMetaType::QVariantMap) {
        appendYamlMap(lines, value.toMap(), indent);
    } else if (value.typeId() == QMetaType::QVariantList) {
        appendYamlList(lines, value.toList(), indent);
    } else {
        lines << QString(indent, QLatin1Char(' ')) + yamlScalar(value);
    }
}

QString modelYamlText(const QVariantMap &modelYaml)
{
    QStringList lines;
    QVariantMap remaining = modelYaml;
    const QStringList orderedKeys = {
        QStringLiteral("model"),
        QStringLiteral("base"),
        QStringLiteral("metadataOverrides"),
        QStringLiteral("config"),
        QStringLiteral("customFields"),
        QStringLiteral("suggestions")
    };

    for (const QString &key : orderedKeys) {
        if (!remaining.contains(key)) continue;
        const QVariant value = remaining.take(key);
        if (value.typeId() == QMetaType::QVariantMap || value.typeId() == QMetaType::QVariantList) {
            lines << key + QStringLiteral(":");
            appendYamlValue(lines, value, 2);
        } else {
            lines << key + QStringLiteral(": ") + yamlScalar(value);
        }
    }
    appendYamlMap(lines, remaining, 0);
    return lines.join(QStringLiteral("\n")) + QStringLiteral("\n");
}

QVariantMap virtualModelMetadata(const QVariantMap &family)
{
    return {
        {QStringLiteral("kind"), QStringLiteral("modelFile")},
        {QStringLiteral("familyId"), family.value(QStringLiteral("id")).toString()},
        {QStringLiteral("virtualModelId"), family.value(QStringLiteral("modelId")).toString()},
        {QStringLiteral("modelYaml"), family.value(QStringLiteral("modelYaml")).toMap()},
        {QStringLiteral("hubFiles"), family.value(QStringLiteral("hubFiles")).toMap()}
    };
}

bool writeTextFile(const QString &path, const QByteArray &content, QIODevice::OpenMode mode = QIODevice::Text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | mode)) {
        return false;
    }
    file.write(content);
    file.close();
    return true;
}

bool writeVirtualModelFilesToDisk(ModelManager *models, const QVariantMap &metadata, QString *errorMessage = nullptr)
{
    if (!models) {
        if (errorMessage) *errorMessage = QStringLiteral("Model manager is not available");
        return false;
    }

    const QString virtualModelId = metadata.value(QStringLiteral("virtualModelId")).toString();
    const QVariantMap modelYaml = metadata.value(QStringLiteral("modelYaml")).toMap();
    const QVariantMap hubFiles = metadata.value(QStringLiteral("hubFiles")).toMap();
    if (virtualModelId.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Virtual model id is empty");
        return false;
    }
    if (modelYaml.isEmpty() && hubFiles.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Virtual model metadata is empty");
        return false;
    }

    const QString virtualDir = models->virtualModelDir(virtualModelId);
    if (!QDir().mkpath(virtualDir)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to create virtual model directory: %1").arg(virtualDir);
        }
        return false;
    }

    bool ok = true;

    if (!modelYaml.isEmpty()) {
        if (!writeTextFile(QDir(virtualDir).absoluteFilePath(QStringLiteral("model.yaml")),
                           modelYamlText(modelYaml).toUtf8())) {
            ok = false;
            Logger::error(QStringLiteral("DownloadInstallService"),
                          QStringLiteral("Failed to write virtual model.yaml for %1").arg(virtualModelId));
        }
    }

    const QVariantMap manifest = hubFiles.value(QStringLiteral("manifest")).toMap();
    if (!manifest.isEmpty()) {
        const QJsonDocument doc(QJsonObject::fromVariantMap(manifest));
        if (!writeTextFile(QDir(virtualDir).absoluteFilePath(QStringLiteral("manifest.json")), doc.toJson(QJsonDocument::Indented))) {
            ok = false;
            Logger::error(QStringLiteral("DownloadInstallService"),
                          QStringLiteral("Failed to write virtual manifest.json for %1").arg(virtualModelId));
        }
    }

    const QVariantMap readme = hubFiles.value(QStringLiteral("readme")).toMap();
    const QString readmeContent = readme.value(QStringLiteral("content")).toString();
    if (!readmeContent.isEmpty()) {
        if (!writeTextFile(QDir(virtualDir).absoluteFilePath(QStringLiteral("README.md")), readmeContent.toUtf8())) {
            ok = false;
            Logger::error(QStringLiteral("DownloadInstallService"),
                          QStringLiteral("Failed to write virtual README.md for %1").arg(virtualModelId));
        }
    }

    const QVariantMap thumbnail = hubFiles.value(QStringLiteral("thumbnail")).toMap();
    const QByteArray thumbnailBytes = QByteArray::fromBase64(thumbnail.value(QStringLiteral("base64")).toString().toLatin1());
    if (!thumbnailBytes.isEmpty()) {
        QString thumbnailFilename = QFileInfo(thumbnail.value(QStringLiteral("filename")).toString()).fileName();
        if (thumbnailFilename.isEmpty()) {
            thumbnailFilename = QStringLiteral("thumbnail.png");
        }
        if (!writeTextFile(QDir(virtualDir).absoluteFilePath(thumbnailFilename),
                           thumbnailBytes,
                           QIODevice::OpenMode())) {
            ok = false;
            Logger::error(QStringLiteral("DownloadInstallService"),
                          QStringLiteral("Failed to write virtual %1 for %2").arg(thumbnailFilename, virtualModelId));
        }
    }

    if (!ok && errorMessage) {
        *errorMessage = QStringLiteral("Failed to write one or more virtual model files for %1").arg(virtualModelId);
    }
    return ok;
}

QString normalizedSha256(const QVariantMap &metadata)
{
    QString expected = metadata.value(QStringLiteral("sha256")).toString().trimmed();
    if (expected.isEmpty()) {
        expected = metadata.value(QStringLiteral("checksum")).toString().trimmed();
    }
    if (expected.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)) {
        expected = expected.mid(QStringLiteral("sha256:").size()).trimmed();
    }
    return expected.toLower();
}

bool fileMatchesSha256(const QString &path, const QString &expectedSha256, QString *actualSha256)
{
    if (expectedSha256.isEmpty()) {
        if (actualSha256) {
            actualSha256->clear();
        }
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (actualSha256) {
            actualSha256->clear();
        }
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        if (actualSha256) {
            actualSha256->clear();
        }
        return false;
    }

    const QString actual = QString::fromLatin1(hash.result().toHex());
    if (actualSha256) {
        *actualSha256 = actual;
    }
    return actual.compare(expectedSha256, Qt::CaseInsensitive) == 0;
}

bool mergeDirectoryContents(const QString &sourcePath, const QString &targetPath)
{
    QDir source(sourcePath);
    if (!source.exists() || !QDir().mkpath(targetPath)) return false;

    bool ok = true;
    for (const QFileInfo &entry : source.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString target = QDir(targetPath).absoluteFilePath(entry.fileName());
        if (entry.isDir()) {
            ok = mergeDirectoryContents(entry.absoluteFilePath(), target) && ok;
            QDir(entry.absoluteFilePath()).removeRecursively();
        } else {
            QFile::remove(target);
            ok = QFile::rename(entry.absoluteFilePath(), target) && ok;
        }
    }
    return ok;
}
}

QVariantMap DownloadInstallService::latestSupportedRuntime(const QVariantMap &runtimeOption)
{
    const QVariantList versionOptions = runtimeOption.value(QStringLiteral("versionOptions")).toList();
    QVariantMap latestRuntime;
    QString latestVersion;

    for (const QVariant &value : versionOptions) {
        const QVariantMap candidate = value.toMap();
        const QString candidateVersion = candidate.value(QStringLiteral("version")).toString();
        if (latestRuntime.isEmpty() || runtimeVersionGreater(candidateVersion, latestVersion)) {
            latestRuntime = candidate;
            latestVersion = candidateVersion;
        }
    }

    // runtimeOptions are built from catalog entries. Older callers may not provide
    // versionOptions, so retain the option itself as a backwards-compatible fallback.
    if (latestRuntime.isEmpty()) {
        latestRuntime = runtimeOption;
        latestVersion = runtimeOption.value(QStringLiteral("latestVersion")).toString();
        if (!latestVersion.isEmpty())
            latestRuntime.insert(QStringLiteral("version"), latestVersion);
    }
    return latestRuntime;
}

DownloadInstallService::DownloadInstallService(DownloadManager *downloads,
                                               ModelManager *models,
                                               RuntimeManager *runtimes,
                                               Settings *settings,
                                               QObject *parent)
    : QObject(parent)
    , m_downloads(downloads)
    , m_models(models)
    , m_runtimes(runtimes)
    , m_settings(settings)
{
    if (m_downloads) {
        connect(m_downloads, &DownloadManager::finished, this, &DownloadInstallService::onDownloadFinished);
        connect(m_downloads, &DownloadManager::error, this,
                [this](const QString &, const QString &, const QString &message) {
                    emit errorOccurred(message);
                });
    }
}

bool DownloadInstallService::localDownloadsAllowed() const
{
    return !m_settings || !m_settings->remoteFirstMode();
}

bool DownloadInstallService::rejectLocalDownloadInRemoteFirstMode()
{
    if (localDownloadsAllowed()) return false;
    emit errorOccurred(QStringLiteral(
        "Remote-first mode disables local model and runtime downloads. "
        "Configure API Gateway or a direct Colab worker, or explicitly enable Local Dev models in Remote Inference settings."));
    return true;
}

bool DownloadInstallService::isSafeArchiveMemberPath(const QString &memberPath)
{
    QString normalized = memberPath.trimmed();
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    normalized = QDir::cleanPath(normalized);
    return !normalized.isEmpty() && normalized != QStringLiteral(".") &&
           !QDir::isAbsolutePath(normalized) &&
           !QRegularExpression(QStringLiteral("^[A-Za-z]:")).match(normalized).hasMatch() &&
           normalized != QStringLiteral("..") &&
           !normalized.startsWith(QStringLiteral("../"));
}

bool DownloadInstallService::archiveContainsOnlySafeMembers(const QString &extractor,
                                                             const QString &archivePath,
                                                             qint64 *unpackedBytes,
                                                             QString *errorMessage)
{
    QProcess listing;
    listing.setProgram(extractor);
    listing.setArguments({QStringLiteral("-tf"), archivePath});
    listing.setProcessChannelMode(QProcess::MergedChannels);
    listing.start();
    if (!listing.waitForStarted(10000) || !listing.waitForFinished(30000) ||
        listing.exitStatus() != QProcess::NormalExit || listing.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not inspect archive members with %1: %2")
                .arg(extractor, QString::fromLocal8Bit(listing.readAll()).trimmed());
        }
        return false;
    }

    const QStringList members = QString::fromLocal8Bit(listing.readAll())
                                    .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &member : members) {
        if (!isSafeArchiveMemberPath(member)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Archive member escapes extraction directory: %1")
                    .arg(member.trimmed());
            }
            return false;
        }
    }

    // A safe-looking member name is insufficient when an archive also carries
    // symlinks or hardlinks: a later entry could be written through that link.
    // Runtime/model archives do not need links, so reject them rather than
    // depending on extractor-specific link semantics.
    QProcess verboseListing;
    verboseListing.setProgram(extractor);
    verboseListing.setArguments({QStringLiteral("-tvf"), archivePath});
    verboseListing.setProcessChannelMode(QProcess::MergedChannels);
    verboseListing.start();
    if (!verboseListing.waitForStarted(10000) || !verboseListing.waitForFinished(30000) ||
        verboseListing.exitStatus() != QProcess::NormalExit || verboseListing.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not inspect archive link entries with %1: %2")
                .arg(extractor, QString::fromLocal8Bit(verboseListing.readAll()).trimmed());
        }
        return false;
    }
    const QStringList verboseEntries = QString::fromLocal8Bit(verboseListing.readAll())
                                          .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    qint64 totalUnpackedBytes = 0;
    const QRegularExpression verboseSizePattern(
        QStringLiteral(R"(^\S+\s+\d+\s+\S+\s+\S+\s+(\d+)\s+)")
    );
    for (const QString &entry : verboseEntries) {
        const QString line = entry.trimmed();
        if (line.startsWith(QLatin1Char('l')) || line.startsWith(QLatin1Char('h'))) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Archive links are not permitted: %1").arg(line);
            }
            return false;
        }
        if (!line.startsWith(QLatin1Char('-')) && !line.startsWith(QLatin1Char('d'))) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Archive contains an unsupported special entry: %1").arg(line);
            }
            return false;
        }
        if (line.startsWith(QLatin1Char('-'))) {
            const QRegularExpressionMatch sizeMatch = verboseSizePattern.match(line);
            bool sizeOk = false;
            const qint64 memberSize = sizeMatch.hasMatch()
                ? sizeMatch.captured(1).toLongLong(&sizeOk) : -1;
            if (!sizeOk || memberSize < 0 ||
                totalUnpackedBytes > std::numeric_limits<qint64>::max() - memberSize) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Could not safely determine archive member size: %1")
                        .arg(line);
                }
                return false;
            }
            totalUnpackedBytes += memberSize;
        }
    }
    if (unpackedBytes) {
        *unpackedBytes = totalUnpackedBytes;
    }
    return true;
}

bool DownloadInstallService::hasSpaceForExtraction(const QString &extractDir,
                                                    qint64 unpackedBytes,
                                                    QString *errorMessage)
{
    constexpr qint64 kExtractionSafetyMarginBytes = 64LL * 1024 * 1024;
    if (unpackedBytes < 0 ||
        unpackedBytes > std::numeric_limits<qint64>::max() - kExtractionSafetyMarginBytes) {
        if (errorMessage) *errorMessage = QStringLiteral("Archive unpacked size is invalid or too large.");
        return false;
    }

    QString probePath = QDir(extractDir).absolutePath();
    while (!QFileInfo::exists(probePath)) {
        QDir parent(probePath);
        if (!parent.cdUp()) break;
        const QString nextPath = parent.absolutePath();
        if (nextPath == probePath) break;
        probePath = nextPath;
    }
    const QStorageInfo storage(probePath);
    const qint64 requiredBytes = unpackedBytes + kExtractionSafetyMarginBytes;
    if (!storage.isReady() || storage.bytesAvailable() < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not determine free disk space for archive extraction: %1")
                .arg(extractDir);
        }
        return false;
    }
    if (storage.bytesAvailable() < requiredBytes) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Not enough free disk space to extract this archive. Need %1 MiB, but only %2 MiB is available.")
                .arg((requiredBytes + 1024 * 1024 - 1) / (1024 * 1024))
                .arg(storage.bytesAvailable() / (1024 * 1024));
        }
        return false;
    }
    return true;
}

bool DownloadInstallService::extractedTreeIsContained(const QString &extractDir, QString *errorMessage)
{
    const QString canonicalRoot = QFileInfo(extractDir).canonicalFilePath();
    if (canonicalRoot.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Extraction directory cannot be canonicalized");
        return false;
    }

    const QDir root(canonicalRoot);
    QDirIterator it(canonicalRoot, QDir::AllEntries | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo entry(it.next());
        const QString canonicalEntry = entry.canonicalFilePath();
        const QString relative = canonicalEntry.isEmpty()
            ? QStringLiteral("..") : QDir::cleanPath(root.relativeFilePath(canonicalEntry));
        if (relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../")) ||
            QDir::isAbsolutePath(relative)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Extracted path resolves outside extraction directory: %1")
                    .arg(entry.absoluteFilePath());
            }
            return false;
        }
    }
    return true;
}

bool DownloadInstallService::writeVirtualModelFiles(const QVariantMap &metadata)
{
    QString errorMessage;
    const bool ok = writeVirtualModelFilesToDisk(m_models, metadata, &errorMessage);
    if (!ok && errorMessage != QStringLiteral("Virtual model metadata is empty")) {
        emit errorOccurred(errorMessage);
    }
    return ok;
}

bool DownloadInstallService::enqueueModelFile(const QVariantMap &family, const QVariantMap &requirement)
{
    if (rejectLocalDownloadInRemoteFirstMode()) return false;
    if (!m_downloads || !m_models) {
        emit errorOccurred(QStringLiteral("Download services are not available"));
        return false;
    }

    const QString selectedFile = requirement.value(QStringLiteral("selectedFile")).toString();
    if (family.isEmpty() || selectedFile.isEmpty()) {
        emit errorOccurred(QStringLiteral("Model download request is incomplete"));
        return false;
    }
    if (requirement.value(QStringLiteral("installed")).toBool() &&
        requirement.value(QStringLiteral("installState")).toInt() != UpdateAvailable) {
        return true;
    }

    QVariantMap metadata = virtualModelMetadata(family);
    if (!writeVirtualModelFiles(metadata))
        return false;

    QString sourceModelId = requirement.value(QStringLiteral("modelId")).toString();
    if (sourceModelId.isEmpty())
        sourceModelId = family.value(QStringLiteral("modelId")).toString();
    if (sourceModelId.isEmpty()) {
        emit errorOccurred(QStringLiteral("Model source id is empty"));
        return false;
    }

    metadata.insert(QStringLiteral("sourceModelId"), sourceModelId);
    metadata.insert(QStringLiteral("filename"), selectedFile);
    metadata.insert(QStringLiteral("expectedSize"), requirement.value(QStringLiteral("size")));
    metadata.insert(QStringLiteral("expectedBytes"), requirement.value(QStringLiteral("sizeBytes")));

    const QVariantList sources = requirement.value(QStringLiteral("sources")).toList();
    for (const QVariant &sourceValue : sources) {
        const QVariantMap source = sourceValue.toMap();
        const QString url = source.value(QStringLiteral("url")).toString();
        bool containsFile = false;
        for (const QVariant &fileValue : source.value(QStringLiteral("files")).toList()) {
            const QVariantMap file = fileValue.toMap();
            if (file.value(QStringLiteral("name")).toString() == selectedFile ||
                file.value(QStringLiteral("file")).toString() == selectedFile) {
                containsFile = true;
                break;
            }
        }
        if (!url.isEmpty() && containsFile) {
            metadata.insert(QStringLiteral("sha256"), source.value(QStringLiteral("sha256")).toString());
            metadata.insert(QStringLiteral("checksum"), source.value(QStringLiteral("checksum")).toString());
            const QString archiveName = QFileInfo(QUrl(url).path()).fileName();
            const bool archive = archiveName.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive) ||
                archiveName.endsWith(QStringLiteral(".tar.gz"), Qt::CaseInsensitive) ||
                archiveName.endsWith(QStringLiteral(".tar.bz2"), Qt::CaseInsensitive) ||
                archiveName.endsWith(QStringLiteral(".tbz2"), Qt::CaseInsensitive);
            if (archive && !archiveName.isEmpty()) {
                metadata.insert(QStringLiteral("archiveMember"), selectedFile);
                metadata.insert(QStringLiteral("requestedFilename"), selectedFile);
                m_updateAvailable.remove(sourceModelId + QStringLiteral("::") + selectedFile);
                return m_downloads->enqueueUrl(url, archiveName,
                                               m_models->concreteModelDir(sourceModelId), metadata);
            }
            metadata.insert(QStringLiteral("sourceUrl"), url);
            m_updateAvailable.remove(sourceModelId + QStringLiteral("::") + selectedFile);
            return m_downloads->enqueueUrl(url, selectedFile,
                                           m_models->concreteModelDir(sourceModelId), metadata);
        }
    }

    const QString key = sourceModelId + QStringLiteral("::") + selectedFile;
    m_updateAvailable.remove(key);
    return m_downloads->enqueue(sourceModelId, selectedFile,
                                m_models->concreteModelDir(sourceModelId), metadata);
}

bool DownloadInstallService::enqueueRuntime(const QVariantMap &family,
                                            const QString &capability,
                                            const QString &familyId,
                                            const QVariantMap &runtime)
{
    if (rejectLocalDownloadInRemoteFirstMode()) return false;
    if (!m_downloads || !m_runtimes) {
        emit errorOccurred(QStringLiteral("Runtime download services are not available"));
        return false;
    }
    if (runtime.isEmpty() || runtime.value(QStringLiteral("installed")).toBool())
        return true;

    const QString asset = runtime.value(QStringLiteral("asset")).toString();
    const QString source = runtime.value(QStringLiteral("source")).toString();
    const QString version = runtime.value(QStringLiteral("version")).toString();
    if (asset.isEmpty() || source.isEmpty()) {
        emit errorOccurred(QStringLiteral("Runtime download request is incomplete"));
        return false;
    }

    const QVariantMap virtualMetadata = virtualModelMetadata(family);
    if (!writeVirtualModelFiles(virtualMetadata))
        return false;

    const QString baseUrl = source + version + QStringLiteral("/");
    QVariantMap metadata;
    metadata.insert(QStringLiteral("id"), runtime.value(QStringLiteral("id")).toString());
    metadata.insert(QStringLiteral("version"), version);
    metadata.insert(QStringLiteral("engineName"),
                    !runtime.value(QStringLiteral("name")).toString().isEmpty()
                        ? runtime.value(QStringLiteral("name")).toString()
                        : (!runtime.value(QStringLiteral("label")).toString().isEmpty()
                            ? runtime.value(QStringLiteral("label")).toString()
                            : runtime.value(QStringLiteral("id")).toString()));
    metadata.insert(QStringLiteral("engineFamily"), runtime.value(QStringLiteral("engineFamily")).toString());
    QString runtimeType = QStringLiteral("tts");
    if (capability == QStringLiteral("stt")) {
        runtimeType = QStringLiteral("stt");
    } else if (capability == QStringLiteral("translation")) {
        // Translation is exposed through the STT/runtime registry domain;
        // registry_schema.sql intentionally accepts only stt/tts/alignment types.
        runtimeType = QStringLiteral("stt");
    } else if (capability == QStringLiteral("forced-alignment")) {
        runtimeType = QStringLiteral("alignment");
    }
    metadata.insert(QStringLiteral("type"), runtimeType);
    metadata.insert(QStringLiteral("library"), runtime.value(QStringLiteral("library")).toString());
    metadata.insert(QStringLiteral("kind"), runtime.value(QStringLiteral("kind"), QStringLiteral("dynamic-library")).toString());
    metadata.insert(QStringLiteral("entrypoint"), runtime.value(QStringLiteral("entrypoint")).toString());
    metadata.insert(QStringLiteral("protocolVersion"), runtime.value(QStringLiteral("protocolVersion")).toString());
    metadata.insert(QStringLiteral("nativeDependencies"), runtime.value(QStringLiteral("nativeDependencies")).toList());
    metadata.insert(QStringLiteral("capabilities"), runtime.value(QStringLiteral("capabilities")).toList());
    metadata.insert(QStringLiteral("modelFormats"), runtime.value(QStringLiteral("modelFormats")).toList());
    metadata.insert(QStringLiteral("dependencyDownloads"), runtime.value(QStringLiteral("dependencyDownloads")).toList());
    metadata.insert(QStringLiteral("sha256"), runtime.value(QStringLiteral("sha256")).toString());
    metadata.insert(QStringLiteral("checksum"), runtime.value(QStringLiteral("checksum")).toString());
    metadata.insert(QStringLiteral("expectedSize"), runtime.value(QStringLiteral("size")));
    metadata.insert(QStringLiteral("expectedBytes"), runtime.value(QStringLiteral("sizeBytes")));

    QVariantMap runtimeMetadata;
    runtimeMetadata.insert(QStringLiteral("backend"), runtime.value(QStringLiteral("backend")).toString());
    runtimeMetadata.insert(QStringLiteral("modelFamily"), familyId);
    runtimeMetadata.insert(QStringLiteral("modelId"), family.value(QStringLiteral("modelId")).toString());
    runtimeMetadata.insert(QStringLiteral("modelVersion"), runtime.value(QStringLiteral("modelVersion")).toString());
    runtimeMetadata.insert(QStringLiteral("runtimeVersion"), version);
    runtimeMetadata.insert(QStringLiteral("asset"), asset);
    runtimeMetadata.insert(QStringLiteral("source"), baseUrl + asset);
    metadata.insert(QStringLiteral("metadata"), runtimeMetadata);

    return m_downloads->enqueueUrl(baseUrl + asset, asset, m_runtimes->backendsPath(), metadata);
}

bool DownloadInstallService::enqueueRecommendedSetup(const QVariantMap &familyItem)
{
    if (rejectLocalDownloadInRemoteFirstMode()) return false;
    QVariantMap family = familyItem.value(QStringLiteral("rawMetadata")).toMap();
    if (family.isEmpty()) {
        family = familyItem;
    }
    const QString familyId = !familyItem.value(QStringLiteral("familyId")).toString().isEmpty()
        ? familyItem.value(QStringLiteral("familyId")).toString()
        : family.value(QStringLiteral("id")).toString();
    const QString capability = !familyItem.value(QStringLiteral("familyCapability")).toString().isEmpty()
        ? familyItem.value(QStringLiteral("familyCapability")).toString()
        : QStringLiteral("tts");

    if (family.isEmpty() || familyId.isEmpty()) {
        emit errorOccurred(QStringLiteral("Recommended setup request is incomplete"));
        return false;
    }

    bool ok = true;
    bool hasWorkOrActiveInstall = false;

    const QVariantList requiredFiles = familyItem.value(QStringLiteral("requiredFiles")).toList();
    for (const QVariant &requirementValue : requiredFiles) {
        QVariantMap requirement = requirementValue.toMap();
        const int installState = requirement.contains(QStringLiteral("installState"))
            ? requirement.value(QStringLiteral("installState")).toInt()
            : NotInstalled;
        const bool installed = requirement.value(QStringLiteral("installed")).toBool() || installState == Installed;
        if (installed) {
            continue;
        }
        if (installState == Downloading || installState == Installing) {
            hasWorkOrActiveInstall = true;
            continue;
        }

        if (requirement.value(QStringLiteral("selectedFile")).toString().isEmpty()) {
            requirement.insert(QStringLiteral("selectedFile"), requirement.value(QStringLiteral("file")).toString());
        }
        if (!enqueueModelFile(family, requirement)) {
            ok = false;
        } else {
            hasWorkOrActiveInstall = true;
        }
    }

    const QVariantList runtimeOptions = familyItem.value(QStringLiteral("runtimeOptions")).toList();
    bool hasCompatibleRuntime = runtimeOptions.isEmpty();
    bool hasLatestCompatibleRuntime = false;
    bool hasActiveRuntimeInstall = false;
    QVariantMap runtimeToInstall;

    for (const QVariant &runtimeValue : runtimeOptions) {
        const QVariantMap runtime = runtimeValue.toMap();
        if (!runtime.value(QStringLiteral("compatible")).toBool()) {
            continue;
        }

        hasCompatibleRuntime = true;
        const QVariantMap latestRuntime = latestSupportedRuntime(runtime);
        const QString latestVersion = latestRuntime.value(QStringLiteral("version")).toString();
        const QString installedVersion = runtime.value(QStringLiteral("version")).toString();
        const int installState = runtime.contains(QStringLiteral("installState"))
            ? runtime.value(QStringLiteral("installState")).toInt()
            : NotInstalled;
        const bool installed = runtime.value(QStringLiteral("installed")).toBool() || installState == Installed;
        const bool latestInstalled = runtime.contains(QStringLiteral("latestInstalled"))
            ? runtime.value(QStringLiteral("latestInstalled")).toBool()
            : (installed && installedVersion == latestVersion);
        if (latestInstalled) {
            hasLatestCompatibleRuntime = true;
            continue;
        }
        const int latestInstallState = runtime.contains(QStringLiteral("latestInstallState"))
            ? runtime.value(QStringLiteral("latestInstallState")).toInt()
            : installState;
        if (latestInstallState == Downloading || latestInstallState == Installing) {
            hasActiveRuntimeInstall = true;
            continue;
        }
        if (runtimeToInstall.isEmpty()) {
            runtimeToInstall = latestRuntime;
        }
    }

    if (!hasCompatibleRuntime) {
        emit errorOccurred(QStringLiteral("No compatible runtime is available for this model on the detected hardware"));
        return false;
    }

    if (!hasLatestCompatibleRuntime) {
        if (!runtimeToInstall.isEmpty()) {
            if (!enqueueRuntime(family, capability, familyId, runtimeToInstall)) {
                ok = false;
            } else {
                hasWorkOrActiveInstall = true;
            }
        } else if (hasActiveRuntimeInstall) {
            hasWorkOrActiveInstall = true;
        }
    }

    if (hasWorkOrActiveInstall) {
        emit installStatesChanged();
    }
    return ok;
}

void DownloadInstallService::onDownloadFinished(const QString &modelId,
                                                const QString &filename,
                                                const QString &localPath,
                                                const QVariantMap &metadata)
{
    QString task = QStringLiteral("stt");
    QString format = QStringLiteral("bin");
    if (filename.endsWith(QStringLiteral(".gguf"))) format = QStringLiteral("gguf");
    else if (filename.endsWith(QStringLiteral(".onnx"))) format = QStringLiteral("onnx");
    
    if (modelId.contains(QStringLiteral("tts"), Qt::CaseInsensitive) || 
        modelId.contains(QStringLiteral("parler"), Qt::CaseInsensitive) || 
        modelId.contains(QStringLiteral("omnivoice"), Qt::CaseInsensitive) || 
        modelId.contains(QStringLiteral("vibevoice"), Qt::CaseInsensitive) || 
        modelId.contains(QStringLiteral("kokoro"), Qt::CaseInsensitive) ||
        modelId.contains(QStringLiteral("qwen3-tts"), Qt::CaseInsensitive) ||
        filename.contains(QStringLiteral("tts"), Qt::CaseInsensitive) || 
        filename.contains(QStringLiteral("omnivoice"), Qt::CaseInsensitive) ||
        filename.contains(QStringLiteral("vibevoice"), Qt::CaseInsensitive) ||
        filename.contains(QStringLiteral("kokoro"), Qt::CaseInsensitive) ||
        filename.contains(QStringLiteral("qwen3-tts"), Qt::CaseInsensitive)) {
        task = QStringLiteral("tts");
    }

    if (filename.contains(QStringLiteral("asr"), Qt::CaseInsensitive) || 
        filename.contains(QStringLiteral("stt"), Qt::CaseInsensitive) ||
        modelId.contains(QStringLiteral("asr"), Qt::CaseInsensitive) || 
        modelId.contains(QStringLiteral("stt"), Qt::CaseInsensitive)) {
        task = QStringLiteral("stt");
    }

    QFileInfo fi(localPath);
    QString dirPath = fi.absolutePath();
    bool isRuntime = dirPath.contains(QStringLiteral("backends"));
    const bool isRuntimeDependency =
        metadata.value(QStringLiteral("kind")).toString() == QStringLiteral("runtimeDependency");
    const QString dependencyRuntimeDir = metadata.value(QStringLiteral("runtimeDir")).toString();
    const QString expectedSha256 = normalizedSha256(metadata);

    // Runtime code and its dependencies are executable content. They must be
    // authenticated before an installer, extractor, or runtime scanner can see them.
    if (expectedSha256.isEmpty()) {
        if (isRuntime || isRuntimeDependency) {
            QFile::remove(localPath);
            Logger::error(QStringLiteral("DownloadInstallService"),
                          QStringLiteral("Refusing runtime download without a SHA-256: %1").arg(filename));
            emit errorOccurred(QStringLiteral("Runtime download is missing a SHA-256: ") + filename);
            return;
        }
    } else {
        QString actualSha256;
        if (!fileMatchesSha256(localPath, expectedSha256, &actualSha256)) {
            QFile::remove(localPath);
            const QString detail = actualSha256.isEmpty()
                ? QStringLiteral("Could not calculate SHA-256")
                : QStringLiteral("Expected %1 but got %2").arg(expectedSha256, actualSha256);
            Logger::error(QStringLiteral("DownloadInstallService"),
                          QStringLiteral("Refusing download with checksum mismatch: %1 (%2)")
                              .arg(filename, detail));
            emit errorOccurred(QStringLiteral("Download checksum does not match: ") + filename);
            return;
        }
    }

    if (isRuntimeDependency) {
        const QString dependency = metadata.value(QStringLiteral("dependency")).toString();
        const QString runtimeDir = metadata.value(QStringLiteral("runtimeDir")).toString();
        if (dependency == QStringLiteral("espeak-ng") &&
            filename.endsWith(QStringLiteral(".msi"), Qt::CaseInsensitive) &&
            !runtimeDir.isEmpty()) {
            if (!hasTrustedAuthenticodeSignature(localPath)) {
                QFile::remove(localPath);
                Logger::error(QStringLiteral("DownloadInstallService"),
                              QStringLiteral("Refusing eSpeak NG MSI without a trusted Authenticode signature: %1")
                                  .arg(filename));
                emit errorOccurred(QStringLiteral("eSpeak NG MSI signature verification failed: ") + filename);
                return;
            }

            const QString msiexec = systemMsiexecPath();
            if (msiexec.isEmpty()) {
                QFile::remove(localPath);
                Logger::error(QStringLiteral("DownloadInstallService"),
                              QStringLiteral("Windows Installer executable was not found in the system directory"));
                emit errorOccurred(QStringLiteral("Windows Installer is unavailable"));
                return;
            }

            const QString targetDir = QDir(runtimeDir).absoluteFilePath(QStringLiteral("espeak-ng"));
            QDir().mkpath(targetDir);

            QProcess *process = new QProcess(this);
            process->setProgram(msiexec);
            process->setArguments({
                QStringLiteral("/a"),
                QDir::toNativeSeparators(localPath),
                QStringLiteral("/qn"),
                QStringLiteral("TARGETDIR=%1").arg(QDir::toNativeSeparators(targetDir))
            });

            QPointer<DownloadInstallService> weakThis(this);
            connect(process, &QProcess::finished, this,
                    [weakThis, process, filename, targetDir, localPath](int exitCode, QProcess::ExitStatus status) {
                process->deleteLater();
                if (!weakThis) return;
                if (exitCode == 0 && status == QProcess::NormalExit) {
                    Logger::info(QStringLiteral("DownloadInstallService"),
                                 QStringLiteral("Extracted %1 dependency to %2").arg(filename, targetDir));
                    QFile::remove(localPath);
                    weakThis->m_runtimes->scanRuntimes();
                } else {
                    Logger::error(QStringLiteral("DownloadInstallService"),
                                  QStringLiteral("Failed to extract runtime dependency %1").arg(filename));
                    emit weakThis->errorOccurred(QStringLiteral("Failed to extract runtime dependency: ") + filename);
                }
            });

            process->start();
            return;
        }
    }

    if (filename.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive) ||
        filename.endsWith(QStringLiteral(".tar.gz"), Qt::CaseInsensitive) ||
        filename.endsWith(QStringLiteral(".tgz"), Qt::CaseInsensitive) ||
        filename.endsWith(QStringLiteral(".tar.bz2"), Qt::CaseInsensitive) ||
        filename.endsWith(QStringLiteral(".tbz2"), Qt::CaseInsensitive)) {
        if (QFileInfo(localPath).size() == 0) {
            QFile::remove(localPath);
            Logger::error(QStringLiteral("DownloadInstallService"),
                          QStringLiteral("Refusing to extract empty archive: %1").arg(filename));
            emit errorOccurred(QStringLiteral("Refusing to extract empty archive: ") + filename);
            return;
        }
        if (!hasExpectedArchiveSignature(localPath, filename)) {
            QFile::remove(localPath);
            Logger::error(QStringLiteral("DownloadInstallService"),
                          QStringLiteral("Refusing to extract invalid archive: %1").arg(filename));
            emit errorOccurred(QStringLiteral("Refusing to extract invalid archive: ") + filename);
            return;
        }

        QString extractName = fi.completeBaseName();
        if (filename.endsWith(QStringLiteral(".tar.gz"), Qt::CaseInsensitive) ||
            filename.endsWith(QStringLiteral(".tar.bz2"), Qt::CaseInsensitive)) {
            QFileInfo tarFi(fi.completeBaseName());
            extractName = tarFi.completeBaseName();
        }
        if (isRuntimeDependency && !dependencyRuntimeDir.isEmpty()) {
            dirPath = dependencyRuntimeDir;
            extractName.prepend(QStringLiteral(".dependency-"));
        } else if (isRuntime && metadata.contains("id") && metadata.contains("version")) {
            QString runtimeId = metadata.value("id").toString();
            QString runtimeVersion = metadata.value("version").toString();
            QString engineFamily = metadata.value("engineFamily").toString();

            if (engineFamily.isEmpty()) {
                for (const auto &plat : {QStringLiteral("-win-"), QStringLiteral("-linux-"), QStringLiteral("-macos-")}) {
                    int idx = runtimeId.indexOf(plat);
                    if (idx > 0) { engineFamily = runtimeId.left(idx); break; }
                }
                if (engineFamily.isEmpty()) engineFamily = runtimeId;
            }

            QString variant = runtimeId;
            if (runtimeId.startsWith(engineFamily + QStringLiteral("-"))) {
                variant = runtimeId.mid(engineFamily.length() + 1);
            }

            QString familyDir = dirPath + QStringLiteral("/") + engineFamily;
            QDir().mkpath(familyDir);
            extractName = variant + QStringLiteral("-") + runtimeVersion;
            dirPath = familyDir;
        }
        QString extractDir = dirPath + QStringLiteral("/") + extractName;

        const QString bsdtar = archiveExtractor(QStringLiteral("bsdtar.exe"));
        if (bsdtar.isEmpty()) {
            QFile::remove(localPath);
            Logger::error(QStringLiteral("DownloadInstallService"),
                          QStringLiteral("Refusing archive extraction because bundled bsdtar.exe is missing"));
            emit errorOccurred(QStringLiteral("Bundled archive extractor is missing. Reinstall LA Studio."));
            return;
        }
        QString archiveInspectionError;
        qint64 unpackedBytes = 0;
        if (!archiveContainsOnlySafeMembers(bsdtar, localPath, &unpackedBytes, &archiveInspectionError)) {
            QFile::remove(localPath);
            Logger::error(QStringLiteral("DownloadInstallService"),
                          QStringLiteral("Refusing unsafe archive %1: %2").arg(filename, archiveInspectionError));
            emit errorOccurred(QStringLiteral("Refusing unsafe archive: ") + filename);
            return;
        }
        if (!hasSpaceForExtraction(extractDir, unpackedBytes, &archiveInspectionError)) {
            Logger::error(QStringLiteral("DownloadInstallService"),
                          QStringLiteral("Refusing extraction of %1: %2").arg(filename, archiveInspectionError));
            emit errorOccurred(archiveInspectionError);
            return;
        }
        QDir().mkpath(extractDir);

        if (isRuntime) {
            QString runtimeId = metadata.value("id").toString();
            QString runtimeVersion = metadata.value("version").toString();
            if (!runtimeId.isEmpty()) {
                m_activeExtractions.insert(runtimeId + QStringLiteral("::") + runtimeVersion);
                emit installStatesChanged();
            }
        }

        QProcess *process = new QProcess(this);
        process->setProgram(bsdtar);
        process->setArguments({QStringLiteral("-xf"), localPath, QStringLiteral("-C"), extractDir});
        process->setProcessChannelMode(QProcess::MergedChannels);
        
        QPointer<DownloadInstallService> weakThis(this);
        connect(process, &QProcess::finished, this, [weakThis, process, isRuntime, isRuntimeDependency, dependencyRuntimeDir, task, format, dirPath, filename, fi, extractDir, metadata, localPath, modelId](int exitCode, QProcess::ExitStatus status) {
            const QString output = QString::fromLocal8Bit(process->readAll()).trimmed();
            process->deleteLater();
            if (!weakThis) return;

            if (isRuntime) {
                QString runtimeId = metadata.value("id").toString();
                QString runtimeVersion = metadata.value("version").toString();
                if (!runtimeId.isEmpty()) {
                    weakThis->m_activeExtractions.remove(runtimeId + QStringLiteral("::") + runtimeVersion);
                    emit weakThis->installStatesChanged();
                }
            }

            if (exitCode == 0 && status == QProcess::NormalExit) {
                Logger::info(QStringLiteral("DownloadInstallService"), QStringLiteral("Extracted %1 to %2").arg(filename, extractDir));

                QString containmentError;
                if (!extractedTreeIsContained(extractDir, &containmentError)) {
                    QDir(extractDir).removeRecursively();
                    QFile::remove(localPath);
                    Logger::error(QStringLiteral("DownloadInstallService"),
                                  QStringLiteral("Rejected extracted archive %1: %2").arg(filename, containmentError));
                    emit weakThis->errorOccurred(QStringLiteral("Extracted archive contains an unsafe path: ") + filename);
                    return;
                }
                
                QFile::remove(localPath);

                if (isRuntimeDependency) {
                    if (dependencyRuntimeDir.isEmpty() ||
                        !mergeDirectoryContents(extractDir, dependencyRuntimeDir)) {
                        Logger::error(QStringLiteral("DownloadInstallService"),
                                      QStringLiteral("Failed to install runtime dependency %1").arg(filename));
                        emit weakThis->errorOccurred(QStringLiteral("Failed to install runtime dependency: ") + filename);
                        return;
                    }
                    QDir(extractDir).removeRecursively();
                    weakThis->m_runtimes->scanRuntimes();
                } else if (isRuntime) {
                    QDir dir(extractDir);
                    QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                    if (subdirs.size() == 1 && dir.entryList(QDir::Files).isEmpty()) {
                        QString subName = subdirs.first();
                        QDir subDir(dir.absoluteFilePath(subName));
                        QStringList entries = subDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
                        for (const auto &entry : entries) {
                            QFile::rename(subDir.absoluteFilePath(entry), dir.absoluteFilePath(entry));
                        }
                        dir.rmdir(subName);
                    }

                    QFile manifestFile(dir.absoluteFilePath(QStringLiteral("backend-manifest.json")));
                    QJsonObject manifest;
                    bool manifestWasValid = false;
                    if (manifestFile.exists()) {
                        if (manifestFile.open(QIODevice::ReadOnly)) {
                            auto doc = QJsonDocument::fromJson(manifestFile.readAll());
                            manifestFile.close();
                            if (doc.isObject()) {
                                manifest = doc.object();
                                manifestWasValid = true;
                            }
                        }
                    }

                    const QString runtimeKind = metadata.value(QStringLiteral("kind"),
                                                               manifest.value(QStringLiteral("kind")).toString()).toString();
                    if (runtimeKind == QStringLiteral("process")) {
                        const QString entrypoint = metadata.value(QStringLiteral("entrypoint"),
                                                                  manifest.value(QStringLiteral("entrypoint")).toString()).toString();
                        const QString cleanEntrypoint = QDir::cleanPath(entrypoint);
                        const bool safeEntrypoint = !entrypoint.isEmpty() && !QDir::isAbsolutePath(entrypoint) &&
                            cleanEntrypoint != QStringLiteral("..") &&
                            !cleanEntrypoint.startsWith(QStringLiteral("../")) &&
                            !cleanEntrypoint.startsWith(QStringLiteral("..\\"));
                        const QString executablePath = safeEntrypoint ? dir.absoluteFilePath(cleanEntrypoint) : QString();
                        // Official llama.cpp release archives do not ship our
                        // backend-manifest.json; the catalog metadata is the
                        // trusted source for their safe relative entrypoint.
                        const bool hasTrustedCatalogEntrypoint = metadata.contains(QStringLiteral("entrypoint"));
                        if ((!manifestWasValid && !hasTrustedCatalogEntrypoint) ||
                            !safeEntrypoint || !QFileInfo(executablePath).isFile()) {
                            dir.removeRecursively();
                            Logger::error(QStringLiteral("DownloadInstallService"),
                                          QStringLiteral("Rejected process runtime package %1: manifest or entrypoint is invalid").arg(filename));
                            emit weakThis->errorOccurred(QStringLiteral("Invalid process runtime package: ") + filename);
                            return;
                        }
                    }

                    QString runtimeId = metadata.value("id").toString();
                    manifest["id"] = runtimeId;
                    manifest["name"] = metadata.value("engineName").toString();
                    manifest["version"] = metadata.value("version").toString();
                    manifest["type"] = metadata.value("type").toString().isEmpty() ? QStringLiteral("stt") : metadata.value("type").toString();

                    QString ef = metadata.value("engineFamily").toString();
                    if (ef.isEmpty()) {
                        for (const auto &plat : {QStringLiteral("-win-"), QStringLiteral("-linux-"), QStringLiteral("-macos-")}) {
                            int idx = runtimeId.indexOf(plat);
                            if (idx > 0) { ef = runtimeId.left(idx); break; }
                        }
                        if (ef.isEmpty()) ef = runtimeId;
                    }
                    manifest["engineFamily"] = ef;
                    QString vr = runtimeId;
                    if (runtimeId.startsWith(ef + QStringLiteral("-")))
                        vr = runtimeId.mid(ef.length() + 1);
                    manifest["variant"] = vr;

                    if (metadata.contains(QStringLiteral("library"))) {
                        manifest["library"] = metadata.value(QStringLiteral("library")).toString();
                    }
                    for (const QString &field : {QStringLiteral("kind"), QStringLiteral("entrypoint"),
                                                 QStringLiteral("protocolVersion")}) {
                        if (metadata.contains(field) && !metadata.value(field).toString().isEmpty()) {
                            manifest[field] = metadata.value(field).toString();
                        }
                    }
                    for (const QString &field : {QStringLiteral("nativeDependencies"), QStringLiteral("capabilities"), QStringLiteral("modelFormats")}) {
                        if (metadata.contains(field)) {
                            manifest[field] = QJsonArray::fromVariantList(metadata.value(field).toList());
                        }
                    }
                    if (metadata.contains(QStringLiteral("metadata"))) {
                        QJsonObject newMeta = QJsonObject::fromVariantMap(
                            metadata.value(QStringLiteral("metadata")).toMap());
                        QJsonObject existingMeta = manifest.value(QStringLiteral("metadata")).toObject();
                        for (auto it = newMeta.begin(); it != newMeta.end(); ++it) {
                            existingMeta[it.key()] = it.value();
                        }
                        manifest["metadata"] = existingMeta;
                    }
                    
                    QJsonDocument doc(manifest);
                    if (manifestFile.open(QIODevice::WriteOnly)) {
                        manifestFile.write(doc.toJson(QJsonDocument::Indented));
                        manifestFile.close();
                    }

                    const QVariantList dependencyDownloads = metadata.value(QStringLiteral("dependencyDownloads")).toList();
                    for (const QVariant &depValue : dependencyDownloads) {
                        const QVariantMap dep = depValue.toMap();
                        const QString url = dep.value(QStringLiteral("url")).toString();
                        const QString depFilename = dep.value(QStringLiteral("filename")).toString();
                        const QString dependency = dep.value(QStringLiteral("dependency")).toString();
                        if (url.isEmpty() || depFilename.isEmpty() || dependency.isEmpty()) continue;

                        QVariantMap depMetadata;
                        depMetadata[QStringLiteral("kind")] = QStringLiteral("runtimeDependency");
                        depMetadata[QStringLiteral("id")] = metadata.value(QStringLiteral("id")).toString();
                        depMetadata[QStringLiteral("version")] = metadata.value(QStringLiteral("version")).toString();
                        depMetadata[QStringLiteral("dependency")] = dependency;
                        depMetadata[QStringLiteral("runtimeDir")] = extractDir;
                        depMetadata[QStringLiteral("sha256")] = dep.value(QStringLiteral("sha256")).toString();
                        depMetadata[QStringLiteral("checksum")] = dep.value(QStringLiteral("checksum")).toString();
                        weakThis->m_downloads->enqueueUrl(url, depFilename, extractDir, depMetadata);
                    }
                    if (dependencyDownloads.isEmpty()) {
                        weakThis->m_runtimes->scanRuntimes();
                    }
                } else {
                    QString installedFilename = filename;
                    const QString archiveMember = metadata.value(QStringLiteral("archiveMember")).toString();
                    // Flatten extractDir if it has a single subdirectory
                    QDir extDir(extractDir);
                    QStringList subdirs = extDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                    if (subdirs.size() == 1 && extDir.entryList(QDir::Files).isEmpty()) {
                        QString subName = subdirs.first();
                        QDir subDir(extDir.absoluteFilePath(subName));
                        QStringList entries = subDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
                        for (const auto &entry : entries) {
                            QFile::rename(subDir.absoluteFilePath(entry), extDir.absoluteFilePath(entry));
                        }
                        extDir.rmdir(subName);
                    }

                    // Move all files from extractDir up to dirPath
                    QStringList entries = extDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
                    for (const auto &entry : entries) {
                        QFile::rename(extDir.absoluteFilePath(entry), QDir(dirPath).absoluteFilePath(entry));
                    }
                    QDir().rmdir(extractDir);

                    if (!archiveMember.isEmpty()) {
                        QString memberPath;
                        QDirIterator memberIt(dirPath, QDir::Files, QDirIterator::Subdirectories);
                        while (memberIt.hasNext()) {
                            const QString candidate = memberIt.next();
                            if (QFileInfo(candidate).fileName() == archiveMember) {
                                memberPath = candidate;
                                break;
                            }
                        }
                        const QString targetPath = QDir(dirPath).absoluteFilePath(archiveMember);
                        if (!memberPath.isEmpty() && memberPath != targetPath) {
                            QFile::remove(targetPath);
                            if (!QFile::copy(memberPath, targetPath)) {
                                Logger::error(QStringLiteral("DownloadInstallService"),
                                              QStringLiteral("Failed to install archive member %1 from %2")
                                                  .arg(archiveMember, filename));
                                emit weakThis->errorOccurred(QStringLiteral("Failed to install model file: ") + archiveMember);
                                return;
                            }
                        }
                        if (QFileInfo::exists(targetPath)) installedFilename = archiveMember;
                    }

                    // Write .la-info.json to dirPath
                    QDir modelDir(dirPath);
                    QFile infoFile(modelDir.absoluteFilePath(QStringLiteral(".la-info.json")));
                    QJsonObject info;
                    if (infoFile.open(QIODevice::ReadOnly)) {
                        const QJsonDocument existingDoc = QJsonDocument::fromJson(infoFile.readAll());
                        if (existingDoc.isObject()) {
                            info = existingDoc.object();
                        }
                        infoFile.close();
                    }

                    QString resolvedId = metadata.value(QStringLiteral("familyId")).toString();
                    if (resolvedId.isEmpty()) {
                        resolvedId = metadata.value(QStringLiteral("virtualModelId")).toString();
                    }
                    if (resolvedId.isEmpty()) {
                        resolvedId = modelId;
                    }

                    if (infoFile.open(QIODevice::WriteOnly)) {
                        QJsonArray ids = info.value(QStringLiteral("ids")).toArray();
                        const QString existingId = info.value(QStringLiteral("id")).toString();
                        if (!existingId.isEmpty() && !ids.contains(QJsonValue(existingId))) {
                            ids.append(existingId);
                        }
                        if (!resolvedId.isEmpty() && !ids.contains(QJsonValue(resolvedId))) {
                            ids.append(resolvedId);
                        }
                        if (!modelId.isEmpty() && !ids.contains(QJsonValue(modelId))) {
                            ids.append(modelId);
                        }
                        info["id"] = resolvedId;
                        info["ids"] = ids;
                        info["task"] = task;
                        QJsonDocument doc(info);
                        infoFile.write(doc.toJson());
                        infoFile.close();
                    }

                    // Re-scan local models
                    weakThis->m_models->scanLocalModelsAsync();

                    const QString sourceModelId = metadata.value(QStringLiteral("sourceModelId")).toString().isEmpty()
                        ? modelId
                        : metadata.value(QStringLiteral("sourceModelId")).toString();
                    const QString installedPath = QDir(dirPath).absoluteFilePath(installedFilename);
                    const qint64 installedSize = QFileInfo::exists(installedPath) ? QFileInfo(installedPath).size() : fi.size();
                    const QString sizeStr = QString::number(installedSize / (1024.0 * 1024.0), 'f', 1) + " MB";
                    weakThis->m_models->addModel(resolvedId, task, format, dirPath, {installedFilename}, sizeStr);
                    weakThis->scheduleModelFileUpdateCheck(sourceModelId, installedFilename, true);
                }
            } else {
                Logger::error(QStringLiteral("DownloadInstallService"),
                              QStringLiteral("Failed to extract %1 (exit=%2, status=%3)%4")
                                  .arg(filename)
                                  .arg(exitCode)
                                  .arg(status == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crashed"))
                                  .arg(output.isEmpty() ? QString() : QStringLiteral(": %1").arg(output)));
                emit weakThis->errorOccurred(QStringLiteral("Failed to extract: ") + filename);
            }
        });
        
        process->start();
        return;
    }

    if (isRuntime) {
        m_runtimes->scanRuntimes();
    } else {
        QString virtualWriteError;
        if (!writeVirtualModelFilesToDisk(m_models, metadata, &virtualWriteError) &&
            virtualWriteError != QStringLiteral("Virtual model metadata is empty") &&
            virtualWriteError != QStringLiteral("Virtual model id is empty")) {
            emit errorOccurred(virtualWriteError);
        }

        QDir modelDir(dirPath);
        QFile infoFile(modelDir.absoluteFilePath(QStringLiteral(".la-info.json")));
        QJsonObject info;
        if (infoFile.open(QIODevice::ReadOnly)) {
            const QJsonDocument existingDoc = QJsonDocument::fromJson(infoFile.readAll());
            if (existingDoc.isObject()) {
                info = existingDoc.object();
            }
            infoFile.close();
        }

        QString resolvedId = metadata.value(QStringLiteral("familyId")).toString();
        if (resolvedId.isEmpty()) {
            resolvedId = metadata.value(QStringLiteral("virtualModelId")).toString();
        }
        if (resolvedId.isEmpty()) {
            resolvedId = modelId;
        }

        if (infoFile.open(QIODevice::WriteOnly)) {
            QJsonArray ids = info.value(QStringLiteral("ids")).toArray();
            const QString existingId = info.value(QStringLiteral("id")).toString();
            if (!existingId.isEmpty() && !ids.contains(QJsonValue(existingId))) {
                ids.append(existingId);
            }
            if (!resolvedId.isEmpty() && !ids.contains(QJsonValue(resolvedId))) {
                ids.append(resolvedId);
            }
            if (!modelId.isEmpty() && !ids.contains(QJsonValue(modelId))) {
                ids.append(modelId);
            }
            info["id"] = resolvedId;
            info["ids"] = ids;
            info["task"] = task;
            QJsonDocument doc(info);
            infoFile.write(doc.toJson());
            infoFile.close();
        }

        QString sizeStr = QString::number(fi.size() / (1024.0 * 1024.0), 'f', 1) + " MB";
        m_models->addModel(resolvedId, task, format, dirPath, {filename}, sizeStr);
        const QString sourceModelId = metadata.value(QStringLiteral("sourceModelId")).toString().isEmpty()
            ? modelId
            : metadata.value(QStringLiteral("sourceModelId")).toString();
        scheduleModelFileUpdateCheck(sourceModelId, filename, true);
    }
}

void DownloadInstallService::scheduleModelFileUpdateCheck(const QString &modelId, const QString &filename, bool acceptRemoteAsBaseline) const
{
    if (!m_models || modelId.isEmpty() || filename.isEmpty()) {
        return;
    }

    const QString key = modelId + QStringLiteral("::") + filename;
    if (m_activeUpdateChecks.contains(key)) {
        return;
    }
    if (!acceptRemoteAsBaseline) {
        const QDateTime lastChecked = m_lastUpdateChecks.value(key);
        if (lastChecked.isValid() && lastChecked.secsTo(QDateTime::currentDateTimeUtc()) < 6 * 60 * 60) {
            return;
        }
    }

    const QString localPath = m_models->filePath(modelId, filename);
    if (localPath.isEmpty() || !QFileInfo::exists(localPath)) {
        return;
    }

    m_activeUpdateChecks.insert(key);
    QPointer<DownloadInstallService> weakThis(const_cast<DownloadInstallService *>(this));
    QThreadPool::globalInstance()->start([weakThis, modelId, filename, localPath, key, acceptRemoteAsBaseline]() {
        const QVariantMap remote = fetchRemoteFileMetadata(modelId, filename);
        if (!weakThis) return;
        QMetaObject::invokeMethod(weakThis.data(), [weakThis, modelId, filename, localPath, key, remote, acceptRemoteAsBaseline]() {
            if (!weakThis) return;
            auto *self = weakThis.data();
            self->m_activeUpdateChecks.remove(key);

            if (remote.isEmpty() || !self->m_models) {
                return;
            }
            self->m_lastUpdateChecks.insert(key, QDateTime::currentDateTimeUtc());

            if (acceptRemoteAsBaseline) {
                self->m_models->setFileMetadata(modelId, filename, remote);
                const bool previous = self->m_updateAvailable.value(key, false);
                self->m_updateAvailable.insert(key, false);
                if (previous) {
                    emit self->installStatesChanged();
                }
                return;
            }

            const QVariantMap local = self->m_models->fileMetadata(modelId, filename);
            bool updateAvailable = false;
            if (!local.isEmpty()) {
                const QString localFingerprint = fileFingerprint(local);
                const QString remoteFingerprint = fileFingerprint(remote);
                if (!localFingerprint.isEmpty() && !remoteFingerprint.isEmpty()) {
                    updateAvailable = localFingerprint != remoteFingerprint;
                } else {
                    const qint64 expectedSize = remoteSize(remote);
                    updateAvailable = expectedSize > 0 && QFileInfo(localPath).size() != expectedSize;
                }
            } else {
                const qint64 expectedSize = remoteSize(remote);
                updateAvailable = expectedSize > 0 && QFileInfo(localPath).size() != expectedSize;
            }

            if (!updateAvailable) {
                self->m_models->setFileMetadata(modelId, filename, remote);
            }

            const bool previous = self->m_updateAvailable.value(key, false);
            if (previous != updateAvailable) {
                self->m_updateAvailable.insert(key, updateAvailable);
                emit self->installStatesChanged();
            } else {
                self->m_updateAvailable.insert(key, updateAvailable);
            }
        }, Qt::QueuedConnection);
    });
}

int DownloadInstallService::modelFileState(const QString &modelId, const QString &filename) const
{
    if (m_downloads && m_downloads->isDownloading(modelId, filename)) {
        return Downloading;
    }
    if (m_models && m_models->hasFile(modelId, filename)) {
        const QString key = modelId + QStringLiteral("::") + filename;
        if (m_updateAvailable.value(key, false)) {
            return UpdateAvailable;
        }
        scheduleModelFileUpdateCheck(modelId, filename);
        return Installed;
    }
    return NotInstalled;
}

int DownloadInstallService::runtimeState(const QString &runtimeId, const QString &version, const QString &installedPath, const QString &assetName) const
{
    QString key = runtimeId + QStringLiteral("::") + version;
    if (m_activeExtractions.contains(key)) {
        return Installing;
    }

    bool installed = false;
    if (m_runtimes) {
        QVariantList installedVers = m_runtimes->runtimeVersions(runtimeId);
        if (version.isEmpty()) {
            installed = !installedVers.isEmpty();
        } else {
            for (const QVariant &inst : installedVers) {
                if (inst.toMap().value(QStringLiteral("version")).toString() == version) {
                    installed = true;
                    break;
                }
            }
        }
    }

    if (!installed && !installedPath.isEmpty()) {
        installed = QFileInfo::exists(installedPath);
    }

    if (installed) {
        return Installed;
    }

    if (m_downloads && !assetName.isEmpty()) {
        QVariantList active = m_downloads->activeDownloads();
        for (const QVariant &val : active) {
            const QVariantMap activeDownload = val.toMap();
            if (activeDownload.value(QStringLiteral("filename")).toString() != assetName) {
                continue;
            }
            const QVariantMap metadata = activeDownload.value(QStringLiteral("metadata")).toMap();
            const QString activeRuntimeId = metadata.value(QStringLiteral("id")).toString();
            const QString activeVersion = metadata.value(QStringLiteral("version")).toString();
            if ((!activeRuntimeId.isEmpty() || !activeVersion.isEmpty()) &&
                (activeRuntimeId != runtimeId || activeVersion != version)) {
                continue;
            }
            if (activeRuntimeId.isEmpty() || activeRuntimeId == runtimeId) {
                return Downloading;
            }
        }
    }

    return NotInstalled;
}

} // namespace LAStudio
