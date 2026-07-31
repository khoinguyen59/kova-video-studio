find_package(Qt6 REQUIRED COMPONENTS Core Test Concurrent Sql Network Multimedia Quick)
find_package(CURL CONFIG REQUIRED)

set(TEST_SOURCES
    main.cpp
    test_FileAccessService.cpp
    test_ModelsPathMigration.cpp
    test_History.cpp
    test_DownloadInstallService.cpp
    test_AudioPreviewService.cpp
    test_ModelsAndRuntimes.cpp
    test_SttSession.cpp
    test_GatewayTtsRunner.cpp
    test_GatewayTtsRunner.h
    test_ColabTtsRunner.cpp
    test_ColabTtsRunner.h
    test_ColabVoiceCloneRunner.cpp
    test_ColabVoiceCloneRunner.h
    test_ColabVoiceDesignRunner.cpp
    test_ColabVoiceDesignRunner.h
    test_ColabAlignmentRunner.cpp
    test_ColabAlignmentRunner.h
    test_ColabSeparationRunner.cpp
    test_ColabSeparationRunner.h
    test_ColabTranslationRunner.cpp
    test_ColabTranslationRunner.h
    test_ColabChatRunner.cpp
    test_ColabChatRunner.h
    test_SubtitleVoice.cpp
    test_SubtitleOcrPipeline.cpp
    test_TtsTextPreprocessor.cpp
    test_TtsRequestValidator.cpp
    test_LlmChatEngine.cpp
    test_RemoteExecution.cpp
    test_StudioCapabilities.cpp
    test_AlignmentTranscriptMatcher.cpp
    test_AlignmentWorkflow.cpp
    test_DubbingProject.cpp
    test_TranslationProject.cpp
    test_WorkflowGraph.cpp
    test_SourceSeparation.cpp
    test_RuntimeHostProtocol.cpp
    test_MediaIngestService.cpp
    test_MediaToolService.cpp
    test_HardwareManager.cpp
    mocks/SttEngine.cpp
    mocks/SttEngineInstance.cpp
    mocks/TtsEngine.cpp
    mocks/TtsEngineInstance.cpp
    mocks/AudioRecorder.cpp
    mocks/AudioPlayer.cpp
    mocks/HFHubClient.cpp
    mocks/DownloadManager.cpp
    mocks/AppController.cpp
    test_SubtitleVoice.h
    test_SubtitleOcrPipeline.h

    ../src/api/ApiServerService.cpp

    ../src/controllers/shared/FileAccessService.cpp
    ../src/controllers/shared/AppUpdateService.cpp
    ../src/controllers/shared/ExampleManager.cpp
    ../src/controllers/models/DownloadInstallService.cpp
    ../src/controllers/models/RemoteModelCatalogController.cpp
    ../src/controllers/alignment/AlignmentExecutionService.cpp
    ../src/controllers/alignment/ColabAlignmentController.cpp
    ../src/controllers/separation/ColabVoiceIsolatorController.cpp
    ../src/dubbing/AlignmentRefinementService.cpp
    ../src/dubbing/DubbingSegmentNormalizer.cpp
    ../src/controllers/alignment/AlignmentTranscriptMatcher.cpp
    ../src/alignment/CrispAlignmentInterface.cpp
    ../src/translation/CrispTranslationInterface.cpp
    ../src/translation/LlamaTranslationInterface.cpp
    ../src/llm/LlmChatEngine.cpp
    ../src/llm/ColabChatRunner.cpp
    ../src/controllers/llm/LlmChatModelSession.cpp
    ../src/controllers/llm/LlmChatController.cpp
    ../src/controllers/models/ModelsPathMigrator.cpp
    ../src/controllers/models/ModelsPathMigrationService.cpp
    ../src/controllers/shared/HistoryRepository.cpp
    ../src/controllers/shared/HistoryService.cpp
    ../src/controllers/shared/AudioPreviewService.cpp
    ../src/controllers/app/StudioActions.cpp
    ../src/controllers/shared/VoiceClonePresetService.cpp
    ../src/controllers/shared/VoiceDesignPresetService.cpp
    ../src/controllers/alignment/AlignmentModelSession.cpp
    ../src/controllers/translation/TranslationModelSession.cpp
    ../src/translation/TranslationProject.cpp
    ../src/translation/TranslationEngine.cpp
    ../src/translation/TranslationEngineInstance.cpp
    ../src/translation/TranslationWorker.cpp
    ../src/translation/GatewayTranslationRunner.cpp
    ../src/translation/ColabTranslationRunner.cpp
    ../src/translation/backends/TranslationBackendFactory.cpp
    ../src/translation/backends/CrispTranslationBackend.cpp
    ../src/translation/backends/LlamaTranslationBackend.cpp
    ../src/translation/TranslationService.cpp
    ../src/controllers/translation/TranslationController.cpp
    ../src/controllers/separation/VoiceIsolatorModelSession.cpp
    ../src/controllers/models/ModelLifecycleController.cpp
    ../src/controllers/stt/SttModelSession.cpp
    ../src/controllers/tts/TtsSharedModelSession.cpp
    ../src/controllers/models/ModelSessionRegistry.cpp
    ../src/controllers/models/StudioConfigurationResolver.cpp
    ../src/controllers/models/CapabilitySettingsSchema.cpp
    ../src/controllers/stt/SttSessionController.cpp
    ../src/controllers/tts/GatewayTtsController.cpp
    ../src/controllers/tts/ColabTtsController.cpp
    ../src/controllers/tts/ColabVoiceCloneController.cpp
    ../src/controllers/tts/ColabVoiceDesignController.cpp
    ../src/controllers/tts/SubtitleVoiceController.cpp
    ../src/subtitles/SrtTimelineParser.cpp
    ../src/subtitles/SubtitleOcrPipeline.cpp
    ../src/tts/SubtitleSmartFitPlanner.cpp
    ../src/tts/TextNormalizationPolicy.cpp
    ../src/tts/TtsTextPreprocessor.cpp
    ../src/tts/TtsRequestValidator.cpp
    ../src/tts/TimedSpeechPipeline.cpp
    ../src/audio/AudioTimelineRenderer.cpp
    ../src/controllers/stt/SttAudioDecoder.cpp
    ../src/dubbing/DubbingProject.cpp
    ../src/dubbing/CapCutDraftExporter.cpp
    ../src/dubbing/EspeakNgPhonemizer.cpp
    ../src/dubbing/DubbingDuration.cpp
    ../src/dubbing/DubbingVoiceReferenceSelector.cpp
    ../src/dubbing/DubbingVoiceReferenceSelector.h
    ../src/dubbing/DubbingTimingProfile.cpp
    ../src/dubbing/DubbingTimingService.cpp
    ../src/controllers/dubbing/DubbingController.cpp
    ../src/dubbing/AudioTimelineMixer.cpp
    ../src/controllers/dubbing/DubbingJobRunner.cpp
    ../src/controllers/dubbing/DubbingRunCoordinator.cpp
    ../src/controllers/dubbing/SourceSeparationConfigurationResolver.cpp
    ../src/controllers/dubbing/DubbingTranscriptionJob.cpp
    ../src/controllers/dubbing/DubbingSynthesisJob.cpp
    ../src/controllers/dubbing/DubbingExportJob.cpp
    ../src/controllers/dubbing/DubbingTranslationJob.cpp
    ../src/controllers/dubbing/DubbingTranslationFixService.cpp
    ../src/dubbing/DubbingTranslationService.cpp
    ../src/dubbing/media/MediaToolService.cpp
    ../src/dubbing/media/AtomicMediaCommit.cpp
    ../src/dubbing/media/MediaIngestService.cpp
    ../src/dubbing/media/RemoteMediaImportService.cpp
    ../src/separation/SeparationAudioIO.cpp
    ../src/separation/SourceSeparationService.cpp
    ../src/separation/SeparationWorker.cpp
    ../src/separation/backends/SeparationBackendFactory.cpp
    ../src/separation/backends/sherpa/SherpaOnnxRuntime.cpp
    ../src/separation/backends/sherpa/SherpaOnnxSeparationBackend.cpp
    ../src/controllers/separation/VoiceIsolatorController.cpp
    ../src/controllers/app/WorkflowActivityManager.cpp
    ../src/workflows/WorkflowTypes.cpp
    ../src/workflows/WorkflowSession.cpp
    ../src/workflows/WorkflowGraph.cpp
    ../src/dubbing/workflow/DubbingWorkflowDefinition.cpp
    ../src/workflows/WorkflowArtifact.cpp
    ../src/workflows/WorkflowRun.cpp
    ../src/workflows/WorkflowRunJournal.cpp
    ../src/workflows/WorkflowTranscript.cpp
    ../src/dubbing/workflow/DubbingWorkflowNodes.cpp
    ../src/dubbing/workflow/DubbingWorkflowAdapter.cpp
    ../src/workflows/NodeRegistry.cpp
    ../src/workflows/WorkflowGraphRunner.cpp
    ../src/alignment/AlignmentWorkflowResolver.cpp
    ../src/core/LogViewService.cpp
    ../src/core/CacheLifecycleService.cpp
    ../src/core/PathUtils.cpp
    ../src/core/MediaRuntimeLocator.cpp
    ../src/core/Logger.cpp
    ../src/core/InferenceThreadPolicy.cpp
    ../src/inference/WorkerThreadHost.cpp
    ../src/core/CapabilityFamilyModel.cpp
    ../src/core/HardwareManager.cpp
    ../src/core/StudioCapabilityRegistry.cpp
    ../src/core/StudioSelectionRepository.cpp
    ../src/core/Settings.cpp
    ../src/core/SecureCredentialStore.cpp
    ../src/remote/ExecutionProvider.cpp
    ../src/remote/ColabSession.cpp
    ../src/remote/GatewayClient.cpp
    ../src/remote/ColabWorkerClient.cpp
    ../src/remote/GatewayModelCatalog.cpp
    ../src/remote/ColabCapabilityCatalog.cpp
    ../src/stt/ColabSttRunner.cpp
    ../src/stt/GatewaySttRunner.cpp
    ../src/tts/GatewayTtsRunner.cpp
    ../src/tts/ColabTtsRunner.cpp
    ../src/tts/ColabVoiceCloneRunner.cpp
    ../src/tts/ColabVoiceDesignRunner.cpp
    ../src/alignment/ColabAlignmentRunner.cpp
    ../src/separation/ColabSeparationRunner.cpp
    ../src/core/LocalizationManager.cpp
    ../src/core/ModelManager.cpp
    ../src/core/CatalogManager.cpp
    ../src/core/RegistryManager.cpp
    ../src/core/RuntimeManager.cpp
    ../src/core/QmlLogger.cpp
    ../src/audio/WavIO.cpp
    ../src/audio/AudioFileDecoder.cpp
    ../src/runtimehost/RuntimeHostProtocol.cpp
    ../src/runtimehost/RuntimeHostClient.cpp
    ../src/runtimehost/RuntimeHostSharedBuffer.cpp
    ../src/runtimehost/RuntimeHostManager.cpp
    ../src/runtimehost/HostedWhisperBackend.cpp
    ../src/runtimehost/HostedLlamaTranslationBackend.cpp
    ../src/audio/WaveformProvider.cpp

    ../src/core/DownloadManager.h
    ../src/core/MediaRuntimeLocator.h
    ../src/subtitles/SubtitleOcrPipeline.h
    ../src/api/ApiServerService.h
    ../src/stt/SttEngine.h
    ../src/stt/SttEngineInstance.h
    ../src/tts/TtsEngine.h
    ../src/tts/TtsEngineInstance.h
    ../src/tts/TtsRequestValidator.h
    ../src/audio/AudioRecorder.h
    ../src/audio/AudioPlayer.h
    ../src/audio/AudioFileDecoder.h
    ../src/runtimehost/RuntimeHostProtocol.h
    ../src/core/HFHubClient.h
    ../src/remote/GatewayClient.h
    ../src/remote/ColabWorkerClient.h
    ../src/remote/GatewayModelCatalog.h
    ../src/remote/ColabCapabilityCatalog.h
    ../src/stt/ColabSttRunner.h
    ../src/stt/GatewaySttRunner.h
    ../src/tts/GatewayTtsRunner.h
    ../src/tts/ColabTtsRunner.h
    ../src/tts/ColabVoiceCloneRunner.h
    ../src/tts/ColabVoiceDesignRunner.h
    ../src/alignment/ColabAlignmentRunner.h
    ../src/separation/ColabSeparationRunner.h
    ../src/controllers/tts/GatewayTtsController.h
    ../src/controllers/tts/ColabTtsController.h
    ../src/controllers/tts/ColabVoiceCloneController.h
    ../src/controllers/tts/ColabVoiceDesignController.h
    ../src/controllers/models/RemoteModelCatalogController.h
    ../src/controllers/alignment/ColabAlignmentController.h
    ../src/controllers/separation/ColabVoiceIsolatorController.h
    ../src/controllers/app/AppController.h
    ../src/controllers/shared/AppUpdateService.h
    ../src/controllers/shared/ExampleManager.h
    ../src/controllers/app/IStudioAction.h
    ../src/controllers/app/StudioActions.h
    ../src/controllers/models/IModelSession.h
    ../src/inference/WorkerThreadHost.h
    ../src/controllers/models/ModelLifecycleController.h
    ../src/controllers/stt/SttModelSession.h
    ../src/controllers/tts/TtsSharedModelSession.h
    ../src/controllers/alignment/AlignmentModelSession.h
    ../src/controllers/translation/TranslationModelSession.h
    ../src/translation/TranslationProject.h
    ../src/translation/TranslationEngine.h
    ../src/translation/TranslationEngineInstance.h
    ../src/translation/TranslationWorker.h
    ../src/translation/GatewayTranslationRunner.h
    ../src/translation/ColabTranslationRunner.h
    ../src/translation/backends/TranslationBackend.h
    ../src/translation/backends/TranslationBackendFactory.h
    ../src/translation/backends/CrispTranslationBackend.h
    ../src/translation/backends/LlamaTranslationBackend.h
    ../src/translation/TranslationService.h
    ../src/controllers/translation/TranslationController.h
    ../src/controllers/separation/VoiceIsolatorModelSession.h
    ../src/controllers/models/ModelSessionRegistry.h
    ../src/llm/LlmChatEngine.h
    ../src/llm/ColabChatRunner.h
    ../src/controllers/llm/LlmChatModelSession.h
    ../src/controllers/llm/LlmChatController.h
    ../src/controllers/app/WorkflowActivityManager.h
    ../src/workflows/WorkflowTypes.h
    ../src/workflows/IWorkflowResolver.h
    ../src/workflows/WorkflowSession.h
    ../src/workflows/WorkflowGraph.h
    ../src/dubbing/workflow/DubbingWorkflowDefinition.h
    ../src/workflows/WorkflowArtifact.h
    ../src/workflows/WorkflowRun.h
    ../src/workflows/WorkflowRunJournal.h
    ../src/workflows/WorkflowTranscript.h
    ../src/dubbing/workflow/DubbingWorkflowNodes.h
    ../src/workflows/WorkflowExecutionAdapter.h
    ../src/dubbing/workflow/DubbingWorkflowAdapter.h
    ../src/workflows/NodeRegistry.h
    ../src/workflows/WorkflowGraphRunner.h
    ../src/alignment/AlignmentWorkflowResolver.h
    ../include/runtimes/CrispTranslationInterface.h
    ../src/separation/SeparationTypes.h
    ../src/separation/SeparationAudioIO.h
    ../src/separation/SourceSeparationService.h
    ../src/separation/SeparationWorker.h
    ../src/separation/backends/SeparationBackend.h
    ../src/separation/backends/SeparationBackendFactory.h
    ../src/separation/backends/sherpa/SherpaOnnxRuntime.h
    ../src/separation/backends/sherpa/SherpaOnnxSeparationBackend.h
    test_SourceSeparation.h
    test_RuntimeHostProtocol.h
    test_MediaIngestService.h
    test_MediaToolService.h
    test_LlmChatEngine.h
    test_RemoteExecution.h
    test_HardwareManager.h
    test_TranslationProject.h
    ../src/controllers/separation/VoiceIsolatorController.h
)

