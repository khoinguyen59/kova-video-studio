#include "ColabVoiceDesignRunner.h"

#include "remote/ColabWorkerClient.h"

#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <limits>

namespace LAStudio {
namespace {

bool decodeWav(const QByteArray &wav, QByteArray *pcm16, QVector<float> *samples,
               int *sampleRate, QString *error)
{
    if (pcm16) pcm16->clear();
    if (samples) samples->clear();
    if (sampleRate) *sampleRate = 0;
    if (wav.size() < 44 || std::memcmp(wav.constData(), "RIFF", 4) != 0
        || std::memcmp(wav.constData() + 8, "WAVE", 4) != 0) {
        if (error) *error = QStringLiteral("Colab worker must return WAV audio");
        return false;
    }
    quint16 format = 0, channels = 0, bits = 0, blockAlign = 0;
    quint32 rate = 0, dataSize = 0;
    const char *data = nullptr;
    int offset = 12;
    while (offset + 8 <= wav.size()) {
        const char *chunk = wav.constData() + offset;
        const quint32 size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(chunk + 4));
        offset += 8;
        if (size > static_cast<quint32>(wav.size() - offset)) {
            if (error) *error = QStringLiteral("Colab worker returned truncated WAV audio");
            return false;
        }
        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
            format = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(chunk + 8));
            channels = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(chunk + 10));
            rate = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(chunk + 12));
            blockAlign = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(chunk + 20));
            bits = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(chunk + 22));
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data = chunk + 8;
            dataSize = size;
        }
        offset += static_cast<int>(size);
        if (size % 2) ++offset;
    }
    const int bytesPerSample = bits / 8;
    if (!data || !channels || !rate || !blockAlign || dataSize % blockAlign
        || blockAlign != channels * bytesPerSample
        || !((format == 1 && bits == 16) || (format == 3 && bits == 32))) {
        if (error) *error = QStringLiteral("Colab worker returned an unsupported WAV format");
        return false;
    }
    const quint32 frames = dataSize / blockAlign;
    if (!frames || frames > static_cast<quint32>(std::numeric_limits<int>::max())) {
        if (error) *error = QStringLiteral("Colab worker returned invalid WAV audio");
        return false;
    }
    QVector<float> mono(static_cast<int>(frames));
    QByteArray output(static_cast<int>(frames * sizeof(qint16)), Qt::Uninitialized);
    for (quint32 frame = 0; frame < frames; ++frame) {
        float mix = 0.0F;
        for (quint16 channel = 0; channel < channels; ++channel) {
            const char *source = data + frame * blockAlign + channel * bytesPerSample;
            if (format == 1) {
                qint16 value = 0;
                std::memcpy(&value, source, sizeof(value));
                mix += static_cast<float>(qFromLittleEndian<qint16>(value)) / 32768.0F;
            } else {
                quint32 raw = 0;
                std::memcpy(&raw, source, sizeof(raw));
                raw = qFromLittleEndian<quint32>(raw);
                float value = 0.0F;
                std::memcpy(&value, &raw, sizeof(value));
                mix += value;
            }
        }
        const float value = std::clamp(mix / static_cast<float>(channels), -1.0F, 1.0F);
        mono[static_cast<int>(frame)] = value;
        const qint16 pcm = static_cast<qint16>(value * 32767.0F);
        std::memcpy(output.data() + static_cast<int>(frame * sizeof(qint16)), &pcm, sizeof(pcm));
    }
    if (pcm16) *pcm16 = output;
    if (samples) *samples = mono;
    if (sampleRate) *sampleRate = static_cast<int>(rate);
    return true;
}

} // namespace

class ColabVoiceDesignRunner::Private final
{
public:
    ColabWorkerClient client;
};

ColabVoiceDesignRunner::ColabVoiceDesignRunner(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
}

ColabVoiceDesignRunner::~ColabVoiceDesignRunner() = default;

void ColabVoiceDesignRunner::generate(const ColabVoiceDesignRequest &request)
{
    QString error;
    if (!d->client.configure(request.workerUrl, request.bearerToken,
                             request.allowInsecureLocalhost, &error)) {
        emit failed(error);
        return;
    }
    QByteArray wav;
    if (!d->client.designVoice(request.text, request.model, request.voiceDescription, request.style,
                               request.language, request.temperature, request.seed,
                               request.cancellation.sharedFlag(), &wav, &error)) {
        emit failed(error);
        return;
    }
    if (request.cancellation.isCancelled()) {
        emit failed(QStringLiteral("VoiceDesign cancelled"));
        return;
    }
    QByteArray pcm16;
    QVector<float> samples;
    int sampleRate = 0;
    if (!decodeWav(wav, &pcm16, &samples, &sampleRate, &error)) {
        emit failed(error);
        return;
    }
    emit progress(100);
    emit finished(pcm16, samples, sampleRate);
}

void ColabVoiceDesignRunner::cancel()
{
    d->client.cancel();
}

} // namespace LAStudio
