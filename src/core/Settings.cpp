#include "Settings.h"
#include "MediaRuntimeLocator.h"
#include "PathUtils.h"
#include "Logger.h"
#include "SecureCredentialStore.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <QStorageInfo>
#include <QStandardPaths>

namespace LAStudio {

namespace {

// Schema 2 deliberately moves existing installations to the same safe
// execution policy as a fresh install: CPU for local work and no implicit
// remote route. GPU work is selected explicitly from the relevant Colab panel.
constexpr int kSettingsSchemaVersion = 2;

QString settingsFilePath()
{
    return PathUtils::dataDir() + QStringLiteral("/settings.json");
}

QString settingsIniPath()
{
    return PathUtils::dataDir() + QStringLiteral("/settings.ini");
}

QString preparedSettingsIniPath()
{
    const QString path = settingsIniPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!QFileInfo::exists(path)) {
        return path;
    }

    // Probe in a short-lived QSettings instance before the live instance is
    // created.  This lets us preserve a malformed INI as evidence instead of
    // allowing a later write to overwrite it.
    bool readable = false;
    {
        QSettings probe(path, QSettings::IniFormat);
        probe.allKeys();
        readable = probe.status() == QSettings::NoError;
    }
    if (readable) {
        return path;
    }

    const QString backup = path + QStringLiteral(".corrupt-")
        + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmsszzzZ"));
    if (QFile::rename(path, backup)) {
        Logger::warning(QStringLiteral("Settings"),
                        QStringLiteral("Quarantined malformed settings file at %1").arg(backup));
    } else {
        Logger::error(QStringLiteral("Settings"),
                      QStringLiteral("Settings file is malformed and could not be quarantined: %1").arg(path));
    }
    return path;
}

bool hasModelFiles(const QString &path)
{
    QDir root(path);
    if (!root.exists()) return false;

    QFile registry(root.absoluteFilePath(QStringLiteral("registry.json")));
    if (registry.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(registry.readAll());
        if (doc.isArray() && !doc.array().isEmpty()) {
            return true;
        }
    }

    const QStringList subDirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &subDirName : subDirs) {
        QDir subDir(root.absoluteFilePath(subDirName));
        const QStringList modelFiles = subDir.entryList(
            {QStringLiteral("*.gguf"), QStringLiteral("*.bin"), QStringLiteral("*.onnx")},
            QDir::Files);
        if (!modelFiles.isEmpty()) {
            return true;
        }
    }

    return false;
}

QString readStableModelsPath()
{
    QFile file(settingsFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return {};
    }

    const QJsonObject storage = doc.object().value(QStringLiteral("storage")).toObject();
    const QString path = storage.value(QStringLiteral("modelsPath")).toString();
    return path.isEmpty() ? QString() : QDir(path).absolutePath();
}

