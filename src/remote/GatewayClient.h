#pragma once

#include <QList>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QNetworkReply;
QT_END_NAMESPACE

namespace LAStudio {

// Small, purpose-built OpenAI-compatible client for 9Router. It has no
// knowledge of Colab sessions or local model runtimes.
class GatewayClient final
{
public:
    struct ChatOptions {
        int maxTokens = 1024;
        float temperature = 0.7F;
        float topP = 0.8F;
    };

    bool configure(const QString &baseUrl, const QString &apiKey, const QString &model,
                   bool allowInsecureLocalhost, QString *errorMessage = nullptr);
    bool isConfigured() const;
    void clear();

    bool streamChat(const QList<QVariantMap> &messages, const ChatOptions &options,
                    const std::shared_ptr<std::atomic_bool> &cancelToken,
                    const std::function<void(const QString &)> &tokenHandler,
                    QString *fullText, QString *errorMessage);
    bool transcribeWav(const QByteArray &wavData, const QString &language,
                       const std::shared_ptr<std::atomic_bool> &cancelToken,
                       QJsonObject *response, QString *errorMessage);
    bool synthesizeSpeech(const QString &text, const QString &voice, float speed,
                          const std::shared_ptr<std::atomic_bool> &cancelToken,
                          QByteArray *wavData, QString *errorMessage);
    void cancel();

private:
    QUrl m_baseUrl;
    QString m_apiKey;
    QString m_model;
    QNetworkReply *m_activeReply = nullptr;
};

} // namespace LAStudio
