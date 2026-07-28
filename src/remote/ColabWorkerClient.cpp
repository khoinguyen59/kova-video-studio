#include "ColabWorkerClient.h"

#include "ExecutionProvider.h"

#include <QBuffer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace LAStudio {

namespace {

// Uploads and GPU jobs can legitimately take minutes. A finite transfer
// timeout still lets the UI recover when a Colab tunnel disappears silently.
constexpr int kInferenceRequestTimeoutMs = 300'000;

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

QString audioMimeTypeForPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("wav")) return QStringLiteral("audio/wav");
    if (suffix == QStringLiteral("mp3")) return QStringLiteral("audio/mpeg");
    if (suffix == QStringLiteral("m4a") || suffix == QStringLiteral("mp4")) return QStringLiteral("audio/mp4");
    if (suffix == QStringLiteral("webm")) return QStringLiteral("audio/webm");
    if (suffix == QStringLiteral("ogg")) return QStringLiteral("audio/ogg");
    if (suffix == QStringLiteral("flac")) return QStringLiteral("audio/flac");
    return {};
}

bool parseJsonResponse(QNetworkReply *reply, QByteArray *body, QJsonObject *response,
                       QString *errorMessage, const QString &invalidResponseMessage)
{
    const QByteArray responseBody = reply->readAll();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    if (body) *body = responseBody;
    if (networkError != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = statusCode >= 400 ? responseError(responseBody, statusCode)
                                               : QStringLiteral("Colab worker request failed: %1").arg(networkErrorText);
        }
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        if (errorMessage) *errorMessage = responseError(responseBody, statusCode);
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(responseBody);
    if (!document.isObject()) {
        if (errorMessage) *errorMessage = invalidResponseMessage;
        return false;
    }
    if (response) *response = document.object();
    return true;
}

QString chatContent(const QJsonObject &root)
{
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty() || !choices.first().isObject()) return {};
    const QJsonObject choice = choices.first().toObject();
    const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
    if (delta.value(QStringLiteral("content")).isString()) return delta.value(QStringLiteral("content")).toString();
    return choice.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
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

bool ColabWorkerClient::transcribeWav(const QByteArray &wavData, const QString &model,
                                      const QString &language,
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
    const QString normalizedModel = model.trimmed();
    if (normalizedModel.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab STT model is required");
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v1/audio/transcriptions")));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "application/json");

    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    multipart->append(formField("model", normalizedModel.toUtf8()));
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

bool ColabWorkerClient::synthesizeSpeech(const QString &text, const QString &model, const QString &voice,
                                         const QString &language, float speed, const QVariantMap &settings,
                                         const std::shared_ptr<std::atomic_bool> &cancelToken,
                                         QByteArray *wavData, QString *errorMessage)
{
    if (wavData) wavData->clear();
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker is not connected");
        return false;
    }
    const QString normalizedText = text.trimmed();
    const QString normalizedModel = model.trimmed();
    const QString normalizedVoice = voice.trimmed();
    if (normalizedText.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Text is required for speech synthesis");
        return false;
    }
    if (normalizedModel.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab TTS model is required");
        return false;
    }
    if (normalizedVoice.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab TTS voice is required");
        return false;
    }

    QJsonObject payload{{QStringLiteral("model"), normalizedModel},
                        {QStringLiteral("input"), normalizedText},
                        {QStringLiteral("voice"), normalizedVoice},
                        {QStringLiteral("response_format"), QStringLiteral("wav")},
                        {QStringLiteral("speed"), qBound(0.25F, speed, 4.0F)}};
    if (!language.trimmed().isEmpty() && language.compare(QStringLiteral("auto"), Qt::CaseInsensitive) != 0) {
        payload.insert(QStringLiteral("language"), language.trimmed());
    }
    if (!settings.isEmpty()) {
        payload.insert(QStringLiteral("settings"), QJsonObject::fromVariantMap(settings));
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v1/audio/speech")));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
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
            *errorMessage = statusCode >= 400 ? responseError(body, statusCode)
                                               : QStringLiteral("Colab worker request failed: %1").arg(networkErrorText);
        }
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        if (errorMessage) *errorMessage = responseError(body, statusCode);
        return false;
    }
    if (body.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker returned empty TTS audio");
        return false;
    }
    if (wavData) *wavData = body;
    return true;
}

