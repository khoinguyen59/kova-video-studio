#include "TtsWorker.h"
#include "backends/TtsBackend.h"
#include "backends/TtsBackendFactory.h"
#include "core/Logger.h"
#include <QFileInfo>
#include <QStringList>

namespace LAStudio {

TtsWorker::TtsWorker(QObject *parent)
    : QObject(parent)
{
}

TtsWorker::~TtsWorker()
{
    unloadVoice();
}

void TtsWorker::loadVoice(const QVariantMap &config)
{
    QString modelPath = config.value("model").toString();
    Logger::info("TtsWorker", QString("Starting to load voice model: %1").arg(modelPath));

    unloadVoice();

    m_backend = TtsBackendFactory::create(config);
    if (!m_backend) {
        Logger::error("TtsWorker", "Failed to create TTS backend: Unsupported configuration");
        emit modelLoaded(false, QStringLiteral("Unsupported backend configuration"), QVariantList());
        return;
    }
    m_backend->setProgressCallback([this](int current, int total, const QString &stage, int chunkIndex, int chunkCount) {
        emit progress(current, total, stage, chunkIndex, chunkCount);
        return !m_cancelRequested.load();
    });

    QString error;
    QVariantList schema;
    if (m_backend->load(config, error, schema)) {
        m_activeModelPath = modelPath;
        emit modelLoaded(true, {}, schema);
    } else {
        m_backend.reset();
        m_activeModelPath.clear();
        emit modelLoaded(false, error, QVariantList());
    }
}

void TtsWorker::unloadVoice()
{
    if (m_backend) {
        Logger::info("TtsWorker", "unloadVoice called");
        m_backend->unload();
        m_backend.reset();
    }
    m_activeModelPath.clear();
}

void TtsWorker::synthesize(const QString &text, int speakerId, float speed, const QVariantMap &settings)
{
    Q_UNUSED(speakerId);
    m_cancelRequested = false;
    if (!m_backend) {
        emit errorOccurred(QStringLiteral("No TTS model loaded"));
        return;
    }

    QStringList settingsKeyList;
    for (auto it = settings.cbegin(); it != settings.cend(); ++it) settingsKeyList.append(it.key());
    const QString settingsKeys = settingsKeyList.isEmpty() ? QStringLiteral("None")
                                                            : settingsKeyList.join(QStringLiteral(", "));
    QString modelName = QFileInfo(m_activeModelPath).fileName();

    Logger::info("TtsWorker", QStringLiteral("Synthesizing: model=\"%1\", textLength=%2, speed=%3, settingsKeys={%4}")
        .arg(modelName)
        .arg(text.length())
        .arg(speed)
        .arg(settingsKeys));

    QVector<float> samples;
    int sampleRate = 24000;
    QString error;

    if (m_backend->synthesize(text, speed, settings, samples, sampleRate, error)) {
        if (m_cancelRequested.load()) {
            emit errorOccurred(QStringLiteral("TTS synthesis was canceled."));
            return;
        }
        emit finished(samples, sampleRate);
    } else {
        emit errorOccurred(error);
    }
}

void TtsWorker::cloneVoice(const QString &text, const QString &referencePath, const QVariantMap &settings)
{
    m_cancelRequested = false;
    if (!m_backend) {
        emit errorOccurred(QStringLiteral("No TTS model loaded"));
        return;
    }

    QStringList settingsKeyList;
    for (auto it = settings.cbegin(); it != settings.cend(); ++it) settingsKeyList.append(it.key());
    const QString settingsKeys = settingsKeyList.isEmpty() ? QStringLiteral("None")
                                                            : settingsKeyList.join(QStringLiteral(", "));
    QString modelName = QFileInfo(m_activeModelPath).fileName();

    Logger::info("TtsWorker", QStringLiteral("Cloning voice: model=\"%1\", textLength=%2, settingsKeys={%3}")
        .arg(modelName)
        .arg(text.length())
        .arg(settingsKeys));

    QVector<float> samples;
    int sampleRate = 24000;
    QString error;

    if (m_backend->cloneVoice(text, referencePath, settings, samples, sampleRate, error)) {
        if (m_cancelRequested.load()) {
            emit errorOccurred(QStringLiteral("TTS synthesis was canceled."));
            return;
        }
        emit finished(samples, sampleRate);
    } else {
        emit errorOccurred(error);
    }
}

void TtsWorker::cancelProcessing()
{
    m_cancelRequested = true;
    if (m_backend) {
        m_backend->cancelProcessing();
    }
}

} // namespace LAStudio
