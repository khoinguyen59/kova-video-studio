#pragma once

#include "inference/InferenceCancellation.h"

#include <QObject>
#include <QByteArray>
#include <QVector>

#include <memory>

namespace LAStudio {

struct GatewayTtsRequest {
    QString gatewayUrl;
    QString apiKey;
    QString model;
    QString text;
    QString voice;
    float speed = 1.0F;
    InferenceCancellationToken cancellation;
    bool allowInsecureLocalhost = false;
};

class GatewayTtsRunner final : public QObject
{
    Q_OBJECT
public:
    explicit GatewayTtsRunner(QObject *parent = nullptr);
    ~GatewayTtsRunner() override;

public slots:
    void synthesize(const GatewayTtsRequest &request);
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

Q_DECLARE_METATYPE(LAStudio::GatewayTtsRequest)
