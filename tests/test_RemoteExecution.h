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
    void temporaryColabWorkerWrapperValidatesAndRemainsEphemeral();
    void temporaryColabWorkerVerifiesCudaCapabilityAndExactModel();
    void temporaryColabWorkerRejectsWrongVariant();
    void staleTranslationPatchContractIsRejected();
    void staleSttWorkerRevisionIsRejected();
    void temporaryColabWorkerRejectsCpuWrongModelAndWrongCapability();
    void newerColabVerificationSupersedesStaleRequest();
    void everyGpuFeatureSurfacesVerifiedColabSessionState();
    void voiceCloneUiMakesConsentAndRequiredInputsActionable();
    void voiceCloneOmniVoiceIsReusableInTtsWithoutLocalFallback();
    void settingsControlsExposeDescriptionsAndKeyboardFocus();
    void workflowActivityOnlyDisplaysMeasuredProgress();
    void everyGpuControllerUsesExactVerifiedColabRoute();
    void appControllerScopesColabSessionsPerCapability();
    void gatewayCredentialUsesDedicatedSecureStoreEntry();
    void remoteFirstModeIsExplicitAndPersistent();
    void remoteFirstVoiceCloneStaysDirectWhenAColabSessionIsAvailable();
    void remoteFirstVoiceDesignStaysDirectWhenAColabSessionIsAvailable();
    void remoteFirstVoiceIsolationStaysDirectWhenAColabSessionIsAvailable();
    void remoteFirstAlignmentStaysDirectWhenAColabSessionIsAvailable();
    void remoteFirstTtsBlocksLocalButPreservesIndependentRoutes();
    void gatewayAndColabTtsControllersStayIndependent();
    void remoteFirstTranslationBlocksLocalExecution();
    void remoteFirstChatBlocksLocalExecution();
    void gatewayAndColabFailuresRemainIndependent();
    void gatewayModelCatalogUsesGatewayOnly();
    void colabCapabilityCatalogUsesDirectWorkerOnly();
    void colabCapabilityCatalogRequiresSupportedContractVersion();
    void remoteCatalogRequestsTimeOut();
    void remoteModelCatalogAggregatesIndependentColabSessions();
    void remoteModelCatalogRetainsHealthyWorkerWhenAnotherFails();
    void colabNotebooksAdvertiseCapabilityContractVersion();
};

} // namespace LAStudio
