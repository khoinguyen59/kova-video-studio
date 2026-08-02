#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace LAStudio {

// These keys are internal to the Dubbing -> TTS execution boundary. They are
// deliberately not model-schema settings and must never be exposed as editor
// controls or copied into a project file.
inline constexpr auto kTtsSavedVoiceId = "_lastudioSavedVoiceId";
inline constexpr auto kTtsSavedVoiceReferencePath = "_lastudioSavedVoiceReferencePath";
inline constexpr auto kTtsSavedVoiceReferenceText = "_lastudioSavedVoiceReferenceText";

inline bool isTtsSavedVoiceProfileSetting(const QString &key)
{
    return key == QLatin1String(kTtsSavedVoiceId)
        || key == QLatin1String(kTtsSavedVoiceReferencePath)
        || key == QLatin1String(kTtsSavedVoiceReferenceText);
}

// A local saved voice is safe for Dubbing only when the active TTS execution
// backend owns a persistent session-level profile primitive. Qwen3-TTS maps
// this to CrispASR's crispasr_session_set_voice(). Other cloning backends may
// accept a reference per request, but that is a new clone operation and is
// intentionally not treated as a reusable Dubbing TTS voice.
inline bool localTtsSupportsSavedVoiceProfile(const QVariantMap &familyConfig)
{
    const QString identity = QStringList{
        familyConfig.value(QStringLiteral("backend")).toString(),
        familyConfig.value(QStringLiteral("id")).toString(),
        familyConfig.value(QStringLiteral("familyId")).toString(),
        familyConfig.value(QStringLiteral("modelId")).toString(),
        familyConfig.value(QStringLiteral("model")).toString()
    }.join(QLatin1Char('|'));
    return identity.contains(QStringLiteral("qwen3-tts"), Qt::CaseInsensitive);
}

} // namespace LAStudio
