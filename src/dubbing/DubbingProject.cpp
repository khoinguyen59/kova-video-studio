#include "dubbing/DubbingProject.h"
#include "workflows/WorkflowTranscript.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace LAStudio {

namespace {
void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}
}

bool DubbingProject::mergeSegmentPatches(const QVariantList &segments,
                                         const QVariantList &patches,
                                         QVariantList &merged,
                                         QString *error)
{
    WorkflowTranscriptArtifact base;
    if (!WorkflowTranscriptArtifact::fromVariantList(segments, base, error)) return false;
    WorkflowTranscriptArtifact result;
    if (!WorkflowTranscriptArtifact::mergePatches(base, patches, result, error)) return false;
    merged = result.toVariantList();
    return true;
}

QJsonObject DubbingProject::toJson() const
{
    QJsonObject json;
    json.insert(QStringLiteral("schemaVersion"), CurrentSchemaVersion);
    json.insert(QStringLiteral("sourceMediaPath"), sourceMediaPath);
    json.insert(QStringLiteral("sourceHash"), sourceHash);
    json.insert(QStringLiteral("masterAudioPath"), masterAudioPath);
    json.insert(QStringLiteral("analysisAudioPath"), analysisAudioPath);
    json.insert(QStringLiteral("backgroundAudioPath"), backgroundAudioPath);
    json.insert(QStringLiteral("sourceDurationMs"), sourceDurationMs);
    json.insert(QStringLiteral("sourceSampleRate"), sourceSampleRate);
    json.insert(QStringLiteral("sourceChannels"), sourceChannels);
    json.insert(QStringLiteral("sourceIsVideo"), sourceIsVideo);
    json.insert(QStringLiteral("sourceLanguage"), sourceLanguage);
    json.insert(QStringLiteral("targetLanguage"), targetLanguage);
    json.insert(QStringLiteral("dubbingQuality"), dubbingQuality);
    json.insert(QStringLiteral("workflowEntryMode"), workflowEntryMode);
    json.insert(QStringLiteral("ttsVoiceId"),
                ttsVoiceId.isEmpty() ? cloneVoicePresetId : ttsVoiceId);
    json.insert(QStringLiteral("durationControl"), QJsonObject::fromVariantMap(durationControl));
    json.insert(QStringLiteral("workflowNodeConfigurations"),
                QJsonObject::fromVariantMap(workflowNodeConfigurations));
    json.insert(QStringLiteral("transcriptConfiguration"),
                QJsonObject::fromVariantMap(transcriptConfiguration));
    json.insert(QStringLiteral("subtitleConfiguration"),
                QJsonObject::fromVariantMap(subtitleConfiguration));
    json.insert(QStringLiteral("timingConfiguration"),
                QJsonObject::fromVariantMap(timingConfiguration));
    json.insert(QStringLiteral("customRewriteConfiguration"),
                QJsonObject::fromVariantMap(customRewriteConfiguration));
    json.insert(QStringLiteral("speakers"), QJsonArray::fromVariantList(speakers));
    json.insert(QStringLiteral("segments"), QJsonArray::fromVariantList(segments));
    return json;
}

