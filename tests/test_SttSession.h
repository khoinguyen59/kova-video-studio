#pragma once
#include <QObject>

namespace LAStudio {

class TestSttSession : public QObject {
    Q_OBJECT

private slots:
    void cleanupTestCase();
    void testSttAudioDecoder();
    void testSttAudioDecoderResamplesStereoWavOffThread();
    void testSttSessionPendingLoads();
    void testSttSessionHistoryRoundTrip();
    void testSttSessionUrlPreview();
    void testSttSessionQmlNotifications();
    void testSttRecordingSourceSelection();
    void testColabSttModelNotebookMapping();
    void testColabSttRunnerUsesAsynchronousJobContract();
    void testSpeechNotebookMatchesDirectColabSttContract();
    void testGatewaySttRunnerPostsOpenAiCompatibleMultipart();
    void testExplicitProviderRoutingDoesNotFallback();
    void testRemoteFirstBlocksLocalStt();
    void testSttRouteSelectionDoesNotFallbackAcrossGatewayAndColab();
};

} // namespace LAStudio
