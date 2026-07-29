#include "RemoteModelCatalogController.h"

#include "core/Settings.h"
#include "remote/ColabSession.h"

#include <QtConcurrent>

namespace LAStudio {
namespace {

struct ColabCatalogRequest {
    QString workerCapability;
    QUrl endpoint;
    QString bearerToken;
};

bool supportsCapability(const QVariantMap &model, const QString &requestedCapability)
{
    const QString requested = requestedCapability.trimmed();
    if (requested.isEmpty()) return true;
    const QString advertised = model.value(QStringLiteral("capability")).toString().trimmed();
    if (advertised.isEmpty()) return true;
    if (advertised == requested) return true;
    return advertised == QStringLiteral("llm")
        && (requested == QStringLiteral("translation") || requested == QStringLiteral("llm-chat"));
}

} // namespace

RemoteModelCatalogController::RemoteModelCatalogController(Settings *settings,
                                                           ColabSession *colabSession,
                                                           QObject *parent)
    : RemoteModelCatalogController(settings,
                                   {{QStringLiteral("stt"), colabSession}}, parent)
{
}

RemoteModelCatalogController::RemoteModelCatalogController(
    Settings *settings, const QMap<QString, ColabSession *> &colabSessions,
    QObject *parent, bool allowInsecureLocalhostForTesting)
    : QObject(parent), m_settings(settings),
      m_allowInsecureLocalhostForTesting(allowInsecureLocalhostForTesting)
{
    for (auto it = colabSessions.cbegin(); it != colabSessions.cend(); ++it) {
        const QString capability = it.key().trimmed().toLower();
        if (!capability.isEmpty() && it.value()) m_colabSessions.insert(capability, it.value());
    }

    connect(&m_gatewayWatcher, &QFutureWatcher<GatewayModelCatalog::Result>::finished,
            this, [this]() {
        const GatewayModelCatalog::Result result = m_gatewayWatcher.result();
        if (m_gatewayRequestGeneration != m_gatewayGeneration) {
            m_gatewayRefreshing = false;
            emit gatewayStateChanged();
            return;
        }
        m_gatewayRefreshing = false;
        if (result.isSuccess()) {
            m_gatewayModels = result.models;
            m_gatewayError.clear();
            m_gatewayAvailable = true;
        } else {
            m_gatewayModels.clear();
            m_gatewayError = result.error;
            m_gatewayAvailable = false;
        }
        emit gatewayModelsChanged();
        emit gatewayStateChanged();
    });
    connect(&m_colabWatcher, &QFutureWatcher<ColabCatalogBatchResult>::finished,
            this, [this]() {
        const ColabCatalogBatchResult result = m_colabWatcher.result();
        if (m_colabRequestGeneration != m_colabGeneration) {
            m_colabRefreshing = false;
            emit colabStateChanged();
            return;
        }
        m_colabRefreshing = false;
        m_colabModels = result.models;
        m_colabError = result.errors.join(QLatin1Char('\n'));
        m_colabAvailable = result.successfulSessions > 0;
        emit colabModelsChanged();
        emit colabStateChanged();
    });

    if (m_settings) {
        connect(m_settings, &Settings::gatewayUrlChanged, this,
                &RemoteModelCatalogController::clearGatewayCatalog);
        connect(m_settings, &Settings::gatewayApiKeyChanged, this,
                &RemoteModelCatalogController::clearGatewayCatalog);
    }
    for (const QPointer<ColabSession> &session : m_colabSessions) {
        if (session) {
            connect(session, &ColabSession::sessionChanged, this,
                    &RemoteModelCatalogController::clearColabCatalog);
            connect(session, &ColabSession::verificationFinished, this,
                    [this](bool success, const QString &message) {
                if (success) {
                    refreshColab();
                } else {
                    m_colabError = message;
                    m_colabAvailable = false;
                    emit colabStateChanged();
                }
            });
        }
    }
}

void RemoteModelCatalogController::refreshGateway()
{
    if (m_gatewayRefreshing) return;
    if (!m_settings) {
        m_gatewayError = QStringLiteral("API Gateway configuration is unavailable");
        m_gatewayAvailable = false;
        emit gatewayStateChanged();
        return;
    }
    const QString url = m_settings->gatewayUrl();
    const QString key = m_settings->gatewayApiKey();
    m_gatewayRequestGeneration = ++m_gatewayGeneration;
    m_gatewayRefreshing = true;
    m_gatewayError.clear();
    emit gatewayStateChanged();
    m_gatewayWatcher.setFuture(QtConcurrent::run([url, key]() {
        return GatewayModelCatalog::fetch(url, key);
    }));
}

void RemoteModelCatalogController::refreshColab()
{
    if (m_colabRefreshing) return;
    QVector<ColabCatalogRequest> requests;
    for (auto it = m_colabSessions.cbegin(); it != m_colabSessions.cend(); ++it) {
        const QPointer<ColabSession> session = it.value();
        if (!session || !session->isActive()) continue;
        requests.append({it.key(), session->endpoint(), session->bearerTokenForRequest()});
    }
    if (requests.isEmpty()) {
        m_colabModels.clear();
        m_colabError = QStringLiteral("Connect at least one direct Colab GPU worker before refreshing its models");
        m_colabAvailable = false;
        emit colabModelsChanged();
        emit colabStateChanged();
        return;
    }
    m_colabRequestGeneration = ++m_colabGeneration;
    m_colabRefreshing = true;
    m_colabError.clear();
    emit colabStateChanged();
    const bool allowInsecureLocalhost = m_allowInsecureLocalhostForTesting;
    m_colabWatcher.setFuture(QtConcurrent::run([requests, allowInsecureLocalhost]() {
        ColabCatalogBatchResult batch;
        for (const ColabCatalogRequest &request : requests) {
            const ColabCapabilityCatalog::Result result = ColabCapabilityCatalog::fetch(
                request.endpoint, request.bearerToken, allowInsecureLocalhost);
            if (!result.isSuccess()) {
                batch.errors.append(QStringLiteral("%1 worker: %2")
                                        .arg(request.workerCapability, result.error));
                continue;
            }
            ++batch.successfulSessions;
            for (const QVariant &entry : result.models) {
                QVariantMap model = entry.toMap();
                model.insert(QStringLiteral("workerCapability"), request.workerCapability);
                batch.models.append(model);
            }
        }
        return batch;
    }));
}

bool RemoteModelCatalogController::pairColab(const QString &workerUrl,
                                              const QString &bearerToken)
{
    const QPointer<ColabSession> session = m_colabSessions.value(QStringLiteral("stt"));
    if (!session) {
        m_colabError = QStringLiteral("Colab session service is unavailable");
        emit colabStateChanged();
        return false;
    }
    QString error;
    if (!session->beginVerifiedSession(workerUrl, bearerToken, {}, {}, &error,
                                       m_allowInsecureLocalhostForTesting)) {
        m_colabError = error;
        m_colabAvailable = false;
        emit colabStateChanged();
        return false;
    }
    m_colabError = QStringLiteral("Checking Colab CUDA worker...");
    m_colabAvailable = false;
    emit colabStateChanged();
    return true;
}

bool RemoteModelCatalogController::isModelSelectable(const QString &provider,
                                                      const QString &modelId,
                                                      const QString &capability) const
{
    const QString providerId = provider.trimmed().toLower();
    const QVariantList *models = nullptr;
    if (providerId == QStringLiteral("api-gateway")) models = &m_gatewayModels;
    if (providerId == QStringLiteral("colab-direct")) models = &m_colabModels;
    if (!models) return false;
    for (const QVariant &value : *models) {
        const QVariantMap model = value.toMap();
        if (!supportsCapability(model, capability)) {
            continue;
        }
        if (model.value(QStringLiteral("modelId")).toString() == modelId.trimmed())
            return model.value(QStringLiteral("selectable")).toBool();
    }
    return false;
}

void RemoteModelCatalogController::clearGatewayCatalog()
{
    ++m_gatewayGeneration;
    const bool hadModels = !m_gatewayModels.isEmpty();
    m_gatewayModels.clear();
    m_gatewayError.clear();
    m_gatewayAvailable = false;
    if (hadModels) emit gatewayModelsChanged();
    emit gatewayStateChanged();
}

void RemoteModelCatalogController::clearColabCatalog()
{
    ++m_colabGeneration;
    const bool hadModels = !m_colabModels.isEmpty();
    m_colabModels.clear();
    m_colabError.clear();
    m_colabAvailable = false;
    if (hadModels) emit colabModelsChanged();
    emit colabStateChanged();
}

} // namespace LAStudio
