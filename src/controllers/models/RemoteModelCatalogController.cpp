#include "RemoteModelCatalogController.h"

#include "core/Settings.h"
#include "remote/ColabSession.h"

#include <QtConcurrent>

namespace LAStudio {

RemoteModelCatalogController::RemoteModelCatalogController(Settings *settings,
                                                           ColabSession *colabSession,
                                                           QObject *parent)
    : QObject(parent), m_settings(settings), m_colabSession(colabSession)
{
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
    connect(&m_colabWatcher, &QFutureWatcher<ColabCapabilityCatalog::Result>::finished,
            this, [this]() {
        const ColabCapabilityCatalog::Result result = m_colabWatcher.result();
        if (m_colabRequestGeneration != m_colabGeneration) {
            m_colabRefreshing = false;
            emit colabStateChanged();
            return;
        }
        m_colabRefreshing = false;
        if (result.isSuccess()) {
            m_colabModels = result.models;
            m_colabError.clear();
            m_colabAvailable = true;
        } else {
            m_colabModels.clear();
            m_colabError = result.error;
            m_colabAvailable = false;
        }
        emit colabModelsChanged();
        emit colabStateChanged();
    });

    if (m_settings) {
        connect(m_settings, &Settings::gatewayUrlChanged, this,
                &RemoteModelCatalogController::clearGatewayCatalog);
        connect(m_settings, &Settings::gatewayApiKeyChanged, this,
                &RemoteModelCatalogController::clearGatewayCatalog);
    }
    if (m_colabSession) {
        connect(m_colabSession, &ColabSession::sessionChanged, this,
                &RemoteModelCatalogController::clearColabCatalog);
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
    if (!m_colabSession || !m_colabSession->isActive()) {
        m_colabModels.clear();
        m_colabError = QStringLiteral("Connect a Colab GPU worker before refreshing its models");
        m_colabAvailable = false;
        emit colabModelsChanged();
        emit colabStateChanged();
        return;
    }
    const QUrl workerUrl = m_colabSession->endpoint();
    const QString token = m_colabSession->bearerTokenForRequest();
    m_colabRequestGeneration = ++m_colabGeneration;
    m_colabRefreshing = true;
    m_colabError.clear();
    emit colabStateChanged();
    m_colabWatcher.setFuture(QtConcurrent::run([workerUrl, token]() {
        return ColabCapabilityCatalog::fetch(workerUrl, token);
    }));
}

bool RemoteModelCatalogController::pairColab(const QString &workerUrl,
                                              const QString &bearerToken)
{
    if (!m_colabSession) {
        m_colabError = QStringLiteral("Colab session service is unavailable");
        emit colabStateChanged();
        return false;
    }
    QString error;
    if (!m_colabSession->setSession(workerUrl, bearerToken, &error)) {
        m_colabError = error;
        m_colabAvailable = false;
        emit colabStateChanged();
        return false;
    }
    refreshColab();
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
        if (providerId == QStringLiteral("colab-direct") && !capability.trimmed().isEmpty()
            && model.value(QStringLiteral("capability")).toString() != capability.trimmed()) {
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
