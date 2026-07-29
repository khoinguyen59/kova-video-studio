#pragma once

#include "inference/InferenceCancellation.h"

#include <QObject>
#include <QByteArray>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

#include <memory>

namespace LAStudio {

struct ColabTtsRequest {
    QUrl workerUrl;
    QString bearerToken;
    QString model;
    QString text;
    QString voice;
    QString language;
    float speed = 1.0F;
    QVariantMap settings;
    InferenceCancellationToken cancellation;
    bool allowInsecureLocalhost = false;
};

class ColabTtsRunner final : public QObject
{
    Q_OBJECT
public:
    explicit ColabTtsRunner(QObject *parent = nullptr);
    ~ColabTtsRunner() override;

public slots:
    void synthesize(const ColabTtsRequest &request);
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

Q_DECLARE_METATYPE(LAStudio::ColabTtsRequest)
