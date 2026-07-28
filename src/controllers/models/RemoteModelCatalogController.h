#pragma once

#include "remote/ColabCapabilityCatalog.h"
#include "remote/GatewayModelCatalog.h"

#include <QFutureWatcher>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>

namespace LAStudio {

class Settings;
class ColabSession;

// Owns presentation state for two separate remote model catalogs.  It never
// copies credentials or availability from one provider to the other.
class RemoteModelCatalogController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool gatewayRefreshing READ gatewayRefreshing NOTIFY gatewayStateChanged)
    Q_PROPERTY(bool gatewayAvailable READ gatewayAvailable NOTIFY gatewayStateChanged)
    Q_PROPERTY(QVariantList gatewayModels READ gatewayModels NOTIFY gatewayModelsChanged)
    Q_PROPERTY(QString gatewayError READ gatewayError NOTIFY gatewayStateChanged)
    Q_PROPERTY(bool colabRefreshing READ colabRefreshing NOTIFY colabStateChanged)
    Q_PROPERTY(bool colabAvailable READ colabAvailable NOTIFY colabStateChanged)
    Q_PROPERTY(QVariantList colabModels READ colabModels NOTIFY colabModelsChanged)
    Q_PROPERTY(QString colabError READ colabError NOTIFY colabStateChanged)

public:
    explicit RemoteModelCatalogController(Settings *settings, ColabSession *colabSession,
                                          QObject *parent = nullptr);
    // Every entry is a separately authenticated temporary worker session. The
    // aggregate catalog is presentation-only; it never creates a shared route
    // or reuses a credential from one capability for another.
    RemoteModelCatalogController(Settings *settings,
                                 const QMap<QString, ColabSession *> &colabSessions,
                                 QObject *parent = nullptr,
                                 bool allowInsecureLocalhostForTesting = false);

    bool gatewayRefreshing() const { return m_gatewayRefreshing; }
    bool gatewayAvailable() const { return m_gatewayAvailable; }
    QVariantList gatewayModels() const { return m_gatewayModels; }
    QString gatewayError() const { return m_gatewayError; }
    bool colabRefreshing() const { return m_colabRefreshing; }
    bool colabAvailable() const { return m_colabAvailable; }
    QVariantList colabModels() const { return m_colabModels; }
    QString colabError() const { return m_colabError; }

    Q_INVOKABLE void refreshGateway();
    Q_INVOKABLE void refreshColab();
    Q_INVOKABLE bool pairColab(const QString &workerUrl, const QString &bearerToken);
    Q_INVOKABLE bool isModelSelectable(const QString &provider, const QString &modelId,
                                       const QString &capability = {}) const;

signals:
    void gatewayStateChanged();
    void gatewayModelsChanged();
    void colabStateChanged();
    void colabModelsChanged();

private:
    struct ColabCatalogBatchResult {
        QVariantList models;
        QStringList errors;
        int successfulSessions = 0;
    };

    void clearGatewayCatalog();
    void clearColabCatalog();

    QPointer<Settings> m_settings;
    QMap<QString, QPointer<ColabSession>> m_colabSessions;
    bool m_allowInsecureLocalhostForTesting = false;
    QFutureWatcher<GatewayModelCatalog::Result> m_gatewayWatcher;
    QFutureWatcher<ColabCatalogBatchResult> m_colabWatcher;
    QVariantList m_gatewayModels;
    QVariantList m_colabModels;
    QString m_gatewayError;
    QString m_colabError;
    bool m_gatewayRefreshing = false;
    bool m_gatewayAvailable = false;
    bool m_colabRefreshing = false;
    bool m_colabAvailable = false;
    quint64 m_gatewayGeneration = 0;
    quint64 m_colabGeneration = 0;
    quint64 m_gatewayRequestGeneration = 0;
    quint64 m_colabRequestGeneration = 0;
};

} // namespace LAStudio
