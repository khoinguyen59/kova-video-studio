#include "dubbing/media/RemoteMediaImportService.h"

#include "core/PathUtils.h"
#include "core/MediaRuntimeLocator.h"
#include "dubbing/media/DouyinBrowserSessionService.h"

#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>

namespace LAStudio {
namespace {

constexpr qint64 kMaximumDownloadBytes = 2LL * 1024 * 1024 * 1024;
constexpr qint64 kMaximumCookieFileBytes = 16LL * 1024 * 1024;

bool isLoopbackHost(const QString &host)
{
    const QString normalized = host.trimmed().toLower();
    return normalized == QStringLiteral("localhost")
        || normalized == QStringLiteral("127.0.0.1")
        || normalized == QStringLiteral("::1");
}

bool isPrivateLiteralAddress(const QString &host)
{
    QHostAddress address;
    if (!address.setAddress(host.trimmed())) return false;
    return address.isNull() || address.isLoopback() || address.isLinkLocal()
        || address.isSiteLocal() || address.isMulticast() || address.isBroadcast()
        || address.isInSubnet(QHostAddress(QStringLiteral("10.0.0.0")), 8)
        || address.isInSubnet(QHostAddress(QStringLiteral("100.64.0.0")), 10)
        || address.isInSubnet(QHostAddress(QStringLiteral("172.16.0.0")), 12)
        || address.isInSubnet(QHostAddress(QStringLiteral("192.168.0.0")), 16)
        || address.isInSubnet(QHostAddress(QStringLiteral("fc00::")), 7);
}

bool resolverNeedsFreshCookies(const QByteArray &diagnostic)
{
    const QString text = QString::fromLocal8Bit(diagnostic);
    return text.contains(QStringLiteral("fresh cookies"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("cookies are needed"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("cookies are required"), Qt::CaseInsensitive);
}

QString safeFileName(QString value)
{
    value = value.trimmed();
    value.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\\\/:*?\"<>|]")),
                  QStringLiteral("_"));
    while (value.contains(QStringLiteral(".."))) value.replace(QStringLiteral(".."), QStringLiteral("_"));
    if (value.isEmpty() || value == QStringLiteral(".") || value == QStringLiteral("_"))
        value = QStringLiteral("media.bin");
    return value.left(120);
}

} // namespace

RemoteMediaImportService::RemoteMediaImportService(const QString &storageRoot, QObject *parent,
                                                   int resolverTimeoutMs)
    : QObject(parent)
    , m_storageRoot(storageRoot.trimmed().isEmpty()
                        ? QDir(PathUtils::cacheDir()).filePath(QStringLiteral("dubbing/link-imports"))
                        : QDir::cleanPath(storageRoot))
    , m_network(new QNetworkAccessManager(this))
    , m_browserSession(new DouyinBrowserSessionService(this))
    , m_resolverTimeoutMs(qMax(1, resolverTimeoutMs))
{
    connect(&m_resolver, &QProcess::readyReadStandardOutput, this,
            [this] { m_resolverOutput += m_resolver.readAllStandardOutput(); });
    connect(&m_resolver, &QProcess::readyReadStandardError, this,
            [this] { m_resolverError += m_resolver.readAllStandardError(); });
    connect(&m_resolver, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_active && error == QProcess::FailedToStart)
            fail(QStringLiteral("The managed public-video adapter could not be started."));
    });
    connect(&m_resolver, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
        m_resolverOutput += m_resolver.readAllStandardOutput();
        m_resolverError += m_resolver.readAllStandardError();
        if (!m_active) return;
        if (!m_pendingResolverTerminationError.isEmpty()) {
            const QString error = m_pendingResolverTerminationError;
            m_pendingResolverTerminationError.clear();
            fail(error);
            return;
        }
        if (status != QProcess::NormalExit || exitCode != 0) {
            fail(resolverNeedsFreshCookies(m_resolverError)
                     ? QStringLiteral("Douyin requires fresh cookies for this link. Choose a Netscape cookie file and retry, or download it in a browser and import the file.")
                     : QStringLiteral("The public-video adapter could not resolve this URL."));
            return;
        }
        const QList<QByteArray> lines = m_resolverOutput.trimmed().split('\n');
        if (lines.size() != 1) {
            fail(QStringLiteral("The public-video URL resolved to zero or multiple media files; playlists are not supported."));
            return;
        }
        const QUrl resolved = QUrl::fromUserInput(QString::fromUtf8(lines.constFirst()).trimmed());
        if (!isSupportedSource(resolved)) {
            fail(QStringLiteral("The public-video adapter resolved to an unsafe or unsupported media URL."));
            return;
        }
        // The resolver no longer needs the cookie once it has returned the
        // signed media URL. Remove the temporary copy before the network
        // download starts so credentials never outlive the resolver phase.
        m_cookieFile.reset();
        validateAndStartDirectDownload(resolved);
            });
    connect(m_browserSession, &DouyinBrowserSessionService::downloadFinished, this,
            [this](bool success, const QString &localPath, const QString &error) {
        if (!m_browserDownloadActive || !m_active) return;
        m_browserDownloadActive = false;
        m_active = false;
        if (success && QFileInfo(localPath).isFile() && QFileInfo(localPath).size() > 0) {
            emit transferProgress(QFileInfo(localPath).size(), QFileInfo(localPath).size());
            emit finished(true, localPath, {});
            return;
        }
        QFile::remove(m_outputPath);
        m_outputPath.clear();
        emit finished(false, {}, error.trimmed().isEmpty()
                                  ? QStringLiteral("The managed Chromium session could not download this Douyin page.")
                                  : error);
    });
}

