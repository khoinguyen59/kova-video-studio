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

public:
    explicit ColabSession(QObject *parent = nullptr);

    QString workerUrl() const;
    QUrl endpoint() const;
    bool isActive() const;

    // HTTP is rejected by default. The final argument exists only for loopback
    // contract tests; production callers must not opt into it.
    bool setSession(const QString &workerUrl, const QString &bearerToken,
                    QString *errorMessage = nullptr, bool allowInsecureLocalhost = false);
    void clear();

    // Never expose this value as a QML property or serialize it to a project.
    QString bearerTokenForRequest() const;

signals:
    void sessionChanged();

private:
    QUrl m_endpoint;
    QString m_bearerToken;
};

} // namespace LAStudio