void writeStableModelsPath(const QString &path)
{
    QDir().mkpath(PathUtils::dataDir());

    QJsonObject root;
    QFile existing(settingsFilePath());
    if (existing.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(existing.readAll());
        if (doc.isObject()) {
            root = doc.object();
        }
    }

    QJsonObject storage = root.value(QStringLiteral("storage")).toObject();
    storage[QStringLiteral("modelsPath")] = QDir(path).absolutePath();
    root[QStringLiteral("storage")] = storage;

    QFile file(settingsFilePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

QString configuredModelsPath(const QString &qSettingsPath)
{
    const QString stablePath = readStableModelsPath();
    if (!stablePath.isEmpty()) return QDir(stablePath).absolutePath();
    if (!qSettingsPath.isEmpty()) return QDir(qSettingsPath).absolutePath();
    return QDir(PathUtils::modelsDir()).absolutePath();
}

QString discoverExistingModelsPath(const QString &qSettingsPath, bool includeMountedVolumes)
{
    QStringList candidates;
    const QString stablePath = readStableModelsPath();
    if (!stablePath.isEmpty()) candidates.append(stablePath);
    if (!qSettingsPath.isEmpty()) candidates.append(QDir(qSettingsPath).absolutePath());
    candidates.append(QDir(PathUtils::modelsDir()).absolutePath());

#ifdef Q_OS_WIN
    if (includeMountedVolumes) {
        const QFileInfoList drives = QDir::drives();
        for (const QFileInfo &drive : drives) {
            candidates.append(QDir(drive.absoluteFilePath()).absoluteFilePath(QStringLiteral("models")));
        }
    }
#else
    Q_UNUSED(includeMountedVolumes);
#endif

    QSet<QString> seen;
    for (const QString &candidate : candidates) {
        const QString normalized = QDir(candidate).absolutePath();
        if (seen.contains(normalized)) continue;
        seen.insert(normalized);
        if (hasModelFiles(normalized)) {
            return normalized;
        }
    }

    return qSettingsPath.isEmpty() ? QDir(PathUtils::modelsDir()).absolutePath() : QDir(qSettingsPath).absolutePath();
}

} // namespace

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_settings(preparedSettingsIniPath(), QSettings::IniFormat)
{
    // Reading all keys forces QSettings to detect parse errors before any
    // value is read or changed.  A future schema is deliberately left intact
    // rather than being silently downgraded by an older application.
    m_settings.allKeys();
    if (m_settings.status() != QSettings::NoError) {
        Logger::error(QStringLiteral("Settings"),
                      QStringLiteral("Settings could not be read: %1").arg(m_settings.fileName()));
    } else {
        const int storedVersion = m_settings.value(QStringLiteral("meta/schemaVersion"), 0).toInt();
        if (storedVersion > kSettingsSchemaVersion) {
            Logger::error(QStringLiteral("Settings"),
                          QStringLiteral("Settings schema version %1 is newer than this application supports (%2)")
                              .arg(storedVersion).arg(kSettingsSchemaVersion));
        } else if (storedVersion < kSettingsSchemaVersion) {
            if (storedVersion < 2) {
                // Older builds defaulted to remote-first and could retain a
                // local GPU cache-offload preference. Neither is appropriate
                // for the CPU-local / Colab-GPU workflow.
                m_settings.setValue(QStringLiteral("engine/device"), QStringLiteral("cpu"));
                m_settings.setValue(QStringLiteral("hardware/offloadKvCache"), false);
                m_settings.setValue(QStringLiteral("remote/remoteFirstMode"), false);
            }
            m_settings.setValue(QStringLiteral("meta/schemaVersion"), kSettingsSchemaVersion);
            m_settings.sync();
            if (m_settings.status() != QSettings::NoError) {
                Logger::error(QStringLiteral("Settings"),
                              QStringLiteral("Could not record settings schema version in %1")
                                  .arg(m_settings.fileName()));
            }
        }
    }

    // Initialize cached values from QSettings
    // Local execution is intentionally CPU-only. GPU workloads are run by the
    // direct Colab workers configured at the feature that needs them.
    m_device = QStringLiteral("cpu");
    m_settings.setValue(QStringLiteral("engine/device"), m_device);
    m_threads = m_settings.value(QStringLiteral("engine/threads"), 4).toInt();
    m_language = m_settings.value(QStringLiteral("engine/language"), QStringLiteral("en")).toString();
    m_uiLanguage = m_settings.value(QStringLiteral("ui/language"), QStringLiteral("en")).toString();
    m_onboardingComplete = m_settings.value(QStringLiteral("ui/onboardingComplete"), false).toBool();
    const QString qSettingsModelsPath = m_settings.value(QStringLiteral("storage/modelsPath"), PathUtils::modelsDir()).toString();
    // Do not recursively probe every mounted volume while the application is
    // being constructed.  In particular, disconnected network drives can
    // block first paint for a long time. Keep the configured path initially
    // and perform legacy-volume discovery after the UI is responsive.
    m_modelsPath = configuredModelsPath(qSettingsModelsPath);
    m_settings.setValue(QStringLiteral("storage/modelsPath"), m_modelsPath);
    m_settings.sync();
    writeStableModelsPath(m_modelsPath);
    Logger::info(QStringLiteral("Settings"), QStringLiteral("Models path: %1").arg(m_modelsPath));
    QTimer::singleShot(5000, this, [this, qSettingsModelsPath]() {
        const QString discovered = discoverExistingModelsPath(qSettingsModelsPath, true);
        if (discovered != m_modelsPath && hasModelFiles(discovered)) {
            Logger::info(QStringLiteral("Settings"),
                         QStringLiteral("Discovered existing models after startup at: %1").arg(discovered));
            setModelsPath(discovered);
        }
    });
    m_selectedRuntime = m_settings.value(QStringLiteral("engine/selectedRuntime"), QString()).toString();
    m_selectedTtsRuntime = m_settings.value(QStringLiteral("engine/selectedTtsRuntime"), QString()).toString();
    m_selectedTtsRuntimeVersion = m_settings.value(QStringLiteral("engine/selectedTtsRuntimeVersion"), QString()).toString();
    m_selectedTtsFamily = m_settings.value(QStringLiteral("tts/selectedFamily"), QString()).toString();
    m_selectedVoiceCloneFamily = m_settings.value(QStringLiteral("voiceCloning/selectedFamily"), QString()).toString();
    m_selectedSttRuntime = m_settings.value(QStringLiteral("stt/selectedRuntime"), QString()).toString();
    m_selectedSttRuntimeVersion = m_settings.value(QStringLiteral("engine/selectedSttRuntimeVersion"), QString()).toString();
    m_selectedSttFamily = m_settings.value(QStringLiteral("stt/selectedFamily"), QString()).toString();
    m_selectedSttModelPath = m_settings.value(QStringLiteral("stt/selectedModelPath"), QString()).toString();
    m_selectedSttModelFile = m_settings.value(QStringLiteral("stt/selectedModelFile"), QString()).toString();
    m_sttLanguage = m_settings.value(QStringLiteral("stt/language"), QStringLiteral("auto")).toString();
    m_sttThreads = m_settings.value(QStringLiteral("stt/threads"), 0).toInt();
    m_sttTranslate = m_settings.value(QStringLiteral("stt/translate"), false).toBool();
    m_offloadKvCache = false;
    m_settings.setValue(QStringLiteral("hardware/offloadKvCache"), false);
    m_guardrailMode = m_settings.value(QStringLiteral("hardware/guardrailMode"), 3).toInt(); // Default to Strict (index 3)
    m_apiServerEnabled = m_settings.value(QStringLiteral("api/serverEnabled"), false).toBool();
    m_apiServerAllowLan = m_settings.value(QStringLiteral("api/serverAllowLan"), false).toBool();
    m_apiServerPort = m_settings.value(QStringLiteral("api/serverPort"), 3900).toInt();
    QString credentialError;
    m_apiServerApiKey = SecureCredentialStore::migrateLegacy(
        m_settings, QStringLiteral("api-server"), QStringLiteral("api/serverApiKey"), &credentialError);
    if (!credentialError.isEmpty()) {
        Logger::error(QStringLiteral("Settings"), QStringLiteral("API credential migration failed: %1").arg(credentialError));
    }
    m_gatewayUrl = m_settings.value(QStringLiteral("remote/gatewayUrl"), QString()).toString().trimmed();
    credentialError.clear();
    m_gatewayApiKey = SecureCredentialStore::migrateLegacy(
        m_settings, QStringLiteral("remote-gateway"), QStringLiteral("remote/gatewayApiKey"), &credentialError);
    if (!credentialError.isEmpty()) {
        Logger::error(QStringLiteral("Settings"), QStringLiteral("Gateway credential migration failed: %1").arg(credentialError));
    }
    m_gatewayLlmModel = m_settings.value(QStringLiteral("remote/gatewayLlmModel"), QString()).toString().trimmed();
    m_gatewayTranslationModel = m_settings.value(QStringLiteral("remote/gatewayTranslationModel"), QString()).toString().trimmed();
    m_gatewaySttModel = m_settings.value(QStringLiteral("remote/gatewaySttModel"), QString()).toString().trimmed();
    m_gatewayTtsModel = m_settings.value(QStringLiteral("remote/gatewayTtsModel"), QString()).toString().trimmed();
    m_gatewayTtsVoice = m_settings.value(QStringLiteral("remote/gatewayTtsVoice"), QStringLiteral("alloy")).toString().trimmed();
    // Local CPU is always usable without any API or remote-worker setup. A
    // feature switches to its Colab GPU worker only when the user connects it.
    m_remoteFirstMode = m_settings.value(QStringLiteral("remote/remoteFirstMode"), false).toBool();
    // Network activity must be an explicit choice. Existing installs without
    // this key therefore default to no automatic update request.
    m_automaticUpdateChecks = m_settings.value(QStringLiteral("updates/automaticChecks"), false).toBool();
    m_updateCheckConsentAsked = m_settings.value(QStringLiteral("updates/consentAsked"), false).toBool();
    m_windowX = m_settings.value(QStringLiteral("window/x"), m_windowX).toInt();
    m_windowY = m_settings.value(QStringLiteral("window/y"), m_windowY).toInt();
    m_windowWidth = qMax(960, m_settings.value(QStringLiteral("window/width"), m_windowWidth).toInt());
    m_windowHeight = qMax(600, m_settings.value(QStringLiteral("window/height"), m_windowHeight).toInt());
    m_windowMaximized = m_settings.value(QStringLiteral("window/maximized"), m_windowMaximized).toBool();
}


QString Settings::device() const
{
    return m_device;
}

void Settings::setDevice(const QString &v)
{
    Q_UNUSED(v);
    const QString cpu = QStringLiteral("cpu");
    if (m_device != cpu) {
        m_device = cpu;
        m_settings.setValue(QStringLiteral("engine/device"), cpu);
        m_settings.sync();
        emit deviceChanged();
    }
}

int Settings::threads() const
{
    return m_threads;
}

void Settings::setThreads(int v)
{
    if (m_threads != v) {
        m_threads = v;
        m_settings.setValue(QStringLiteral("engine/threads"), v);
        m_settings.sync();
        emit threadsChanged();
    }
}

QString Settings::language() const
{
    return m_language;
}

void Settings::setLanguage(const QString &v)
{
    if (m_language != v) {
        m_language = v;
        m_settings.setValue(QStringLiteral("engine/language"), v);
        m_settings.sync();
        emit languageChanged();
    }
}

QString Settings::uiLanguage() const
{
    return m_uiLanguage;
}

void Settings::setUiLanguage(const QString &v)
{
    if (m_uiLanguage != v) {
        m_uiLanguage = v;
        m_settings.setValue(QStringLiteral("ui/language"), v);
        m_settings.sync();
        emit uiLanguageChanged();
    }
}

QString Settings::modelsPath() const
{
    return m_modelsPath;
}

void Settings::setModelsPath(const QString &v)
{
    const QString normalized = QDir(v).absolutePath();
    if (m_modelsPath != normalized) {
        m_modelsPath = normalized;
        m_settings.setValue(QStringLiteral("storage/modelsPath"), normalized);
        m_settings.sync();
        writeStableModelsPath(normalized);
        emit modelsPathChanged();
    }
}

QString Settings::selectedRuntime() const
{
    return m_selectedRuntime;
}

void Settings::setSelectedRuntime(const QString &v)
{
    if (m_selectedRuntime != v) {
        m_selectedRuntime = v;
        m_settings.setValue(QStringLiteral("engine/selectedRuntime"), v);
        m_settings.sync();
        emit selectedRuntimeChanged();
    }
}

QString Settings::selectedTtsRuntime() const
{
    return m_selectedTtsRuntime;
}

void Settings::setSelectedTtsRuntime(const QString &v)
{
    if (m_selectedTtsRuntime != v) {
        m_selectedTtsRuntime = v;
        m_settings.setValue(QStringLiteral("engine/selectedTtsRuntime"), v);
        m_settings.sync();
        emit selectedTtsRuntimeChanged();
    }
}

QString Settings::selectedTtsRuntimeVersion() const
{
    return m_selectedTtsRuntimeVersion;
}

void Settings::setSelectedTtsRuntimeVersion(const QString &v)
{
    if (m_selectedTtsRuntimeVersion != v) {
        m_selectedTtsRuntimeVersion = v;
        m_settings.setValue(QStringLiteral("engine/selectedTtsRuntimeVersion"), v);
        m_settings.sync();
        emit selectedTtsRuntimeVersionChanged();
    }
}

QString Settings::selectedTtsFamily() const
{
    return m_selectedTtsFamily;
}

void Settings::setSelectedTtsFamily(const QString &v)
{
    if (m_selectedTtsFamily != v) {
        m_selectedTtsFamily = v;
        m_settings.setValue(QStringLiteral("tts/selectedFamily"), v);
        m_settings.sync();
        emit selectedTtsFamilyChanged();
    }
}

QString Settings::selectedVoiceCloneFamily() const
{
    return m_selectedVoiceCloneFamily;
}

void Settings::setSelectedVoiceCloneFamily(const QString &v)
{
    if (m_selectedVoiceCloneFamily != v) {
        m_selectedVoiceCloneFamily = v;
        m_settings.setValue(QStringLiteral("voiceCloning/selectedFamily"), v);
        m_settings.sync();
        emit selectedVoiceCloneFamilyChanged();
    }
}

QString Settings::selectedSttRuntime() const
{
    return m_selectedSttRuntime;
}

void Settings::setSelectedSttRuntime(const QString &v)
{
    if (m_selectedSttRuntime != v) {
        m_selectedSttRuntime = v;
        m_settings.setValue(QStringLiteral("stt/selectedRuntime"), v);
        m_settings.sync();
        emit selectedSttRuntimeChanged();
    }
}

QString Settings::selectedSttRuntimeVersion() const
{
    return m_selectedSttRuntimeVersion;
}

void Settings::setSelectedSttRuntimeVersion(const QString &v)
{
    if (m_selectedSttRuntimeVersion != v) {
        m_selectedSttRuntimeVersion = v;
        m_settings.setValue(QStringLiteral("engine/selectedSttRuntimeVersion"), v);
        m_settings.sync();
        emit selectedSttRuntimeVersionChanged();
    }
}

QString Settings::selectedSttFamily() const
{
    return m_selectedSttFamily;
}

void Settings::setSelectedSttFamily(const QString &v)
{
    if (m_selectedSttFamily != v) {
        m_selectedSttFamily = v;
        m_settings.setValue(QStringLiteral("stt/selectedFamily"), v);
        m_settings.sync();
        emit selectedSttFamilyChanged();
    }
}

QString Settings::selectedSttModelPath() const
{
    return m_selectedSttModelPath;
}

void Settings::setSelectedSttModelPath(const QString &v)
{
    if (m_selectedSttModelPath != v) {
        m_selectedSttModelPath = v;
        m_settings.setValue(QStringLiteral("stt/selectedModelPath"), v);
        m_settings.sync();
        emit selectedSttModelPathChanged();
    }
}

QString Settings::selectedSttModelFile() const
{
    return m_selectedSttModelFile;
}

void Settings::setSelectedSttModelFile(const QString &v)
{
    if (m_selectedSttModelFile != v) {
        m_selectedSttModelFile = v;
        m_settings.setValue(QStringLiteral("stt/selectedModelFile"), v);
        m_settings.sync();
        emit selectedSttModelFileChanged();
    }
}

QString Settings::sttLanguage() const
{
    return m_sttLanguage;
}

void Settings::setSttLanguage(const QString &v)
{
    if (m_sttLanguage != v) {
        m_sttLanguage = v;
        m_settings.setValue(QStringLiteral("stt/language"), v);
        m_settings.sync();
        emit sttLanguageChanged();
    }
}

int Settings::sttThreads() const
{
    return m_sttThreads;
}

void Settings::setSttThreads(int v)
{
    v = qBound(0, v, 64);
    if (m_sttThreads != v) {
        m_sttThreads = v;
        m_settings.setValue(QStringLiteral("stt/threads"), v);
        m_settings.sync();
        emit sttThreadsChanged();
    }
}

bool Settings::sttTranslate() const
{
    return m_sttTranslate;
}

void Settings::setSttTranslate(bool v)
{
    if (m_sttTranslate != v) {
        m_sttTranslate = v;
        m_settings.setValue(QStringLiteral("stt/translate"), v);
        m_settings.sync();
        emit sttTranslateChanged();
    }
}

bool Settings::offloadKvCache() const
{
    return m_offloadKvCache;
}

void Settings::setOffloadKvCache(bool v)
{
    Q_UNUSED(v);
    if (m_offloadKvCache) {
        m_offloadKvCache = false;
        m_settings.setValue(QStringLiteral("hardware/offloadKvCache"), false);
        m_settings.sync();
        emit offloadKvCacheChanged();
    }
}

int Settings::guardrailMode() const
{
    return m_guardrailMode;
}

void Settings::setGuardrailMode(int v)
{
    if (m_guardrailMode != v) {
        m_guardrailMode = v;
        m_settings.setValue(QStringLiteral("hardware/guardrailMode"), v);
        m_settings.sync();
        emit guardrailModeChanged();
    }
}

bool Settings::apiServerEnabled() const
{
    return m_apiServerEnabled;
}

void Settings::setApiServerEnabled(bool v)
{
    if (m_apiServerEnabled != v) {
        m_apiServerEnabled = v;
        m_settings.setValue(QStringLiteral("api/serverEnabled"), v);
        m_settings.sync();
        emit apiServerEnabledChanged();
    }
}

bool Settings::apiServerAllowLan() const
{
    return m_apiServerAllowLan;
}

void Settings::setApiServerAllowLan(bool v)
{
    if (m_apiServerAllowLan != v) {
        m_apiServerAllowLan = v;
        m_settings.setValue(QStringLiteral("api/serverAllowLan"), v);
        m_settings.sync();
        emit apiServerAllowLanChanged();
    }
}

int Settings::apiServerPort() const
{
    return m_apiServerPort;
}

void Settings::setApiServerPort(int v)
{
    v = qBound(1, v, 65535);
    if (m_apiServerPort != v) {
        m_apiServerPort = v;
        m_settings.setValue(QStringLiteral("api/serverPort"), v);
        m_settings.sync();
        emit apiServerPortChanged();
    }
}

QString Settings::apiServerApiKey() const
{
    return m_apiServerApiKey;
}

void Settings::setApiServerApiKey(const QString &v)
{
    const QString normalized = v.trimmed();
    if (m_apiServerApiKey != normalized) {
        m_apiServerApiKey = normalized;
        QString credentialError;
        if (!SecureCredentialStore::write(m_settings, QStringLiteral("api-server"), normalized, &credentialError)) {
            Logger::error(QStringLiteral("Settings"), QStringLiteral("API credential was not persisted: %1").arg(credentialError));
        }
        m_settings.remove(QStringLiteral("api/serverApiKey"));
        m_settings.sync();
        emit apiServerApiKeyChanged();
    }
}

QString Settings::gatewayUrl() const
{
    return m_gatewayUrl;
}

void Settings::setGatewayUrl(const QString &v)
{
    const QString normalized = v.trimmed();
    if (m_gatewayUrl == normalized) return;
    m_gatewayUrl = normalized;
    m_settings.setValue(QStringLiteral("remote/gatewayUrl"), normalized);
    m_settings.sync();
    emit gatewayUrlChanged();
}

QString Settings::gatewayApiKey() const
{
    return m_gatewayApiKey;
}

bool Settings::gatewayApiKeyConfigured() const
{
    return !m_gatewayApiKey.isEmpty();
}

bool Settings::setGatewayApiKey(const QString &v)
{
    const QString normalized = v.trimmed();
    if (m_gatewayApiKey == normalized) return true;
    QString credentialError;
    if (!SecureCredentialStore::write(m_settings, QStringLiteral("remote-gateway"), normalized, &credentialError)) {
        Logger::error(QStringLiteral("Settings"), QStringLiteral("Gateway credential was not persisted: %1").arg(credentialError));
        return false;
    }
    m_gatewayApiKey = normalized;
    m_settings.remove(QStringLiteral("remote/gatewayApiKey"));
    m_settings.sync();
    emit gatewayApiKeyChanged();
    return true;
}

QString Settings::gatewayLlmModel() const
{
    return m_gatewayLlmModel;
}

void Settings::setGatewayLlmModel(const QString &v)
{
    const QString normalized = v.trimmed();
    if (m_gatewayLlmModel == normalized) return;
    m_gatewayLlmModel = normalized;
    m_settings.setValue(QStringLiteral("remote/gatewayLlmModel"), normalized);
    m_settings.sync();
    emit gatewayLlmModelChanged();
}

QString Settings::gatewayTranslationModel() const
{
    return m_gatewayTranslationModel;
}

void Settings::setGatewayTranslationModel(const QString &v)
{
    const QString normalized = v.trimmed();
    if (m_gatewayTranslationModel == normalized) return;
    m_gatewayTranslationModel = normalized;
    m_settings.setValue(QStringLiteral("remote/gatewayTranslationModel"), normalized);
    m_settings.sync();
    emit gatewayTranslationModelChanged();
}

QString Settings::gatewaySttModel() const
{
    return m_gatewaySttModel;
}

void Settings::setGatewaySttModel(const QString &v)
{
    const QString normalized = v.trimmed();
    if (m_gatewaySttModel == normalized) return;
    m_gatewaySttModel = normalized;
    m_settings.setValue(QStringLiteral("remote/gatewaySttModel"), normalized);
    m_settings.sync();
    emit gatewaySttModelChanged();
}

QString Settings::gatewayTtsModel() const
{
    return m_gatewayTtsModel;
}

void Settings::setGatewayTtsModel(const QString &v)
{
    const QString normalized = v.trimmed();
    if (m_gatewayTtsModel == normalized) return;
    m_gatewayTtsModel = normalized;
    m_settings.setValue(QStringLiteral("remote/gatewayTtsModel"), normalized);
    m_settings.sync();
    emit gatewayTtsModelChanged();
}

QString Settings::gatewayTtsVoice() const
{
    return m_gatewayTtsVoice;
}

void Settings::setGatewayTtsVoice(const QString &v)
{
    const QString normalized = v.trimmed();
    if (m_gatewayTtsVoice == normalized) return;
    m_gatewayTtsVoice = normalized;
    m_settings.setValue(QStringLiteral("remote/gatewayTtsVoice"), normalized);
    m_settings.sync();
    emit gatewayTtsVoiceChanged();
}

bool Settings::remoteFirstMode() const
{
    return m_remoteFirstMode;
}

void Settings::setRemoteFirstMode(bool v)
{
    if (m_remoteFirstMode == v) return;
    m_remoteFirstMode = v;
    m_settings.setValue(QStringLiteral("remote/remoteFirstMode"), v);
    m_settings.sync();
    emit remoteFirstModeChanged();
}

bool Settings::automaticUpdateChecks() const
{
    return m_automaticUpdateChecks;
}

void Settings::setAutomaticUpdateChecks(bool v)
{
    if (m_automaticUpdateChecks != v) {
        m_automaticUpdateChecks = v;
        m_settings.setValue(QStringLiteral("updates/automaticChecks"), v);
        m_settings.sync();
        emit automaticUpdateChecksChanged();
    }
}

bool Settings::updateCheckConsentAsked() const
{
    return m_updateCheckConsentAsked;
}

void Settings::setUpdateCheckConsentAsked(bool v)
{
    if (m_updateCheckConsentAsked != v) {
        m_updateCheckConsentAsked = v;
        m_settings.setValue(QStringLiteral("updates/consentAsked"), v);
        m_settings.sync();
        emit updateCheckConsentAskedChanged();
    }
}

void Settings::setOnboardingComplete(bool v)
{
    if (m_onboardingComplete == v) return;
    m_onboardingComplete = v;
    m_settings.setValue(QStringLiteral("ui/onboardingComplete"), v);
    m_settings.sync();
    emit onboardingCompleteChanged();
}

void Settings::saveWindowPlacement(int x, int y, int width, int height, bool maximized)
{
    const int normalizedWidth = qMax(960, width);
    const int normalizedHeight = qMax(600, height);
    if (m_windowX == x && m_windowY == y && m_windowWidth == normalizedWidth
        && m_windowHeight == normalizedHeight && m_windowMaximized == maximized) {
        return;
    }
    m_windowX = x;
    m_windowY = y;
    m_windowWidth = normalizedWidth;
    m_windowHeight = normalizedHeight;
    m_windowMaximized = maximized;
    m_settings.setValue(QStringLiteral("window/x"), m_windowX);
    m_settings.setValue(QStringLiteral("window/y"), m_windowY);
    m_settings.setValue(QStringLiteral("window/width"), m_windowWidth);
    m_settings.setValue(QStringLiteral("window/height"), m_windowHeight);
    m_settings.setValue(QStringLiteral("window/maximized"), m_windowMaximized);
    m_settings.sync();
    emit windowPlacementChanged();
}

qint64 Settings::modelsPathAvailableBytes() const
{
    const QStorageInfo storage(m_modelsPath);
    return storage.isValid() && storage.isReady() ? storage.bytesAvailable() : -1;
}

bool Settings::externalMediaToolsAvailable() const
{
    return MediaRuntimeLocator::resolve().isComplete();
}

} // namespace LAStudio

