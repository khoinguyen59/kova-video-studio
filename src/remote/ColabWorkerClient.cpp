#include "ColabWorkerClient.h"

#include "ExecutionProvider.h"

#include <QBuffer>
#include <QEventLoop>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace LAStudio {

namespace {

QString responseError(const QByteArray &body, int statusCode)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject()) {
        const QJsonValue detail = document.object().value(QStringLiteral("detail"));
        if (detail.isString() && !detail.toString().trimmed().isEmpty()) return detail.toString().trimmed();
        const QJsonValue error = document.object().value(QStringLiteral("error"));
        if (error.isObject()) {
            const QString message = error.toObject().value(QStringLiteral("message")).toString().trimmed();
            if (!message.isEmpty()) return message;
        }
    }
    return QStringLiteral("Colab worker returned HTTP %1").arg(statusCode);
}

QHttpPart formField(const QByteArray &name, const QByteArray &value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QVariant(QStringLiteral("form-data; name=\"%1\"").arg(QString::fromLatin1(name))));
    part.setBody(value);
    return part;
}

} // namespace

bool ColabWorkerClient::configure(const QUrl &workerUrl, const QString &bearerToken,
                                  bool allowInsecureLocalhost, QString *errorMessage)
{
    const RemoteEndpointValidation validated = validateRemoteEndpoint(
        workerUrl.toString(), RemoteEndpointKind::ColabWorker, allowInsecureLocalhost);
    const QString normalizedToken = bearerToken.trimmed();
    if (!validated.isValid()) {
        if (errorMessage) *errorMessage = validated.error;
        return false;
    }
    if (normalizedToken.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker bearer token is required");
        return false;
    }
    m_workerUrl = validated.normalizedUrl;
    m_bearerToken = normalizedToken;
    return true;
}

void ColabWorkerClient::clear()
{
    cancel();
    m_workerUrl = {};
    m_bearerToken.clear();
}

bool ColabWorkerClient::transcribeWav(const QByteArray &wavData, const QString &language,
                                      const std::shared_ptr<std::atomic_bool> &cancelToken,
                                      QJsonObject *response, QString *errorMessage)
{
    if (response) *response = {};
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker is not connected");
        return false;
    }
    if (wavData.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Audio input is empty");
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v1/audio/transcriptions")));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "application/json");

    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    multipart->append(formField("model", "colab"));
    multipart->append(formField("response_format", "verbose_json"));
    if (!language.trimmed().isEmpty() && language.compare(QStringLiteral("auto"), Qt::CaseInsensitive) != 0) {
        multipart->append(formField("language", language.trimmed().toUtf8()));
    }
    QHttpPart audioPart;
    audioPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"audio.wav\"")));
    audioPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("audio/wav")));
    auto *audioBuffer = new QBuffer(multipart);
    audioBuffer->setData(wavData);
    audioBuffer->open(QIODevice::ReadOnly);
    audioPart.setBodyDevice(audioBuffer);
    multipart->append(audioPart);

    QNetworkReply *reply = manager.post(request, multipart);
    multipart->setParent(reply);
    m_activeReply = reply;
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();

    const QByteArray body = reply->readAll();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    m_activeReply = nullptr;
    reply->deleteLater();

    if (cancelToken && cancelToken->load(std::memory_order_relaxed)) return false;
    if (networkError != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = statusCode >= 400 ? responseError(body, statusCode)
                                               : QStringLiteral("Colab worker request failed: %1").arg(networkErrorText);
        }
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (statusCode < 200 || statusCode >= 300) {
        if (errorMessage) *errorMessage = responseError(body, statusCode);
        return false;
    }
    if (!document.isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker returned an invalid transcription response");
        return false;
    }
    if (response) *response = document.object();
    return true;
}

void ColabWorkerClient::cancel()
{
    if (m_activeReply) m_activeReply->abort();
}

} // namespace LAStudio