RemoteMediaImportService::~RemoteMediaImportService() = default;

bool RemoteMediaImportService::setCookieFilePath(const QString &path, QString *error)
{
    const QString localPath = QDir::cleanPath(path.trimmed());
    const QFileInfo info(localPath);
    if (localPath.isEmpty() || !info.isFile() || !info.isReadable()) {
        if (error) *error = QStringLiteral("Choose a readable Netscape cookie file.");
        return false;
    }
    if (info.size() <= 0 || info.size() > kMaximumCookieFileBytes) {
        if (error) *error = QStringLiteral("The cookie file must be between 1 byte and 16 MiB.");
        return false;
    }
    QFile cookieFile(localPath);
    if (!cookieFile.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("The selected cookie file could not be read.");
        return false;
    }
    const QByteArray sample = cookieFile.read(256 * 1024);
    if (!sample.contains('\t')) {
        if (error) *error = QStringLiteral("The cookie file must use Netscape tab-separated format.");
        return false;
    }
    m_cookieSourcePath = info.absoluteFilePath();
    return true;
}

void RemoteMediaImportService::clearCookieFilePath()
{
    m_cookieSourcePath.clear();
    m_cookieFile.reset();
}

bool RemoteMediaImportService::prepareCookieFile(QString *error)
{
    m_cookieFile.reset();
    if (m_cookieSourcePath.isEmpty()) return true;

    QFile source(m_cookieSourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("The selected Douyin cookie file is no longer readable.");
        return false;
    }
    const QByteArray contents = source.read(kMaximumCookieFileBytes + 1);
    if (contents.isEmpty() || contents.size() > kMaximumCookieFileBytes) {
        if (error) *error = QStringLiteral("The selected Douyin cookie file is invalid or exceeds 16 MiB.");
        return false;
    }

    auto temporary = std::make_unique<QTemporaryFile>(
        QDir(QDir::tempPath()).filePath(QStringLiteral("LA-Studio-douyin-cookies-XXXXXX.txt")));
    temporary->setAutoRemove(true);
    if (!temporary->open()
        || temporary->write(contents) != contents.size()
        || !temporary->flush()) {
        if (error) *error = QStringLiteral("Could not create the temporary Douyin cookie file.");
        return false;
    }
    temporary->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    temporary->close();
    m_cookieFile = std::move(temporary);
    return true;
}