add_executable(LAStudioUnitTests ${TEST_SOURCES})

add_custom_command(TARGET LAStudioUnitTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/data"
        "$<TARGET_FILE_DIR:LAStudioUnitTests>/data"
    VERBATIM
)

target_compile_definitions(LAStudioUnitTests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    LASTUDIO_UNIT_TESTS=1
    LASTUDIO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(LAStudioUnitTests PRIVATE
    ${LASTUDIO_GENERATED_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/audio
    ${CMAKE_SOURCE_DIR}/src/controllers
    ${CMAKE_SOURCE_DIR}/src/controllers/app
    ${CMAKE_SOURCE_DIR}/src/controllers/alignment
    ${CMAKE_SOURCE_DIR}/src/controllers/dubbing
    ${CMAKE_SOURCE_DIR}/src/controllers/stt
    ${CMAKE_SOURCE_DIR}/src/controllers/tts
    ${CMAKE_SOURCE_DIR}/src/controllers/translation
    ${CMAKE_SOURCE_DIR}/src/controllers/separation
    ${CMAKE_SOURCE_DIR}/src/controllers/models
    ${CMAKE_SOURCE_DIR}/src/controllers/shared
    ${CMAKE_SOURCE_DIR}/src/dubbing
    ${CMAKE_SOURCE_DIR}/src/dubbing/media
    ${CMAKE_SOURCE_DIR}/src/dubbing/workflow
    ${CMAKE_SOURCE_DIR}/src/alignment
    ${CMAKE_SOURCE_DIR}/src/separation
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${LLAMA_CPP_SOURCE_DIR}/include
    ${LLAMA_CPP_SOURCE_DIR}/ggml/include
)

target_link_libraries(LAStudioUnitTests PRIVATE
    vietnorm::vietnorm
    Qt6::Core
    Qt6::Test
    Qt6::Concurrent
    Qt6::Sql
    Qt6::Network
    Qt6::Multimedia
    Qt6::Quick
    CURL::libcurl
    dxgi
)

if(WIN32)
    target_link_libraries(LAStudioUnitTests PRIVATE shell32 wintrust crypt32)
endif()

get_target_property(QT6_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT QT6_CORE_DLL)
    get_target_property(QT6_CORE_DLL Qt6::Core IMPORTED_LOCATION)
endif()
get_filename_component(QT_BIN_DIR "${QT6_CORE_DLL}" DIRECTORY)
get_filename_component(QT_ROOT_DIR "${QT_BIN_DIR}" DIRECTORY)
set(QT_PLUGIN_DIR "${QT_ROOT_DIR}/plugins")

set(LASTUDIO_TEST_SUITES
    TestFileAccessService
    TestModelsPathMigration
    TestHistory
    TestDownloadInstallService
    TestAudioPreviewService
    TestModelsAndRuntimes
    TestSttSession
    TestStudioCapabilities
    TestAlignmentTranscriptMatcher
    TestAlignmentWorkflow
    TestDubbingProject
    TestTranslationProject
    TestWorkflowGraph
    TestSourceSeparation
    TestSubtitleVoice
    TestSubtitleOcrPipeline
    TestRuntimeHostProtocol
    TestMediaIngestService
    TestMediaToolService
    TestHardwareManager
    TestTtsTextPreprocessor
    TestTtsRequestValidator
    TestLlmChatEngine
    TestRemoteExecution
    TestGatewayTtsRunner
    TestColabTtsRunner
    TestColabVoiceCloneRunner
    TestColabVoiceDesignRunner
    TestColabAlignmentRunner
    TestColabSeparationRunner
    TestColabTranslationRunner
    TestColabChatRunner
)

foreach(LASTUDIO_TEST_SUITE IN LISTS LASTUDIO_TEST_SUITES)
    add_test(NAME ${LASTUDIO_TEST_SUITE} COMMAND LAStudioUnitTests)
    set_tests_properties(${LASTUDIO_TEST_SUITE} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        ENVIRONMENT "LASTUDIO_TEST_SUITE=${LASTUDIO_TEST_SUITE};QT_PLUGIN_PATH=${QT_PLUGIN_DIR};QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT_BIN_DIR}"
    )
endforeach()

if(WIN32)
    find_program(POWERSHELL_EXECUTABLE NAMES powershell.exe powershell)
    if(POWERSHELL_EXECUTABLE)
        add_test(NAME RemoteLivePreflightContract
            COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/test_RemoteLivePreflightContract.ps1")
        set_tests_properties(RemoteLivePreflightContract PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 30)
    endif()
endif()

add_test(NAME QmlRouteSmoke COMMAND $<TARGET_FILE:LAStudio>)
set_tests_properties(QmlRouteSmoke PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    ENVIRONMENT "LASTUDIO_DATA_DIR=${CMAKE_BINARY_DIR}/qml-smoke-data;LASTUDIO_QML_SMOKE=1;QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${QT_PLUGIN_DIR}"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT_BIN_DIR}"
    TIMEOUT 75
)

if(WIN32)
    # main.cpp intentionally removes PATH from the DLL search order. Deploy
    # the Qt runtime beside the smoke executable so this test verifies the
    # same trusted-layout rule used by the portable package, rather than
    # weakening the production DLL-search policy just for tests.
    find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt.exe windeployqt
        HINTS "${QT_BIN_DIR}" REQUIRED)
    add_test(NAME PrepareQmlRouteSmokeRuntime
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --verbose 0
            --qmldir "${CMAKE_SOURCE_DIR}/qml" --no-translations
            --compiler-runtime "$<TARGET_FILE:LAStudio>")
    set_tests_properties(PrepareQmlRouteSmokeRuntime PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        FIXTURES_SETUP QmlRouteSmokeRuntime
        TIMEOUT 60)
    set_tests_properties(QmlRouteSmoke PROPERTIES
        FIXTURES_REQUIRED QmlRouteSmokeRuntime)
endif()
