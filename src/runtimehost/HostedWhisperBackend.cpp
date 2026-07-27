#include "HostedWhisperBackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace LAStudio {

HostedWhisperBackend::HostedWhisperBackend()
{
    QObject::connect(&m_client, &RuntimeHostClient::hostExited, [this](const QString &) {
        releaseHostPermit();
    });
}

HostedWhisperBackend::~HostedWhisperBackend()
{
    unloadModel();
}

bool HostedWhisperBackend::loadModel(const QString &modelPath, bool useGpu,
                                     const QString &runtimePath, QString &error)
{
    unloadModel();
    m_modelPath = modelPath;
    m_runtimePath = runtimePath;
    m_useGpu = useGpu;
    return startHost(error);
}

bool HostedWhisperBackend::startHost(QString &error)
{
    if (m_modelPath.isEmpty()) {
        error = QStringLiteral("Whisper model path is empty.");
        return false;
    }
    // A crashed process may not deliver its queued hostExited signal before
    // the worker retries. Release is idempotent and prevents a leaked GPU slot.
    releaseHostPermit();
    const bool gpu = m_useGpu || m_runtimePath.contains(QStringLiteral("cuda"), Qt::CaseInsensitive);
    if (!RuntimeHostManager::instance().acquire(m_runtimeFamily, gpu, &error)) return false;
    m_gpuPermit = gpu;
    m_permitAcquired = true;
    const QString hostPath = QDir(QCoreApplication::applicationDirPath())
                                 .absoluteFilePath(QStringLiteral("LAStudioRuntimeHost.exe"));
    if (!QFileInfo(hostPath).isFile()) {
        error = QStringLiteral("LAStudioRuntimeHost.exe is missing: %1").arg(hostPath);
        releaseHostPermit();
        return false;
    }
    if (!m_client.start(hostPath, &error)) {
        releaseHostPermit();
        return false;
    }
    const QCborMap config{
        {QStringLiteral("adapter"), QStringLiteral("whisper")},
        {QStringLiteral("model"), m_modelPath},
        {QStringLiteral("runtimePath"), m_runtimePath},
        {QStringLiteral("useGpu"), m_useGpu}
    };
    QCborValue ignoredSchema;
    if (!m_client.load(config, &ignoredSchema, &error)) {
        m_client.shutdown();
        releaseHostPermit();
        return false;
    }
    return true;
}

bool HostedWhisperBackend::ensureHost(QString &error)
{
    return m_client.isRunning() || startHost(error);
}

void HostedWhisperBackend::unloadModel()
{
    QString ignored;
    m_client.shutdown(&ignored);
    releaseHostPermit();
    m_modelPath.clear();
    m_runtimePath.clear();
    m_useGpu = false;
}

void HostedWhisperBackend::cancelProcessing()
{
    m_client.cancelCurrent();
}

void HostedWhisperBackend::setProgressCallback(std::function<void(int percent)> callback)
{
    m_progressCallback = std::move(callback);
    m_client.setProgressCallback([this](const QCborMap &payload) {
        if (!m_progressCallback) return;
        const int current = static_cast<int>(payload.value(QStringLiteral("current")).toInteger());
        const int total = static_cast<int>(payload.value(QStringLiteral("total")).toInteger());
        if (total > 0) m_progressCallback(qBound(0, current * 100 / total, 100));
    });
}

bool HostedWhisperBackend::transcribe(const QVector<float> &samples,
                                      const QString &language,
                                      int threads,
                                      bool translate,
                                      const QVariantMap &settings,
                                      QString &fullText,
                                      QVariantList &segments,
                                      QString &error)
{
    if (samples.isEmpty()) {
        error = QStringLiteral("Whisper input audio is empty.");
        return false;
    }
    if (!ensureHost(error)) return false;
    const QCborMap request{
        {QStringLiteral("mode"), QStringLiteral("transcribe")},
        {QStringLiteral("language"), language},
        {QStringLiteral("threads"), threads},
        {QStringLiteral("translate"), translate},
        {QStringLiteral("settings"), QCborValue::fromVariant(settings)}
    };
    QCborMap result;
    if (!m_client.execute(request, samples, &result, nullptr, nullptr, &error, 16000)) return false;
    fullText = result.value(QStringLiteral("fullText")).toString();
    segments = result.value(QStringLiteral("segments")).toVariant().toList();
    return true;
}

void HostedWhisperBackend::releaseHostPermit()
{
    if (!m_permitAcquired) return;
    RuntimeHostManager::instance().release(m_runtimeFamily, m_gpuPermit);
    m_permitAcquired = false;
    m_gpuPermit = false;
}

} // namespace LAStudio
