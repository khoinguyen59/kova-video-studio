#pragma once
#include <QObject>

namespace LAStudio {

class TestModelsAndRuntimes : public QObject {
    Q_OBJECT

private slots:
    void cleanupTestCase();
    void testModelManagerConcreteModelDir();
    void testModelManagerResolvesSplitVirtualModelFiles();
    void testCapabilityFamilyModelSuitability();
    void testVoiceDesignFamiliesExposeRuntimeOptions();
    void testForcedAlignmentCatalogEntry();
    void testVoiceIsolationRuntimeCatalogEntry();
    void testProcessRuntimeManifest();
    void testProcessRuntimeRejectsMissingEntrypoint();
    void testLlamaRuntimeManifestRejectsIncompatibleAbi();
    void testOptionalLlamaTranslationRuntimeLoad();
    void testLlamaTranslationRejectsIncompatibleRuntimeAbi();
    void testLlamaContextTranslationParser();
    void testLlamaCatalogIncludesAllWindowsX64Runtimes();
    void testStudioSelectionRepositoryRemembersFilesPerFamily();
    void testVieNeuV3CatalogIncludesMossExternalData();
    void testCapabilityFamilyModelAcceptsExistingModelFiles();
    void testCapabilityFamilyModelIgnoresEmptyInitialSelection();
    void testQwen3TtsUsesAutomaticFrameLimit();
    void testQwen3TtsDoesNotExposeUnsupportedLengthScale();
    void testLogViewServicePending();
    void testLogSanitization();
    void testStudioConfigurationResolver();
    void testTranslationRecommendationUsesCompatibleRuntime();
    void testCapabilitySettingsSchemaPreservesRuntimeVoiceChoices();
    void testUpdateChecksRequireExplicitConsent();
    void testRuntimeManifestPreservesNativeDependencies();
};

} // namespace LAStudio