bool ColabWorkerClient::designVoice(const QString &text, const QString &model,
                                    const QString &voiceDescription, const QString &style,
                                    const QString &language, float temperature, qint64 seed,
                                    const std::shared_ptr<std::atomic_bool> &cancelToken,
                                    QByteArray *wavData, QString *errorMessage)
{
    if (wavData) wavData->clear();
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker is not connected");
        return false;
    }
    const QString normalizedText = text.trimmed();
    const QString normalizedModel = model.trimmed();
    const QString normalizedDescription = voiceDescription.trimmed();
    if (normalizedText.isEmpty() || normalizedModel.isEmpty() || normalizedDescription.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Text, VoiceDesign model, and voice description are required");
        return false;
    }

    QJsonObject payload{{QStringLiteral("model"), normalizedModel},
                        {QStringLiteral("input"), normalizedText},
                        {QStringLiteral("voice_description"), normalizedDescription},
                        {QStringLiteral("language"), language.trimmed().isEmpty() ? QStringLiteral("en") : language.trimmed()},
                        {QStringLiteral("temperature"), qBound(0.1F, temperature, 2.0F)},
                        {QStringLiteral("seed"), seed},
                        {QStringLiteral("response_format"), QStringLiteral("wav")}};
    if (!style.trimmed().isEmpty()) payload.insert(QStringLiteral("style"), style.trimmed());

    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v1/audio/voice_designs")));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
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
            *errorMessage = statusCode >= 400 ? responseError(body, statusCode)
                                               : QStringLiteral("Colab worker request failed: %1").arg(networkErrorText);
        }
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        if (errorMessage) *errorMessage = responseError(body, statusCode);
        return false;
    }
    if (body.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker returned empty VoiceDesign audio");
        return false;
    }
    if (wavData) *wavData = body;
    return true;
}

bool ColabWorkerClient::alignAudioFile(const QString &audioPath, const QString &transcript,
                                       const QString &language, const QString &model,
                                       const std::shared_ptr<std::atomic_bool> &cancelToken,
                                       QJsonObject *response, QString *errorMessage)
{
    if (response) *response = {};
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker is not connected");
        return false;
    }
    const QString normalizedTranscript = transcript.trimmed();
    const QString normalizedModel = model.trimmed();
    if (normalizedTranscript.isEmpty() || normalizedModel.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Audio, transcript, and alignment model are required");
        return false;
    }

    const QString audioMimeType = audioMimeTypeForPath(audioPath);
    if (audioMimeType.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Alignment audio must use a supported audio filename extension");
        return false;
    }
    QFile *audio = new QFile(audioPath);
    constexpr qint64 maxUploadBytes = 512LL * 1024LL * 1024LL;
    if (!audio->open(QIODevice::ReadOnly) || audio->size() <= 0 || audio->size() > maxUploadBytes) {
        delete audio;
        if (errorMessage) *errorMessage = QStringLiteral("Alignment audio must be a readable file no larger than 512 MB");
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v1/audio/alignments")));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "application/json");
    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    multipart->append(formField("model", normalizedModel.toUtf8()));
    multipart->append(formField("transcript", normalizedTranscript.toUtf8()));
    multipart->append(formField("language", language.trimmed().isEmpty() ? QByteArrayLiteral("en") : language.trimmed().toUtf8()));
    QHttpPart audioPart;
    const QString sourceFilename = QFileInfo(audio->fileName()).fileName();
    const QString filename = sourceFilename.isEmpty() ? QStringLiteral("audio.wav") : sourceFilename;
    audioPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QVariant(QStringLiteral("form-data; name=\"audio\"; filename=\"%1\"").arg(filename)));
    audioPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(audioMimeType));
    audio->setParent(multipart);
    audioPart.setBodyDevice(audio);
    multipart->append(audioPart);

    QNetworkReply *reply = manager.post(request, multipart);
    multipart->setParent(reply);
    m_activeReply = reply;
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    m_activeReply = nullptr;
    if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
        reply->deleteLater();
        return false;
    }
    const bool ok = parseJsonResponse(reply, nullptr, response, errorMessage,
                                      QStringLiteral("Colab worker returned an invalid alignment response"));
    reply->deleteLater();
    return ok;
}

