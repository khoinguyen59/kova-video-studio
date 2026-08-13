#include "dubbing/media/ColabMediaDownloadRunner.h"

#include "core/PathUtils.h"
#include "remote/ColabSession.h"
#include "remote/ExecutionProvider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

namespace LAStudio {
namespace {

constexpr qint64 kMaximumResultBytes = 4LL * 1024 * 1024 * 1024;

bool privateLiteralAddress(const QString &host)
{
    QHostAddress address;
    if (!address.setAddress(host.trimmed())) return false;
    return address.isLoopback() || address.isLinkLocal() || address.isSiteLocal()
        || address.isMulticast()
        || address.isInSubnet(QHostAddress(QStringLiteral("10.0.0.0")), 8)
        || address.isInSubnet(QHostAddress(QStringLiteral("100.64.0.0")), 10)
        || address.isInSubnet(QHostAddress(QStringLiteral("172.16.0.0")), 12)
        || address.isInSubnet(QHostAddress(QStringLiteral("192.168.0.0")), 16)
        || address.isInSubnet(QHostAddress(QStringLiteral("fc00::")), 7);
}

} // namespace

ColabMediaDownloadRunner::ColabMediaDownloadRunner(ColabSession *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
    , m_network(new QNetworkAccessManager(this))
{
    m_pollTimer.setInterval(900);
    m_pollTimer.setSingleShot(false);
    connect(&m_pollTimer, &QTimer::timeout, this, &ColabMediaDownloadRunner::requestStatus);
}

ColabMediaDownloadRunner::~ColabMediaDownloadRunner()
{
    cancel();
}

void ColabMediaDownloadRunner::setSession(ColabSession *session)
{
    if (m_active) cancel();
    m_session = session;
}

bool ColabMediaDownloadRunner::validateSourceUrl(const QUrl &sourceUrl, QString *error) const
{
    if (!sourceUrl.isValid() || sourceUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0
        || sourceUrl.host().trimmed().isEmpty() || !sourceUrl.userInfo().isEmpty()) {
        if (error) *error = QStringLiteral("Enter one public HTTPS media link. Local, credentialed, and HTTP URLs are not accepted.");
        return false;
    }
    const QString host = sourceUrl.host().trimmed().toLower();
    if (host == QStringLiteral("localhost") || privateLiteralAddress(host)) {
        if (error) *error = QStringLiteral("Only public media URLs can be sent to the Colab downloader.");
        return false;
    }
    return true;
}

QNetworkRequest ColabMediaDownloadRunner::authenticatedRequest(const QString &relativePath) const
{
    QNetworkRequest request(appendRemotePath(m_session->endpoint(), relativePath));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ")
                         + m_session->bearerTokenForRequest().toUtf8());
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(45'000);
    return request;
}

bool ColabMediaDownloadRunner::download(const QUrl &sourceUrl)
{
    if (m_active) return false;
    QString error;
    if (!validateSourceUrl(sourceUrl, &error)) {
        emit finished(false, {}, error);
        return false;
    }
    if (!m_session || !m_session->hasVerifiedRoute(QStringLiteral("media-download"),
                                                    QStringLiteral("yt-dlp-media-download"),
                                                    &error)) {
        emit finished(false, {}, error.isEmpty()
             ? QStringLiteral("Connect and check the dedicated Colab media downloader first.") : error);
        return false;
    }

    m_active = true;
    m_cancelled = false;
    m_outputWriteFailed = false;
    m_jobId.clear();
    m_suggestedFileName.clear();
    emit phaseChanged(QStringLiteral("Submitting link to the verified Colab downloader"));

    QNetworkRequest request = authenticatedRequest(QStringLiteral("v1/media/downloads"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QJsonObject payload{{QStringLiteral("url"), sourceUrl.toString(QUrl::FullyEncoded)}};
    QNetworkReply *reply = m_network->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_requestReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply != m_requestReply) { reply->deleteLater(); return; }
        m_requestReply.clear();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError networkError = reply->error();
        const QByteArray body = reply->readAll();
        const QString replyError = reply->errorString();
        reply->deleteLater();
        if (m_cancelled) { finish(false, {}, QStringLiteral("Colab media download cancelled.")); return; }
        if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
            QJsonDocument document = QJsonDocument::fromJson(body);
            const QString detail = document.isObject()
                ? document.object().value(QStringLiteral("detail")).toString() : replyError;
            finish(false, {}, safeDetail(detail.isEmpty() ? QStringLiteral("The Colab downloader rejected this link.") : detail));
            return;
        }
        const QJsonDocument document = QJsonDocument::fromJson(body);
        const QString jobId = document.isObject()
            ? document.object().value(QStringLiteral("job_id")).toString().trimmed() : QString();
        if (jobId.isEmpty() || jobId.size() > 128) {
            finish(false, {}, QStringLiteral("The Colab downloader returned an invalid job identifier."));
            return;
        }
        m_jobId = jobId;
        emit phaseChanged(QStringLiteral("Colab is downloading the public media"));
        m_pollTimer.start();
        requestStatus();
    });
    return true;
}

