#include "WavIO.h"
#include "core/Logger.h"

#include <QFile>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <limits>

namespace LAStudio {

#pragma pack(push, 1)
struct RiffHeader {
    char     riff[4];
    uint32_t chunkSize;
    char     wave[4];
};
struct FmtChunk {
    char     fmt[4];
    uint32_t subchunkSize;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};
struct DataChunkHeader {
    char     data[4];
    uint32_t dataSize;
};
#pragma pack(pop)

WavIO::WavData WavIO::loadAsFloat(const QString &path)
{
    WavData result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        Logger::error("WavIO", "Cannot open: " + path);
        return result;
    }

    QByteArray raw = file.readAll();
    if (raw.size() < static_cast<qsizetype>(sizeof(RiffHeader))) {
        Logger::error("WavIO", "File too small: " + path);
        return result;
    }

    RiffHeader riff{};
    std::memcpy(&riff, raw.constData(), sizeof(riff));
    if (std::memcmp(riff.riff, "RIFF", 4) != 0 || std::memcmp(riff.wave, "WAVE", 4) != 0) {
        Logger::error("WavIO", "Not a WAV file: " + path);
        return result;
    }

    const char *ptr = raw.constData() + sizeof(RiffHeader);
    const char *end = raw.constData() + raw.size();
    FmtChunk fmt{};
    bool hasFmt = false;
    const char *dataPtr = nullptr;
    uint32_t dataSize = 0;

    while (ptr + 8 <= end) {
        const char *chunkId = ptr;
        uint32_t chunkSize = 0;
        std::memcpy(&chunkSize, ptr + 4, 4);
        ptr += 8;
        const qsizetype available = end - ptr;
        if (chunkSize > static_cast<uint32_t>(available)) {
            Logger::error("WavIO", "Truncated chunk: " + path);
            return result;
        }

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            if (chunkSize < 16) {
                Logger::error("WavIO", "Invalid fmt chunk: " + path);
                return result;
            }
            std::memcpy(&fmt, chunkId, sizeof(FmtChunk));
            hasFmt = true;
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            dataPtr = ptr;
            dataSize = chunkSize;
        }

        ptr += chunkSize;
        if (chunkSize % 2 != 0) {
            if (ptr == end) {
                Logger::error("WavIO", "Missing chunk padding: " + path);
                return result;
            }
            ++ptr;
        }
    }

    if (!hasFmt || !dataPtr) {
        Logger::error("WavIO", "Missing fmt or data chunk: " + path);
        return result;
    }

    const int bps = fmt.bitsPerSample;
    const int bytesPerSample = bps / 8;
    if (fmt.numChannels == 0 || fmt.sampleRate == 0 || fmt.blockAlign == 0 ||
        bps == 0 || bps % 8 != 0 || bytesPerSample == 0 ||
        dataSize % fmt.blockAlign != 0 ||
        (bps == 16 && fmt.audioFormat != 1) ||
        (bps == 32 && fmt.audioFormat != 3) ||
        (bps != 16 && bps != 32)) {
        Logger::error("WavIO", QString("Invalid or unsupported WAV format bps %1 format %2")
            .arg(bps).arg(fmt.audioFormat));
        return result;
    }
    if (fmt.blockAlign != fmt.numChannels * bytesPerSample) {
        Logger::error("WavIO", "Invalid block alignment: " + path);
        return result;
    }

    const quint64 sampleCount = dataSize / static_cast<quint32>(bytesPerSample);
    if (sampleCount > static_cast<quint64>(std::numeric_limits<int>::max())) {
        Logger::error("WavIO", "WAV sample count is too large: " + path);
        return result;
    }
    result.sampleRate = static_cast<int>(fmt.sampleRate);
    result.channels = fmt.numChannels;
    result.samples.resize(static_cast<int>(sampleCount));

    if (bps == 16) {
        for (quint64 i = 0; i < sampleCount; ++i) {
            int16_t sample = 0;
            std::memcpy(&sample, dataPtr + i * sizeof(sample), sizeof(sample));
            result.samples[static_cast<int>(i)] = static_cast<float>(sample) / 32768.0f;
        }
    } else { // IEEE 32-bit float
        for (quint64 i = 0; i < sampleCount; ++i) {
            float sample = 0.0f;
            std::memcpy(&sample, dataPtr + i * sizeof(sample), sizeof(sample));
            result.samples[static_cast<int>(i)] = sample;
        }
    }

    return result;
}

