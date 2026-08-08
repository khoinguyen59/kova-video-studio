#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqml.h>

#include "core/Settings.h"
#include "core/LocalizationManager.h"
#include "core/HFHubClient.h"
#include "core/DownloadManager.h"
#include "core/ModelManager.h"
#include "core/CatalogManager.h"
#include "core/RegistryManager.h"
#include "core/RuntimeManager.h"
#include "core/LogViewService.h"
#include "core/CacheLifecycleService.h"
#include "stt/SttEngine.h"
#include "tts/TtsEngine.h"
#include "translation/TranslationEngine.h"
#include "llm/LlmChatEngine.h"
#include "controllers/llm/LlmChatController.h"
#include "audio/AudioRecorder.h"
#include "audio/AudioPlayer.h"
#include "audio/WaveformProvider.h"
#include "controllers/dubbing/DubbingController.h"
#include "AudioPreviewService.h"
#include "HistoryService.h"
#include "ModelsPathMigrationService.h"
#include "FileAccessService.h"
#include "DownloadInstallService.h"
#include "controllers/models/RemoteModelCatalogController.h"
#include "controllers/alignment/AlignmentExecutionService.h"
#include "controllers/alignment/ColabAlignmentController.h"
#include "TranslationController.h"
#include "VoiceClonePresetService.h"
#include "VoiceDesignPresetService.h"
#include "SttSessionController.h"
#include "SubtitleVoiceController.h"
#include "controllers/subtitles/SubtitleOcrController.h"
#include "subtitles/SubtitleOcrRuntimeService.h"
#include "controllers/tts/GatewayTtsController.h"
#include "controllers/tts/ColabTtsController.h"
#include "controllers/tts/ColabVoiceCloneController.h"
#include "controllers/tts/ColabVoiceDesignController.h"
#include "VoiceIsolatorController.h"
#include "controllers/separation/ColabVoiceIsolatorController.h"
#include "controllers/separation/VoiceCloneReferenceIsolatorController.h"
#include "AppUpdateService.h"
#include "ExampleManager.h"
#include "controllers/app/WorkflowActivityManager.h"
#include "api/ApiServerService.h"
#include "remote/ColabSession.h"

#include "ModelSessionRegistry.h"

