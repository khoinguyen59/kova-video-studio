#include "HostedLlamaTranslationBackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace LAStudio {

HostedLlamaTranslationBackend::HostedLlamaTranslationBackend()
{
    QObject::connect(&m_client, &RuntimeHostClient::hostExited, [this](const QString &) {
        releaseHostPermit();
        m_loaded = false;
    });
}

HostedLlamaTranslationBackend::~HostedLlamaTranslationBackend()
{
    unloadModel();
}

bool HostedLlamaTranslationBackend::loadModel(const TranslationBackendConfiguration &configuration,
                                              QString &error)
{
    unloadModel();
    m_configuration = configuration;
    m_hasConfiguration = true;
    return startHost(error);
}

bool HostedLlamaTranslationBackend::startHost(QString &error)
{
    if (!m_hasConfiguration || m_configuration.modelPath.isEmpty()) {
        error = QStringLiteral("Llama runtime has no saved model configuration.");
        return false;
    }
    releaseHostPermit();
    const bool gpu = m_configuration.useGpu
                     || m_configuration.runtimePath.contains(QStringLiteral("cuda"), Qt::CaseInsensitive);
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
        {QStringLiteral("adapter"), QStringLiteral("llama")},
        {QStringLiteral("model"), m_configuration.modelPath},
        {QStringLiteral("runtimePath"), m_configuration.runtimePath},
        {QStringLiteral("useGpu"), m_configuration.useGpu},
        {QStringLiteral("threads"), m_configuration.threads}
    };
    QCborValue schema;
    if (!m_client.load(config, &schema, &error)) {
        m_client.shutdown();
        releaseHostPermit();
        return false;
    }
    m_loaded = true;
    return true;
}

bool HostedLlamaTranslationBackend::ensureHost(QString &error)
{
    return m_client.isRunning() || startHost(error);
}

void HostedLlamaTranslationBackend::unloadModel()
{
    QString ignored;
    m_client.shutdown(&ignored);
    releaseHostPermit();
    m_loaded = false;
    m_hasConfiguration = false;
    m_configuration = {};
}

void HostedLlamaTranslationBackend::cancelProcessing()
{
    m_client.cancelCurrent();
}

bool HostedLlamaTranslationBackend::translate(const TranslationInferenceRequest &request,
                                              QVariantList &patches,
                                              TranslationProgressCallback progress,
                                              QString &error)
{
    if (!m_loaded && !m_hasConfiguration) {
        error = QStringLiteral("Llama runtime is not loaded.");
        return false;
    }
    if (!ensureHost(error)) return false;
    const QCborMap payload{
        {QStringLiteral("mode"), QStringLiteral("translate")},
        {QStringLiteral("segments"), QCborValue::fromVariant(request.segments)},
        {QStringLiteral("sourceLanguage"), request.sourceLanguage},
        {QStringLiteral("targetLanguage"), request.targetLanguage},
        {QStringLiteral("task"), request.task},
        {QStringLiteral("maxTokens"), request.maxTokens}
    };
    QCborMap result;
    m_client.setProgressCallback([&progress](const QCborMap &progressPayload) {
        if (!progress) return;
        const qint64 current = progressPayload.value(QStringLiteral("current")).toInteger();
        const qint64 total = progressPayload.value(QStringLiteral("total")).toInteger();
        if (total > 0) progress(static_cast<int>(current * 100 / total));
    });
    const bool executed = m_client.execute(payload, {}, &result, nullptr, nullptr, &error);
    m_client.setProgressCallback({});
    if (!executed) return false;
    patches = result.value(QStringLiteral("patches")).toVariant().toList();
    if (progress) progress(100);
    return true;
}

void HostedLlamaTranslationBackend::releaseHostPermit()
{
    if (!m_permitAcquired) return;
    RuntimeHostManager::instance().release(m_runtimeFamily, m_gpuPermit);
    m_permitAcquired = false;
    m_gpuPermit = false;
}

} // namespace LAStudio