bool ColabWorkerClient::createSeparationJob(const QString &audioPath, const QString &model,
                                            QJsonObject *job, QString *errorMessage)
{
    if (job) *job = {};
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker is not connected");
        return false;
    }
    const QString audioMimeType = audioMimeTypeForPath(audioPath);
    if (audioMimeType.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Separation audio must use a supported audio filename extension");
        return false;
    }
    QFile *audio = new QFile(audioPath);
    constexpr qint64 maxUploadBytes = 512LL * 1024LL * 1024LL;
    if (!audio->open(QIODevice::ReadOnly) || audio->size() <= 0 || audio->size() > maxUploadBytes) {
        delete audio;
        if (errorMessage) *errorMessage = QStringLiteral("Separation audio must be a readable file no larger than 512 MB");
        return false;
    }
    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v1/audio/separations")));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "application/json");
    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    multipart->append(formField("model", model.trimmed().isEmpty() ? QByteArrayLiteral("htdemucs") : model.trimmed().toUtf8()));
    multipart->append(formField("stems", "vocals,background"));
    QHttpPart audioPart;
    const QString sourceFilename = QFileInfo(audio->fileName()).fileName();
    const QString filename = sourceFilename.isEmpty() ? QStringLiteral("audio.wav") : sourceFilename;
    audioPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"%1\"").arg(filename)));
    audioPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(audioMimeType));
    audio->setParent(multipart);
    audioPart.setBodyDevice(audio);
    multipart->append(audioPart);
    QNetworkReply *reply = manager.post(request, multipart);
    multipart->setParent(reply);
    m_activeReply = reply;
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    m_activeReply = nullptr;
    const bool ok = parseJsonResponse(reply, nullptr, job, errorMessage,
                                      QStringLiteral("Colab worker returned an invalid separation job"));
    reply->deleteLater();
    return ok;
}

bool ColabWorkerClient::separationJobStatus(const QString &jobId, QJsonObject *job, QString *errorMessage)
{
    if (job) *job = {};
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty() || jobId.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab separation job is unavailable");
        return false;
    }
    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v1/audio/separations/%1").arg(jobId.trimmed())));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = manager.get(request);
    m_activeReply = reply;
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    m_activeReply = nullptr;
    const bool ok = parseJsonResponse(reply, nullptr, job, errorMessage,
                                      QStringLiteral("Colab worker returned an invalid separation status"));
    reply->deleteLater();
    return ok;
}

bool ColabWorkerClient::downloadSeparationArtifact(const QString &jobId, const QString &stem,
                                                   const std::shared_ptr<std::atomic_bool> &cancelToken,
                                                   QByteArray *wavData, QString *errorMessage)
{
    if (wavData) wavData->clear();
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty() || jobId.trimmed().isEmpty()
        || (stem != QStringLiteral("vocals") && stem != QStringLiteral("background"))) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab separation artifact is unavailable");
        return false;
    }
    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl,
        QStringLiteral("v1/audio/separations/%1/artifacts/%2").arg(jobId.trimmed(), stem)));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "audio/wav, application/octet-stream");
    QNetworkReply *reply = manager.get(request);
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
    if (networkError != QNetworkReply::NoError || statusCode < 200 || statusCode >= 300) {
        if (errorMessage) *errorMessage = statusCode >= 400 ? responseError(body, statusCode)
            : QStringLiteral("Colab worker request failed: %1").arg(networkErrorText);
        return false;
    }
    if (body.size() < 44 || !body.startsWith("RIFF")) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker returned an invalid WAV separation artifact");
        return false;
    }
    if (wavData) *wavData = body;
    return true;
}