QStringList RemoteMediaImportService::publicVideoResolverArguments(const QUrl &sourceUrl,
                                                                    const QString &cookieFilePath)
{
    // `--` makes the page URL positional even if its query/path begins with
    // an option-looking token. QProcess receives this list without a shell.
    QStringList arguments{QStringLiteral("--no-playlist"), QStringLiteral("--no-warnings")};
    if (cookieFilePath.trimmed().isEmpty()) {
        arguments.append(QStringLiteral("--no-cookies"));
    } else {
        arguments.append({QStringLiteral("--cookies"), cookieFilePath});
    }
    arguments.append({QStringLiteral("--get-url"), QStringLiteral("--"),
                      sourceUrl.toString(QUrl::FullyEncoded)});
    return arguments;
}

bool isDouyinUrl(const QUrl &sourceUrl)
{
    const QString host = sourceUrl.host().trimmed().toLower();
    return sourceUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
        && sourceUrl.userInfo().isEmpty()
        && (host == QStringLiteral("douyin.com") || host.endsWith(QStringLiteral(".douyin.com")));
}

bool RemoteMediaImportService::isSupportedSource(const QUrl &sourceUrl) const
{
    if (!sourceUrl.isValid() || sourceUrl.host().trimmed().isEmpty() || !sourceUrl.userInfo().isEmpty())
        return false;
    if (sourceUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
        && !isLoopbackHost(sourceUrl.host()) && !isPrivateLiteralAddress(sourceUrl.host()))
        return true;
    // A loopback HTTP exception permits deterministic offline regression tests.
    return sourceUrl.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0
        && isLoopbackHost(sourceUrl.host());
}

bool RemoteMediaImportService::isPublicVideoPage(const QUrl &sourceUrl) const
{
    const QString host = sourceUrl.host().trimmed().toLower();
    return sourceUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
        && sourceUrl.userInfo().isEmpty()
        && (host == QStringLiteral("youtube.com") || host.endsWith(QStringLiteral(".youtube.com"))
            || host == QStringLiteral("youtu.be") || host == QStringLiteral("tiktok.com")
            || host.endsWith(QStringLiteral(".tiktok.com")) || host == QStringLiteral("douyin.com")
            || host.endsWith(QStringLiteral(".douyin.com")) || host == QStringLiteral("v.douyin.com"));
}

bool RemoteMediaImportService::resolvePublicVideoPage(const QUrl &sourceUrl)
{
    if (m_douyinBrowserEnabled && isDouyinUrl(sourceUrl))
        return resolvePublicVideoInBrowser(sourceUrl);
    const QString executable = MediaRuntimeLocator::resolve().ytDlp;
    if (executable.isEmpty()) {
        emit finished(false, {}, QStringLiteral("Public-video support requires the managed yt-dlp adapter."));
        return false;
    }
    QString cookieError;
    if (!prepareCookieFile(&cookieError)) {
        emit finished(false, {}, cookieError);
        return false;
    }
    m_resolverOutput.clear();
    m_resolverError.clear();
    m_pendingResolverTerminationError.clear();
    m_active = true;
    const quint64 resolverRunId = ++m_resolverRunId;
    m_resolver.setProgram(executable);
    m_resolver.setArguments(publicVideoResolverArguments(
        sourceUrl, m_cookieFile ? m_cookieFile->fileName() : QString()));
    m_resolver.start();
    QTimer::singleShot(m_resolverTimeoutMs, this, [this, resolverRunId] {
        if (m_active && resolverRunId == m_resolverRunId
            && m_resolver.state() != QProcess::NotRunning) {
            // Do not report a retryable failure until QProcess has actually
            // stopped. Starting it again while its old process is alive is a
            // race on Windows and used to make the Retry action fail.
            m_pendingResolverTerminationError =
                QStringLiteral("The public-video adapter timed out while resolving the URL.");
            m_resolver.kill();
        }
    });
    return true;
}

