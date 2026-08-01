#include "subtitles/ColabSubtitleOcrRunner.h"

#include "remote/ColabWorkerClient.h"

namespace LAStudio {

class ColabSubtitleOcrRunner::Private final
{
public:
    ColabWorkerClient client;
};

ColabSubtitleOcrRunner::ColabSubtitleOcrRunner(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
}

ColabSubtitleOcrRunner::~ColabSubtitleOcrRunner() = default;

void ColabSubtitleOcrRunner::recognize(const QUrl &workerUrl, const QString &bearerToken,
                                        const QString &model, const QString &language,
                                        const QByteArray &croppedFramePng,
                                        bool allowInsecureLocalhost)
{
    QString error;
    if (!d->client.configure(workerUrl, bearerToken, allowInsecureLocalhost, &error)) {
        emit failed(error);
        return;
    }
    QString text;
    double confidence = 0.0;
    if (!d->client.recognizeSubtitleImage(croppedFramePng, model, language, &text, &confidence,
                                          &error)) {
        emit failed(error.isEmpty() ? QStringLiteral("Colab Subtitle OCR request was cancelled.") : error);
        return;
    }
    emit finished(text, confidence);
}

void ColabSubtitleOcrRunner::cancel()
{
    d->client.cancel();
}

} // namespace LAStudio