bool ColabWorkerClient::cancelSeparationJob(const QString &jobId, QString *errorMessage)
{
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty() || jobId.trimmed().isEmpty()) return false;
    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v1/audio/separations/%1").arg(jobId.trimmed())));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    QNetworkReply *reply = manager.deleteResource(request);
    m_activeReply = reply;
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    m_activeReply = nullptr;
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const bool ok = reply->error() == QNetworkReply::NoError && statusCode >= 200 && statusCode < 300;
    if (!ok && errorMessage) *errorMessage = responseError(body, statusCode);
    reply->deleteLater();
    return ok;
}

bool ColabWorkerClient::translateSegments(const QVariantList &segments, const QString &sourceLanguage,
                                          const QString &targetLanguage, const QString &model,
                                          const std::shared_ptr<std::atomic_bool> &cancelToken,
                                          QJsonObject *response, QString *errorMessage)
{
    if (response) *response = {};
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker is not connected");
        return false;
    }
    if (segments.isEmpty() || model.trimmed().isEmpty() || sourceLanguage.trimmed().isEmpty() || targetLanguage.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab translation requires segments, languages, and a model");
        return false;
    }
    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v1/translations")));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "application/json");
    const QJsonObject payload{{QStringLiteral("model"), model.trimmed()},
                              {QStringLiteral("source_language"), sourceLanguage.trimmed()},
                              {QStringLiteral("target_language"), targetLanguage.trimmed()},
                              {QStringLiteral("segments"), QJsonArray::fromVariantList(segments)}};
    QNetworkReply *reply = manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_activeReply = reply;
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    m_activeReply = nullptr;
    if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
        reply->deleteLater();
        return false;
    }
    const bool ok = parseJsonResponse(reply, nullptr, response, errorMessage,
                                      QStringLiteral("Colab worker returned an invalid translation response"));
    reply->deleteLater();
    return ok;
}

bool ColabWorkerClient::streamChat(const QList<QVariantMap> &messages, const QString &model,
                                   int maxTokens, float temperature, float topP,
                                   const std::shared_ptr<std::atomic_bool> &cancelToken,
                                   const std::function<void(const QString &)> &tokenHandler,
                                   QString *fullText, QString *errorMessage)
{
    if (fullText) fullText->clear();
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker is not connected");
        return false;
    }
    if (model.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab chat model is required");
        return false;
    }
    QJsonArray requestMessages;
    for (const QVariantMap &message : messages) {
        const QString role = message.value(QStringLiteral("role")).toString().trimmed();
        if (!role.isEmpty()) requestMessages.append(QJsonObject{{QStringLiteral("role"), role},
                                                                {QStringLiteral("content"), message.value(QStringLiteral("content")).toString()}});
    }
    if (requestMessages.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("A chat message is required");
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v1/chat/completions")));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "text/event-stream, application/json");
    const QJsonObject payload{{QStringLiteral("model"), model.trimmed()},
                              {QStringLiteral("messages"), requestMessages},
                              {QStringLiteral("stream"), true},
                              {QStringLiteral("max_tokens"), qBound(1, maxTokens, 32768)},
                              {QStringLiteral("temperature"), qBound(0.01F, temperature, 2.0F)},
                              {QStringLiteral("top_p"), qBound(0.01F, topP, 1.0F)}};
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
        if (!document.isObject()) { parseError = QStringLiteral("Colab worker returned an invalid chat response"); return; }
        const QString token = chatContent(document.object());
        if (!token.isEmpty()) { accumulated += token; if (tokenHandler) tokenHandler(token); }
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
    if (reply->error() != QNetworkReply::OperationCanceledError) consumeAvailable();
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
        if (errorMessage) *errorMessage = statusCode >= 400 ? responseError(responseBody, statusCode)
                                                             : QStringLiteral("Colab worker request failed: %1").arg(networkErrorText);
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        if (errorMessage) *errorMessage = responseError(responseBody, statusCode);
        return false;
    }
    if (!parseError.isEmpty()) { if (errorMessage) *errorMessage = parseError; return false; }
    if (fullText) *fullText = accumulated;
    return true;
}