bool RemoteMediaImportService::resolvePublicVideoInBrowser(const QUrl &sourceUrl)
{
    if (!m_browserSession) {
        emit finished(false, {}, QStringLiteral("The managed Chromium session is unavailable in this build."));
        return false;
    }
    QString error;
    if (!m_browserSession->available(&error)) {
        emit finished(false, {}, error);
        return false;
    }
    if (!QDir().mkpath(m_storageRoot)) {
        emit finished(false, {}, QStringLiteral("Cannot create LA Studio media staging storage."));
        return false;
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_outputPath = QDir(m_storageRoot).filePath(id + QStringLiteral("-douyin.mp4"));
    QFile::remove(m_outputPath);
    m_active = true;
    m_browserDownloadActive = true;
    if (!m_browserSession->download(sourceUrl, m_outputPath, &error)) {
        m_browserDownloadActive = false;
        m_active = false;
        m_outputPath.clear();
        emit finished(false, {}, error.isEmpty()
                                  ? QStringLiteral("The managed Chromium session could not be started.")
                                  : error);
        return false;
    }
    return true;
}

bool RemoteMediaImportService::download(const QUrl &sourceUrl)
{
    cancel();
    if (isPublicVideoPage(sourceUrl)) return resolvePublicVideoPage(sourceUrl);
    if (!isSupportedSource(sourceUrl)) {
        emit finished(false, {}, QStringLiteral("Enter a direct HTTPS media file or a supported public YouTube, TikTok, or Douyin URL. HTTP is accepted only for local testing."));
        return false;
    }

    validateAndStartDirectDownload(sourceUrl);
    return true;
}

void RemoteMediaImportService::validateAndStartDirectDownload(const QUrl &sourceUrl)
{
    if (!m_active) m_active = true;
    // Loopback HTTP is deliberately restricted to the deterministic offline
    // test transport.  It is never accepted for public URLs or normal HTTP.
    if (sourceUrl.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0
        && isLoopbackHost(sourceUrl.host())) {
        startDirectDownload(sourceUrl);
        return;
    }
    QHostAddress literal;
    if (literal.setAddress(sourceUrl.host())) {
        if (isPrivateLiteralAddress(sourceUrl.host()))
            fail(QStringLiteral("The media URL resolves to an unsafe private address."));
        else
            startDirectDownload(sourceUrl);
        return;
    }

    const QString host = sourceUrl.host().trimmed();
    m_hostLookupId = QHostInfo::lookupHost(host, this, [this, sourceUrl](const QHostInfo &result) {
        m_hostLookupId = -1;
        if (!m_active) return;
        if (result.error() != QHostInfo::NoError || result.addresses().isEmpty()) {
            fail(QStringLiteral("Cannot resolve the media host safely."));
            return;
        }
        for (const QHostAddress &address : result.addresses()) {
            if (isPrivateLiteralAddress(address.toString())) {
                fail(QStringLiteral("The media URL resolves to an unsafe private address."));
                return;
            }
        }
        startDirectDownload(sourceUrl);
    });
}

void RemoteMediaImportService::startDirectDownload(const QUrl &sourceUrl)
{

    if (!QDir().mkpath(m_storageRoot)) {
        fail(QStringLiteral("Cannot create LA Studio media staging storage."));
        return;
    }

    QNetworkRequest request(sourceUrl);
    // Redirects are manually restarted through validateAndStartDirectDownload
    // so every redirect target receives the same literal and DNS safety check
    // as the original URL before any connection or staging write is made.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LA-Studio/MediaImport"));
    m_active = true;
    m_bytesWritten = 0;
    m_outputPath.clear();
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &RemoteMediaImportService::consumeAvailableData);
    // Reject a known oversized response before opening a staged file.  The
    // byte-count check below remains necessary for chunked/lying servers.
    connect(m_reply, &QNetworkReply::metaDataChanged, this, [this]() {
        if (!m_active || !m_reply) return;
        if (!isSupportedSource(m_reply->url())) {
            fail(QStringLiteral("The media URL redirected to an unsafe or unsupported address."));
            return;
        }
        const QVariant length = m_reply->header(QNetworkRequest::ContentLengthHeader);
        bool ok = false;
        const qint64 announcedBytes = length.toLongLong(&ok);
        if (ok && announcedBytes > kMaximumDownloadBytes)
            fail(QStringLiteral("The media link exceeds the 2 GiB import limit."));
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        if (!m_active) return;
        if (received > kMaximumDownloadBytes || total > kMaximumDownloadBytes) {
            fail(QStringLiteral("The media link exceeds the 2 GiB import limit."));
            return;
        }
        emit transferProgress(received, total);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_active || !m_reply) return;

        const QUrl redirect = m_reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (!redirect.isEmpty()) {
            const QUrl target = m_reply->url().resolved(redirect);
            if (!isSupportedSource(target)) {
                fail(QStringLiteral("The media URL redirected to an unsafe or unsupported address."));
                return;
            }
            m_reply->deleteLater();
            m_reply = nullptr;
            validateAndStartDirectDownload(target);
            return;
        }

        consumeAvailableData();
        if (!m_active || !m_reply) return;

        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (m_reply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("Network error while downloading the media link."));
            return;
        }
        if (status < 200 || status >= 300) {
            fail(QStringLiteral("The media link returned HTTP %1.").arg(status));
            return;
        }
        if (!m_output || m_bytesWritten <= 0) {
            fail(QStringLiteral("The media link returned an empty file."));
            return;
        }
        if (!m_output->commit() || !QFileInfo(m_outputPath).isFile()
            || QFileInfo(m_outputPath).size() <= 0) {
            fail(QStringLiteral("Could not atomically save the downloaded media."));
            return;
        }
        m_output.reset();
        m_active = false;
        const QString completedPath = m_outputPath;
        m_reply->deleteLater();
        m_reply = nullptr;
        emit finished(true, completedPath, {});
    });
}

