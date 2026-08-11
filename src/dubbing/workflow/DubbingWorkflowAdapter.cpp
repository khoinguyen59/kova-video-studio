#include "dubbing/workflow/DubbingWorkflowAdapter.h"

#include "controllers/dubbing/DubbingJobRunner.h"
#include "core/Logger.h"

namespace LAStudio {

DubbingWorkflowAdapter::DubbingWorkflowAdapter(DubbingJobRunner *runner, QObject *parent)
    : WorkflowExecutionAdapter(parent), m_runner(runner)
{
    if (!m_runner) return;
    connect(m_runner, &DubbingJobRunner::stageCompleted,
            this, &DubbingWorkflowAdapter::stageCompleted);
    connect(m_runner, &DubbingJobRunner::errorOccurred,
            this, &DubbingWorkflowAdapter::failed);
    connect(m_runner, &DubbingJobRunner::stateChanged, this, [this]() {
        if (!m_runner || !m_runner->processing() || !activeNodeMatchesStage()) return;
        emit progress(m_runner->progress(), m_runner->stage());
    });
}

void DubbingWorkflowAdapter::start(const QString &nodeType, const QVariantMap &inputs,
                                   const QVariantMap &parameters)
{
    Logger::info(QStringLiteral("DubbingWorkflow"),
                 QStringLiteral("Node start type=%1 inputs=%2 parameters=%3")
                     .arg(nodeType, inputs.keys().join(QLatin1Char(',')), parameters.keys().join(QLatin1Char(','))));
    if (!m_runner) {
        emit failed(QStringLiteral("Dubbing workflow runtime is unavailable."));
        return;
    }
    m_activeNodeType = nodeType;
    if (nodeType == QStringLiteral("media.ingest")) {
        m_runner->startIngest(inputs.value(QStringLiteral("media")).toString());
    } else if (nodeType == QStringLiteral("audio.source-separate")) {
        m_runner->startSourceSeparation(inputs.value(QStringLiteral("audio")).toString(), parameters);
    } else if (nodeType == QStringLiteral("audio.transcribe")) {
        m_runner->startTranscription(parameters.value(QStringLiteral("language"), QStringLiteral("auto")).toString(),
                                     inputs.value(QStringLiteral("audio")).toString(),
                                     inputs.value(QStringLiteral("fallbackAudio")).toString(), parameters);
    } else if (nodeType == QStringLiteral("text.translate-transcript")) {
        int unresolvedConflicts = 0;
        for (const QVariant &value : inputs.value(QStringLiteral("transcript")).toList()) {
            const QVariantMap segment = value.toMap();
            if (segment.value(QStringLiteral("fusionNeedsReview")).toBool()
                || segment.value(QStringLiteral("fusionStatus")).toString()
                       == QStringLiteral("conflict")) {
                ++unresolvedConflicts;
            }
        }
        if (unresolvedConflicts > 0) {
            emit failed(QStringLiteral("Resolve %1 STT/OCR conflict(s) before Translate. The workflow will not choose a source silently.")
                            .arg(unresolvedConflicts));
            return;
        }
        m_runner->startTranslation(parameters.value(QStringLiteral("sourceLanguage"), QStringLiteral("zh")).toString(),
                                   parameters.value(QStringLiteral("targetLanguage")).toString(),
                                   inputs.value(QStringLiteral("transcript")).toList(), parameters);
    } else if (nodeType == QStringLiteral("dubbing.synthesize-segments")) {
        QVariantMap synthesisSettings = parameters.value(QStringLiteral("synthesisSettings")).toMap();
        m_runner->startAudioGeneration(inputs.value(QStringLiteral("transcript")).toList(),
                                       parameters.value(QStringLiteral("projectPath")).toString(),
                                       synthesisSettings);
    } else if (nodeType == QStringLiteral("dubbing.fit-timing")) {
        m_runner->fitTiming(inputs.value(QStringLiteral("timeline")).toList(),
                            parameters.value(QStringLiteral("projectPath")).toString());
    } else if (nodeType == QStringLiteral("audio.mix-timeline")) {
        m_runner->renderPreview(inputs.value(QStringLiteral("timeline")).toList(),
                                parameters.value(QStringLiteral("projectPath")).toString(),
                                parameters.value(QStringLiteral("outputPath")).toString());
    } else if (nodeType == QStringLiteral("media.export")) {
        m_runner->startExport(inputs.value(QStringLiteral("sourceMedia")).toString(),
                              inputs.value(QStringLiteral("dubbedAudio")).toString(),
                              parameters.value(QStringLiteral("destination")).toString(),
                              inputs.value(QStringLiteral("subtitles")).toList());
    } else {
        emit failed(QStringLiteral("No execution capability exists for node type: %1").arg(nodeType));
    }
}

bool DubbingWorkflowAdapter::activeNodeMatchesStage() const
{
    if (!m_runner) return false;
    const QString stage = m_runner->stage();
    return (m_activeNodeType == QStringLiteral("media.ingest") && stage == QStringLiteral("import"))
        || (m_activeNodeType == QStringLiteral("audio.source-separate") && stage == QStringLiteral("source-separation"))
        || (m_activeNodeType == QStringLiteral("audio.transcribe") && stage == QStringLiteral("transcription"))
        || (m_activeNodeType == QStringLiteral("text.translate-transcript") && stage == QStringLiteral("translation"))
        || (m_activeNodeType == QStringLiteral("dubbing.synthesize-segments") && stage == QStringLiteral("tts"))
        || (m_activeNodeType == QStringLiteral("dubbing.fit-timing") && stage == QStringLiteral("timing"))
        || (m_activeNodeType == QStringLiteral("audio.mix-timeline") && stage == QStringLiteral("mix"))
        || (m_activeNodeType == QStringLiteral("media.export") && stage == QStringLiteral("export"));
}

void DubbingWorkflowAdapter::cancel()
{
    if (m_runner) m_runner->cancel();
}

void DubbingWorkflowAdapter::resume(const QVariantMap &decision)
{
    Q_UNUSED(decision)
    // Review gates are owned by the graph executor because they are workflow
    // control-flow, not a Dubbing-specific execution capability.
}

} // namespace LAStudio
