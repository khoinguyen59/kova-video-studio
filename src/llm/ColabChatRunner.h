#pragma once

#include <QList>
#include <QObject>
#include <QUrl>
#include <QVariantMap>

#include <QtQml/qqml.h>

namespace LAStudio {

// Request data for one direct Colab chat stream. It intentionally does not
// contain API Gateway settings or credentials.
struct ColabChatRequest
{
    QUrl workerUrl;
    QString bearerToken;
    QString model;
    QList<QVariantMap> messages;
    int maxTokens = 1024;
    int contextTokens = 4096;
    float temperature = 0.7F;
    float topP = 0.8F;
    int topK = 20;
    float repeatPenalty = 1.05F;
    QString requestId;
    bool allowInsecureLocalhost = false;
};

class ColabChatRunner final : public QObject
{
    Q_OBJECT
public:
    explicit ColabChatRunner(QObject *parent = nullptr);
    ~ColabChatRunner() override;

public slots:
    void generate(const LAStudio::ColabChatRequest &request);
    void cancel();

signals:
    void tokenGenerated(const QString &requestId, const QString &token);
    void finished(const QString &requestId, const QString &text);
    void cancelled(const QString &requestId, const QString &text);
    void failed(const QString &requestId, const QString &error);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace LAStudio

Q_DECLARE_METATYPE(LAStudio::ColabChatRequest)
