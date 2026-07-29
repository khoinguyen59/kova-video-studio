#include "GatewayClient.h"

#include "ExecutionProvider.h"

#include <QEventLoop>
#include <QBuffer>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace LAStudio {

namespace {

// Streaming requests can last for a while, but a silent endpoint must never
// leave a feature worker blocked indefinitely.
constexpr int kInferenceRequestTimeoutMs = 300'000;

QString responseErrorMessage(const QByteArray &body, int statusCode)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject()) {
        const QJsonObject root = document.object();
        const QJsonValue error = root.value(QStringLiteral("error"));
        if (error.isObject()) {
            const QString message = error.toObject().value(QStringLiteral("message")).toString().trimmed();
            if (!message.isEmpty()) return message;
        }
        const QString message = root.value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty()) return message;
    }
    return QStringLiteral("Gateway returned HTTP %1").arg(statusCode);
}

QString contentFromResponse(const QJsonObject &root)
{
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty() || !choices.first().isObject()) return {};
    const QJsonObject choice = choices.first().toObject();
    const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
    if (delta.value(QStringLiteral("content")).isString()) {
        return delta.value(QStringLiteral("content")).toString();
    }
    const QJsonObject message = choice.value(QStringLiteral("message")).toObject();
    return message.value(QStringLiteral("content")).toString();
}

QHttpPart multipartField(const QByteArray &name, const QByteArray &value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QVariant(QStringLiteral("form-data; name=\"%1\"").arg(QString::fromLatin1(name))));
    part.setBody(value);
    return part;
}

} // namespace

bool GatewayClient::configure(const QString &baseUrl, const QString &apiKey, const QString &model,
                              bool allowInsecureLocalhost, QString *errorMessage)
{
    const RemoteEndpointValidation endpoint = validateRemoteEndpoint(
        baseUrl, RemoteEndpointKind::ApiGateway, allowInsecureLocalhost);
    const QString normalizedKey = apiKey.trimmed();
    const QString normalizedModel = model.trimmed();
    if (!endpoint.isValid()) {
        if (errorMessage) *errorMessage = endpoint.error;
        return false;
    }
    if (normalizedKey.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("API Gateway key is required");
        return false;
    }
    if (normalizedModel.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("API Gateway model is required");
        return false;
    }

    m_baseUrl = endpoint.normalizedUrl;
    m_apiKey = normalizedKey;
    m_model = normalizedModel;
    return true;
}

bool GatewayClient::isConfigured() const
{
    return m_baseUrl.isValid() && !m_apiKey.isEmpty() && !m_model.isEmpty();
}

void GatewayClient::clear()
{
    cancel();
    m_baseUrl = {};
    m_apiKey.clear();
    m_model.clear();
}

bool GatewayClient::streamChat(const QList<QVariantMap> &messages, const ChatOptions &options,
                               const std::shared_ptr<std::atomic_bool> &cancelToken,
                               const std::function<void(const QString &)> &tokenHandler,
                               QString *fullText, QString *errorMessage)
{
    if (fullText) fullText->clear();
    if (!isConfigured()) {
        if (errorMessage) *errorMessage = QStringLiteral("API Gateway is not configured");
        return false;
    }

    QJsonArray requestMessages;
    for (const QVariantMap &message : messages) {
        const QString role = message.value(QStringLiteral("role")).toString().trimmed();
        const QString content = message.value(QStringLiteral("content")).toString();
        if (!role.isEmpty()) {
            requestMessages.append(QJsonObject{{QStringLiteral("role"), role},
                                               {QStringLiteral("content"), content}});
        }
    }
    if (requestMessages.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("A chat message is required");
        return false;
    }

    const QJsonObject payload{
        {QStringLiteral("model"), m_model},
        {QStringLiteral("messages"), requestMessages},
        {QStringLiteral("stream"), true},
        {QStringLiteral("max_tokens"), options.maxTokens},
        {QStringLiteral("temperature"), options.temperature},
        {QStringLiteral("top_p"), options.topP}
    };

    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_baseUrl, QStringLiteral("chat/completions")));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());
    request.setRawHeader("Accept", "text/event-stream, application/json");

    QNetworkReply *reply = manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_activeReply = reply;
    QByteArray pending;
    QByteArray responseBody;
    QString accumulated;
    QString parseError;
    QEventLoop eventLoop;

    const auto consumePayload = [&](QByteArray line) {
        line = line.trimmed();
        if (line.isEmpty() || line == QByteArrayLiteral("[DONE]")) return;
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject()) {
            parseError = QStringLiteral("API Gateway returned an invalid chat response");
            return;
        }
        const QString token = contentFromResponse(document.object());
        if (!token.isEmpty()) {
            accumulated += token;
            if (tokenHandler) tokenHandler(token);
        }
    };
    const auto consumeAvailable = [&]() {
        const QByteArray bytes = reply->readAll();
        pending += bytes;
        responseBody += bytes;
        while (true) {
            const int lineEnd = pending.indexOf('\n');
            if (lineEnd < 0) break;
            QByteArray line = pending.left(lineEnd);
            pending.remove(0, lineEnd + 1);
            if (line.startsWith("data:")) line.remove(0, 5);
            consumePayload(line);
        }
    };

    QObject::connect(reply, &QNetworkReply::readyRead, &eventLoop, consumeAvailable);
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    consumeAvailable();
    if (!pending.trimmed().isEmpty()) {
        QByteArray finalPayload = pending.trimmed();
        if (finalPayload.startsWith("data:")) finalPayload.remove(0, 5);
        consumePayload(finalPayload);
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    m_activeReply = nullptr;
    reply->deleteLater();

    if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
        if (fullText) *fullText = accumulated;
        return false;
    }
    if (networkError != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = statusCode >= 400
                ? responseErrorMessage(responseBody, statusCode)
                : QStringLiteral("API Gateway request failed: %1").arg(networkErrorText);
        }
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        if (errorMessage) *errorMessage = responseErrorMessage(responseBody, statusCode);
        return false;
    }
    if (!parseError.isEmpty()) {
        if (errorMessage) *errorMessage = parseError;
        return false;
    }

    if (fullText) *fullText = accumulated;
    return true;
}