void ColabMediaDownloadRunner::requestStatus()
{
    if (!m_active || m_jobId.isEmpty() || m_requestReply) return;
    QNetworkReply *reply = m_network->get(authenticatedRequest(
        QStringLiteral("v1/media/downloads/%1").arg(m_jobId)));
    m_requestReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply != m_requestReply) { reply->deleteLater(); return; }
        m_requestReply.clear();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError networkError = reply->error();
        const QByteArray body = reply->readAll();
        const QString replyError = reply->errorString();
        reply->deleteLater();
        if (m_cancelled) { finish(false, {}, QStringLiteral("Colab media download cancelled.")); return; }
        if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
            finish(false, {}, safeDetail(replyError.isEmpty()
                ? QStringLiteral("Could not check the Colab download job.") : replyError));
            return;
        }
        const QJsonDocument document = QJsonDocument::fromJson(body);
        const QJsonObject job = document.object();
        const QString state = job.value(QStringLiteral("state")).toString().trimmed().toLower();
        const qint64 received = static_cast<qint64>(job.value(QStringLiteral("received_bytes")).toDouble(0));
        const qint64 total = static_cast<qint64>(job.value(QStringLiteral("total_bytes")).toDouble(-1));
        emit transferProgress(received, total);
        if (state == QStringLiteral("ready")) {
            const QString fileName = job.value(QStringLiteral("file_name")).toString();
            if (!fileName.isEmpty() && fileName.size() <= 180
                && !fileName.contains(QLatin1Char('/'))
                && !fileName.contains(QLatin1Char('\\'))) {
                m_suggestedFileName = fileName;
            }
            m_pollTimer.stop();
            requestResultFile();
        } else if (state == QStringLiteral("failed") || state == QStringLiteral("cancelled")) {
            m_pollTimer.stop();
            finish(false, {}, safeDetail(job.value(QStringLiteral("detail")).toString().isEmpty()
                ? QStringLiteral("The Colab downloader could not retrieve this media.")
                : job.value(QStringLiteral("detail")).toString()));
        } else if (state != QStringLiteral("queued") && state != QStringLiteral("downloading")) {
            m_pollTimer.stop();
            finish(false, {}, QStringLiteral("The Colab downloader returned an unknown job state."));
        }
    });
}

void ColabMediaDownloadRunner::requestResultFile()
{
    if (!m_active || m_jobId.isEmpty()) return;
    const QString root = QDir(PathUtils::cacheDir()).filePath(QStringLiteral("dubbing/colab-downloads"));
    if (!QDir().mkpath(root)) {
        finish(false, {}, QStringLiteral("Could not create the local Colab media library."));
        return;
    }
    QString extension = QFileInfo(m_suggestedFileName).suffix().toLower();
    if (!QRegularExpression(QStringLiteral("^[a-z0-9]{1,8}$")).match(extension).hasMatch())
        extension = QStringLiteral("media");
    m_outputPath = QDir(root).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)
                                       + QLatin1Char('.') + extension);
    m_output = std::make_unique<QSaveFile>(m_outputPath);
    m_outputWriteFailed = false;
    if (!m_output->open(QIODevice::WriteOnly)) {
        finish(false, {}, QStringLiteral("Could not create the downloaded media file."));
        return;
    }
    QNetworkRequest request = authenticatedRequest(QStringLiteral("v1/media/downloads/%1/file").arg(m_jobId));
    request.setRawHeader("Accept", "application/octet-stream");
    QNetworkReply *reply = m_network->get(request);
    m_fileReply = reply;
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) { emit transferProgress(received, total); });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (reply != m_fileReply || !m_output) return;
        const QByteArray data = reply->readAll();
        if (data.isEmpty()) return;
        if (m_output->size() + data.size() > kMaximumResultBytes
            || m_output->write(data) != data.size()) {
            m_outputWriteFailed = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply != m_fileReply) { reply->deleteLater(); return; }
        m_fileReply.clear();
        if (m_output) {
            const QByteArray tail = reply->readAll();
            if (!tail.isEmpty()) {
                if (m_output->size() + tail.size() > kMaximumResultBytes
                    || m_output->write(tail) != tail.size()) {
                    m_outputWriteFailed = true;
                }
            }
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString replyError = reply->errorString();
        reply->deleteLater();
        if (m_cancelled) { finish(false, {}, QStringLiteral("Colab media download cancelled.")); return; }
        if (networkError != QNetworkReply::NoError || status < 200 || status >= 300 || m_outputWriteFailed || !m_output
            || m_output->size() <= 0 || !m_output->commit()) {
            const QString detail = m_outputWriteFailed
                ? QStringLiteral("The completed media exceeded the 4 GiB limit or could not be written locally.")
                : (replyError.isEmpty()
                    ? QStringLiteral("Could not retrieve the completed media from Colab.") : replyError);
            finish(false, {}, safeDetail(detail));
            return;
        }
        // finish() clears its internal request state.  Keep a value copy so
        // the completed local path survives that cleanup and reaches the
        // controller/QML signal instead of becoming an empty reference.
        const QString completedPath = m_outputPath;
        m_output.reset();
        finish(true, completedPath);
    });
}

void ColabMediaDownloadRunner::cancel()
{
    if (!m_active) return;
    m_cancelled = true;
    m_pollTimer.stop();
    if (m_requestReply) m_requestReply->abort();
    if (m_fileReply) m_fileReply->abort();
    if (!m_requestReply && !m_fileReply)
        finish(false, {}, QStringLiteral("Colab media download cancelled."));
}

void ColabMediaDownloadRunner::finish(bool success, const QString &localPath, const QString &error)
{
    if (!m_active && !m_cancelled) return;
    m_pollTimer.stop();
    if (m_output) {
        m_output->cancelWriting();
        m_output.reset();
    }
    if (!success && !m_outputPath.isEmpty()) QFile::remove(m_outputPath);
    m_requestReply.clear();
    m_fileReply.clear();
    m_active = false;
    m_cancelled = false;
    m_jobId.clear();
    m_outputPath.clear();
    m_suggestedFileName.clear();
    m_outputWriteFailed = false;
    emit finished(success, localPath, error);
}

QString ColabMediaDownloadRunner::safeDetail(const QString &detail) const
{
    QString safe = detail.trimmed();
    safe.replace(QRegularExpression(QStringLiteral(R"(https?://[^\s<>\"']+)"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("[media URL removed]"));
    return safe.left(600);
}

} // namespace LAStudio
