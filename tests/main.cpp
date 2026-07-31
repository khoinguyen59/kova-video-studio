#include <QCoreApplication>
#include <QTemporaryDir>
#include <QtTest>
#include <iostream>

#include "test_FileAccessService.h"
#include "test_ModelsPathMigration.h"
#include "test_History.h"
#include "test_DownloadInstallService.h"
#include "test_AudioPreviewService.h"
#include "test_ModelsAndRuntimes.h"
#include "test_SttSession.h"
#include "test_StudioCapabilities.h"
#include "test_AlignmentTranscriptMatcher.h"
#include "test_AlignmentWorkflow.h"
#include "test_DubbingProject.h"
#include "test_TranslationProject.h"
#include "test_WorkflowGraph.h"
#include "test_SourceSeparation.h"
#include "test_RuntimeHostProtocol.h"
#include "test_MediaIngestService.h"
#include "test_MediaToolService.h"
#include "test_HardwareManager.h"
#include "test_SubtitleVoice.h"
#include "test_SubtitleOcrPipeline.h"
#include "test_SubtitleOcrController.h"
#include "test_SubtitleOcrRuntimeService.h"
#include "test_TtsTextPreprocessor.h"
#include "test_TtsRequestValidator.h"
#include "test_LlmChatEngine.h"
#include "test_RemoteExecution.h"
#include "test_GatewayTtsRunner.h"
#include "test_ColabTtsRunner.h"
#include "test_ColabVoiceCloneRunner.h"
#include "test_ColabVoiceDesignRunner.h"
#include "test_ColabAlignmentRunner.h"
#include "test_ColabSeparationRunner.h"
#include "test_ColabTranslationRunner.h"
#include "test_ColabChatRunner.h"

