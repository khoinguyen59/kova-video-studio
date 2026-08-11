#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace LAStudio {

// Versioned, dependency-free representation of a dubbing edit session.
// Generated audio is deliberately kept out of the JSON; it belongs in the
// project's cache directory and can be regenerated from the segment fields.
class DubbingProject
{
public:
    static constexpr int CurrentSchemaVersion = 13;

    QString projectPath;
    QString sourceMediaPath;
    QString sourceHash;
    QString masterAudioPath;
    QString analysisAudioPath;
    QString backgroundAudioPath;
    qint64 sourceDurationMs = 0;
    int sourceSampleRate = 0;
    int sourceChannels = 0;
    bool sourceIsVideo = false;
    QString sourceLanguage = QStringLiteral("zh");
    QString targetLanguage = QStringLiteral("vi");
    QString dubbingQuality = QStringLiteral("adaptive");
    // This is a durable operator choice, separate from the transient workflow
    // runner state.  It is deliberately retained when a project is reopened
    // so the Dubbing entry gate can ask the operator to confirm or change it
    // without touching transcripts, subtitles, artifacts or node selections.
    QString workflowEntryMode;
    // The selected TTS voice is project configuration.  This is either a
    // built-in voice ID or the durable ID of a saved Voice Cloning-library
    // preset; transient worker profile IDs are deliberately never persisted.
    QString ttsVoiceId;
    // Source compatibility for callers compiled against schema <= 11.  New
    // Dubbing code must use ttsVoiceId; serialization never writes this key.
    QString cloneVoicePresetId;
    QVariantMap durationControl = QVariantMap{{QStringLiteral("enabled"), true},
                                               {QStringLiteral("unit"), QStringLiteral("phoneme-v1")},
                                               {QStringLiteral("lowerToleranceRatio"), 0.20},
                                               {QStringLiteral("upperToleranceRatio"), 0.20},
                                               {QStringLiteral("autoRewrite"), true}};
    QVariantMap workflowNodeConfigurations;
    // Kept independently from the custom-workflow model selection so a
    // transcript source/ROI choice survives reopening an adaptive project.
    // It intentionally contains no remote URL, token, or staged file path.
    QVariantMap transcriptConfiguration;
    // Style and source kind are durable editing decisions. File paths and
    // imported file contents are deliberately not persisted: reviewed cues are
    // stored in segments, while an external source remains user-owned.
    QVariantMap subtitleConfiguration;
    // Resolution policy is persisted, but the original media is never
    // rewritten. Individual segment ripple offsets remain reviewable.
    QVariantMap timingConfiguration;
    QVariantMap customRewriteConfiguration;
    QVariantList speakers;
    QVariantList segments;

    bool save(QString *error = nullptr) const;
    static bool load(const QString &path, DubbingProject &project, QString *error = nullptr);

    static bool mergeSegmentPatches(const QVariantList &segments,
                                    const QVariantList &patches,
                                    QVariantList &merged,
                                    QString *error = nullptr);

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &json, DubbingProject &project, QString *error = nullptr);
};

} // namespace LAStudio
