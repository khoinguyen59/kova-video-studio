#include "ColabVoiceCloneRunner.h"

#include "audio/AudioFileDecoder.h"
#include "remote/ColabWorkerClient.h"

#include <QFileInfo>
#include <QtEndian>
#include <QThread>

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
        } else if (std::memcmp(chunk, "data", 4) == 0) { data = chunk + 8; dataSize = size; }
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
                qint16 value = 0; std::memcpy(&value, source, sizeof(value));
                mix += static_cast<float>(qFromLittleEndian<qint16>(value)) / 32768.0F;
            } else {
                quint32 raw = 0; std::memcpy(&raw, source, sizeof(raw)); raw = qFromLittleEndian<quint32>(raw);
                float value = 0.0F; std::memcpy(&value, &raw, sizeof(value)); mix += value;
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

QString workerJobError(const QJsonObject &job)
{
    const QJsonObject error = job.value(QStringLiteral("error")).toObject();
    const QString message = error.value(QStringLiteral("message")).toString().trimmed();
    return message.isEmpty() ? QStringLiteral("Colab voice job failed") : message;
}

} // namespace

class ColabVoiceCloneRunner::Private final
{
public:
    ColabWorkerClient client;
};

ColabVoiceCloneRunner::ColabVoiceCloneRunner(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
}

ColabVoiceCloneRunner::~ColabVoiceCloneRunner() = default;

void ColabVoiceCloneRunner::clone(const ColabVoiceCloneRequest &request)
{
    QString error;
    if (request.model.trimmed().isEmpty()) {
        emit failed(QStringLiteral("Select an exact voice-cloning model before connecting Colab"));
        return;
    }
    if (!d->client.configure(request.workerUrl, request.bearerToken,
                             request.allowInsecureLocalhost, &error)) {
        emit failed(error);
        return;
    }

    const auto waitForJob = [this, &request, &error](const QString &jobId,
                                                       const QString &jobKind,
                                                       QJsonObject *completed) {
        for (int attempt = 0; attempt < 7200; ++attempt) {
            if (request.cancellation.isCancelled()) {
                d->client.cancelVoiceJob(jobId, nullptr);
                error = QStringLiteral("Voice cloning cancelled");
                return false;
            }
            QJsonObject job;
            if (!d->client.voiceJobStatus(jobId, &job, &error)) return false;
            const QString status = job.value(QStringLiteral("status")).toString().trimmed().toLower();
            const int percent = qBound(0, job.value(QStringLiteral("percent")).toInt(), 100);
            const QString stage = job.value(QStringLiteral("stage")).toString().trimmed();
            // This is the exact worker job percentage, not a fabricated
            // weighted percentage across profile and generation phases.
            emit progress(percent, stage.isEmpty() ? jobKind : jobKind + QStringLiteral(": ") + stage);
            if (status == QStringLiteral("succeeded")) { if (completed) *completed = job; return true; }
            if (status == QStringLiteral("failed") || status == QStringLiteral("cancelled")) {
                error = workerJobError(job);
                return false;
            }
            QThread::msleep(250);
        }
        d->client.cancelVoiceJob(jobId, nullptr);
        error = QStringLiteral("Colab voice job timed out");
        return false;
    };

    QString profileId = request.existingProfileId.trimmed();
    if (profileId.isEmpty()) {
        if (!request.consentConfirmed) {
            emit failed(QStringLiteral("Confirm permission before creating a voice profile"));
            return;
        }
        const QFileInfo referenceInfo(request.referencePath);
        const QString suffix = referenceInfo.suffix().toLower();
        if (!referenceInfo.exists() || referenceInfo.size() <= 0
            || referenceInfo.size() > 256LL * 1024LL * 1024LL
            || (suffix != QStringLiteral("wav") && suffix != QStringLiteral("mp3")
                && suffix != QStringLiteral("flac"))) {
            emit failed(QStringLiteral("Reference audio must be a WAV, MP3, or FLAC file smaller than 256 MB"));
            return;
        }
        const WavIO::WavData referenceAudio = AudioFileDecoder::decodeMono(
            request.referencePath, 24000);
        const double durationSeconds = referenceAudio.sampleRate > 0
            ? static_cast<double>(referenceAudio.samples.size()) / referenceAudio.sampleRate : 0.0;
        if (referenceAudio.samples.isEmpty() || durationSeconds < 3.0 || durationSeconds > 30.0) {
            emit failed(QStringLiteral("Reference audio must be a decodable 3–30 second clip"));
            return;
        }
        emit progress(0, QStringLiteral("upload_reference"));
        QJsonObject profileJob;
        if (!d->client.createVoiceProfileJob(request.model, request.referencePath, request.referenceName,
                                             request.referenceText, request.language, true,
                                             &profileJob, &error)) { emit failed(error); return; }
        const QString jobId = profileJob.value(QStringLiteral("id")).toString();
        if (jobId.isEmpty() || !waitForJob(jobId, QStringLiteral("profile"), &profileJob)) {
            emit failed(error);
            return;
        }
        profileId = profileJob.value(QStringLiteral("result")).toObject().value(QStringLiteral("id")).toString();
        if (profileId.isEmpty()) profileId = profileJob.value(QStringLiteral("result")).toObject()
            .value(QStringLiteral("profile")).toObject().value(QStringLiteral("id")).toString();
        if (profileId.isEmpty()) { emit failed(QStringLiteral("Colab worker did not return a voice profile ID")); return; }
        emit profileReady(profileId);
    }

    emit progress(0, QStringLiteral("queue_generation"));
    QJsonObject generationJob;
    if (!d->client.createVoiceGenerationJob(request.model, profileId, request.text, request.language,
                                            request.speed, request.steps, &generationJob, &error)) {
        emit failed(error); return;
    }
    const QString generationId = generationJob.value(QStringLiteral("id")).toString();
    if (generationId.isEmpty()
        || !waitForJob(generationId, QStringLiteral("generation"), &generationJob)) {
        emit failed(error);
        return;
    }
    QByteArray wav;
    if (!d->client.downloadVoiceJobAudio(generationId, &wav, &error)) { emit failed(error); return; }
    QByteArray pcm16;
    QVector<float> samples;
    int sampleRate = 0;
    if (!decodeWav(wav, &pcm16, &samples, &sampleRate, &error)) { emit failed(error); return; }
    emit progress(100, QStringLiteral("complete"));
    emit finished(pcm16, samples, sampleRate);
}

void ColabVoiceCloneRunner::cancel()
{
    d->client.cancel();
}

void ColabVoiceCloneRunner::deleteProfile(const ColabVoiceCloneRequest &request)
{
    QString error;
    if (!d->client.configure(request.workerUrl, request.bearerToken,
                             request.allowInsecureLocalhost, &error)) {
        emit failed(error);
        return;
    }
    if (!d->client.deleteVoiceProfile(request.existingProfileId, &error)) {
        emit failed(error);
        return;
    }
    emit profileDeleted();
}

} // namespace LAStudio
