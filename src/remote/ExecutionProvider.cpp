#include "ExecutionProvider.h"

#include <QUrlQuery>

namespace LAStudio {

namespace {

bool isLoopbackHost(const QString &host)
{
    const QString normalized = host.trimmed().toLower();
    return normalized == QStringLiteral("localhost")
        || normalized == QStringLiteral("127.0.0.1")
        || normalized == QStringLiteral("::1")
        || normalized == QStringLiteral("[::1]");
}

QString normalizedPath(const QString &path)
{
    QString normalized = path.trimmed();
    if (normalized == QStringLiteral("/")) return {};
    while (normalized.endsWith(QLatin1Char('/')) && normalized.size() > 1) {
        normalized.chop(1);
    }
    return normalized;
}

} // namespace

QString executionProviderId(ExecutionProvider provider)
{
    switch (provider) {
    case ExecutionProvider::LocalDev:
        return QStringLiteral("local-dev");
    case ExecutionProvider::ApiGateway:
        return QStringLiteral("api-gateway");
    case ExecutionProvider::ColabDirect:
        return QStringLiteral("colab-direct");
    }
    return {};
}

QString executionProviderDisplayName(ExecutionProvider provider)
{
    switch (provider) {
    case ExecutionProvider::LocalDev:
        return QStringLiteral("Local development");
    case ExecutionProvider::ApiGateway:
        return QStringLiteral("API Gateway");
    case ExecutionProvider::ColabDirect:
        return QStringLiteral("Colab GPU");
    }
    return {};
}

bool executionProviderFromId(const QString &id, ExecutionProvider *provider)
{
    if (!provider) return false;

    const QString normalized = id.trimmed().toLower();
    if (normalized == QStringLiteral("local-dev")) {
        *provider = ExecutionProvider::LocalDev;
        return true;
    }
    if (normalized == QStringLiteral("api-gateway")) {
        *provider = ExecutionProvider::ApiGateway;
        return true;
    }
    if (normalized == QStringLiteral("colab-direct")) {
        *provider = ExecutionProvider::ColabDirect;
        return true;
    }
    return false;
}

RemoteEndpointValidation validateRemoteEndpoint(const QString &rawUrl,
                                                RemoteEndpointKind kind,
                                                bool allowInsecureLocalhost)
{
    RemoteEndpointValidation result;
    const QString trimmed = rawUrl.trimmed();
    if (trimmed.isEmpty()) {
        result.error = QStringLiteral("Remote endpoint URL is required");
        return result;
    }

    QUrl parsed = QUrl::fromUserInput(trimmed);
    if (!parsed.isValid() || parsed.scheme().isEmpty() || parsed.host().isEmpty()) {
        result.error = QStringLiteral("Remote endpoint URL is invalid");
        return result;
    }
    if (!parsed.userInfo().isEmpty() || parsed.hasQuery() || !parsed.fragment().isEmpty()) {
        result.error = QStringLiteral("Remote endpoint URL must not contain credentials, query parameters, or fragments");
        return result;
    }

    const QString scheme = parsed.scheme().toLower();
    const bool localHttp = allowInsecureLocalhost && scheme == QStringLiteral("http")
        && isLoopbackHost(parsed.host());
    if (scheme != QStringLiteral("https") && !localHttp) {
        result.error = QStringLiteral("Remote endpoint URL must use HTTPS");
        return result;
    }

    const QString path = normalizedPath(parsed.path());
    if (kind == RemoteEndpointKind::ApiGateway
        && !path.isEmpty() && path != QStringLiteral("/v1")) {
        result.error = QStringLiteral("API Gateway URL must end at /v1");
        return result;
    }

    parsed.setScheme(scheme);
    parsed.setPath(path);
    parsed.setQuery(QUrlQuery());
    parsed.setFragment({});
    result.normalizedUrl = parsed;
    return result;
}

QUrl appendRemotePath(const QUrl &baseUrl, const QString &relativePath)
{
    QUrl result = baseUrl;
    const QString basePath = normalizedPath(result.path());
    const QString suffix = relativePath.trimmed().startsWith(QLatin1Char('/'))
        ? relativePath.trimmed().mid(1)
        : relativePath.trimmed();
    result.setPath((basePath.isEmpty() ? QString() : basePath) + QLatin1Char('/') + suffix);
    return result;
}

} // namespace LAStudio
