#pragma once

#include "inference/InferenceCancellation.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVector>

#include <memory>

namespace LAStudio {

struct ColabSttRequest {
    QUrl workerUrl;
    QString bearerToken;
    QString model;
    QVector<float> samples;
    QString language;
    InferenceCancellationToken cancellation;
    bool allowInsecureLocalhost = false;
};

class ColabSttRunner final : public QObject
{
    Q_OBJECT
public:
    explicit ColabSttRunner(QObject *parent = nullptr);
    ~ColabSttRunner() override;

public slots:
    void transcribe(const ColabSttRequest &request);
    void cancel();

private slots:
    void pollActiveJob();

signals:
    void progress(int percent);
    void finished(const QString &text, const QVariantList &segments);
    void failed(const QString &error);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace LAStudio

Q_DECLARE_METATYPE(LAStudio::ColabSttRequest)
