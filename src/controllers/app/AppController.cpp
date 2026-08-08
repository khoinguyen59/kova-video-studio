#include "controllers/app/AppController.h"

#include "core/Settings.h"
#include "controllers/models/ModelSessionRegistry.h"
#include "core/StudioSelectionRepository.h"
#include "controllers/app/WorkflowActivityManager.h"
#include "controllers/translation/TranslationModelSession.h"
#include "controllers/llm/LlmChatModelSession.h"
#include "core/HFHubClient.h"
#include "core/DownloadManager.h"
#include "core/ModelManager.h"
#include "core/RegistryManager.h"
#include "core/PathUtils.h"
#include "stt/SttEngine.h"
#include "tts/TtsEngine.h"
#include "audio/AudioRecorder.h"
#include "audio/AudioPlayer.h"
#include "audio/WaveformProvider.h"
#include "controllers/shared/AppUpdateService.h"
#include "api/ApiServerService.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QSaveFile>
#include <QSysInfo>
#include <QTextStream>
#include "core/Logger.h"
#include <QTimer>
#include <QUrl>

namespace LAStudio {

AppController *AppController::s_instance = nullptr;

AppController::AppController(QObject *parent)
    : QObject(parent)
{
    s_instance = this;

    PathUtils::ensureDirsExist();

    m_settings  = new Settings(this);
    m_localization = new LocalizationManager(m_settings, this);
    m_hub       = new HFHubClient(this);
    m_downloads = new DownloadManager(m_hub, this);
    m_models    = new ModelManager(this);
    m_models->setModelsRoot(m_settings->modelsPath());
    m_catalog   = new CatalogManager(this);
    m_registry  = new RegistryManager(this);
    connect(m_catalog, &CatalogManager::errorOccurred, this,
            [this](const QString &message) { enqueueError(message, QStringLiteral("Catalog")); });
    connect(m_registry, &RegistryManager::errorOccurred, this,
            [this](const QString &message) { enqueueError(message, QStringLiteral("Registry")); });
    m_registry->initializeFromCatalog(m_catalog);
    m_logs      = new LogViewService(this);
    m_cache     = new CacheLifecycleService(this);
    m_stt       = new SttEngine(this);
    m_tts       = new TtsEngine(this);
    m_translationEngine = new TranslationEngine({}, this);
    m_llmEngine = new LlmChatEngine(this);
    m_colabSession = new ColabSession(this);
    m_colabTtsSession = new ColabSession(this);
    m_colabVoiceCloneSession = new ColabSession(this);
    m_colabVoiceDesignSession = new ColabSession(this);
    m_colabAlignmentSession = new ColabSession(this);
    m_colabSeparationSession = new ColabSession(this);
    m_colabVoiceCloneReferenceIsolatorSession = new ColabSession(this);
    m_colabTranslationSession = new ColabSession(this);
    m_colabSubtitleOcrSession = new ColabSession(this);
    m_colabChatSession = new ColabSession(this);
    Logger::info(QStringLiteral("App"), QStringLiteral("Initializing runtime services."));
    m_runtimes  = new RuntimeManager(m_catalog, m_settings, this);
    m_alignment = new AlignmentExecutionService(m_runtimes, m_models, this);
    m_colabAlignment = new ColabAlignmentController(m_colabAlignmentSession, m_settings, this);
    m_voiceIsolator = new VoiceIsolatorController(this);
    m_colabVoiceIsolator = new ColabVoiceIsolatorController(m_colabSeparationSession, m_settings, this);
    m_colabVoiceCloneReferenceIsolator = new ColabVoiceIsolatorController(
        m_colabVoiceCloneReferenceIsolatorSession, m_settings, this);
    m_voiceCloneReferenceIsolator = new VoiceCloneReferenceIsolatorController(
        m_voiceIsolator, m_colabVoiceCloneReferenceIsolator, this);
    Logger::info(QStringLiteral("App"), QStringLiteral("Initializing model session registry."));
    m_sessionRegistry = new ModelSessionRegistry(m_stt, m_tts, m_translationEngine, m_llmEngine, m_alignment, m_voiceIsolator, this);
    m_translation = new TranslationController(m_translationEngine,
        qobject_cast<TranslationModelSession*>(m_sessionRegistry->sessionForCapability(QStringLiteral("translation"))),
        m_settings, m_colabTranslationSession, this);
    m_llmChat = new LlmChatController(m_llmEngine,
        qobject_cast<LlmChatModelSession*>(m_sessionRegistry->sessionForCapability(QStringLiteral("llm-chat"))),
        m_settings, m_colabChatSession, this);
    m_recorder  = new AudioRecorder(this);
    m_player    = new AudioPlayer(this);
    m_waveformProvider = new WaveformProvider();

    m_preview   = new AudioPreviewService(m_tts, m_player, m_waveformProvider, this);
    m_history   = new HistoryService(m_tts, m_recorder, this);
    m_gatewayTts = new GatewayTtsController(m_settings, m_player, m_waveformProvider, m_history, this);
    m_colabTts = new ColabTtsController(m_colabTtsSession, m_settings, m_player, m_waveformProvider, m_history, this);
    m_colabVoiceClone = new ColabVoiceCloneController(m_colabVoiceCloneSession, m_settings, m_player, m_waveformProvider, m_history, this);
    m_colabVoiceDesign = new ColabVoiceDesignController(m_colabVoiceDesignSession, m_settings, m_player, m_waveformProvider, m_history, this);
    m_modelsMigration = new ModelsPathMigrationService(m_settings, m_models, m_downloads, m_stt, m_tts, this);
    m_files     = new FileAccessService(this);
    m_downloadInstall = new DownloadInstallService(m_downloads, m_models, m_runtimes, m_settings, this);
    m_remoteModels = new RemoteModelCatalogController(m_settings, {
        {QStringLiteral("stt"), m_colabSession},
        {QStringLiteral("tts"), m_colabTtsSession},
        {QStringLiteral("voice-cloning"), m_colabVoiceCloneSession},
        {QStringLiteral("voice-design"), m_colabVoiceDesignSession},
        {QStringLiteral("forced-alignment"), m_colabAlignmentSession},
        {QStringLiteral("voice-isolation"), m_colabSeparationSession},
        {QStringLiteral("translation"), m_colabTranslationSession},
        {QStringLiteral("subtitle-ocr"), m_colabSubtitleOcrSession},
        {QStringLiteral("chat"), m_colabChatSession},
    }, this);
    m_voiceClonePresets = new VoiceClonePresetService(this);
    m_voiceDesignPresets = new VoiceDesignPresetService(this);
    m_sttSession = new SttSessionController(this);
    m_subtitleVoice = new SubtitleVoiceController(m_tts, m_player, m_history, this);
    m_dubbing = new DubbingController(m_sttSession, m_tts, m_translationEngine, m_models, m_runtimes, this);
    m_subtitleOcrRuntime = new SubtitleOcrRuntimeService(m_downloads, this);
    m_subtitleOcr = new SubtitleOcrController(m_subtitleVoice, m_dubbing, this);
    m_subtitleOcr->setRuntimeService(m_subtitleOcrRuntime);
    m_subtitleOcr->setColabSession(m_colabSubtitleOcrSession);
    m_dubbing->setSubtitleOcrController(m_subtitleOcr);
    // Dubbing owns a project-level selection, while the preset service owns
    // the durable reference media.  Inject the service instead of reading the
    // library indirectly from QML so every run is validated by the controller.
    m_dubbing->setVoiceClonePresetService(m_voiceClonePresets);
    m_dubbing->setRemoteServices(m_settings, m_colabTranslationSession, m_colabTtsSession,
                                 m_colabVoiceCloneSession, m_colabSeparationSession,
                                 m_colabAlignmentSession);
    m_updates = new AppUpdateService(m_downloads, this);
    m_examples = new ExampleManager(this);
    m_workflows = new WorkflowActivityManager(
        m_sessionRegistry, m_tts, m_sttSession, m_alignment, m_dubbing,
        m_gatewayTts, m_colabTts, m_colabVoiceClone, m_colabVoiceDesign,
        m_colabAlignment, m_voiceIsolator, m_colabVoiceIsolator, m_voiceCloneReferenceIsolator,
        m_translation, m_subtitleOcr, m_llmChat, this);
    m_apiServer = new ApiServerService(m_settings, m_tts, m_stt, this);
    Logger::info(QStringLiteral("App"), QStringLiteral("Application services initialized."));

    connect(m_preview, &AudioPreviewService::errorOccurred, this, &AppController::onError);
    connect(m_player, &AudioPlayer::errorOccurred, this, &AppController::onError);
    connect(m_history, &HistoryService::errorOccurred, this, &AppController::onError);
    connect(m_gatewayTts, &GatewayTtsController::errorOccurred, this, &AppController::onError);
    connect(m_colabTts, &ColabTtsController::errorOccurred, this, &AppController::onError);
    connect(m_colabVoiceClone, &ColabVoiceCloneController::errorOccurred, this, &AppController::onError);
    connect(m_colabVoiceDesign, &ColabVoiceDesignController::errorOccurred, this, &AppController::onError);
    connect(m_subtitleOcr, &SubtitleOcrController::errorChanged, this, [this]() {
        if (m_subtitleOcr && !m_subtitleOcr->error().isEmpty())
            enqueueError(m_subtitleOcr->error(), QStringLiteral("Subtitle OCR"));
    });
    connect(m_subtitleOcrRuntime, &SubtitleOcrRuntimeService::errorChanged, this, [this]() {
        if (m_subtitleOcrRuntime && !m_subtitleOcrRuntime->error().isEmpty()) {
            enqueueError(m_subtitleOcrRuntime->error(), QStringLiteral("Subtitle OCR runtime"));
        }
    });
    connect(m_colabAlignment, &ColabAlignmentController::failed, this, &AppController::onError);
    connect(m_colabVoiceIsolator, &ColabVoiceIsolatorController::errorOccurred, this, &AppController::onError);
    connect(m_downloadInstall, &DownloadInstallService::errorOccurred, this, &AppController::onError);
    connect(m_alignment, &AlignmentExecutionService::failed, this,
            [this](const QString &, const QString &message) { onError(message); });
    connect(m_translation, &TranslationController::errorTextChanged, this, [this]() {
        if (m_translation && !m_translation->errorText().isEmpty()) onError(m_translation->errorText());
    });
    connect(m_llmChat, &LlmChatController::errorTextChanged, this, [this]() {
        if (m_llmChat && !m_llmChat->errorText().isEmpty()) {
            enqueueError(m_llmChat->errorText(), QStringLiteral("LLM Chat"));
        }
    });
    connect(m_dubbing, &DubbingController::errorChanged, this, [this]() {
        if (m_dubbing && !m_dubbing->lastError().isEmpty()) {
            enqueueError(m_dubbing->lastError(), QStringLiteral("Dubbing"));
        }
    });
    connect(m_voiceIsolator, &VoiceIsolatorController::stateChanged, this, [this]() {
        if (m_voiceIsolator && !m_voiceIsolator->lastError().isEmpty()) {
            enqueueError(m_voiceIsolator->lastError(), QStringLiteral("Voice Isolator"));
        }
    });
    connect(m_apiServer, &ApiServerService::lastErrorChanged, this, [this]() {
        if (m_apiServer && !m_apiServer->lastError().isEmpty()) {
            enqueueError(m_apiServer->lastError(), QStringLiteral("Local API Server"));
        }
    });
    connect(m_voiceClonePresets, &VoiceClonePresetService::errorOccurred, this, &AppController::onError);
    connect(m_voiceDesignPresets, &VoiceDesignPresetService::errorOccurred, this, &AppController::onError);
    connect(m_updates, &AppUpdateService::errorOccurred, this, &AppController::onError);
    connect(m_cache, &CacheLifecycleService::errorOccurred, this, &AppController::onError);
    connect(m_llmEngine, &LlmChatEngine::errorOccurred, this, &AppController::onError);

    connect(m_stt, &SttEngine::errorOccurred, this, &AppController::onError);
    connect(m_tts, &TtsEngine::errorOccurred, this, &AppController::onError);
    connect(m_hub, &HFHubClient::searchError, this, &AppController::onError);
    connect(m_downloads, &DownloadManager::error, this,
            [this](const QString &, const QString &, const QString &err) { onError(err); });

    connect(m_settings, &Settings::modelsPathChanged, this, [this]() {
        m_models->setModelsRoot(m_settings->modelsPath());
        m_models->scanLocalModelsAsync();
    });

    // Constructing AppController happens before the first QML frame.  A
    // recursive models-tree scan can be slow on network or removable storage,
    // so defer it until the application has had an opportunity to paint.
    QTimer::singleShot(250, this, [this]() {
        if (m_models) m_models->scanLocalModelsAsync();
    });

    QTimer::singleShot(2000, this, [this]() {
        if (m_updates && m_settings && m_settings->automaticUpdateChecks()) {
            m_updates->checkForUpdates(QStringLiteral("stable"));
        }
    });
}

AppController::~AppController()
{
    s_instance = nullptr;
}

AppController *AppController::instance()
{
    return s_instance;
}

AppController *AppController::create(QQmlEngine *, QJSEngine *)
{
    if (!s_instance) {
        s_instance = new AppController;
    }
    return s_instance;
}

void AppController::onError(const QString &msg)
{
    enqueueError(msg);
}

void AppController::clearError()
{
    if (m_errorNotifications.isEmpty()) {
        return;
    }
    m_errorNotifications.removeFirst();
    m_errorMessage = m_errorNotifications.isEmpty()
        ? QString() : m_errorNotifications.constFirst().toMap().value(QStringLiteral("message")).toString();
    emit errorMessageChanged();
    emit errorNotificationsChanged();
}

void AppController::enqueueError(const QString &message, const QString &source)
{
    const QString trimmedMessage = message.trimmed();
    if (trimmedMessage.isEmpty()) {
        return;
    }

    const QVariantMap previous = m_errorNotifications.isEmpty()
        ? QVariantMap() : m_errorNotifications.constLast().toMap();
    if (previous.value(QStringLiteral("message")).toString() == trimmedMessage &&
        previous.value(QStringLiteral("source")).toString() == source) {
        return;
    }

    QVariantMap notification;
    notification.insert(QStringLiteral("id"), QString::number(m_nextErrorNotificationId++));
    notification.insert(QStringLiteral("severity"), QStringLiteral("error"));
    notification.insert(QStringLiteral("source"), source);
    notification.insert(QStringLiteral("message"), trimmedMessage);
    notification.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    const bool queueWasEmpty = m_errorNotifications.isEmpty();
    m_errorNotifications.append(notification);
    if (queueWasEmpty) {
        m_errorMessage = trimmedMessage;
        emit errorMessageChanged();
    }
    emit errorNotificationsChanged();
}

void AppController::copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

QString AppController::createProblemReport()
{
    const QString reportsRoot = PathUtils::logsDir() + QStringLiteral("/support-reports");
    const QString reportId = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmsszzzZ"));
    const QString reportDirPath = reportsRoot + QLatin1Char('/') + reportId;
    if (!QDir().mkpath(reportDirPath)) {
        onError(tr("Could not create the local diagnostics package."));
        return {};
    }

    QDir crashDir(PathUtils::logsDir() + QStringLiteral("/crashes"));
    const QFileInfoList dumps = crashDir.entryInfoList({QStringLiteral("*.dmp")}, QDir::Files,
                                                       QDir::Time);
    QString dumpStatus = tr("No crash dump was available.");
    if (!dumps.isEmpty()) {
        const QFileInfo &latestDump = dumps.constFirst();
        const QString copiedDump = reportDirPath + QLatin1Char('/') + latestDump.fileName();
        if (QFile::copy(latestDump.absoluteFilePath(), copiedDump)) {
            dumpStatus = tr("Included crash dump: %1").arg(latestDump.fileName());
        } else {
            dumpStatus = tr("Could not copy crash dump; attach it manually from: %1")
                             .arg(latestDump.absoluteFilePath());
        }
    }

    QString logTail = m_logs ? m_logs->readSanitizedLogFile() : QString();
    constexpr qsizetype maxLogTailCharacters = 64 * 1024;
    if (logTail.size() > maxLogTailCharacters) {
        logTail = tr("[Earlier log lines omitted]\n") + logTail.right(maxLogTailCharacters);
    }

    QSaveFile reportFile(reportDirPath + QStringLiteral("/diagnostics.txt"));
    if (!reportFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QDir(reportDirPath).removeRecursively();
        onError(tr("Could not write the local diagnostics package."));
        return {};
    }

    QTextStream stream(&reportFile);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "LA Studio local diagnostics package\n"
           << "Created (UTC): " << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n"
           << "Application version: " << QCoreApplication::applicationVersion() << "\n"
           << "Operating system: " << QSysInfo::prettyProductName() << "\n"
           << "\nThis package was created only after a user action. Nothing was uploaded automatically.\n"
           << "Review the files before attaching them to an issue. Do not share API keys, private media, or transcripts.\n\n"
           << dumpStatus << "\n\n"
           << "--- Sanitized application log tail ---\n"
           << logTail;
    if (!reportFile.commit()) {
        QDir(reportDirPath).removeRecursively();
        onError(tr("Could not finalize the local diagnostics package."));
        return {};
    }

    Logger::info("support", QStringLiteral("Created local diagnostics package: %1").arg(reportDirPath));
    return reportDirPath;
}

QString AppController::logsDir() const
{
    return PathUtils::logsDir();
}

QString AppController::dataDir() const
{
    return PathUtils::dataDir();
}

QString AppController::licensesDir() const
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString installedLicenses = appDir.absoluteFilePath(QStringLiteral("../licenses"));
    if (QDir(installedLicenses).exists()) {
        return QDir::cleanPath(installedLicenses);
    }

    // Development builds may run from a build directory instead of the staged
    // installer layout.  Return the conventional path without creating it so
    // the UI never claims that legal documents have been installed when they
    // have not.
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("licenses")));
}

QString AppController::colabNotebooksDir() const
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        appDir.absoluteFilePath(QStringLiteral("docs/colab-notebooks")),
        appDir.absoluteFilePath(QStringLiteral("../docs/colab-notebooks")),
        appDir.absoluteFilePath(QStringLiteral("../../../notebooks")),
        QDir::current().absoluteFilePath(QStringLiteral("notebooks")),
    };

    for (const QString &candidate : candidates) {
        const QString cleaned = QDir::cleanPath(candidate);
        if (QDir(cleaned).exists()) {
            return cleaned;
        }
    }
    return {};
}

bool AppController::openColabNotebooksDirectory()
{
    const QString directory = colabNotebooksDir();
    if (directory.isEmpty()) {
        onError(tr("The packaged Colab notebooks folder could not be found."));
        return false;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
        onError(tr("Could not open the packaged Colab notebooks folder."));
        return false;
    }
    return true;
}

} // namespace LAStudio
