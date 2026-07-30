#include "GatewayTtsRunner.h"

#include "remote/GatewayClient.h"

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
        if (error) *error = QStringLiteral("API Gateway must return WAV audio");
        return false;
    }

    quint16 format = 0;
    quint16 channels = 0;
    quint32 rate = 0;
    quint16 bits = 0;
    quint16 blockAlign = 0;
    const char *data = nullptr;
    quint32 dataSize = 0;
    int offset = 12;
    while (offset + 8 <= wav.size()) {
        const char *chunk = wav.constData() + offset;
        const quint32 chunkSize = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(chunk + 4));
        offset += 8;
        if (chunkSize > static_cast<quint32>(wav.size() - offset)) {
            if (error) *error = QStringLiteral("API Gateway returned truncated WAV audio");
            return false;
        }
        if (std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
            format = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(chunk + 8));
            channels = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(chunk + 10));
            rate = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(chunk + 12));
            blockAlign = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(chunk + 20));
            bits = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(chunk + 22));
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data = chunk + 8;
            dataSize = chunkSize;
        }
        offset += static_cast<int>(chunkSize);
        if (chunkSize % 2 != 0) ++offset;
    }

    const int bytesPerSample = bits / 8;
    if (!data || channels == 0 || rate == 0 || blockAlign == 0 || bytesPerSample == 0
        || blockAlign != channels * bytesPerSample || dataSize % blockAlign != 0
        || !((format == 1 && bits == 16) || (format == 3 && bits == 32))) {
        if (error) *error = QStringLiteral("API Gateway returned an unsupported WAV format");
        return false;
    }
    const quint32 frames = dataSize / blockAlign;
    if (frames == 0 || frames > static_cast<quint32>(std::numeric_limits<int>::max())) {
        if (error) *error = QStringLiteral("API Gateway returned invalid WAV audio");
        return false;
    }

    QVector<float> mono(static_cast<int>(frames));
    QByteArray out(static_cast<int>(frames * sizeof(qint16)), Qt::Uninitialized);
    for (quint32 frame = 0; frame < frames; ++frame) {
        float mixed = 0.0F;
        for (quint16 channel = 0; channel < channels; ++channel) {
            const char *source = data + frame * blockAlign + channel * bytesPerSample;
            if (format == 1) {
                qint16 value = 0;
                std::memcpy(&value, source, sizeof(value));
                mixed += static_cast<float>(qFromLittleEndian<qint16>(value)) / 32768.0F;
            } else {
                quint32 raw = 0;
                std::memcpy(&raw, source, sizeof(raw));
                raw = qFromLittleEndian<quint32>(raw);
                float value = 0.0F;
                std::memcpy(&value, &raw, sizeof(value));
                mixed += value;
            }
        }
        mixed = std::clamp(mixed / static_cast<float>(channels), -1.0F, 1.0F);
        mono[static_cast<int>(frame)] = mixed;
        const qint16 value = static_cast<qint16>(mixed * 32767.0F);
        std::memcpy(out.data() + static_cast<int>(frame * sizeof(qint16)), &value, sizeof(value));
    }
    if (pcm16) *pcm16 = out;
    if (samples) *samples = mono;
    if (sampleRate) *sampleRate = static_cast<int>(rate);
    return true;
}

} // namespace

class GatewayTtsRunner::Private final
{
public:
    GatewayClient client;
};

GatewayTtsRunner::GatewayTtsRunner(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
}

GatewayTtsRunner::~GatewayTtsRunner() = default;

void GatewayTtsRunner::synthesize(const GatewayTtsRequest &request)
{
    QString error;
    if (!d->client.configure(request.gatewayUrl, request.apiKey, request.model,
                             request.allowInsecureLocalhost, &error)) {
        emit failed(error);
        return;
    }
    QByteArray wav;
    if (!d->client.synthesizeSpeech(request.text, request.voice, request.speed,
                                    request.cancellation.sharedFlag(), &wav, &error)) {
        emit failed(request.cancellation.isCancelled() ? QStringLiteral("Speech synthesis cancelled") : error);
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

void GatewayTtsRunner::cancel()
{
    d->client.cancel();
}

} // namespace LAStudio
