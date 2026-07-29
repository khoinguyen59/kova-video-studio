#pragma once

#include <QObject>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqml.h>
#include <QtQml/qqmlregistration.h>

namespace LAStudio {

class LlmChatEngine;
class LlmChatModelSession;
class Settings;
class ColabSession;
class ColabChatRunner;

class LlmChatController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LlmChatController is managed by AppController")
    Q_PROPERTY(QVariantList conversations READ conversations NOTIFY conversationsChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(QString activeConversationId READ activeConversationId NOTIFY activeConversationChanged)
    Q_PROPERTY(bool generating READ generating NOTIFY generatingChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(QString systemPrompt READ systemPrompt WRITE setSystemPrompt NOTIFY settingsChanged)
    Q_PROPERTY(int contextTokens READ contextTokens WRITE setContextTokens NOTIFY settingsChanged)
    Q_PROPERTY(int maxTokens READ maxTokens WRITE setMaxTokens NOTIFY settingsChanged)
    Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY settingsChanged)
    Q_PROPERTY(double topP READ topP WRITE setTopP NOTIFY settingsChanged)
    Q_PROPERTY(int topK READ topK WRITE setTopK NOTIFY settingsChanged)
    Q_PROPERTY(double repeatPenalty READ repeatPenalty WRITE setRepeatPenalty NOTIFY settingsChanged)
    // Active is the route selected for the next request. It does not configure,
    // clear, or forward traffic to the other provider.
    Q_PROPERTY(bool gatewayActive READ gatewayActive NOTIFY gatewayStateChanged)
    Q_PROPERTY(QString gatewayModel READ gatewayModel WRITE setGatewayModel NOTIFY gatewayModelChanged)
    Q_PROPERTY(bool colabActive READ colabActive NOTIFY colabStateChanged)
    Q_PROPERTY(QString colabModel READ colabModel WRITE setColabModel NOTIFY colabModelChanged)
    Q_PROPERTY(QString colabNotebookFile READ colabNotebookFile NOTIFY colabModelChanged)

public:
    explicit LlmChatController(LlmChatEngine *engine, LlmChatModelSession *session,
                               Settings *settings, ColabSession *colabSession, QObject *parent = nullptr);
    ~LlmChatController() override;

    QVariantList conversations() const { return m_conversations; }
    QVariantList messages() const { return m_messages; }
    QString activeConversationId() const { return m_activeId; }
    bool generating() const { return m_generating; }
    QString errorText() const { return m_error; }
    QString systemPrompt() const { return m_systemPrompt; }
    int contextTokens() const { return m_contextTokens; }
    int maxTokens() const { return m_maxTokens; }
    double temperature() const { return m_temperature; }
    double topP() const { return m_topP; }
    int topK() const { return m_topK; }
    double repeatPenalty() const { return m_repeatPenalty; }
    bool gatewayActive() const;
    QString gatewayModel() const;
    bool colabActive() const;
    QString colabModel() const { return m_colabModel; }
    QString colabNotebookFile() const;

    void setSystemPrompt(const QString &value);
    void setContextTokens(int value);
    void setMaxTokens(int value);
    void setTemperature(double value);
    void setTopP(double value);
    void setTopK(int value);
    void setRepeatPenalty(double value);
    void setGatewayModel(const QString &value);
    void setColabModel(const QString &value);
    Q_INVOKABLE bool selectColabModel(const QString &model);
    Q_INVOKABLE QString notebookForColabModel(const QString &model) const;

    Q_INVOKABLE void newConversation();
    Q_INVOKABLE void selectConversation(const QString &id);
    Q_INVOKABLE void renameConversation(const QString &id, const QString &title);
    Q_INVOKABLE void deleteConversation(const QString &id);
    Q_INVOKABLE void clearConversation();
    Q_INVOKABLE void sendMessage(const QString &text);
    Q_INVOKABLE void stopGeneration();
    Q_INVOKABLE void regenerateLastResponse();
    Q_INVOKABLE void copyMessage(const QString &text);
    Q_INVOKABLE void useGateway();
    Q_INVOKABLE bool connectColab(const QString &workerUrl, const QString &bearerToken);
    Q_INVOKABLE void useColab();
    Q_INVOKABLE void useLocal();

signals:
    void conversationsChanged();
    void messagesChanged();
    void activeConversationChanged();
    void generatingChanged();
    void errorTextChanged();
    void settingsChanged();
    void statusChanged();
    void gatewayStateChanged();
    void gatewayModelChanged();
    void colabStateChanged();
    void colabModelChanged();

private slots:
    void onToken(const QString &requestId, const QString &token);
    void onFinished(const QString &requestId, const QString &text);
    void onCancelled(const QString &requestId, const QString &text);
    void onEngineError(const QString &message);
    void onColabError(const QString &requestId, const QString &message);
    void onEngineModelLoadedChanged();

private:
    enum class Provider { Local, Gateway, Colab };

    void persist();
    void load();
    void setError(const QString &message);
    void setGenerating(bool value);
    QString newId() const;
    void ensureActive();
    void selectProvider(Provider provider);

    LlmChatEngine *m_engine = nullptr;
    LlmChatModelSession *m_session = nullptr;
    Settings *m_settings = nullptr;
    ColabSession *m_colabSession = nullptr;
    ColabChatRunner *m_colabRunner = nullptr;
    QThread m_colabThread;
    Provider m_provider = Provider::Local;
    bool m_activateColabWhenVerified = false;
    QString m_colabModel = QStringLiteral("qwen3.5-2b");
    QVariantList m_conversations;
    QVariantList m_messages;
    QString m_activeId;
    QString m_requestId;
    int m_streamingMessageIndex = -1;
    bool m_generating = false;
    QString m_error;
    QString m_systemPrompt;
    int m_contextTokens = 4096;
    int m_maxTokens = 1024;
    double m_temperature = 0.7;
    double m_topP = 0.8;
    int m_topK = 20;
    double m_repeatPenalty = 1.05;
};

} // namespace LAStudio
