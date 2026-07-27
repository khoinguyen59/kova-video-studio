#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <atomic>
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

    bool transcribeWav(const QByteArray &wavData, const QString &language,
                       const std::shared_ptr<std::atomic_bool> &cancelToken,
                       QJsonObject *response, QString *errorMessage);
    bool synthesizeSpeech(const QString &text, const QString &model, const QString &voice,
                          const QString &language, float speed, const QVariantMap &settings,
                          const std::shared_ptr<std::atomic_bool> &cancelToken,
                          QByteArray *wavData, QString *errorMessage);
    bool createVoiceProfileJob(const QString &referencePath, const QString &name,
                               const QString &referenceText, const QString &language,
                               bool separateMusic, QJsonObject *job, QString *errorMessage);
    bool createVoiceGenerationJob(const QString &profileId, const QString &text,
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