bool ColabWorkerClient::createVoiceProfileJob(const QString &model, const QString &referencePath,
                                              const QString &name,
                                              const QString &referenceText, const QString &language,
                                              bool separateMusic, QJsonObject *job, QString *errorMessage)
{
    if (job) *job = {};
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker is not connected");
        return false;
    }
    const QString referenceMimeType = audioMimeTypeForPath(referencePath);
    const QString referenceSuffix = QFileInfo(referencePath).suffix().toLower();
    constexpr qint64 maxReferenceUploadBytes = 256LL * 1024LL * 1024LL;
    QFile *reference = new QFile(referencePath);
    if (referenceMimeType.isEmpty()
        || (referenceSuffix != QStringLiteral("wav") && referenceSuffix != QStringLiteral("mp3")
            && referenceSuffix != QStringLiteral("flac"))
        || !reference->open(QIODevice::ReadOnly) || reference->size() <= 0
        || reference->size() > maxReferenceUploadBytes) {
        delete reference;
        if (errorMessage) *errorMessage = QStringLiteral("Reference audio must be a readable WAV, MP3, or FLAC file no larger than 256 MB");
        return false;
    }
    const QString normalizedName = name.trimmed();
    const QString normalizedText = referenceText.trimmed();
    const QString normalizedModel = model.trimmed().toLower();
    if (normalizedModel.isEmpty() || normalizedName.isEmpty() || normalizedText.isEmpty()) {
        reference->close();
        delete reference;
        if (errorMessage) *errorMessage = QStringLiteral("Voice-cloning model, name, and exact reference transcript are required");
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v2/jobs/profile")));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "application/json");
    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    multipart->append(formField("model", normalizedModel.toUtf8()));
    multipart->append(formField("name", normalizedName.toUtf8()));
    multipart->append(formField("consent_confirmed", "true"));
    multipart->append(formField("ref_text", normalizedText.toUtf8()));
    multipart->append(formField("language", language.trimmed().isEmpty() ? QByteArrayLiteral("vi") : language.trimmed().toUtf8()));
    multipart->append(formField("separate_music", separateMusic ? QByteArrayLiteral("true") : QByteArrayLiteral("false")));
    QHttpPart audioPart;
    const QString sourceFilename = QFileInfo(reference->fileName()).fileName();
    const QString filename = sourceFilename.isEmpty() ? QStringLiteral("reference.wav") : sourceFilename;
    audioPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QVariant(QStringLiteral("form-data; name=\"ref_audio\"; filename=\"%1\"").arg(filename)));
    audioPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(referenceMimeType));
    reference->setParent(multipart);
    audioPart.setBodyDevice(reference);
    multipart->append(audioPart);
    QNetworkReply *reply = manager.post(request, multipart);
    multipart->setParent(reply);
    m_activeReply = reply;
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    m_activeReply = nullptr;
    const bool ok = parseJsonResponse(reply, nullptr, job, errorMessage,
                                      QStringLiteral("Colab worker returned an invalid profile job"));
    reply->deleteLater();
    return ok;
}