bool GatewayClient::transcribeWav(const QByteArray &wavData, const QString &language,
                                  const std::shared_ptr<std::atomic_bool> &cancelToken,
                                  QJsonObject *response, QString *errorMessage)
{
    if (response) *response = {};
    if (!isConfigured()) {
        if (errorMessage) *errorMessage = QStringLiteral("API Gateway is not configured");
        return false;
    }
    if (wavData.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Audio input is empty");
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_baseUrl, QStringLiteral("audio/transcriptions")));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());
    request.setRawHeader("Accept", "application/json");
    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    multipart->append(multipartField("model", m_model.toUtf8()));
    multipart->append(multipartField("response_format", "verbose_json"));
    if (!language.trimmed().isEmpty() && language.compare(QStringLiteral("auto"), Qt::CaseInsensitive) != 0) {
        multipart->append(multipartField("language", language.trimmed().toUtf8()));
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
            *errorMessage = statusCode >= 400 ? responseErrorMessage(body, statusCode)
                                               : QStringLiteral("API Gateway request failed: %1").arg(networkErrorText);
        }
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        if (errorMessage) *errorMessage = responseErrorMessage(body, statusCode);
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("API Gateway returned an invalid transcription response");
        return false;
    }
    if (response) *response = document.object();
    return true;
}

bool GatewayClient::synthesizeSpeech(const QString &text, const QString &voice, float speed,
                                     const std::shared_ptr<std::atomic_bool> &cancelToken,
                                     QByteArray *wavData, QString *errorMessage)
{
    if (wavData) wavData->clear();
    if (!isConfigured()) {
        if (errorMessage) *errorMessage = QStringLiteral("API Gateway is not configured");
        return false;
    }
    const QString normalizedText = text.trimmed();
    const QString normalizedVoice = voice.trimmed();
    if (normalizedText.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Text is required for speech synthesis");
        return false;
    }
    if (normalizedVoice.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("API Gateway TTS voice is required");
        return false;
    }

    const QJsonObject payload{
        {QStringLiteral("model"), m_model},
        {QStringLiteral("input"), normalizedText},
        {QStringLiteral("voice"), normalizedVoice},
        {QStringLiteral("response_format"), QStringLiteral("wav")},
        {QStringLiteral("speed"), qBound(0.25F, speed, 4.0F)}
    };
    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_baseUrl, QStringLiteral("audio/speech")));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());
    request.setRawHeader("Accept", "audio/wav, application/octet-stream");
    QNetworkReply *reply = manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
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
            *errorMessage = statusCode >= 400 ? responseErrorMessage(body, statusCode)
                                               : QStringLiteral("API Gateway request failed: %1").arg(networkErrorText);
        }
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        if (errorMessage) *errorMessage = responseErrorMessage(body, statusCode);
        return false;
    }
    if (body.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("API Gateway returned empty TTS audio");
        return false;
    }
    if (wavData) *wavData = body;
    return true;
}

void GatewayClient::cancel()
{
    if (m_activeReply) m_activeReply->abort();
}

} // namespace LAStudio
