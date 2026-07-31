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
    static constexpr int CurrentSchemaVersion = 9;

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
    QString sourceLanguage = QStringLiteral("en");
    QString targetLanguage = QStringLiteral("vi");
    QString dubbingQuality = QStringLiteral("adaptive");
    // The selected clone-voice preset is project configuration.  Only its
    // durable library ID is persisted; reference media remains owned by the
    // local preset library and temporary Colab profile IDs are never saved.
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