bool ColabWorkerClient::createVoiceGenerationJob(const QString &model, const QString &profileId,
                                                 const QString &text,
                                                 const QString &language, float speed, int steps,
                                                 QJsonObject *job, QString *errorMessage)
{
    if (job) *job = {};
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker is not connected");
        return false;
    }
    const QString normalizedModel = model.trimmed().toLower();
    if (normalizedModel.isEmpty() || profileId.trimmed().isEmpty() || text.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Voice-cloning model, profile, and text are required");
        return false;
    }
    const QJsonObject payload{{QStringLiteral("model"), normalizedModel},
                              {QStringLiteral("profile_id"), profileId.trimmed()},
                              {QStringLiteral("text"), text.trimmed()},
                              {QStringLiteral("language"), language.trimmed().isEmpty() ? QStringLiteral("vi") : language.trimmed()},
                              {QStringLiteral("speed"), qBound(0.1F, speed, 2.0F)},
                              {QStringLiteral("num_step"), qBound(1, steps, 64)}};
    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v2/jobs/generation")));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_activeReply = reply;
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    m_activeReply = nullptr;
    const bool ok = parseJsonResponse(reply, nullptr, job, errorMessage,
                                      QStringLiteral("Colab worker returned an invalid generation job"));
    reply->deleteLater();
    return ok;
}

bool ColabWorkerClient::voiceJobStatus(const QString &jobId, QJsonObject *job, QString *errorMessage)
{
    if (job) *job = {};
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty() || jobId.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker job is unavailable");
        return false;
    }
    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v2/jobs/%1").arg(jobId.trimmed())));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = manager.get(request);
    m_activeReply = reply;
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    m_activeReply = nullptr;
    const bool ok = parseJsonResponse(reply, nullptr, job, errorMessage,
                                      QStringLiteral("Colab worker returned an invalid job status"));
    reply->deleteLater();
    return ok;
}

bool ColabWorkerClient::cancelVoiceJob(const QString &jobId, QString *errorMessage)
{
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty() || jobId.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker job is unavailable");
        return false;
    }
    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v2/jobs/%1").arg(jobId.trimmed())));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = manager.sendCustomRequest(request, "DELETE");
    m_activeReply = reply;
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    m_activeReply = nullptr;
    QJsonObject ignored;
    const bool ok = parseJsonResponse(reply, nullptr, &ignored, errorMessage,
                                      QStringLiteral("Colab worker returned an invalid cancellation response"));
    reply->deleteLater();
    return ok;
}

bool ColabWorkerClient::downloadVoiceJobAudio(const QString &jobId, QByteArray *wavData, QString *errorMessage)
{
    if (wavData) wavData->clear();
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty() || jobId.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker job is unavailable");
        return false;
    }
    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v2/jobs/%1/audio").arg(jobId.trimmed())));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "audio/wav, application/octet-stream");
    QNetworkReply *reply = manager.get(request);
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
    if (networkError != QNetworkReply::NoError || statusCode < 200 || statusCode >= 300) {
        if (errorMessage) {
            *errorMessage = statusCode >= 400 ? responseError(body, statusCode)
                                               : QStringLiteral("Colab worker request failed: %1").arg(networkErrorText);
        }
        return false;
    }
    if (body.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker returned empty voice audio");
        return false;
    }
    if (wavData) *wavData = body;
    return true;
}

bool ColabWorkerClient::deleteVoiceProfile(const QString &profileId, QString *errorMessage)
{
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty() || profileId.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab voice profile is unavailable");
        return false;
    }
    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v1/profiles/%1").arg(profileId.trimmed())));
    request.setTransferTimeout(kInferenceRequestTimeoutMs);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = manager.sendCustomRequest(request, "DELETE");
    m_activeReply = reply;
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    m_activeReply = nullptr;
    QJsonObject ignored;
    const bool ok = parseJsonResponse(reply, nullptr, &ignored, errorMessage,
                                      QStringLiteral("Colab worker returned an invalid profile deletion response"));
    reply->deleteLater();
    return ok;
}

void ColabWorkerClient::cancel()
{
    if (m_activeReply) m_activeReply->abort();
}

} // namespace LAStudio
