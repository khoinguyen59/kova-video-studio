#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace LAStudio::DubbingColabModelRoutes {

inline QString normalizedNodeId(const QString &nodeId)
{
    return nodeId.trimmed().toLower();
}

inline QVariantList optionsForNode(const QString &nodeId)
{
    const QString id = normalizedNodeId(nodeId);
    const auto option = [](const QString &modelId, const QString &displayName,
                           const QString &notebook) {
        return QVariantMap{
            {QStringLiteral("modelId"), modelId},
            {QStringLiteral("displayName"), displayName},
            {QStringLiteral("notebook"), notebook},
            {QStringLiteral("selectable"), true}
        };
    };

    if (id == QStringLiteral("source-separate")) {
        return {
            option(QStringLiteral("sherpa-onnx-spleeter-2stems-fp16"),
                   QStringLiteral("Spleeter 2 Stems FP16"),
                   QStringLiteral("LA_STUDIO_SEPARATION_SPLEETER_2STEMS_GPU.ipynb")),
            option(QStringLiteral("sherpa-onnx-uvr-vocals-ft"),
                   QStringLiteral("UVR Vocals FT"),
                   QStringLiteral("LA_STUDIO_SEPARATION_UVR_VOCALS_GPU.ipynb"))
        };
    }
    if (id == QStringLiteral("transcribe")) {
        return {
            option(QStringLiteral("nemotron-3.5-asr-streaming-0.6b"),
                   QStringLiteral("Nemotron 3.5 ASR Streaming 0.6B"),
                   QStringLiteral("LA_STUDIO_STT_NEMOTRON_3_5_0_6B_GPU.ipynb")),
            option(QStringLiteral("whisper.cpp"), QStringLiteral("Whisper.cpp"),
                   QStringLiteral("LA_STUDIO_STT_WHISPER_GPU.ipynb")),
            option(QStringLiteral("qwen3-asr-0.6b"), QStringLiteral("Qwen3-ASR 0.6B"),
                   QStringLiteral("LA_STUDIO_STT_QWEN3_ASR_0_6B_GPU.ipynb")),
            option(QStringLiteral("qwen3-asr-1.7b"), QStringLiteral("Qwen3-ASR 1.7B"),
                   QStringLiteral("LA_STUDIO_STT_QWEN3_ASR_1_7B_GPU.ipynb"))
        };
    }
    if (id == QStringLiteral("translate")) {
        return {
            option(QStringLiteral("m2m100-418m"), QStringLiteral("M2M100 418M"),
                   QStringLiteral("LA_STUDIO_TRANSLATION_M2M100_418M_GPU.ipynb")),
            option(QStringLiteral("madlad400-3b-mt"), QStringLiteral("MADLAD-400 3B MT"),
                   QStringLiteral("LA_STUDIO_TRANSLATION_MADLAD400_3B_GPU.ipynb")),
            option(QStringLiteral("hy-mt2-1.8b"), QStringLiteral("Hy-MT2 1.8B"),
                   QStringLiteral("LA_STUDIO_TRANSLATION_HY_MT2_1_8B_GPU.ipynb"))
        };
    }
    if (id == QStringLiteral("synthesize")) {
        return {
            option(QStringLiteral("kokoro"), QStringLiteral("Kokoro"),
                   QStringLiteral("LA_STUDIO_TTS_KOKORO_GPU.ipynb")),
            option(QStringLiteral("kokoro-vietnamese"), QStringLiteral("Kokoro Vietnamese"),
                   QStringLiteral("LA_STUDIO_TTS_KOKORO_VIETNAMESE_GPU.ipynb")),
            option(QStringLiteral("omnivoice"), QStringLiteral("OmniVoice"),
                   QStringLiteral("LA_STUDIO_TTS_OMNIVOICE_GPU.ipynb")),
            option(QStringLiteral("qwen3-tts-1.7b-customvoice"),
                   QStringLiteral("Qwen3-TTS 1.7B CustomVoice"),
                   QStringLiteral("LA_STUDIO_TTS_QWEN3_CUSTOMVOICE_1_7B_GPU.ipynb")),
            option(QStringLiteral("vibevoice"), QStringLiteral("VibeVoice 0.5B"),
                   QStringLiteral("LA_STUDIO_TTS_VIBEVOICE_0_5B_GPU.ipynb")),
            option(QStringLiteral("vieneu-tts-v2-turbo"), QStringLiteral("VieNeu-TTS v2 Turbo"),
                   QStringLiteral("LA_STUDIO_TTS_VIENEU_V2_TURBO_GPU.ipynb")),
            option(QStringLiteral("vieneu-tts-v3-turbo"), QStringLiteral("VieNeu-TTS v3 Turbo"),
                   QStringLiteral("LA_STUDIO_TTS_VIENEU_V3_TURBO_GPU.ipynb")),
            option(QStringLiteral("voxcpm2"), QStringLiteral("VoxCPM2"),
                   QStringLiteral("LA_STUDIO_TTS_VOXCPM2_GPU.ipynb"))
        };
    }
    if (id == QStringLiteral("voice-clone")) {
        return {
            option(QStringLiteral("omnivoice"), QStringLiteral("OmniVoice"),
                   QStringLiteral("LA_STUDIO_VOICE_CLONE_OMNIVOICE_GPU.ipynb")),
            option(QStringLiteral("qwen3-tts-0.6b-base"), QStringLiteral("Qwen3-TTS Base 0.6B"),
                   QStringLiteral("LA_STUDIO_VOICE_CLONE_QWEN3_BASE_0_6B_GPU.ipynb")),
            option(QStringLiteral("qwen3-tts-1.7b-base"), QStringLiteral("Qwen3-TTS Base 1.7B"),
                   QStringLiteral("LA_STUDIO_VOICE_CLONE_QWEN3_BASE_1_7B_GPU.ipynb")),
            option(QStringLiteral("vieneu-tts-v2-turbo"), QStringLiteral("VieNeu-TTS v2 Turbo"),
                   QStringLiteral("LA_STUDIO_VOICE_CLONE_VIENEU_V2_TURBO_GPU.ipynb")),
            option(QStringLiteral("vieneu-tts-v3-turbo"), QStringLiteral("VieNeu-TTS v3 Turbo"),
                   QStringLiteral("LA_STUDIO_VOICE_CLONE_VIENEU_V3_TURBO_GPU.ipynb")),
            option(QStringLiteral("voxcpm2"), QStringLiteral("VoxCPM2"),
                   QStringLiteral("LA_STUDIO_VOICE_CLONE_VOXCPM2_GPU.ipynb"))
        };
    }
    if (id == QStringLiteral("alignment")) {
        return {
            option(QStringLiteral("wav2vec2-aligner-zh"), QStringLiteral("Wav2Vec2 Chinese Aligner"),
                   QStringLiteral("LA_STUDIO_ALIGNMENT_WAV2VEC2_ZH_GPU.ipynb")),
            option(QStringLiteral("canary-ctc-aligner"), QStringLiteral("Canary CTC Aligner"),
                   QStringLiteral("LA_STUDIO_ALIGNMENT_CANARY_CTC_GPU.ipynb")),
            option(QStringLiteral("mms-forced-aligner-onnx"), QStringLiteral("MMS Forced Aligner"),
                   QStringLiteral("LA_STUDIO_ALIGNMENT_MMS_ONNX_GPU.ipynb")),
            option(QStringLiteral("qwen3-forced-aligner-0.6b"), QStringLiteral("Qwen3 Forced Aligner 0.6B"),
                   QStringLiteral("LA_STUDIO_ALIGNMENT_QWEN3_0_6B_GPU.ipynb"))
        };
    }
    return {};
}

