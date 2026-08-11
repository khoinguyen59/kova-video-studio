#include "dubbing/workflow/DubbingWorkflowDefinition.h"

namespace LAStudio {

namespace {
WorkflowGraphNode node(const QString &id, const QString &type, const QString &title,
                       const QVariantMap &properties = {})
{
    WorkflowGraphNode result;
    result.id = id;
    result.typeId = type;
    result.typeVersion = 1;
    result.title = title;
    result.properties = properties;
    result.parameters = properties;
    return result;
}

WorkflowGraphEdge link(const QString &id, const QString &fromNode, const QString &fromPort,
                       const QString &toNode, const QString &toPort)
{
    WorkflowGraphEdge result;
    result.id = id;
    result.sourceNodeId = fromNode;
    result.sourcePortId = fromPort;
    result.targetNodeId = toNode;
    result.targetPortId = toPort;
    return result;
}
}

WorkflowGraph DubbingWorkflowDefinition::create()
{
    WorkflowGraph graph;
    graph.id = QString::fromLatin1(Id);
    graph.version = Version;
    graph.kind = QStringLiteral("system");
    graph.title = QStringLiteral("Default Dubbing");
    graph.description = QStringLiteral("Remote-first dubbing workflow with independent Gateway and Colab routes.");
    graph.interfaceDefinition = {
        {QStringLiteral("inputs"), QVariantList{
            QVariantMap{{QStringLiteral("id"), QStringLiteral("media")}, {QStringLiteral("type"), QStringLiteral("media.source@1")}, {QStringLiteral("required"), true}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("targetLanguage")}, {QStringLiteral("type"), QStringLiteral("scalar.string")}, {QStringLiteral("required"), true}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("outputDestination")}, {QStringLiteral("type"), QStringLiteral("file.path")}, {QStringLiteral("required"), true}}
        }},
        {QStringLiteral("outputs"), QVariantList{
            QVariantMap{{QStringLiteral("id"), QStringLiteral("exportedMedia")}, {QStringLiteral("type"), QStringLiteral("media.export@1")}}
        }}
    };
    graph.policies = {{QStringLiteral("defaultFailure"), QStringLiteral("stop")},
                      {QStringLiteral("maxParallelNodes"), 2},
                      {QStringLiteral("offlineOnly"), false},
                      {QStringLiteral("remoteExecution"), QStringLiteral("explicit-per-node")}};
    graph.nodes = {
        node(QStringLiteral("media-input"), QStringLiteral("core.input"), QStringLiteral("Source Media"),
             {{QStringLiteral("dataType"), QStringLiteral("media.source@1")}}),
        node(QStringLiteral("ingest"), QStringLiteral("media.ingest"), QStringLiteral("Import & Normalize"),
             {{QStringLiteral("analysisSampleRate"), 16000}, {QStringLiteral("masterSampleRate"), 48000}}),
        node(QStringLiteral("source-separate"), QStringLiteral("audio.source-separate"), QStringLiteral("Isolate Voice"),
             {{QStringLiteral("qualityPreset"), QStringLiteral("quality")}, {QStringLiteral("fallback"), QStringLiteral("original-audio")},
              {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
              {QStringLiteral("modelId"), QStringLiteral("sherpa-onnx-spleeter-2stems-fp16")}}),
        node(QStringLiteral("transcribe"), QStringLiteral("audio.transcribe"), QStringLiteral("Transcribe"),
             {{QStringLiteral("language"), QStringLiteral("auto")},
              {QStringLiteral("transcriptSource"), QStringLiteral("stt")},
              {QStringLiteral("wordTimestamps"), true},
              {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
              {QStringLiteral("modelId"), QStringLiteral("whisper.cpp")},
              {QStringLiteral("refineAlignmentWithColab"), false},
              {QStringLiteral("alignmentModelId"), QStringLiteral("mms-forced-aligner-onnx")}}),
        node(QStringLiteral("review-transcript"), QStringLiteral("core.review-gate"), QStringLiteral("Review Transcript"),
             {{QStringLiteral("mode"), QStringLiteral("always")}, {QStringLiteral("editor"), QStringLiteral("dubbing.transcript")}}),
        node(QStringLiteral("translate"), QStringLiteral("text.translate-transcript"), QStringLiteral("Translate Transcript"),
             {{QStringLiteral("sourceLanguage"), QStringLiteral("zh")}, {QStringLiteral("targetLanguage"), QStringLiteral("vi")},
              {QStringLiteral("executionProvider"), QStringLiteral("api-gateway")},
              {QStringLiteral("qualityPreset"), QStringLiteral("balanced")},
              {QStringLiteral("durationAware"), true}, {QStringLiteral("unit"), QStringLiteral("phoneme-v1")},
              {QStringLiteral("maxPreTtsIterations"), 4}, {QStringLiteral("candidatesPerIteration"), 3},
              {QStringLiteral("pauseThresholdMs"), 250}}),
        node(QStringLiteral("review-translation"), QStringLiteral("core.review-gate"), QStringLiteral("Review Translation"),
             {{QStringLiteral("mode"), QStringLiteral("always")}, {QStringLiteral("editor"), QStringLiteral("dubbing.translation")}}),
        node(QStringLiteral("assign-voices"), QStringLiteral("dubbing.assign-voices"), QStringLiteral("Select TTS Voice"),
             {{QStringLiteral("voicePolicy"), QStringLiteral("project-selected-tts-voice")}}),
        node(QStringLiteral("synthesize"), QStringLiteral("dubbing.synthesize-segments"), QStringLiteral("TTS (Text to Speech)"),
             {{QStringLiteral("cache"), QStringLiteral("auto")},
              {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
              {QStringLiteral("modelId"), QStringLiteral("kokoro")},
              {QStringLiteral("voice"), QStringLiteral("af_heart")}}),
        node(QStringLiteral("fit-timing"), QStringLiteral("dubbing.fit-timing"), QStringLiteral("Fit Timing"),
             {{QStringLiteral("policy"), QStringLiteral("preserve-video")}}),
        node(QStringLiteral("review-conflicts"), QStringLiteral("core.review-gate"), QStringLiteral("Review Timing Conflicts"),
             {{QStringLiteral("mode"), QStringLiteral("on-conflict")}, {QStringLiteral("editor"), QStringLiteral("dubbing.timeline")}}),
        node(QStringLiteral("mix"), QStringLiteral("audio.mix-timeline"), QStringLiteral("Mix Timeline"),
             {{QStringLiteral("sampleRate"), 48000}, {QStringLiteral("loudnessPreset"), QStringLiteral("web-stereo")}}),
        node(QStringLiteral("export"), QStringLiteral("media.export"), QStringLiteral("Export Media"),
             {{QStringLiteral("videoCodecPolicy"), QStringLiteral("copy-if-possible")}, {QStringLiteral("audioCodec"), QStringLiteral("aac")},
              {QStringLiteral("overwrite"), QStringLiteral("confirm")}})
    };
    graph.edges = {
        link(QStringLiteral("l01"), QStringLiteral("media-input"), QStringLiteral("value"), QStringLiteral("ingest"), QStringLiteral("media")),
        link(QStringLiteral("l02"), QStringLiteral("ingest"), QStringLiteral("masterAudio"), QStringLiteral("source-separate"), QStringLiteral("audio")),
        link(QStringLiteral("l02b"), QStringLiteral("source-separate"), QStringLiteral("vocals"), QStringLiteral("transcribe"), QStringLiteral("audio")),
        link(QStringLiteral("l02c"), QStringLiteral("ingest"), QStringLiteral("analysisAudio"), QStringLiteral("transcribe"), QStringLiteral("fallbackAudio")),
        link(QStringLiteral("l03"), QStringLiteral("transcribe"), QStringLiteral("transcript"), QStringLiteral("review-transcript"), QStringLiteral("artifact")),
        link(QStringLiteral("l04"), QStringLiteral("review-transcript"), QStringLiteral("artifact"), QStringLiteral("translate"), QStringLiteral("transcript")),
        link(QStringLiteral("l05"), QStringLiteral("translate"), QStringLiteral("transcript"), QStringLiteral("review-translation"), QStringLiteral("artifact")),
        link(QStringLiteral("l06"), QStringLiteral("review-translation"), QStringLiteral("artifact"), QStringLiteral("assign-voices"), QStringLiteral("transcript")),
        link(QStringLiteral("l07"), QStringLiteral("assign-voices"), QStringLiteral("transcript"), QStringLiteral("synthesize"), QStringLiteral("transcript")),
        link(QStringLiteral("l08"), QStringLiteral("synthesize"), QStringLiteral("timeline"), QStringLiteral("fit-timing"), QStringLiteral("timeline")),
        link(QStringLiteral("l09"), QStringLiteral("fit-timing"), QStringLiteral("timeline"), QStringLiteral("review-conflicts"), QStringLiteral("artifact")),
        link(QStringLiteral("l10"), QStringLiteral("review-conflicts"), QStringLiteral("artifact"), QStringLiteral("mix"), QStringLiteral("timeline")),
        link(QStringLiteral("l11"), QStringLiteral("source-separate"), QStringLiteral("background"), QStringLiteral("mix"), QStringLiteral("background")),
        link(QStringLiteral("l12"), QStringLiteral("media-input"), QStringLiteral("value"), QStringLiteral("export"), QStringLiteral("sourceMedia")),
        link(QStringLiteral("l13"), QStringLiteral("mix"), QStringLiteral("audio"), QStringLiteral("export"), QStringLiteral("dubbedAudio")),
        link(QStringLiteral("l14"), QStringLiteral("review-translation"), QStringLiteral("artifact"), QStringLiteral("export"), QStringLiteral("subtitles"))
    };
    return graph;
}

} // namespace LAStudio
