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
    bool configure(const QUrl &workerUrl, const QString &bearerToken,
                   bool allowInsecureLocalhost, QString *errorMessage = nullptr);
    void clear();
    void cancel();

    bool transcribeWav(const QByteArray &wavData, const QString &model, const QString &language,
                       const std::shared_ptr<std::atomic_bool> &cancelToken,
                       QJsonObject *response, QString *errorMessage);
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
                             QJsonObject *job, QString *errorMessage);
    bool separationJobStatus(const QString &jobId, QJsonObject *job, QString *errorMessage);
    bool downloadSeparationArtifact(const QString &jobId, const QString &stem,
                                    const std::shared_ptr<std::atomic_bool> &cancelToken,
                                    QByteArray *wavData, QString *errorMessage);
    bool cancelSeparationJob(const QString &jobId, QString *errorMessage = nullptr);
    bool translateSegments(const QVariantList &segments, const QString &sourceLanguage,
                           const QString &targetLanguage, const QString &model,
                           const std::shared_ptr<std::atomic_bool> &cancelToken,
                           QJsonObject *response, QString *errorMessage);
    bool streamChat(const QList<QVariantMap> &messages, const QString &model, int maxTokens,
                    float temperature, float topP,
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
