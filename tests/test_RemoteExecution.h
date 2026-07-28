#pragma once

#include <QObject>

namespace LAStudio {

class TestRemoteExecution : public QObject {
    Q_OBJECT

private slots:
    void executionProvidersHaveStableIds();
    void remoteEndpointsRequireHttpsByDefault();
    void apiGatewayEndpointNormalizesV1Url();
    void colabSessionIsMemoryOnlyAndCanBeCleared();
    void gatewayCredentialUsesDedicatedSecureStoreEntry();
    void remoteFirstModeIsExplicitAndPersistent();
    void remoteFirstVoiceCloneStaysDirectWhenAColabSessionIsAvailable();
    void remoteFirstVoiceDesignStaysDirectWhenAColabSessionIsAvailable();
    void remoteFirstVoiceIsolationStaysDirectWhenAColabSessionIsAvailable();
    void gatewayAndColabFailuresRemainIndependent();
    void gatewayModelCatalogUsesGatewayOnly();
    void colabCapabilityCatalogUsesDirectWorkerOnly();
};

} // namespace LAStudio
