#pragma once

#include "inference/InferenceCancellation.h"

#include <QObject>
#include <QVariantList>
#include <QVector>

#include <memory>

namespace LAStudio {

struct GatewaySttRequest {
    QString gatewayUrl;
    QString apiKey;
    QString model;
    QVector<float> samples;
    QString language;
    InferenceCancellationToken cancellation;
    bool allowInsecureLocalhost = false;
};

class GatewaySttRunner final : public QObject
{
    Q_OBJECT
public:
    explicit GatewaySttRunner(QObject *parent = nullptr);
    ~GatewaySttRunner() override;

public slots:
    void transcribe(const GatewaySttRequest &request);
    void cancel();

signals:
    void progress(int percent);
    void finished(const QString &text, const QVariantList &segments);
    void failed(const QString &error);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace LAStudio

Q_DECLARE_METATYPE(LAStudio::GatewaySttRequest)
