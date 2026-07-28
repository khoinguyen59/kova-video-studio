#pragma once

#include "inference/InferenceCancellation.h"

#include <QObject>
#include <QUrl>

#include <memory>

namespace LAStudio {

struct ColabSeparationRequest {
    QUrl workerUrl;
    QString bearerToken;
    QString audioPath;
    QString outputRoot;
    QString model = QStringLiteral("htdemucs");
    InferenceCancellationToken cancellation;
    bool allowInsecureLocalhost = false;
};

struct ColabSeparationResult {
    QString vocalsPath;
    QString backgroundPath;
    QString jobId;
};

// Direct temporary Colab separation runner. It intentionally has no Gateway dependency.
class ColabSeparationRunner final : public QObject
{
    Q_OBJECT
public:
    explicit ColabSeparationRunner(QObject *parent = nullptr);
    ~ColabSeparationRunner() override;

public slots:
    void separate(const LAStudio::ColabSeparationRequest &request);
    void cancel();

signals:
    void progress(int percent);
    void finished(const LAStudio::ColabSeparationResult &result);
    void failed(const QString &error);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace LAStudio

Q_DECLARE_METATYPE(LAStudio::ColabSeparationRequest)
Q_DECLARE_METATYPE(LAStudio::ColabSeparationResult)
