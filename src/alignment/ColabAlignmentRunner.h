#pragma once

#include "inference/InferenceCancellation.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>

#include <memory>

namespace LAStudio {

struct ColabAlignmentRequest {
    QUrl workerUrl;
    QString bearerToken;
    QString audioPath;
    QString transcript;
    QString language;
    QString outputFormat;
    QString model = QStringLiteral("qwen3-forced-aligner-0.6b");
    InferenceCancellationToken cancellation;
    bool allowInsecureLocalhost = false;
};

struct ColabAlignmentResult {
    QVariantList segments;
    QVariantList unalignedTokens;
    double duration = 0.0;
    QString output;
};

// This runner talks only to a temporary Colab worker. It has no API Gateway
// dependency and cannot forward alignment data through a gateway.
class ColabAlignmentRunner final : public QObject
{
    Q_OBJECT
public:
    explicit ColabAlignmentRunner(QObject *parent = nullptr);
    ~ColabAlignmentRunner() override;

public slots:
    void align(const ColabAlignmentRequest &request);
    void cancel();

signals:
    void progress(int percent);
    void finished(const LAStudio::ColabAlignmentResult &result);
    void failed(const QString &error);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace LAStudio

Q_DECLARE_METATYPE(LAStudio::ColabAlignmentRequest)
Q_DECLARE_METATYPE(LAStudio::ColabAlignmentResult)
