#include "dubbing/media/RemoteMediaImportService.h"

#include "core/PathUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

namespace LAStudio {
namespace {

constexpr qint64 kMaximumDownloadBytes = 2LL * 1024 * 1024 * 1024;

bool isLoopbackHost(const QString &host)
{
    const QString normalized = host.trimmed().toLower();
    return normalized == QStringLiteral("localhost")
        || normalized == QStringLiteral("127.0.0.1")
        || normalized == QStringLiteral("::1");
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

RemoteMediaImportService::RemoteMediaImportService(const QString &storageRoot, QObject *parent)
    : QObject(parent)
    , m_storageRoot(storageRoot.trimmed().isEmpty()
                        ? QDir(PathUtils::cacheDir()).filePath(QStringLiteral("dubbing/link-imports"))
                        : QDir::cleanPath(storageRoot))
    , m_network(new QNetworkAccessManager(this))
{
}

bool RemoteMediaImportService::isSupportedSource(const QUrl &sourceUrl) const
{
    if (!sourceUrl.isValid() || sourceUrl.host().trimmed().isEmpty() || !sourceUrl.userInfo().isEmpty())
        return false;
    if (sourceUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0)
        return true;
    // A loopback HTTP exception permits deterministic offline regression tests.
    return sourceUrl.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0
        && isLoopbackHost(sourceUrl.host());
}

bool RemoteMediaImportService::download(const QUrl &sourceUrl)
{
    cancel();
    if (!isSupportedSource(sourceUrl)) {
        emit finished(false, {}, QStringLiteral("Enter a direct HTTPS media URL. HTTP is accepted only for local testing."));
        return false;
    }

    if (!QDir().mkpath(m_storageRoot)) {
        emit finished(false, {}, QStringLiteral("Cannot create LA Studio media staging storage."));
        return false;
    }

    QNetworkRequest request(sourceUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
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
    return true;
}

void RemoteMediaImportService::cancel()
{
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
    emit finished(false, {}, error);
}

} // namespace LAStudio
