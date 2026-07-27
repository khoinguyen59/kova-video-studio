#pragma once

#include "RuntimeHostClient.h"
#include "RuntimeHostManager.h"
#include "stt/backends/SttBackend.h"

namespace LAStudio {

class HostedWhisperBackend final : public SttBackend {
public:
    HostedWhisperBackend();
    ~HostedWhisperBackend() override;
    bool loadModel(const QString &modelPath, bool useGpu, const QString &runtimePath, QString &error) override;
    void unloadModel() override;
    void cancelProcessing() override;
    void setProgressCallback(std::function<void(int percent)> callback) override;
    bool transcribe(const QVector<float> &samples,
                    const QString &language,
                    int threads,
                    bool translate,
                    const QVariantMap &settings,
                    QString &fullText,
                    QVariantList &segments,
                    QString &error) override;

private:
    bool startHost(QString &error);
    bool ensureHost(QString &error);
    void releaseHostPermit();
    RuntimeHostClient m_client;
    QString m_modelPath;
    QString m_runtimePath;
    bool m_useGpu = false;
    std::function<void(int percent)> m_progressCallback;
    bool m_gpuPermit = false;
    bool m_permitAcquired = false;
    QString m_runtimeFamily = QStringLiteral("whisper");
};

} // namespace LAStudio