#include <QFile>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QTemporaryDir testDataDir;
    if (!testDataDir.isValid()) {
        std::cerr << "Failed to create an isolated LA Studio test-data directory.\n";
        return 1;
    }
    qputenv("LASTUDIO_DATA_DIR", testDataDir.path().toUtf8());

    // Ensure that application info is available for tests that use Settings/PathUtils
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LAStudioUnitTests"));
    app.setOrganizationName(QStringLiteral("LAStudio"));

    int status = 0;
    const QString requestedSuite = qEnvironmentVariable("LASTUDIO_TEST_SUITE").trimmed();

    auto runSuite = [&status, &requestedSuite](QObject* testObj, const char* name) {
        if (!requestedSuite.isEmpty() && requestedSuite != QString::fromLatin1(name)) {
            return;
        }
        std::cout << "\n==================================================\n";
        std::cout << "Running suite: " << name << "\n";
        std::cout << "==================================================\n";

        QString filename = QString("%1_results.txt").arg(name);
        QByteArray filenameBytes = filename.toLocal8Bit();
        char* fileArg = filenameBytes.data();

        char* localArgv[] = {
            const_cast<char*>("LAStudioUnitTests"),
            const_cast<char*>("-o"),
            fileArg,
            nullptr
        };
        int localArgc = 3;

        int suiteStatus = QTest::qExec(testObj, localArgc, localArgv);
        status |= suiteStatus;

        QFile file(filename);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                std::cout << in.readLine().toStdString() << "\n";
            }
            file.close();
            file.remove();
        } else {
            std::cout << "Failed to read test results file: " << name << "_results.txt\n";
        }
    };

    {
        LAStudio::TestFileAccessService suite;
        runSuite(&suite, "TestFileAccessService");
    }

    {
        LAStudio::TestModelsPathMigration suite;
        runSuite(&suite, "TestModelsPathMigration");
    }

    {
        LAStudio::TestHistory suite;
        runSuite(&suite, "TestHistory");
    }

    {
        LAStudio::TestDownloadInstallService suite;
        runSuite(&suite, "TestDownloadInstallService");
    }

    {
        LAStudio::TestAudioPreviewService suite;
        runSuite(&suite, "TestAudioPreviewService");
    }

    {
        LAStudio::TestModelsAndRuntimes suite;
        runSuite(&suite, "TestModelsAndRuntimes");
    }

    {
        LAStudio::TestSttSession suite;
        runSuite(&suite, "TestSttSession");
    }

    {
        LAStudio::TestStudioCapabilities suite;
        runSuite(&suite, "TestStudioCapabilities");
    }

    {
        LAStudio::TestAlignmentTranscriptMatcher suite;
        runSuite(&suite, "TestAlignmentTranscriptMatcher");
    }

    {
        LAStudio::TestAlignmentWorkflow suite;
        runSuite(&suite, "TestAlignmentWorkflow");
    }

    {
        LAStudio::TestDubbingProject suite;
        runSuite(&suite, "TestDubbingProject");
    }

    {
        LAStudio::TestTranslationProject suite;
        runSuite(&suite, "TestTranslationProject");
    }

    {
        LAStudio::TestWorkflowGraph suite;
        runSuite(&suite, "TestWorkflowGraph");
    }

    {
        LAStudio::TestSourceSeparation suite;
        runSuite(&suite, "TestSourceSeparation");
    }

    {
        LAStudio::TestSubtitleVoice suite;
        runSuite(&suite, "TestSubtitleVoice");
    }

    {
        LAStudio::TestSubtitleOcrPipeline suite;
        runSuite(&suite, "TestSubtitleOcrPipeline");
    }

    {
        LAStudio::TestSubtitleOcrController suite;
        runSuite(&suite, "TestSubtitleOcrController");
    }

    {
        LAStudio::TestSubtitleOcrRuntimeService suite;
        runSuite(&suite, "TestSubtitleOcrRuntimeService");
    }

    {
        LAStudio::TestRuntimeHostProtocol suite;
        runSuite(&suite, "TestRuntimeHostProtocol");
    }

    {
        LAStudio::TestMediaIngestService suite;
        runSuite(&suite, "TestMediaIngestService");
    }

    {
        LAStudio::TestMediaToolService suite;
        runSuite(&suite, "TestMediaToolService");
    }

    {
        LAStudio::TestHardwareManager suite;
        runSuite(&suite, "TestHardwareManager");
    }

    {
        LAStudio::TestTtsTextPreprocessor suite;
        runSuite(&suite, "TestTtsTextPreprocessor");
    }

    {
        LAStudio::TestTtsRequestValidator suite;
        runSuite(&suite, "TestTtsRequestValidator");
    }

    {
        LAStudio::TestLlmChatEngine suite;
        runSuite(&suite, "TestLlmChatEngine");
    }

    {
        LAStudio::TestRemoteExecution suite;
        runSuite(&suite, "TestRemoteExecution");
    }

    {
        LAStudio::TestGatewayTtsRunner suite;
        runSuite(&suite, "TestGatewayTtsRunner");
    }

    {
        LAStudio::TestColabTtsRunner suite;
        runSuite(&suite, "TestColabTtsRunner");
    }
    {
        LAStudio::TestColabTranslationRunner suite;
        runSuite(&suite, "TestColabTranslationRunner");
    }
    {
        LAStudio::TestColabChatRunner suite;
        runSuite(&suite, "TestColabChatRunner");
    }
    {
        LAStudio::TestColabVoiceCloneRunner suite;
        runSuite(&suite, "TestColabVoiceCloneRunner");
    }
    {
        LAStudio::TestColabVoiceDesignRunner suite;
        runSuite(&suite, "TestColabVoiceDesignRunner");
    }
    {
        LAStudio::TestColabAlignmentRunner suite;
        runSuite(&suite, "TestColabAlignmentRunner");
    }
    {
        LAStudio::TestColabSeparationRunner suite;
        runSuite(&suite, "TestColabSeparationRunner");
    }

    std::cout << "\n==================================================\n";
    std::cout << "All test suites completed with status code: " << status << "\n";
    std::cout << "==================================================\n";

    return status;
}