bool DubbingProject::fromJson(const QJsonObject &json, DubbingProject &project, QString *error)
{
    const int version = json.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (version < 1 || version > CurrentSchemaVersion) {
        setError(error, QStringLiteral("Unsupported dubbing project schema version: %1").arg(version));
        return false;
    }

    project.sourceMediaPath = json.value(QStringLiteral("sourceMediaPath")).toString();
    project.sourceHash = json.value(QStringLiteral("sourceHash")).toString();
    project.masterAudioPath = json.value(QStringLiteral("masterAudioPath")).toString();
    project.analysisAudioPath = json.value(QStringLiteral("analysisAudioPath")).toString();
    project.backgroundAudioPath = json.value(QStringLiteral("backgroundAudioPath")).toString();
    project.sourceDurationMs = json.value(QStringLiteral("sourceDurationMs")).toVariant().toLongLong();
    project.sourceSampleRate = json.value(QStringLiteral("sourceSampleRate")).toInt();
    project.sourceChannels = json.value(QStringLiteral("sourceChannels")).toInt();
    project.sourceIsVideo = json.value(QStringLiteral("sourceIsVideo")).toBool();
    project.sourceLanguage = json.value(QStringLiteral("sourceLanguage")).toString(QStringLiteral("zh"));
    project.targetLanguage = json.value(QStringLiteral("targetLanguage")).toString(QStringLiteral("vi"));
    project.dubbingQuality = json.value(QStringLiteral("dubbingQuality")).toString(QStringLiteral("adaptive"));
    if (project.dubbingQuality != QStringLiteral("adaptive")
        && project.dubbingQuality != QStringLiteral("custom"))
        project.dubbingQuality = QStringLiteral("fast");
    if (version >= 13) {
        const QString entryMode = json.value(QStringLiteral("workflowEntryMode")).toString().trimmed().toLower();
        if (entryMode == QStringLiteral("automatic") || entryMode == QStringLiteral("step"))
            project.workflowEntryMode = entryMode;
    }
    // Schema 12 renames the project decision from the implementation-specific
    // clone preset to a TTS voice.  Preserve every existing selection on load
    // but write only ttsVoiceId on the next save.
    if (version >= 12)
        project.ttsVoiceId = json.value(QStringLiteral("ttsVoiceId")).toString().trimmed();
    else if (version >= 8)
        project.ttsVoiceId = json.value(QStringLiteral("cloneVoicePresetId")).toString().trimmed();
    project.cloneVoicePresetId = project.ttsVoiceId;
    project.durationControl = json.value(QStringLiteral("durationControl")).toObject().toVariantMap();
    if (project.durationControl.isEmpty()) {
        project.durationControl = QVariantMap{{QStringLiteral("enabled"), version >= 3},
                                              {QStringLiteral("unit"), QStringLiteral("phoneme-v1")},
                                              {QStringLiteral("lowerToleranceRatio"), 0.20},
                                              {QStringLiteral("upperToleranceRatio"), 0.20},
                                              {QStringLiteral("autoRewrite"), true}};
    } else if (version < 4) {
        // Schema 4 separates faithful translation from length adaptation. The old
        // opt-in controlled a different single-pass/outside-tolerance workflow.
        project.durationControl.insert(QStringLiteral("autoRewrite"), true);
    }
    if (version >= 6) {
        project.workflowNodeConfigurations =
            json.value(QStringLiteral("workflowNodeConfigurations")).toObject().toVariantMap();
    }
    if (version >= 7) {
        project.customRewriteConfiguration =
            json.value(QStringLiteral("customRewriteConfiguration")).toObject().toVariantMap();
    }
    if (version >= 9) {
        project.transcriptConfiguration =
            json.value(QStringLiteral("transcriptConfiguration")).toObject().toVariantMap();
    }
    if (version >= 10) {
        project.subtitleConfiguration =
            json.value(QStringLiteral("subtitleConfiguration")).toObject().toVariantMap();
    }
    if (version >= 11) {
        project.timingConfiguration =
            json.value(QStringLiteral("timingConfiguration")).toObject().toVariantMap();
    }
    project.speakers = json.value(QStringLiteral("speakers")).toArray().toVariantList();
    project.segments = json.value(QStringLiteral("segments")).toArray().toVariantList();
    return true;
}

bool DubbingProject::save(QString *error) const
{
    if (projectPath.isEmpty()) {
        setError(error, QStringLiteral("Dubbing project path is empty."));
        return false;
    }

    const QFileInfo info(projectPath);
    if (!QDir().mkpath(info.absolutePath())) {
        setError(error, QStringLiteral("Cannot create project directory: %1").arg(info.absolutePath()));
        return false;
    }

    QSaveFile file(projectPath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("Cannot write dubbing project: %1").arg(file.errorString()));
        return false;
    }
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        setError(error, QStringLiteral("Cannot commit dubbing project: %1").arg(file.errorString()));
        return false;
    }
    return true;
}

bool DubbingProject::load(const QString &path, DubbingProject &project, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot open dubbing project: %1").arg(file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("Invalid dubbing project JSON: %1").arg(parseError.errorString()));
        return false;
    }

    DubbingProject loaded;
    if (!fromJson(document.object(), loaded, error)) {
        return false;
    }
    loaded.projectPath = QFileInfo(path).absoluteFilePath();
    project = std::move(loaded);
    return true;
}

} // namespace LAStudio