inline QString notebookForModel(const QString &nodeId, const QString &modelId)
{
    const QString normalized = modelId.trimmed().toLower();
    for (const QVariant &entry : optionsForNode(nodeId)) {
        const QVariantMap option = entry.toMap();
        if (option.value(QStringLiteral("modelId")).toString() == normalized)
            return option.value(QStringLiteral("notebook")).toString();
    }
    return {};
}

inline QString defaultModelForNode(const QString &nodeId)
{
    const QString id = normalizedNodeId(nodeId);
    if (id == QStringLiteral("source-separate"))
        return QStringLiteral("sherpa-onnx-spleeter-2stems-fp16");
    if (id == QStringLiteral("transcribe"))
        return QStringLiteral("whisper.cpp");
    if (id == QStringLiteral("translate"))
        return QStringLiteral("m2m100-418m");
    if (id == QStringLiteral("synthesize"))
        return QStringLiteral("kokoro");
    if (id == QStringLiteral("voice-clone"))
        return QStringLiteral("omnivoice");
    if (id == QStringLiteral("alignment"))
        return QStringLiteral("mms-forced-aligner-onnx");
    return {};
}

inline bool supports(const QString &nodeId, const QString &modelId)
{
    return !notebookForModel(nodeId, modelId).isEmpty();
}

inline QString defaultVoiceForTtsModel(const QString &modelId)
{
    const QString model = modelId.trimmed().toLower();
    if (model == QStringLiteral("kokoro")) return QStringLiteral("af_heart");
    if (model == QStringLiteral("kokoro-vietnamese"))
        return QString::fromUtf8("diem_trinh");
    if (model == QStringLiteral("qwen3-tts-1.7b-customvoice"))
        return QStringLiteral("Aiden");
    if (model == QStringLiteral("vibevoice"))
        return QStringLiteral("carter");
    if (model.startsWith(QStringLiteral("vieneu-tts-")))
        return QString::fromUtf8("Phạm Tuyên");
    return QStringLiteral("auto");
}

inline QString defaultLanguageForTtsModel(const QString &modelId)
{
    const QString model = modelId.trimmed().toLower();
    if (model == QStringLiteral("kokoro")
        || model == QStringLiteral("qwen3-tts-1.7b-customvoice")
        || model == QStringLiteral("vibevoice"))
        return QStringLiteral("en");
    if (model == QStringLiteral("kokoro-vietnamese")
        || model.startsWith(QStringLiteral("vieneu-tts-")))
        return QStringLiteral("vi");
    return QStringLiteral("auto");
}

} // namespace LAStudio::DubbingColabModelRoutes
