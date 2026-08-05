#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QtQml/qqml.h>

namespace LAStudio {

class IModelSession;
class AlignmentExecutionService;
class ModelSessionRegistry;
class SttSessionController;
class TtsEngine;
class DubbingController;
class GatewayTtsController;
class ColabTtsController;
class ColabVoiceCloneController;
class ColabVoiceDesignController;
class ColabAlignmentController;
class VoiceIsolatorController;
class ColabVoiceIsolatorController;
class VoiceCloneReferenceIsolatorController;
class TranslationController;
class SubtitleOcrController;
class LlmChatController;

class WorkflowActivityManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("WorkflowActivityManager is managed by AppController")

    Q_PROPERTY(QVariantList activeWorkflows READ activeWorkflows NOTIFY workflowsChanged)
    Q_PROPERTY(QVariantList activeSessions READ activeSessions NOTIFY workflowsChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY workflowsChanged)
    Q_PROPERTY(int sessionCount READ sessionCount NOTIFY workflowsChanged)
    Q_PROPERTY(int runningCount READ runningCount NOTIFY workflowsChanged)
    Q_PROPERTY(bool hasActiveWorkflows READ hasActiveWorkflows NOTIFY workflowsChanged)

public:
    explicit WorkflowActivityManager(ModelSessionRegistry *sessionRegistry,
                             TtsEngine *tts,
                             SttSessionController *sttSession,
                             AlignmentExecutionService *alignment,
                             DubbingController *dubbing = nullptr,
                             GatewayTtsController *gatewayTts = nullptr,
                             ColabTtsController *colabTts = nullptr,
                             ColabVoiceCloneController *colabVoiceClone = nullptr,
                             ColabVoiceDesignController *colabVoiceDesign = nullptr,
                             ColabAlignmentController *colabAlignment = nullptr,
                             VoiceIsolatorController *voiceIsolator = nullptr,
                             ColabVoiceIsolatorController *colabVoiceIsolator = nullptr,
                             VoiceCloneReferenceIsolatorController *voiceCloneReferenceIsolator = nullptr,
                             TranslationController *translation = nullptr,
                             SubtitleOcrController *subtitleOcr = nullptr,
                             LlmChatController *llmChat = nullptr,
                             QObject *parent = nullptr);

    QVariantList activeWorkflows() const;
    QVariantList activeSessions() const;
    int activeCount() const;
    int sessionCount() const;
    int runningCount() const;
    bool hasActiveWorkflows() const { return activeCount() > 0 || sessionCount() > 0; }

    Q_INVOKABLE void stopWorkflow(const QString &id);
    Q_INVOKABLE void openWorkflow(const QString &id);
    // Used by production-backed cross-studio actions (for example Dubbing's
    // Alignment stage) without pretending an inactive job exists.
    Q_INVOKABLE void openStudioRoute(const QString &routeId);
    Q_INVOKABLE void openVoiceCloningStudio();

signals:
    void workflowsChanged();
    void openRequested(const QString &routeId);

private slots:
    void refresh();

private:
    QVariantMap ttsWorkflow() const;
    QVariantMap sttWorkflow() const;
    QVariantMap alignmentWorkflow() const;
    QVariantMap dubbingWorkflow() const;
    QVariantMap gatewayTtsWorkflow() const;
    QVariantMap colabTtsWorkflow() const;
    QVariantMap voiceCloneWorkflow() const;
    QVariantMap voiceDesignWorkflow() const;
    QVariantMap colabAlignmentWorkflow() const;
    QVariantMap localVoiceIsolationWorkflow() const;
    QVariantMap colabVoiceIsolationWorkflow() const;
    QVariantMap voiceCloneReferenceIsolationWorkflow() const;
    QVariantMap translationWorkflow() const;
    QVariantMap subtitleOcrWorkflow() const;
    QVariantMap llmChatWorkflow() const;
    QVariantList sessionItems(IModelSession *session) const;
    QVariantMap makeWorkflow(const QString &id,
                             const QString &type,
                             const QString &title,
                             const QString &routeId,
                             const QString &iconName,
                             int progress,
                             bool progressEstimated,
                             const QString &stageLabel,
                             bool cancellable) const;
    void updateActiveStartTimes(const QVariantList &workflows) const;
    static QString stateLabel(int stateValue);
    static QString iconForCapability(const QString &capabilityId);
    static QString fallbackTitleForCapability(const QString &capabilityId);
    static QString routeForCapability(const QString &capabilityId);
    static QString studioRouteForCapability(const QString &capabilityId);
    static QString statusLabel(bool stopping);
    static QString ttsTitleForMode(const QString &mode);

    ModelSessionRegistry *m_sessionRegistry = nullptr;
    TtsEngine *m_tts = nullptr;
    SttSessionController *m_sttSession = nullptr;
    AlignmentExecutionService *m_alignment = nullptr;
    DubbingController *m_dubbing = nullptr;
    GatewayTtsController *m_gatewayTts = nullptr;
    ColabTtsController *m_colabTts = nullptr;
    ColabVoiceCloneController *m_colabVoiceClone = nullptr;
    ColabVoiceDesignController *m_colabVoiceDesign = nullptr;
    ColabAlignmentController *m_colabAlignment = nullptr;
    VoiceIsolatorController *m_voiceIsolator = nullptr;
    ColabVoiceIsolatorController *m_colabVoiceIsolator = nullptr;
    VoiceCloneReferenceIsolatorController *m_voiceCloneReferenceIsolator = nullptr;
    TranslationController *m_translation = nullptr;
    SubtitleOcrController *m_subtitleOcr = nullptr;
    LlmChatController *m_llmChat = nullptr;
    mutable QHash<QString, QDateTime> m_startedAtById;
    mutable QSet<QString> m_stoppingIds;
};

} // namespace LAStudio
