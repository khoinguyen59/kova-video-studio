#pragma once

#include <QObject>
#include <QUrl>
#include <QtQml/qqml.h>

namespace LAStudio {

// A Colab worker is a temporary direct session. It intentionally has no
// dependency on Settings or a Gateway client, so its token can never be
// persisted alongside API Gateway credentials.
class ColabSession : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString workerUrl READ workerUrl NOTIFY sessionChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY sessionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY sessionErrorChanged)

public:
    explicit ColabSession(QObject *parent = nullptr);

    QString workerUrl() const;
    QUrl endpoint() const;
    bool isActive() const;
    QString lastError() const { return m_lastError; }

    // Feature-local QML, including the dubbing workflow nodes, uses this
    // wrapper to pair a temporary Colab worker without ever exposing or
    // persisting its bearer token.
    Q_INVOKABLE bool connectTemporaryWorker(const QString &workerUrl,
                                            const QString &bearerToken);
    Q_INVOKABLE void disconnectTemporaryWorker();

    // HTTP is rejected by default. The final argument exists only for loopback
    // contract tests; production callers must not opt into it.
    bool setSession(const QString &workerUrl, const QString &bearerToken,
                    QString *errorMessage = nullptr, bool allowInsecureLocalhost = false);
    void clear();

    // Never expose this value as a QML property or serialize it to a project.
    QString bearerTokenForRequest() const;

signals:
    void sessionChanged();
    void sessionErrorChanged();

private:
    QUrl m_endpoint;
    QString m_bearerToken;
    QString m_lastError;
};

} // namespace LAStudio
