#pragma once

#include <QObject>
#include <QPointer>
#include <QDateTime>
#include <QUrl>
#include <QtQml/qqml.h>

class QNetworkAccessManager;
class QNetworkReply;

namespace LAStudio {

// A Colab worker is a temporary direct session. It intentionally has no
// dependency on Settings or a Gateway client, so its token can never be
// persisted alongside API Gateway credentials.
class ColabSession : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString workerUrl READ workerUrl NOTIFY sessionChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY sessionChanged)
    Q_PROPERTY(bool checking READ isChecking NOTIFY verificationChanged)
    Q_PROPERTY(bool verified READ isVerified NOTIFY verificationChanged)
    Q_PROPERTY(QString verificationState READ verificationState NOTIFY verificationChanged)
    Q_PROPERTY(QString verificationMessage READ verificationMessage NOTIFY verificationChanged)
    Q_PROPERTY(QString expectedCapability READ expectedCapability NOTIFY verificationChanged)
    Q_PROPERTY(QString expectedModel READ expectedModel NOTIFY verificationChanged)
    Q_PROPERTY(QString expectedVariant READ expectedVariant NOTIFY verificationChanged)
    Q_PROPERTY(QString reportedGpu READ reportedGpu NOTIFY verificationChanged)
    Q_PROPERTY(QString verifiedAt READ verifiedAt NOTIFY verificationChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY sessionErrorChanged)

public:
    explicit ColabSession(QObject *parent = nullptr);

    QString workerUrl() const;
    QUrl endpoint() const;
    bool isActive() const;
    bool isChecking() const { return m_checking; }
    bool isVerified() const { return m_verified; }
    QString verificationState() const { return m_verificationState; }
    QString verificationMessage() const { return m_verificationMessage; }
    QString expectedCapability() const { return m_expectedCapability; }
    QString expectedModel() const { return m_expectedModel; }
    // Every current exact-model notebook exposes one immutable configuration.
    // Keep it explicit so Local CPU file variants cannot be mistaken for a
    // Colab worker variant, and so future multi-variant workers have a stable
    // verification field to bind to.
    QString expectedVariant() const { return m_expectedVariant; }
    QString reportedGpu() const { return m_reportedGpu; }
    QString verifiedAt() const {
        return m_verifiedAt.isValid() ? m_verifiedAt.toLocalTime().toString(Qt::ISODate) : QString();
    }
    QString lastError() const { return m_lastError; }

    // A direct worker is usable for a feature only when the active session was
    // verified for that exact capability/model pair.  `active` alone is not a
    // routing guarantee: the user may have changed model after pairing a
    // different single-model notebook.
    bool hasVerifiedRoute(const QString &capability, const QString &model,
                          QString *errorMessage = nullptr,
                          const QString &variant = QStringLiteral("fixed")) const;

    // The two-argument overload verifies a generic CUDA worker. Production
    // feature UIs should use the exact overload so a worker cannot be paired
    // for the wrong capability or model.
    Q_INVOKABLE bool connectTemporaryWorker(const QString &workerUrl,
                                            const QString &bearerToken);
    Q_INVOKABLE bool connectTemporaryWorker(const QString &workerUrl,
                                            const QString &bearerToken,
                                            const QString &expectedCapability,
                                            const QString &expectedModel,
                                            const QString &expectedVariant = QStringLiteral("fixed"));
    // Re-runs the same health and exact-capability handshake for the active
    // temporary session. This is deliberately asynchronous so a dead Colab
    // tunnel cannot block the UI thread.
    Q_INVOKABLE bool checkConnection();
    Q_INVOKABLE void disconnectTemporaryWorker();

    // Starts an asynchronous /health + /v1/capabilities verification. HTTP is
    // available only to local contract tests; production callers use HTTPS.
    bool beginVerifiedSession(const QString &workerUrl, const QString &bearerToken,
                              const QString &expectedCapability,
                              const QString &expectedModel,
                              QString *errorMessage = nullptr,
                              bool allowInsecureLocalhost = false,
                              const QString &expectedVariant = QStringLiteral("fixed"));

    // HTTP is rejected by default. The final argument exists only for loopback
    // contract tests; production callers must not opt into it. This trusted
    // low-level method intentionally skips network verification and is kept
    // for runners/catalog unit tests. Feature UIs must use beginVerifiedSession.
    bool setSession(const QString &workerUrl, const QString &bearerToken,
                    QString *errorMessage = nullptr, bool allowInsecureLocalhost = false);
    void clear();

    // Never expose this value as a QML property or serialize it to a project.
    QString bearerTokenForRequest() const;
    // Not exposed to QML. This only propagates an explicit loopback-test opt-in
    // from a trusted contract session to its runner request.
    bool allowsInsecureLocalhostForTests() const { return m_allowInsecureLocalhostForTests; }

signals:
    void sessionChanged();
    void sessionErrorChanged();
    void verificationChanged();
    void verificationFinished(bool success, const QString &message);

private:
    enum class VerificationStage {
        None,
        Health,
        Capabilities
    };

    void cancelVerification();
    void requestVerificationDocument(VerificationStage stage, quint64 generation);
    void handleVerificationReply(QNetworkReply *reply, VerificationStage stage,
                                 quint64 generation);
    void failVerification(const QString &message, quint64 generation);
    void finishVerification(quint64 generation);
    void setLastError(const QString &message);

    QUrl m_endpoint;
    QString m_bearerToken;
    QString m_expectedCapability;
    QString m_expectedModel;
    QString m_expectedVariant;
    QString m_reportedGpu;
    QDateTime m_verifiedAt;
    QString m_verificationState = QStringLiteral("disconnected");
    QString m_verificationMessage;
    QString m_lastError;
    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_verificationReply;
    quint64 m_verificationGeneration = 0;
    bool m_allowInsecureLocalhostForTests = false;
    bool m_checking = false;
    bool m_verified = false;
};

} // namespace LAStudio