namespace LAStudio {
 
class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Settings*        settings  READ settings  CONSTANT)
    Q_PROPERTY(LocalizationManager* localization READ localization CONSTANT)
    Q_PROPERTY(HFHubClient*     hub       READ hub       CONSTANT)
    Q_PROPERTY(DownloadManager* downloads READ downloads CONSTANT)
    Q_PROPERTY(ModelManager*    models    READ models    CONSTANT)
    Q_PROPERTY(CatalogManager*  catalog   READ catalog   CONSTANT)
    Q_PROPERTY(RegistryManager* registry  READ registry  CONSTANT)
    Q_PROPERTY(RuntimeManager*  runtimes  READ runtimes  CONSTANT)
    Q_PROPERTY(LogViewService*  logs      READ logs      CONSTANT)
    Q_PROPERTY(CacheLifecycleService* cache READ cache CONSTANT)
    Q_PROPERTY(SttEngine*       stt       READ stt       CONSTANT)
    Q_PROPERTY(TtsEngine*       tts       READ tts       CONSTANT)
    Q_PROPERTY(TranslationEngine* translationEngine READ translationEngine CONSTANT)
    Q_PROPERTY(LlmChatEngine* llmEngine READ llmEngine CONSTANT)
    Q_PROPERTY(LlmChatController* llmChat READ llmChat CONSTANT)
    Q_PROPERTY(ColabSession* colabSession READ colabSession CONSTANT)
    Q_PROPERTY(ColabSession* colabSttSession READ colabSttSession CONSTANT)
    Q_PROPERTY(ColabSession* colabTtsSession READ colabTtsSession CONSTANT)
    Q_PROPERTY(ColabSession* colabVoiceCloneSession READ colabVoiceCloneSession CONSTANT)
    Q_PROPERTY(ColabSession* colabVoiceDesignSession READ colabVoiceDesignSession CONSTANT)
    Q_PROPERTY(ColabSession* colabAlignmentSession READ colabAlignmentSession CONSTANT)
    Q_PROPERTY(ColabSession* colabSeparationSession READ colabSeparationSession CONSTANT)
    Q_PROPERTY(ColabSession* colabVoiceCloneReferenceIsolatorSession READ colabVoiceCloneReferenceIsolatorSession CONSTANT)
    Q_PROPERTY(ColabSession* colabTranslationSession READ colabTranslationSession CONSTANT)
    Q_PROPERTY(ColabSession* colabSubtitleOcrSession READ colabSubtitleOcrSession CONSTANT)
    Q_PROPERTY(ColabSession* colabChatSession READ colabChatSession CONSTANT)
    Q_PROPERTY(AudioRecorder*   recorder  READ recorder  CONSTANT)
    Q_PROPERTY(AudioPlayer*     player    READ player    CONSTANT)
    Q_PROPERTY(AudioPreviewService* preview READ preview CONSTANT)
    Q_PROPERTY(HistoryService*  history   READ history   CONSTANT)
    Q_PROPERTY(ModelsPathMigrationService* modelsMigration READ modelsMigration CONSTANT)
    Q_PROPERTY(FileAccessService* files READ files CONSTANT)
    Q_PROPERTY(DownloadInstallService* downloadInstall READ downloadInstall CONSTANT)
    Q_PROPERTY(RemoteModelCatalogController* remoteModels READ remoteModels CONSTANT)
    Q_PROPERTY(AlignmentExecutionService* alignment READ alignment CONSTANT)
    Q_PROPERTY(ColabAlignmentController* colabAlignment READ colabAlignment CONSTANT)
    Q_PROPERTY(TranslationController* translation READ translation CONSTANT)
    Q_PROPERTY(VoiceClonePresetService* voiceClonePresets READ voiceClonePresets CONSTANT)
    Q_PROPERTY(VoiceDesignPresetService* voiceDesignPresets READ voiceDesignPresets CONSTANT)
    Q_PROPERTY(SttSessionController* sttSession READ sttSession CONSTANT)
    Q_PROPERTY(GatewayTtsController* gatewayTts READ gatewayTts CONSTANT)
    Q_PROPERTY(ColabTtsController* colabTts READ colabTts CONSTANT)
    Q_PROPERTY(ColabVoiceCloneController* colabVoiceClone READ colabVoiceClone CONSTANT)
    Q_PROPERTY(ColabVoiceDesignController* colabVoiceDesign READ colabVoiceDesign CONSTANT)
    Q_PROPERTY(SubtitleVoiceController* subtitleVoice READ subtitleVoice CONSTANT)
    Q_PROPERTY(SubtitleOcrRuntimeService* subtitleOcrRuntime READ subtitleOcrRuntime CONSTANT)
    Q_PROPERTY(SubtitleOcrController* subtitleOcr READ subtitleOcr CONSTANT)
    Q_PROPERTY(DubbingController* dubbing READ dubbing CONSTANT)
    Q_PROPERTY(VoiceIsolatorController* voiceIsolator READ voiceIsolator CONSTANT)
    Q_PROPERTY(ColabVoiceIsolatorController* colabVoiceIsolator READ colabVoiceIsolator CONSTANT)
    Q_PROPERTY(ColabVoiceIsolatorController* colabVoiceCloneReferenceIsolator READ colabVoiceCloneReferenceIsolator CONSTANT)
    Q_PROPERTY(VoiceCloneReferenceIsolatorController* voiceCloneReferenceIsolator READ voiceCloneReferenceIsolator CONSTANT)
    Q_PROPERTY(AppUpdateService* updates READ updates CONSTANT)
    Q_PROPERTY(ExampleManager* examples READ examples CONSTANT)
    Q_PROPERTY(ModelSessionRegistry* sessionRegistry READ sessionRegistry CONSTANT)
    Q_PROPERTY(WorkflowActivityManager* workflows READ workflows CONSTANT)
    Q_PROPERTY(ApiServerService* apiServer READ apiServer CONSTANT)

    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(int pendingErrorCount READ pendingErrorCount NOTIFY errorNotificationsChanged)
    Q_PROPERTY(QVariantList errorNotifications READ errorNotifications NOTIFY errorNotificationsChanged)
    Q_PROPERTY(QString logsDir READ logsDir CONSTANT)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(QString licensesDir READ licensesDir CONSTANT)
    Q_PROPERTY(QString colabNotebooksDir READ colabNotebooksDir CONSTANT)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    static AppController *instance();
    static AppController *create(QQmlEngine *, QJSEngine *);

    Settings*        settings()  const { return m_settings; }
    LocalizationManager* localization() const { return m_localization; }
    HFHubClient*     hub()       const { return m_hub; }
    DownloadManager* downloads() const { return m_downloads; }
    ModelManager*    models()    const { return m_models; }
    CatalogManager*  catalog()   const { return m_catalog; }
    RegistryManager* registry()  const { return m_registry; }
    RuntimeManager*  runtimes()  const { return m_runtimes; }
    LogViewService*  logs()      const { return m_logs; }
    CacheLifecycleService* cache() const { return m_cache; }
    SttEngine*       stt()       const { return m_stt; }
    TtsEngine*       tts()       const { return m_tts; }
    TranslationEngine* translationEngine() const { return m_translationEngine; }
    LlmChatEngine* llmEngine() const { return m_llmEngine; }
    LlmChatController* llmChat() const { return m_llmChat; }
    ColabSession* colabSession() const { return m_colabSession; }
    ColabSession* colabSttSession() const { return m_colabSession; }
    ColabSession* colabTtsSession() const { return m_colabTtsSession; }
    ColabSession* colabVoiceCloneSession() const { return m_colabVoiceCloneSession; }
    ColabSession* colabVoiceDesignSession() const { return m_colabVoiceDesignSession; }
    ColabSession* colabAlignmentSession() const { return m_colabAlignmentSession; }
    ColabSession* colabSeparationSession() const { return m_colabSeparationSession; }
    ColabSession* colabVoiceCloneReferenceIsolatorSession() const { return m_colabVoiceCloneReferenceIsolatorSession; }
    ColabSession* colabTranslationSession() const { return m_colabTranslationSession; }
    ColabSession* colabSubtitleOcrSession() const { return m_colabSubtitleOcrSession; }
    ColabSession* colabChatSession() const { return m_colabChatSession; }
    AudioRecorder*   recorder()  const { return m_recorder; }
    AudioPlayer*     player()    const { return m_player; }
    AudioPreviewService* preview() const { return m_preview; }
    HistoryService*  history()   const { return m_history; }
    ModelsPathMigrationService* modelsMigration() const { return m_modelsMigration; }
    FileAccessService* files() const { return m_files; }
    DownloadInstallService* downloadInstall() const { return m_downloadInstall; }
    RemoteModelCatalogController* remoteModels() const { return m_remoteModels; }
    AlignmentExecutionService* alignment() const { return m_alignment; }
    ColabAlignmentController* colabAlignment() const { return m_colabAlignment; }
    TranslationController* translation() const { return m_translation; }
    VoiceClonePresetService* voiceClonePresets() const { return m_voiceClonePresets; }
    VoiceDesignPresetService* voiceDesignPresets() const { return m_voiceDesignPresets; }
    SttSessionController* sttSession() const { return m_sttSession; }
    GatewayTtsController* gatewayTts() const { return m_gatewayTts; }
    ColabTtsController* colabTts() const { return m_colabTts; }
    ColabVoiceCloneController* colabVoiceClone() const { return m_colabVoiceClone; }
    ColabVoiceDesignController* colabVoiceDesign() const { return m_colabVoiceDesign; }
    SubtitleVoiceController* subtitleVoice() const { return m_subtitleVoice; }
    SubtitleOcrRuntimeService* subtitleOcrRuntime() const { return m_subtitleOcrRuntime; }
    SubtitleOcrController* subtitleOcr() const { return m_subtitleOcr; }
    DubbingController* dubbing() const { return m_dubbing; }
    VoiceIsolatorController* voiceIsolator() const { return m_voiceIsolator; }
    ColabVoiceIsolatorController* colabVoiceIsolator() const { return m_colabVoiceIsolator; }
    ColabVoiceIsolatorController* colabVoiceCloneReferenceIsolator() const { return m_colabVoiceCloneReferenceIsolator; }
    VoiceCloneReferenceIsolatorController* voiceCloneReferenceIsolator() const { return m_voiceCloneReferenceIsolator; }
    AppUpdateService* updates() const { return m_updates; }
    ExampleManager* examples() const { return m_examples; }
    ModelSessionRegistry* sessionRegistry() const { return m_sessionRegistry; }
    WorkflowActivityManager* workflows() const { return m_workflows; }
    ApiServerService* apiServer() const { return m_apiServer; }

    WaveformProvider* waveformProvider() const { return m_waveformProvider; }

    QString errorMessage() const { return m_errorMessage; }
    int pendingErrorCount() const { return m_errorNotifications.size(); }
    QVariantList errorNotifications() const { return m_errorNotifications; }
    QString logsDir() const;
    QString dataDir() const;
    QString licensesDir() const;
    QString colabNotebooksDir() const;

    Q_INVOKABLE void clearError();
    Q_INVOKABLE void copyToClipboard(const QString &text);
    Q_INVOKABLE QString createProblemReport();
    Q_INVOKABLE bool openColabNotebooksDirectory();

