#pragma once

#include <QUrl>
#include <QString>
#include <QMetaType>

namespace LAStudio {

// The execution path is deliberately explicit. A model selection must never
// infer its source from a display name or silently jump between providers.
enum class ExecutionProvider {
    LocalDev,
    ApiGateway,
    ColabDirect
};

enum class RemoteEndpointKind {
    ApiGateway,
    ColabWorker
};

struct RemoteEndpointValidation {
    QUrl normalizedUrl;
    QString error;

    bool isValid() const { return error.isEmpty() && normalizedUrl.isValid(); }
};

QString executionProviderId(ExecutionProvider provider);
QString executionProviderDisplayName(ExecutionProvider provider);
bool executionProviderFromId(const QString &id, ExecutionProvider *provider);

// Production remote endpoints must use HTTPS. The optional localhost escape
// hatch exists only for developer/test servers and must be opted into by the
// caller; it is never enabled by persisted settings.
RemoteEndpointValidation validateRemoteEndpoint(const QString &rawUrl,
                                                RemoteEndpointKind kind,
                                                bool allowInsecureLocalhost = false);

QUrl appendRemotePath(const QUrl &baseUrl, const QString &relativePath);

} // namespace LAStudio

Q_DECLARE_METATYPE(LAStudio::ExecutionProvider)
