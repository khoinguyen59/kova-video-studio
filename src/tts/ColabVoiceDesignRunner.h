#pragma once

#include "inference/InferenceCancellation.h"

#include <QObject>
#include <QByteArray>
#include <QUrl>
#include <QVector>

#include <memory>

namespace LAStudio {

struct ColabVoiceDesignRequest {
    QUrl workerUrl;
    QString bearerToken;
    QString model;
    QString text;
    QString voiceDescription;
    QString style;
    QString language;
    float temperature = 0.9F;
    qint64 seed = -1;
    InferenceCancellationToken cancellation;
    bool allowInsecureLocalhost = false;
};

// Direct Colab VoiceDesign runner. It intentionally has no Gateway client.
class ColabVoiceDesignRunner final : public QObject
{
    Q_OBJECT
public:
    explicit ColabVoiceDesignRunner(QObject *parent = nullptr);
    ~ColabVoiceDesignRunner() override;

public slots:
    void generate(const ColabVoiceDesignRequest &request);
    void cancel();

signals:
    void progress(int percent);
    void finished(const QByteArray &pcm16, const QVector<float> &samples, int sampleRate);
    void failed(const QString &error);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace LAStudio

Q_DECLARE_METATYPE(LAStudio::ColabVoiceDesignRequest)
