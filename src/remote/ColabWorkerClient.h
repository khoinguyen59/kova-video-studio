#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QNetworkReply;
QT_END_NAMESPACE

namespace LAStudio {

// Direct client for a temporary Colab worker. The worker URL/token are never
// persisted here and this class intentionally has no API Gateway dependency.
class ColabWorkerClient final
{
public:
    using UploadProgressCallback = std::function<void(qint64 sent, qint64 total)>;
    // A remote job becoming ready only means the WAV artifacts are available
    // on Colab.  Keep the return transfer independently observable.
    using DownloadProgressCallback = std::function<void(qint64 received, qint64 total)>;

    bool configure(const QUrl &workerUrl, const QString &bearerToken,
                   bool allowInsecureLocalhost, QString *errorMessage = nullptr);
    void clear();
    void cancel();

    bool transcribeWav(const QByteArray &wavData, const QString &model, const QString &language,
                       const std::shared_ptr<std::atomic_bool> &cancelToken,
                       QJsonObject *response, QString *errorMessage);
    // Long recordings must not keep a Cloudflare tunnel request open while the
    // GPU is transcribing.  The exact STT notebooks accept the upload, return
    // a job id promptly, and expose the result through short status requests.
    bool createTranscriptionJob(const QByteArray &wavData, const QString &model,
                                const QString &language, QJsonObject *job,
                                QString *errorMessage,
                                const UploadProgressCallback &uploadProgress = {});
    bool transcriptionJobStatus(const QString &jobId, QJsonObject *job,
                                QString *errorMessage);
    bool cancelTranscriptionJob(const QString &jobId,
                                QString *errorMessage = nullptr);
    bool synthesizeSpeech(const QString &text, const QString &model, const QString &voice,
                          const QString &language, float speed, const QVariantMap &settings,
                          const std::shared_ptr<std::atomic_bool> &cancelToken,
                          QByteArray *wavData, QString *errorMessage);
    bool designVoice(const QString &text, const QString &model, const QString &voiceDescription,
                     const QString &style, const QString &language, float temperature, qint64 seed,
                     const std::shared_ptr<std::atomic_bool> &cancelToken,
                     QByteArray *wavData, QString *errorMessage);
    bool alignAudioFile(const QString &audioPath, const QString &transcript, const QString &language,
                        const QString &model, const std::shared_ptr<std::atomic_bool> &cancelToken,
                        QJsonObject *response, QString *errorMessage);
    bool createSeparationJob(const QString &audioPath, const QString &model,
                             const QString &artifactFormat,
                             QJsonObject *job, QString *errorMessage);
    bool separationJobStatus(const QString &jobId, QJsonObject *job, QString *errorMessage);
    bool downloadSeparationArtifact(const QString &jobId, const QString &stem,
                                    const QString &artifactFormat,
                                    const std::shared_ptr<std::atomic_bool> &cancelToken,
                                    QByteArray *artifactData, QString *errorMessage,
                                    const DownloadProgressCallback &downloadProgress = {});
    bool cancelSeparationJob(const QString &jobId, QString *errorMessage = nullptr);
    bool translateSegments(const QVariantList &segments, const QString &sourceLanguage,
                           const QString &targetLanguage, const QString &model,
                           const std::shared_ptr<std::atomic_bool> &cancelToken,
                           QJsonObject *response, QString *errorMessage);
    // The caller supplies an already-cropped ROI frame, never a source video.
    // An empty text string is a valid "no subtitle in this sample" result;
    // malformed/missing fields remain a contract failure.
    bool recognizeSubtitleImage(const QByteArray &pngData, const QString &model,
                                const QString &language, QString *text, double *confidence,
                                QString *errorMessage);
    bool streamChat(const QList<QVariantMap> &messages, const QString &model, int maxTokens,
                    int contextTokens, float temperature, float topP, int topK,
                    float repeatPenalty,
                    const std::shared_ptr<std::atomic_bool> &cancelToken,
                    const std::function<void(const QString &)> &tokenHandler,
                    QString *fullText, QString *errorMessage);
    bool createVoiceProfileJob(const QString &model, const QString &referencePath, const QString &name,
                               const QString &referenceText, const QString &language,
                               bool separateMusic, QJsonObject *job, QString *errorMessage);
    bool createVoiceGenerationJob(const QString &model, const QString &profileId, const QString &text,
                                  const QString &language, float speed, int steps,
                                  QJsonObject *job, QString *errorMessage);
    bool voiceJobStatus(const QString &jobId, QJsonObject *job, QString *errorMessage);
    bool cancelVoiceJob(const QString &jobId, QString *errorMessage = nullptr);
    bool downloadVoiceJobAudio(const QString &jobId, QByteArray *wavData, QString *errorMessage);
    bool deleteVoiceProfile(const QString &profileId, QString *errorMessage = nullptr);

private:
    QUrl m_workerUrl;
    QString m_bearerToken;
    QNetworkReply *m_activeReply = nullptr;
};

} // namespace LAStudio