void RemoteMediaImportService::cancel()
{
    if (m_hostLookupId >= 0) {
        QHostInfo::abortHostLookup(m_hostLookupId);
        m_hostLookupId = -1;
    }
    if (m_resolver.state() != QProcess::NotRunning) {
        m_pendingResolverTerminationError = QStringLiteral("Media link import canceled.");
        m_resolver.kill();
        return;
    }
    if (m_browserDownloadActive && m_browserSession) {
        m_browserSession->cancel();
        m_browserDownloadActive = false;
        if (m_active) fail(QStringLiteral("Media link import canceled."));
        return;
    }
    if (m_active) fail(QStringLiteral("Media link import canceled."));
}

QString RemoteMediaImportService::outputFileName() const
{
    if (!m_reply) return QStringLiteral("media.bin");
    QString leaf = QFileInfo(m_reply->url().path()).fileName();
    leaf = QUrl::fromPercentEncoding(leaf.toUtf8());
    return safeFileName(leaf);
}

bool RemoteMediaImportService::ensureOutputFile()
{
    if (m_output) return true;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_outputPath = QDir(m_storageRoot).filePath(id + QStringLiteral("-") + outputFileName());
    m_output = std::make_unique<QSaveFile>(m_outputPath);
    if (!m_output->open(QIODevice::WriteOnly)) {
        m_output.reset();
        fail(QStringLiteral("Cannot open app-owned staging storage for this media link."));
        return false;
    }
    return true;
}

void RemoteMediaImportService::consumeAvailableData()
{
    if (!m_active || !m_reply) return;
    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty()) return;
    if (m_bytesWritten > kMaximumDownloadBytes - chunk.size()) {
        fail(QStringLiteral("The media link exceeds the 2 GiB import limit."));
        return;
    }
    if (!ensureOutputFile()) return;
    if (m_output->write(chunk) != chunk.size()) {
        fail(QStringLiteral("Cannot write the downloaded media to app-owned staging storage."));
        return;
    }
    m_bytesWritten += chunk.size();
}

void RemoteMediaImportService::fail(const QString &error)
{
    if (!m_active) return;
    m_active = false;
    if (m_reply) m_reply->abort();
    m_output.reset();
    m_outputPath.clear();
    m_cookieFile.reset();
    emit finished(false, {}, error);
}

} // namespace LAStudio
