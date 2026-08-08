#include "controllers/app/AppController.h"
#include "core/PathUtils.h"
#include "remote/ColabSession.h"

namespace LAStudio {

AppController* AppController::s_instance = nullptr;

AppController::AppController(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    m_settings = new Settings(this);
    m_localization = new LocalizationManager(m_settings, this);
    m_hub = new HFHubClient(this);
    m_downloads = new DownloadManager(m_hub, this);
    m_models = new ModelManager(this);
    m_catalog = new CatalogManager(this);
    m_registry = new RegistryManager(this);
    m_registry->initializeFromCatalog(m_catalog);
    m_runtimes = new RuntimeManager(nullptr, m_settings, this);
    m_logs = new LogViewService(this);
    m_cache = new CacheLifecycleService(this);
    m_stt = new SttEngine(this);
    m_tts = new TtsEngine(this);
    m_recorder = new AudioRecorder(this);
    m_player = new AudioPlayer(this);
    m_waveformProvider = new WaveformProvider();
    m_preview = new AudioPreviewService(m_tts, m_player, m_waveformProvider, this);
    m_history = new HistoryService(m_tts, m_recorder, this);
    m_modelsMigration = new ModelsPathMigrationService(m_settings, m_models, m_downloads, m_stt, m_tts, this);
    m_files = new FileAccessService(this);
    m_downloadInstall = new DownloadInstallService(m_downloads, m_models, m_runtimes, m_settings, this);
    m_alignment = new AlignmentExecutionService(m_runtimes, m_models, this);
    m_colabSession = new ColabSession(this);
    m_colabTtsSession = new ColabSession(this);
    m_colabVoiceCloneSession = new ColabSession(this);
    m_colabVoiceDesignSession = new ColabSession(this);
    m_colabAlignmentSession = new ColabSession(this);
    m_colabSeparationSession = new ColabSession(this);
    // Voice Clone's optional reference-cleanup route owns a separate Colab
    // session. Keep the test double aligned with the production controller so
    // tests can catch accidental coupling to the standalone Isolator worker.
    m_colabVoiceCloneReferenceIsolatorSession = new ColabSession(this);
    m_colabTranslationSession = new ColabSession(this);
    m_colabChatSession = new ColabSession(this);
    m_voiceIsolator = new VoiceIsolatorController(this);
    m_colabVoiceCloneReferenceIsolator = new ColabVoiceIsolatorController(
        m_colabVoiceCloneReferenceIsolatorSession, m_settings, this);
    m_voiceCloneReferenceIsolator = new VoiceCloneReferenceIsolatorController(
        m_voiceIsolator, m_colabVoiceCloneReferenceIsolator, this);
    m_sessionRegistry = new ModelSessionRegistry(m_stt, m_tts, m_alignment, m_voiceIsolator, this);
    m_voiceClonePresets = new VoiceClonePresetService(this);
    m_voiceDesignPresets = new VoiceDesignPresetService(this);
    m_sttSession = new SttSessionController(this);
    m_updates = new AppUpdateService(m_downloads, this);
    m_workflows = new WorkflowActivityManager(
        m_sessionRegistry, m_tts, m_sttSession, m_alignment, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, this);
}

AppController::~AppController()
{
    s_instance = nullptr;
}

AppController* AppController::instance()
{
    if (!s_instance) {
        // Create a dummy instance that lives for the duration of the process
        s_instance = new AppController();
    }
    return s_instance;
}

AppController* AppController::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

void AppController::clearError() {}
void AppController::copyToClipboard(const QString &) {}
QString AppController::logsDir() const { return PathUtils::logsDir(); }
QString AppController::dataDir() const { return PathUtils::dataDir(); }
QString AppController::licensesDir() const { return PathUtils::dataDir(); }
QString AppController::colabNotebooksDir() const { return QString(); }
QString AppController::createProblemReport() { return QString(); }
bool AppController::openColabNotebooksDirectory() { return false; }
void AppController::onError(const QString &) {}

} // namespace LAStudio
