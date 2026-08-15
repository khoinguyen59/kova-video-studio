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
    QString model = QStringLiteral("sherpa-onnx-spleeter-2stems-fp16");
    // FLAC keeps the separated PCM stems lossless while avoiding the very
    // large 44.1 kHz stereo WAV transfer.  WAV remains available for an
    // operator or a legacy worker that explicitly needs it.
    QString artifactFormat = QStringLiteral("flac");
    InferenceCancellationToken cancellation;
    bool allowInsecureLocalhost = false;
    // Protocol guard, not a user setting. The exact worker writes its two
    // negotiated-format artifacts immediately after it reaches 90%; it must not keep the
    // desktop waiting indefinitely if it never transitions to ready.
    int finalizeTimeoutMs = 5 * 60 * 1000;
    int statusPollIntervalMs = 350;
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
    // Do not fabricate workflow percentages. Expose the current remote or
    // transfer phase separately, together with measured artifact bytes.
    void phaseChanged(const QString &phase);
    void artifactTransferProgress(const QString &artifact, qint64 receivedBytes, qint64 totalBytes);
    void finished(const LAStudio::ColabSeparationResult &result);
    void failed(const QString &error);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace LAStudio

Q_DECLARE_METATYPE(LAStudio::ColabSeparationRequest)
Q_DECLARE_METATYPE(LAStudio::ColabSeparationResult)
