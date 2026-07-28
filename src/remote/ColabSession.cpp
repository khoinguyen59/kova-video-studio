#include "ColabSession.h"

#include "ExecutionProvider.h"

namespace LAStudio {

ColabSession::ColabSession(QObject *parent)
    : QObject(parent)
{
}

QString ColabSession::workerUrl() const
{
    return m_endpoint.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
}

QUrl ColabSession::endpoint() const
{
    return m_endpoint;
}

bool ColabSession::isActive() const
{
    return m_endpoint.isValid() && !m_bearerToken.isEmpty();
}

bool ColabSession::connectTemporaryWorker(const QString &workerUrl,
                                          const QString &bearerToken)
{
    QString error;
    if (!setSession(workerUrl, bearerToken, &error)) {
        if (m_lastError != error) {
            m_lastError = error;
            emit sessionErrorChanged();
        }
        return false;
    }
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit sessionErrorChanged();
    }
    return true;
}

void ColabSession::disconnectTemporaryWorker()
{
    clear();
}

bool ColabSession::setSession(const QString &workerUrl, const QString &bearerToken,
                              QString *errorMessage, bool allowInsecureLocalhost)
{
    const RemoteEndpointValidation validated = validateRemoteEndpoint(
        workerUrl, RemoteEndpointKind::ColabWorker, allowInsecureLocalhost);
    const QString normalizedToken = bearerToken.trimmed();
    if (!validated.isValid()) {
        if (errorMessage) *errorMessage = validated.error;
        return false;
    }
    if (normalizedToken.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker bearer token is required");
        return false;
    }

    const bool changed = m_endpoint != validated.normalizedUrl || m_bearerToken != normalizedToken;
    m_endpoint = validated.normalizedUrl;
    m_bearerToken = normalizedToken;
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit sessionErrorChanged();
    }
    if (changed) emit sessionChanged();
    return true;
}

void ColabSession::clear()
{
    if (!m_endpoint.isValid() && m_bearerToken.isEmpty()) return;
    m_endpoint = {};
    m_bearerToken.clear();
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit sessionErrorChanged();
    }
    emit sessionChanged();
}

QString ColabSession::bearerTokenForRequest() const
{
    return m_bearerToken;
}

} // namespace LAStudio
