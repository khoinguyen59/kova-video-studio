#include "ColabWorkerClient.h"

#include "ExecutionProvider.h"

#include <QBuffer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
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

    QFile *audio = new QFile(audioPath);
    constexpr qint64 maxUploadBytes = 512LL * 1024LL * 1024LL;
    if (!audio->open(QIODevice::ReadOnly) || audio->size() <= 0 || audio->size() > maxUploadBytes) {
        delete audio;
        if (errorMessage) *errorMessage = QStringLiteral("Alignment audio must be a readable file no larger than 512 MB");
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v1/audio/alignments")));
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
    audioPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("application/octet-stream")));
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
    QFile *audio = new QFile(audioPath);
    constexpr qint64 maxUploadBytes = 512LL * 1024LL * 1024LL;
    if (!audio->open(QIODevice::ReadOnly) || audio->size() <= 0 || audio->size() > maxUploadBytes) {
        delete audio;
        if (errorMessage) *errorMessage = QStringLiteral("Separation audio must be a readable file no larger than 512 MB");
        return false;
    }
    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v1/audio/separations")));
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
    audioPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("application/octet-stream")));
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

bool ColabWorkerClient::createVoiceProfileJob(const QString &referencePath, const QString &name,
                                              const QString &referenceText, const QString &language,
                                              bool separateMusic, QJsonObject *job, QString *errorMessage)
{
    if (job) *job = {};
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker is not connected");
        return false;
    }
    QFile *reference = new QFile(referencePath);
    if (!reference->open(QIODevice::ReadOnly) || reference->size() <= 0) {
        delete reference;
        if (errorMessage) *errorMessage = QStringLiteral("Reference audio could not be read");
        return false;
    }
    const QString normalizedName = name.trimmed();
    const QString normalizedText = referenceText.trimmed();
    if (normalizedName.isEmpty() || normalizedText.isEmpty()) {
        reference->close();
        delete reference;
        if (errorMessage) *errorMessage = QStringLiteral("Voice name and exact reference transcript are required");
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v2/jobs/profile")));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "application/json");
    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
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
    audioPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("application/octet-stream")));
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

bool ColabWorkerClient::createVoiceGenerationJob(const QString &profileId, const QString &text,
                                                 const QString &language, float speed, int steps,
                                                 QJsonObject *job, QString *errorMessage)
{
    if (job) *job = {};
    if (!m_workerUrl.isValid() || m_bearerToken.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker is not connected");
        return false;
    }
    if (profileId.trimmed().isEmpty() || text.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Voice profile and text are required");
        return false;
    }
    const QJsonObject payload{{QStringLiteral("profile_id"), profileId.trimmed()},
                              {QStringLiteral("text"), text.trimmed()},
                              {QStringLiteral("language"), language.trimmed().isEmpty() ? QStringLiteral("vi") : language.trimmed()},
                              {QStringLiteral("speed"), qBound(0.1F, speed, 2.0F)},
                              {QStringLiteral("num_step"), qBound(1, steps, 64)}};
    QNetworkAccessManager manager;
    QNetworkRequest request(appendRemotePath(m_workerUrl, QStringLiteral("v2/jobs/generation")));
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