WavIO::WavData WavIO::loadAsFloatMono16k(const QString &path)
{
    WavData wav = loadAsFloat(path);
    if (wav.samples.isEmpty()) return wav;

    // Stereo → mono
    if (wav.channels == 2) {
        int monoLen = wav.samples.size() / 2;
        QVector<float> mono(monoLen);
        for (int i = 0; i < monoLen; ++i)
            mono[i] = (wav.samples[i * 2] + wav.samples[i * 2 + 1]) * 0.5f;
        wav.samples = mono;
        wav.channels = 1;
    }

    // Resample to 16kHz via linear interpolation
    if (wav.sampleRate != 16000 && wav.sampleRate > 0) {
        double ratio = 16000.0 / wav.sampleRate;
        int newLen = static_cast<int>(wav.samples.size() * ratio);
        QVector<float> resampled(newLen);
        for (int i = 0; i < newLen; ++i) {
            double srcIdx = i / ratio;
            int idx0 = static_cast<int>(srcIdx);
            int idx1 = std::min(idx0 + 1, static_cast<int>(wav.samples.size() - 1));
            double frac = srcIdx - idx0;
            resampled[i] = static_cast<float>(wav.samples[idx0] * (1.0 - frac) +
                                               wav.samples[idx1] * frac);
        }
        wav.samples = resampled;
        wav.sampleRate = 16000;
    }

    return wav;
}

WavIO::WavData WavIO::loadAsFloatMono24k(const QString &path)
{
    WavData wav = loadAsFloat(path);
    if (wav.samples.isEmpty()) return wav;

    // Stereo → mono
    if (wav.channels == 2) {
        int monoLen = wav.samples.size() / 2;
        QVector<float> mono(monoLen);
        for (int i = 0; i < monoLen; ++i)
            mono[i] = (wav.samples[i * 2] + wav.samples[i * 2 + 1]) * 0.5f;
        wav.samples = mono;
        wav.channels = 1;
    }

    // Resample to 24kHz via linear interpolation
    if (wav.sampleRate != 24000 && wav.sampleRate > 0) {
        double ratio = 24000.0 / wav.sampleRate;
        int newLen = static_cast<int>(wav.samples.size() * ratio);
        QVector<float> resampled(newLen);
        for (int i = 0; i < newLen; ++i) {
            double srcIdx = i / ratio;
            int idx0 = static_cast<int>(srcIdx);
            int idx1 = std::min(idx0 + 1, static_cast<int>(wav.samples.size() - 1));
            double frac = srcIdx - idx0;
            resampled[i] = static_cast<float>(wav.samples[idx0] * (1.0 - frac) +
                                               wav.samples[idx1] * frac);
        }
        wav.samples = resampled;
        wav.sampleRate = 24000;
    }

    return wav;
}

bool WavIO::saveFloat(const QString &path, const float *samples,
                       int numSamples, int sampleRate, int channels)
{
    QVector<int16_t> pcm(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        float s = std::clamp(samples[i], -1.0f, 1.0f);
        pcm[i] = static_cast<int16_t>(s * 32767.0f);
    }
    return savePcm16(path, pcm.constData(), numSamples, sampleRate, channels);
}

bool WavIO::savePcm16(const QString &path, const int16_t *samples,
                       int numSamples, int sampleRate, int channels)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;

    uint32_t dataSize = static_cast<uint32_t>(numSamples * 2);

    RiffHeader riff;
    std::memcpy(riff.riff, "RIFF", 4);
    riff.chunkSize = 36 + dataSize;
    std::memcpy(riff.wave, "WAVE", 4);

    FmtChunk fmt;
    std::memcpy(fmt.fmt, "fmt ", 4);
    fmt.subchunkSize  = 16;
    fmt.audioFormat    = 1; // PCM
    fmt.numChannels    = static_cast<uint16_t>(channels);
    fmt.sampleRate     = static_cast<uint32_t>(sampleRate);
    fmt.bitsPerSample  = 16;
    fmt.blockAlign     = static_cast<uint16_t>(channels * 2);
    fmt.byteRate       = fmt.sampleRate * fmt.blockAlign;

    DataChunkHeader data;
    std::memcpy(data.data, "data", 4);
    data.dataSize = dataSize;

    file.write(reinterpret_cast<const char *>(&riff), sizeof(riff));
    file.write(reinterpret_cast<const char *>(&fmt), sizeof(fmt));
    file.write(reinterpret_cast<const char *>(&data), sizeof(data));
    file.write(reinterpret_cast<const char *>(samples), dataSize);

    return true;
}

} // namespace LAStudio

