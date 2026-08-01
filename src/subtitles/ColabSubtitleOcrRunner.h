#pragma once

#include <QObject>
#include <QUrl>

#include <memory>

namespace LAStudio {

// Executes one already-cropped subtitle frame on the direct Colab worker.
// It deliberately accepts image bytes rather than a video path: the desktop
// keeps source media local and uploads only the current ROI sample.
class ColabSubtitleOcrRunner final : public QObject
{
    Q_OBJECT
public:
    explicit ColabSubtitleOcrRunner(QObject *parent = nullptr);
    ~ColabSubtitleOcrRunner() override;

public slots:
    void recognize(const QUrl &workerUrl, const QString &bearerToken,
                   const QString &model, const QString &language,
                   const QByteArray &croppedFramePng,
                   bool allowInsecureLocalhost = false);
    void cancel();

signals:
    void finished(const QString &text, double confidence);
    void failed(const QString &error);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace LAStudio
