#include "GatewaySttRunner.h"

#include "remote/GatewayClient.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cstring>

namespace LAStudio {

namespace {

QByteArray makeMono16kWav(const QVector<float> &samples)
{
    const quint32 dataSize = static_cast<quint32>(samples.size() * static_cast<int>(sizeof(qint16)));
    QByteArray wav(44 + static_cast<int>(dataSize), Qt::Uninitialized);
    const auto writeBytes = [&wav](int offset, const char *data, int size) { std::memcpy(wav.data() + offset, data, size); };
    const auto write16 = [&wav](int offset, quint16 value) { std::memcpy(wav.data() + offset, &value, sizeof(value)); };
    const auto write32 = [&wav](int offset, quint32 value) { std::memcpy(wav.data() + offset, &value, sizeof(value)); };
    writeBytes(0, "RIFF", 4); write32(4, 36 + dataSize); writeBytes(8, "WAVE", 4);
    writeBytes(12, "fmt ", 4); write32(16, 16); write16(20, 1); write16(22, 1);
    write32(24, 16000); write32(28, 32000); write16(32, 2); write16(34, 16);
    writeBytes(36, "data", 4); write32(40, dataSize);
    for (int index = 0; index < samples.size(); ++index) {
        const qint16 pcm = static_cast<qint16>(std::clamp(samples.at(index), -1.0F, 1.0F) * 32767.0F);
        std::memcpy(wav.data() + 44 + index * static_cast<int>(sizeof(qint16)), &pcm, sizeof(pcm));
    }
    return wav;
}

} // namespace

class GatewaySttRunner::Private final
{
public:
    GatewayClient client;
};

GatewaySttRunner::GatewaySttRunner(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
}

GatewaySttRunner::~GatewaySttRunner() = default;

void GatewaySttRunner::transcribe(const GatewaySttRequest &request)
{
    QString error;
    if (!d->client.configure(request.gatewayUrl, request.apiKey, request.model,
                             request.allowInsecureLocalhost, &error)) {
        emit failed(error);
        return;
    }
    QJsonObject response;
    if (!d->client.transcribeWav(makeMono16kWav(request.samples), request.language,
                                 request.cancellation.sharedFlag(), &response, &error)) {
        emit failed(request.cancellation.isCancelled() ? QStringLiteral("Transcription cancelled")
                                                       : error);
        return;
    }
    QVariantList segments;
    for (const QJsonValue &value : response.value(QStringLiteral("segments")).toArray()) {
        const QJsonObject segment = value.toObject();
        segments.append(QVariantMap{{QStringLiteral("id"), segment.value(QStringLiteral("id")).toVariant()},
                                    {QStringLiteral("start"), segment.value(QStringLiteral("start")).toDouble()},
                                    {QStringLiteral("end"), segment.value(QStringLiteral("end")).toDouble()},
                                    {QStringLiteral("text"), segment.value(QStringLiteral("text")).toString().trimmed()}});
    }
    const QString text = response.value(QStringLiteral("text")).toString().trimmed();
    if (text.isEmpty()) {
        emit failed(QStringLiteral("API Gateway returned an empty transcript"));
        return;
    }
    emit progress(100);
    emit finished(text, segments);
}

void GatewaySttRunner::cancel()
{
    d->client.cancel();
}

} // namespace LAStudio