signals:
    void errorMessageChanged();
    void errorNotificationsChanged();

private slots:
    void onError(const QString &msg);

private:
    void enqueueError(const QString &message, const QString &source = {});

    static AppController *s_instance;

    Settings*        m_settings = nullptr;
    LocalizationManager* m_localization = nullptr;
    HFHubClient*     m_hub = nullptr;
    DownloadManager* m_downloads = nullptr;
    ModelManager*    m_models = nullptr;
    CatalogManager*  m_catalog = nullptr;
    RegistryManager* m_registry = nullptr;
    RuntimeManager*  m_runtimes = nullptr;
    LogViewService*  m_logs = nullptr;
    CacheLifecycleService* m_cache = nullptr;
    SttEngine*       m_stt = nullptr;
    TtsEngine*       m_tts = nullptr;
    TranslationEngine* m_translationEngine = nullptr;
    LlmChatEngine* m_llmEngine = nullptr;
    LlmChatController* m_llmChat = nullptr;
    // Each temporary Colab credential is scoped to the feature whose notebook
    // created it. m_colabSession remains the STT/legacy catalog session.
    ColabSession* m_colabSession = nullptr;
    ColabSession* m_colabTtsSession = nullptr;
    ColabSession* m_colabVoiceCloneSession = nullptr;
    ColabSession* m_colabVoiceDesignSession = nullptr;
    ColabSession* m_colabAlignmentSession = nullptr;
    ColabSession* m_colabSeparationSession = nullptr;
    // Voice Clone has its own reference-cleanup session. Its temporary
    // Spleeter credentials must not be coupled to the standalone Isolator UI.
    ColabSession* m_colabVoiceCloneReferenceIsolatorSession = nullptr;
    ColabSession* m_colabTranslationSession = nullptr;
    ColabSession* m_colabSubtitleOcrSession = nullptr;
    ColabSession* m_colabChatSession = nullptr;
    AudioRecorder*   m_recorder = nullptr;
    AudioPlayer*     m_player = nullptr;
    WaveformProvider* m_waveformProvider = nullptr;
    AudioPreviewService* m_preview = nullptr;
    HistoryService*  m_history = nullptr;
    ModelsPathMigrationService* m_modelsMigration = nullptr;
    FileAccessService* m_files = nullptr;
    DownloadInstallService* m_downloadInstall = nullptr;
    RemoteModelCatalogController* m_remoteModels = nullptr;
    AlignmentExecutionService* m_alignment = nullptr;
    ColabAlignmentController* m_colabAlignment = nullptr;
    TranslationController* m_translation = nullptr;
    VoiceClonePresetService* m_voiceClonePresets = nullptr;
    VoiceDesignPresetService* m_voiceDesignPresets = nullptr;
    SttSessionController* m_sttSession = nullptr;
    GatewayTtsController* m_gatewayTts = nullptr;
    ColabTtsController* m_colabTts = nullptr;
    ColabVoiceCloneController* m_colabVoiceClone = nullptr;
    ColabVoiceDesignController* m_colabVoiceDesign = nullptr;
    SubtitleVoiceController* m_subtitleVoice = nullptr;
    SubtitleOcrRuntimeService* m_subtitleOcrRuntime = nullptr;
    SubtitleOcrController* m_subtitleOcr = nullptr;
    DubbingController* m_dubbing = nullptr;
    VoiceIsolatorController* m_voiceIsolator = nullptr;
    ColabVoiceIsolatorController* m_colabVoiceIsolator = nullptr;
    ColabVoiceIsolatorController* m_colabVoiceCloneReferenceIsolator = nullptr;
    VoiceCloneReferenceIsolatorController* m_voiceCloneReferenceIsolator = nullptr;
    AppUpdateService* m_updates = nullptr;
    ExampleManager* m_examples = nullptr;
    ModelSessionRegistry* m_sessionRegistry = nullptr;
    WorkflowActivityManager* m_workflows = nullptr;
    ApiServerService* m_apiServer = nullptr;

    QString m_errorMessage;
    QVariantList m_errorNotifications;
    quint64 m_nextErrorNotificationId = 1;
};

} // namespace LAStudio
