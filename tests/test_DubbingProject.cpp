#include "test_DubbingProject.h"

#include "dubbing/DubbingProject.h"
#include "dubbing/DubbingSubtitleService.h"
#include "dubbing/DubbingTimingService.h"
#include "dubbing/CapCutDraftExporter.h"
#include "dubbing/DubbingTranscriptFusionService.h"
#include "controllers/dubbing/DubbingController.h"
#include "controllers/dubbing/DubbingColabModelRoutes.h"
#include "controllers/dubbing/DubbingJobRunner.h"
#include "controllers/dubbing/DubbingTranscriptionJob.h"
#include "controllers/dubbing/DubbingSynthesisJob.h"
#include "controllers/dubbing/DubbingTranslationJob.h"
#include "controllers/dubbing/DubbingTranslationFixService.h"
#include "controllers/subtitles/SubtitleOcrController.h"
#include "controllers/shared/VoiceClonePresetService.h"
#include "dubbing/AlignmentRefinementService.h"
#include "dubbing/DubbingSegmentNormalizer.h"
#include "dubbing/DubbingDuration.h"
#include "dubbing/DubbingVoiceReferenceSelector.h"
#include "dubbing/AudioTimelineMixer.h"
#include "dubbing/media/AtomicMediaCommit.h"
#include "controllers/app/AppController.h"
#include "controllers/app/WorkflowActivityManager.h"
#include "audio/WavIO.h"
#include "core/Settings.h"
#include "core/PathUtils.h"
#include "controllers/stt/SttSessionController.h"
#include "remote/ColabSession.h"
#include "stt/SttEngine.h"
#include "tts/TtsEngine.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

#include <algorithm>
#include <cstring>

namespace LAStudio {

namespace {

class ScopedEnvironmentValue final
{
public:
    ScopedEnvironmentValue(const char *name, const QByteArray &value)
        : m_name(name)
        , m_wasSet(qEnvironmentVariableIsSet(name))
        , m_previous(qgetenv(name))
    {
        qputenv(name, value);
    }

    ~ScopedEnvironmentValue()
    {
        if (m_wasSet)
            qputenv(m_name.constData(), m_previous);
        else
            qunsetenv(m_name.constData());
    }

private:
    QByteArray m_name;
    bool m_wasSet = false;
    QByteArray m_previous;
};

bool writeFixtureFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Text)
        && file.write(contents) == contents.size();
}

QByteArray batchSubtitleOcrFfmpegScript()
{
    return QByteArrayLiteral(
        "@echo off\r\n"
        "set \"last=\"\r\n"
        ":next\r\n"
        "if \"%~1\"==\"\" goto done\r\n"
        "set \"last=%~1\"\r\n"
        "shift\r\n"
        "goto next\r\n"
        ":done\r\n"
        "set \"LASTUDIO_TEST_FRAME=%last%\"\r\n"
        "for %%A in (\"%last%\") do set \"LASTUDIO_TEST_FRAME_DIR=%%~dpA\"\r\n"
        "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"$bytes=[Convert]::FromBase64String('iVBORw0KGgoAAAANSUhEUgAAAUAAAAASCAIAAAClw5C1AAAACXBIWXMAAAABAAAAAQBPJcTWAAAAV0lEQVR4nO3TsQnAMBDAwDx4/5Gf7ODGCO4mUKOZmQ9oOrv7ugG4dF4HAPcMDGEGhjADQ5iBIczAEGZgCDMwhBkYwgwMYQaGMANDmIEhzMAQZmAIMzCE/UUWA0OS8G1mAAAAAElFTkSuQmCC'); 0..100 | ForEach-Object { [IO.File]::WriteAllBytes((Join-Path $env:LASTUDIO_TEST_FRAME_DIR ('frame-{0:d6}.png' -f $_)), $bytes) }\"\r\n");
}

class DubbingSttWorkerMock final : public QObject
{
public:
    explicit DubbingSttWorkerMock(bool completeTranscription = false)
        : m_completeTranscription(completeTranscription)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    consume(socket);
                });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                    m_pending.remove(socket);
                    socket->deleteLater();
                });
            }
        });
    }

    ~DubbingSttWorkerMock() override
    {
        // QObject destroys C++ members before it disconnects its signal
        // handlers.  Close test sockets here, while m_pending is still alive,
        // so a late disconnected() signal cannot access a destroyed QHash.
        const auto sockets = m_pending.keys();
        for (QTcpSocket *socket : sockets) {
            QObject::disconnect(socket, nullptr, this, nullptr);
            socket->abort();
            delete socket;
        }
        m_pending.clear();
        m_server.close();
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QString workerUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }
    QByteArray request() const { return m_request; }
    QByteArray requests() const { return m_requests; }

private:
    static QByteArray jsonResponse(const QByteArray &status, const QByteArray &payload)
    {
        return QByteArrayLiteral("HTTP/1.1 ") + status
            + QByteArrayLiteral("\r\nContent-Type: application/json\r\nContent-Length: ")
            + QByteArray::number(payload.size())
            + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + payload;
    }

    void consume(QTcpSocket *socket)
    {
        if (!socket) return;
        QByteArray &pending = m_pending[socket];
        pending += socket->readAll();
        const int headerEnd = pending.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const QRegularExpressionMatch contentLength = QRegularExpression(
            QStringLiteral("Content-Length: (\\d+)"),
            QRegularExpression::CaseInsensitiveOption).match(
                QString::fromLatin1(pending.left(headerEnd)));
        const int bodyLength = contentLength.hasMatch() ? contentLength.captured(1).toInt() : 0;
        const int requestLength = headerEnd + 4 + bodyLength;
        if (pending.size() < requestLength) return;

        const QByteArray request = pending.left(requestLength);
        pending.remove(0, requestLength);
        if (m_request.isEmpty()) m_request = request;
        m_requests += request;

        if (!m_completeTranscription) {
            socket->write("HTTP/1.1 503 Service Unavailable\r\n"
                          "Content-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }

        QByteArray response;
        if (request.startsWith("POST /v2/uploads/stt HTTP/1.1")) {
            response = jsonResponse(QByteArrayLiteral("201 Created"),
                QByteArrayLiteral("{\"upload_id\":\"dubbing-stt-upload\",\"chunk_bytes\":2097152}"));
        } else if (request.startsWith("PUT /v2/uploads/stt/dubbing-stt-upload/chunks/0 HTTP/1.1")) {
            response = jsonResponse(QByteArrayLiteral("200 OK"),
                QByteArrayLiteral("{\"received_bytes\":") + QByteArray::number(bodyLength)
                    + QByteArrayLiteral(",\"next_chunk\":1}"));
        } else if (request.startsWith("POST /v2/uploads/stt/dubbing-stt-upload/commit HTTP/1.1")) {
            response = jsonResponse(QByteArrayLiteral("202 Accepted"),
                QByteArrayLiteral("{\"job_id\":\"dubbing-stt-job\",\"status\":\"queued\",\"progress\":5}"));
        } else if (request.startsWith("GET /v2/jobs/transcriptions/dubbing-stt-job HTTP/1.1")) {
            response = jsonResponse(QByteArrayLiteral("200 OK"), QByteArrayLiteral(
                "{\"job_id\":\"dubbing-stt-job\",\"status\":\"succeeded\",\"progress\":100,"
                "\"result\":{\"text\":\"Shared OCR\",\"segments\":[{\"id\":0,\"start\":0.0,"
                "\"end\":1.5,\"text\":\"Shared OCR\"}]}}"));
        } else {
            response = jsonResponse(QByteArrayLiteral("404 Not Found"),
                QByteArrayLiteral("{\"detail\":\"Unexpected test endpoint\"}"));
        }
        socket->write(response);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_pending;
    QByteArray m_request;
    QByteArray m_requests;
    bool m_completeTranscription = false;
};

class ExactRouteWorkerMock final : public QObject
{
public:
    ExactRouteWorkerMock(const QString &capability, const QString &model)
        : m_capability(capability), m_model(model)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    QByteArray &pending = m_pending[socket];
                    pending += socket->readAll();
                    const int headerEnd = pending.indexOf("\r\n\r\n");
                    if (headerEnd < 0) return;
                    const QByteArray request = pending.left(headerEnd + 4);
                    const QByteArray firstLine = request.left(request.indexOf("\r\n"));
                    QByteArray response;
                    const QString workerRevision = m_capability == QStringLiteral("stt")
                        ? QStringLiteral("stt-2026-07-30.2")
                        : (m_capability == QStringLiteral("translation")
                               ? QStringLiteral("translation-2026-07-30.3")
                               : QString());
                    const QString responseContract = m_capability == QStringLiteral("translation")
                        ? QStringLiteral("translation-patches-v3") : QString();
                    if (firstLine.startsWith("GET /health ")) {
                        response = QStringLiteral(
                            R"({"status":"ready","ready":true,"device":"cuda","model":"%1","variant":"fixed","worker_revision":"%2","response_contract":"%3","cpu_fallback":false})")
                                       .arg(m_model, workerRevision, responseContract).toUtf8();
                    } else if (firstLine.startsWith("GET /v1/capabilities ")) {
                        response = QStringLiteral(
                            R"({"contract_version":1,"device":"cuda","worker_revision":"%1","response_contract":"%2","capabilities":[{"id":"%3","models":[{"id":"%4","variant":"fixed","device":"cuda","loaded":true,"response_contract":"%2"}]}]})")
                                       .arg(workerRevision, responseContract, m_capability, m_model).toUtf8();
                    } else {
                        socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        socket->disconnectFromHost();
                        return;
                    }
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                  + QByteArray::number(response.size())
                                  + "\r\nConnection: close\r\n\r\n" + response);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                    m_pending.remove(socket);
                    socket->deleteLater();
                });
            }
        });
    }

    ~ExactRouteWorkerMock() override
    {
        const auto sockets = m_pending.keys();
        for (QTcpSocket *socket : sockets) {
            QObject::disconnect(socket, nullptr, this, nullptr);
            socket->abort();
            delete socket;
        }
        m_pending.clear();
        m_server.close();
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QString workerUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }

private:
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_pending;
    QString m_capability;
    QString m_model;
};

QByteArray loopbackVoiceWav()
{
    constexpr quint32 frameCount = 24000;
    constexpr quint32 dataSize = frameCount * sizeof(qint16);
    QByteArray wav(static_cast<int>(44 + dataSize), '\0');
    std::memcpy(wav.data(), "RIFF", 4);
    const quint32 fileSize = 36 + dataSize;
    std::memcpy(wav.data() + 4, &fileSize, sizeof(fileSize));
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    const quint32 fmtSize = 16;
    const quint16 format = 1;
    const quint16 channels = 1;
    const quint32 sampleRate = 24000;
    const quint32 byteRate = sampleRate * sizeof(qint16);
    const quint16 blockAlign = sizeof(qint16);
    const quint16 bits = 16;
    std::memcpy(wav.data() + 16, &fmtSize, sizeof(fmtSize));
    std::memcpy(wav.data() + 20, &format, sizeof(format));
    std::memcpy(wav.data() + 22, &channels, sizeof(channels));
    std::memcpy(wav.data() + 24, &sampleRate, sizeof(sampleRate));
    std::memcpy(wav.data() + 28, &byteRate, sizeof(byteRate));
    std::memcpy(wav.data() + 32, &blockAlign, sizeof(blockAlign));
    std::memcpy(wav.data() + 34, &bits, sizeof(bits));
    std::memcpy(wav.data() + 36, "data", 4);
    std::memcpy(wav.data() + 40, &dataSize, sizeof(dataSize));
    return wav;
}

class DubbingVoiceCloneWorkerMock final : public QObject
{
public:
    DubbingVoiceCloneWorkerMock()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { consume(socket); });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                    m_pending.remove(socket);
                    socket->deleteLater();
                });
            }
        });
    }

    ~DubbingVoiceCloneWorkerMock() override
    {
        const auto sockets = m_pending.keys();
        for (QTcpSocket *socket : sockets) {
            QObject::disconnect(socket, nullptr, this, nullptr);
            socket->abort();
            delete socket;
        }
        m_pending.clear();
        m_server.close();
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QString workerUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }
    int profileCreations() const { return m_profileCreations; }
    int generationCreations() const { return m_generationCreations; }
    QList<QByteArray> profileRequests() const { return m_profileRequests; }
    QList<QByteArray> generationRequests() const { return m_generationRequests; }
    void setHoldGeneration(bool hold) { m_holdGeneration = hold; }

private:
    static QByteArray jsonResponse(int status, const QByteArray &body)
    {
        return QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status)
            + QByteArrayLiteral(" OK\r\nContent-Type: application/json\r\nContent-Length: ")
            + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
    }

    void respond(QTcpSocket *socket, const QByteArray &request)
    {
        const QByteArray firstLine = request.left(request.indexOf("\r\n"));
        QByteArray response;
        if (firstLine.startsWith("POST /v2/jobs/profile ")) {
            ++m_profileCreations;
            m_profileRequests.append(request);
            response = jsonResponse(202, QByteArrayLiteral("{\"id\":\"profile-job-")
                                             + QByteArray::number(m_profileCreations)
                                             + QByteArrayLiteral("\",\"status\":\"queued\",\"percent\":0}"));
        } else if (firstLine.startsWith("GET /v2/jobs/profile-job-")) {
            const QByteArray id = firstLine.mid(QByteArrayLiteral("GET /v2/jobs/profile-job-").size())
                                      .split(' ').constFirst();
            response = jsonResponse(200, QByteArrayLiteral("{\"status\":\"succeeded\",\"percent\":100,\"result\":{\"id\":\"profile-")
                                             + id + QByteArrayLiteral("\"}}"));
        } else if (firstLine.startsWith("POST /v2/jobs/generation ")) {
            ++m_generationCreations;
            m_generationRequests.append(request);
            response = jsonResponse(202, QByteArrayLiteral("{\"id\":\"generation-job-")
                                             + QByteArray::number(m_generationCreations)
                                             + QByteArrayLiteral("\",\"status\":\"queued\",\"percent\":0}"));
        } else if (firstLine.startsWith("GET /v2/jobs/generation-job-")
                   && firstLine.endsWith("/audio HTTP/1.1")) {
            const QByteArray wav = loopbackVoiceWav();
            response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: ")
                + QByteArray::number(wav.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + wav;
        } else if (firstLine.startsWith("GET /v2/jobs/generation-job-")) {
            const QByteArray status = m_holdGeneration ? QByteArrayLiteral("queued")
                                                        : QByteArrayLiteral("succeeded");
            response = jsonResponse(200, QByteArrayLiteral("{\"status\":\"") + status
                                             + QByteArrayLiteral("\",\"percent\":50}"));
        } else if (firstLine.startsWith("DELETE /v2/jobs/") || firstLine.startsWith("DELETE /v1/profiles/")) {
            response = jsonResponse(200, QByteArrayLiteral("{\"cancelled\":true}"));
        } else {
            response = jsonResponse(404, QByteArrayLiteral("{\"detail\":\"Unexpected request\"}"));
        }
        socket->write(response);
        socket->disconnectFromHost();
    }

    void consume(QTcpSocket *socket)
    {
        QByteArray &pending = m_pending[socket];
        pending += socket->readAll();
        const int headerEnd = pending.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const QRegularExpression lengthPattern(QStringLiteral("Content-Length: (\\d+)"),
                                               QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = lengthPattern.match(
            QString::fromLatin1(pending.left(headerEnd)));
        const int bodyLength = match.hasMatch() ? match.captured(1).toInt() : 0;
        const int requestLength = headerEnd + 4 + bodyLength;
        if (pending.size() < requestLength) return;
        const QByteArray request = pending.left(requestLength);
        pending.remove(0, requestLength);
        respond(socket, request);
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_pending;
    int m_profileCreations = 0;
    int m_generationCreations = 0;
    bool m_holdGeneration = false;
    QList<QByteArray> m_profileRequests;
    QList<QByteArray> m_generationRequests;
};

class ColabSessionReset final
{
public:
    explicit ColabSessionReset(ColabSession *session) : m_session(session) {}
    ~ColabSessionReset() { if (m_session) m_session->clear(); }

private:
    ColabSession *m_session = nullptr;
};

} // namespace

void TestDubbingProject::normalizesLmStudioTranslationFixConfiguration()
{
    const QVariantMap config =
        DubbingTranslationFixService::normalizedConfiguration({
            {QStringLiteral("provider"), QStringLiteral("api")},
            {QStringLiteral("configured"), true},
            {QStringLiteral("serverUrl"),
             QStringLiteral(" http://127.0.0.1:1234/v1/chat/completions ")},
            {QStringLiteral("model"), QStringLiteral(" qwen3.5-2b ")},
            {QStringLiteral("runtimeId"), QStringLiteral(" llama-win-x86_64-cuda-12.4 ")},
            {QStringLiteral("runtimeVersion"), QStringLiteral(" b10036 ")},
            {QStringLiteral("selectedFiles"),
             QVariantMap{{QStringLiteral("model"), QStringLiteral("Qwen3.5-2B-Q8_0.gguf")}}},
            {QStringLiteral("maxAttempts"), 99},
            {QStringLiteral("temperature"), 4.0}
        });
    QCOMPARE(config.value(QStringLiteral("serverUrl")).toString(),
             QStringLiteral("http://127.0.0.1:1234/v1/chat/completions"));
    QCOMPARE(config.value(QStringLiteral("provider")).toString(), QStringLiteral("api"));
    QVERIFY(config.value(QStringLiteral("configured")).toBool());
    QCOMPARE(config.value(QStringLiteral("model")).toString(),
             QStringLiteral("qwen3.5-2b"));
    QCOMPARE(config.value(QStringLiteral("runtimeId")).toString(), QString());
    QCOMPARE(config.value(QStringLiteral("runtimeVersion")).toString(), QString());
    QVERIFY(config.value(QStringLiteral("selectedFiles")).toMap().isEmpty());
    QCOMPARE(config.value(QStringLiteral("maxAttempts")).toInt(), 8);
    QCOMPARE(config.value(QStringLiteral("temperature")).toDouble(), 1.5);
    QCOMPARE(
        DubbingTranslationFixService::chatUrl(
            config.value(QStringLiteral("serverUrl")).toString()).toString(),
        QStringLiteral("http://127.0.0.1:1234/api/v1/chat"));
    QCOMPARE(
        DubbingTranslationFixService::modelsUrl(
            config.value(QStringLiteral("serverUrl")).toString()).toString(),
        QStringLiteral("http://127.0.0.1:1234/api/v1/models"));

    const QVariantMap invalidProvider =
        DubbingTranslationFixService::normalizedConfiguration({
            {QStringLiteral("provider"), QStringLiteral("unknown")}
        });
    QCOMPARE(invalidProvider.value(QStringLiteral("provider")).toString(),
             QStringLiteral("lmstudio"));
    QVERIFY(!invalidProvider.value(QStringLiteral("configured")).toBool());

    const QVariantMap cliConfig =
        DubbingTranslationFixService::normalizedConfiguration({
            {QStringLiteral("provider"), QStringLiteral("cli")},
            {QStringLiteral("cliAgent"), QStringLiteral("codex")},
            {QStringLiteral("model"), QStringLiteral("gpt-4o")},
            {QStringLiteral("configured"), true}
        });
    QCOMPARE(cliConfig.value(QStringLiteral("provider")).toString(), QStringLiteral("cli"));
    QCOMPARE(cliConfig.value(QStringLiteral("cliAgent")).toString(), QStringLiteral("codex"));
    QCOMPARE(cliConfig.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-4o"));
    QVERIFY(cliConfig.value(QStringLiteral("configured")).toBool());

    const QVariantMap directConfig =
        DubbingTranslationFixService::normalizedConfiguration({
            {QStringLiteral("provider"), QStringLiteral("colab-direct")},
            {QStringLiteral("configured"), true},
            {QStringLiteral("serverUrl"), QStringLiteral("https://stale-local-worker.invalid")},
            {QStringLiteral("apiKey"), QStringLiteral("must-not-persist")},
            {QStringLiteral("model"), QStringLiteral("qwen3.5-2b")},
            {QStringLiteral("runtimeId"), QStringLiteral("llama-cpu")},
            {QStringLiteral("runtimeVersion"), QStringLiteral("b10036")},
            {QStringLiteral("selectedFiles"), QVariantMap{{QStringLiteral("model"), QStringLiteral("stale.gguf")}}}
        });
    QCOMPARE(directConfig.value(QStringLiteral("provider")).toString(), QStringLiteral("colab-direct"));
    QVERIFY(directConfig.value(QStringLiteral("configured")).toBool());
    QCOMPARE(directConfig.value(QStringLiteral("serverUrl")).toString(), QString());
    QCOMPARE(directConfig.value(QStringLiteral("apiKey")).toString(), QString());
    QCOMPARE(directConfig.value(QStringLiteral("runtimeId")).toString(), QString());
    QCOMPARE(directConfig.value(QStringLiteral("runtimeVersion")).toString(), QString());
    QVERIFY(directConfig.value(QStringLiteral("selectedFiles")).toMap().isEmpty());
}

void TestDubbingProject::remoteTranslationRoutesDoNotFallbackBetweenGatewayAndColab()
{
    const QVariantList segments = {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-1")},
                    {QStringLiteral("sourceText"), QStringLiteral("Hello")}}
    };

    // Neither case provides a local TranslationEngine.  Each selected remote
    // route must fail only for its own missing configuration, never by loading
    // local inference or switching to the other remote route.
    DubbingTranslationJob job(nullptr, nullptr, nullptr, nullptr);
    QSignalSpy failures(&job, &DubbingTranslationJob::failed);

    QVERIFY(!job.start(QStringLiteral("en"), QStringLiteral("vi"), segments,
                        QVariantMap{{QStringLiteral("executionProvider"), QStringLiteral("api-gateway")}},
                        QStringLiteral("gateway-only")));
    QCOMPARE(failures.count(), 1);
    QCOMPARE(failures.takeFirst().at(0).toString(),
             QStringLiteral("API Gateway configuration is unavailable."));

    QVERIFY(!job.start(QStringLiteral("en"), QStringLiteral("vi"), segments,
                        QVariantMap{{QStringLiteral("executionProvider"), QStringLiteral("colab-direct")}},
                        QStringLiteral("colab-only")));
    QCOMPARE(failures.count(), 1);
    QCOMPARE(failures.takeFirst().at(0).toString(),
             QStringLiteral("Connect a Colab GPU worker before running this Translation node."));
}

void TestDubbingProject::remoteTtsRoutesDoNotFallbackBetweenGatewayAndColab()
{
    const QVariantList segments = {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-1")},
                    {QStringLiteral("targetText"), QStringLiteral("Xin chao")},
                    {QStringLiteral("startMs"), 0},
                    {QStringLiteral("endMs"), 1000}}
    };

    // No local TTS engine exists.  Each remote selection must report its own
    // missing dependency instead of using local synthesis or the other route.
    DubbingSynthesisJob job(nullptr);
    QSignalSpy failures(&job, &DubbingSynthesisJob::failed);

    QVERIFY(!job.start(segments, QStringLiteral("C:/temp/project.ladub.json"),
                        QVariantMap{{QStringLiteral("executionProvider"), QStringLiteral("api-gateway")}},
                        QStringLiteral("gateway-only")));
    QCOMPARE(failures.count(), 1);
    QCOMPARE(failures.takeFirst().at(0).toString(),
             QStringLiteral("API Gateway configuration is unavailable."));

    QVERIFY(!job.start(segments, QStringLiteral("C:/temp/project.ladub.json"),
                        QVariantMap{{QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
                                    {QStringLiteral("modelId"), QStringLiteral("kokoro")}},
                        QStringLiteral("colab-only")));
    QCOMPARE(failures.count(), 1);
    QCOMPARE(failures.takeFirst().at(0).toString(),
             QStringLiteral("Connect a Colab GPU worker before running this TTS node."));
}

void TestDubbingProject::colabDubbingVoiceCloningIsDirectAndRequiresConsent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    constexpr int sampleRate = 24000;
    QVector<float> referenceSamples(sampleRate * 4);
    constexpr double pi = 3.14159265358979323846;
    for (int index = 0; index < referenceSamples.size(); ++index)
        referenceSamples[index] = 0.10F * qSin(2.0 * pi * 180.0 * index / sampleRate);
    const QString sourcePath = dir.filePath(QStringLiteral("source.wav"));
    QVERIFY(WavIO::saveFloat(sourcePath, referenceSamples.constData(), referenceSamples.size(), sampleRate));

    const QVariantList segments = {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-1")},
                    {QStringLiteral("sourceText"), QStringLiteral("Reference speech")},
                    {QStringLiteral("targetText"), QStringLiteral("Dubbed speech")},
                    {QStringLiteral("startMs"), 0},
                    {QStringLiteral("endMs"), 4000}}
    };
    const QString projectPath = dir.filePath(QStringLiteral("project.ladub.json"));
    DubbingSynthesisJob job(nullptr);
    QSignalSpy failures(&job, &DubbingSynthesisJob::failed);

    // Gateway must reject voice cloning before it looks for a Colab session
    // or attempts local voice generation.
    QVERIFY(!job.start(segments, projectPath,
                        QVariantMap{{QStringLiteral("executionProvider"), QStringLiteral("api-gateway")},
                                    {QStringLiteral("voiceCloningEnabled"), true},
                                    {QStringLiteral("cloneVoicePreset"),
                                     QVariantMap{{QStringLiteral("id"), QStringLiteral("preset-source")},
                                                 {QStringLiteral("name"), QStringLiteral("Source")},
                                                 {QStringLiteral("audioPath"), sourcePath}}}},
                        QStringLiteral("gateway-clone")));
    QCOMPARE(failures.count(), 1);
    QCOMPARE(failures.takeFirst().at(0).toString(),
             QStringLiteral("The selected saved voice is not supported by this API Gateway TTS route. Choose a compatible built-in voice or Direct Colab; LA Studio will not substitute a voice."));

    // A saved library voice requires its matching verified worker. Dubbing
    // must not silently switch to API Gateway or local TTS.
    QVERIFY(!job.start(segments, projectPath,
                        QVariantMap{{QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
                                    {QStringLiteral("modelId"), QStringLiteral("kokoro")},
                                    {QStringLiteral("voiceCloneModelId"), QStringLiteral("omnivoice")},
                                    {QStringLiteral("voiceCloningEnabled"), true},
                                    {QStringLiteral("cloneVoicePreset"),
                                     QVariantMap{{QStringLiteral("id"), QStringLiteral("preset-source")},
                                                 {QStringLiteral("name"), QStringLiteral("Source")},
                                                 {QStringLiteral("audioPath"), sourcePath}}}},
                        QStringLiteral("colab-without-consent")));
    QCOMPARE(failures.count(), 1);
    QCOMPARE(failures.takeFirst().at(0).toString(),
             QStringLiteral("Connect a Colab voice-cloning worker before running this TTS node."));

    // Once consented, the selected source reference is resolved locally and
    // the next dependency checked is only the direct Colab session.
    QVERIFY(!job.start(segments, projectPath,
                        QVariantMap{{QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
                                    {QStringLiteral("modelId"), QStringLiteral("kokoro")},
                                    {QStringLiteral("voiceCloneModelId"), QStringLiteral("omnivoice")},
                                    {QStringLiteral("voiceCloningEnabled"), true},
                                    {QStringLiteral("voiceCloneConsentConfirmed"), true},
                                    {QStringLiteral("cloneVoicePreset"),
                                     QVariantMap{{QStringLiteral("id"), QStringLiteral("preset-source")},
                                                 {QStringLiteral("name"), QStringLiteral("Source")},
                                                 {QStringLiteral("audioPath"), sourcePath}}}},
                        QStringLiteral("colab-direct-only")));
    QCOMPARE(failures.count(), 1);
    QCOMPARE(failures.takeFirst().at(0).toString(),
             QStringLiteral("Connect a Colab voice-cloning worker before running this TTS node."));
}

void TestDubbingProject::parsesLmStudioTranslationFixResponses()
{
    QCOMPARE(
        DubbingTranslationFixService::cleanAssistantText(
            QStringLiteral("<think>internal reasoning</think>\n"
                           "Bản dịch: \"Một câu đã sửa.\"")),
        QStringLiteral("Một câu đã sửa."));
    QCOMPARE(
        DubbingTranslationFixService::cleanAssistantText(
            QStringLiteral("```text\nMột câu khác.\n```")),
        QStringLiteral("Một câu khác."));
    QCOMPARE(
        DubbingTranslationFixService::parseCliResponse(
            QByteArray("{\"type\":\"thread.started\"}\n"
                       "{\"type\":\"item.completed\",\"item\":{\"type\":\"agent_message\",\"text\":\"Bản dịch Codex.\"}}\n"
                       "{\"type\":\"turn.completed\"}\n")),
        QStringLiteral("Bản dịch Codex."));
    QCOMPARE(
        DubbingTranslationFixService::parseCliResponse(
            QByteArray("{\"type\":\"assistant\",\"message\":{\"content\":[{\"type\":\"text\",\"text\":\"Bản dịch Claude.\"}]}}\n")),
        QStringLiteral("Bản dịch Claude."));
    QCOMPARE(
        DubbingTranslationFixService::parseCliResponse(
            QByteArray("Bản dịch Antigravity.\n")),
        QStringLiteral("Bản dịch Antigravity."));
}

void TestDubbingProject::buildsConsistentCliInvocations()
{
    const auto claude = DubbingTranslationFixService::cliInvocation(
        QStringLiteral("claude"), QStringLiteral("sonnet"),
        QStringLiteral("test prompt"),
        QStringLiteral("C:/tools/claude.exe"), {}, 30);
    QCOMPARE(claude.program, QStringLiteral("C:/tools/claude.exe"));
    QVERIFY(claude.promptViaStdin);
    QVERIFY(claude.arguments.contains(QStringLiteral("--no-session-persistence")));
    QVERIFY(claude.arguments.contains(QStringLiteral("--tools")));
    QVERIFY(claude.arguments.contains(QStringLiteral("sonnet")));

    const auto codex = DubbingTranslationFixService::cliInvocation(
        QStringLiteral("codex"), QStringLiteral("gpt-5"),
        QStringLiteral("test prompt"),
        QStringLiteral("C:/tools/codex.exe"), {}, 30);
    QVERIFY(codex.promptViaStdin);
    QCOMPARE(codex.arguments.constFirst(), QStringLiteral("exec"));
    QVERIFY(codex.arguments.contains(QStringLiteral("--json")));
    QVERIFY(codex.arguments.contains(QStringLiteral("read-only")));
    QVERIFY(codex.arguments.contains(QStringLiteral("gpt-5")));

    const QString logPath = QStringLiteral("C:/Temp/agy-test.log");
    const QString prompt = QStringLiteral("Reply with exactly: OK");
    const auto antigravity = DubbingTranslationFixService::cliInvocation(
        QStringLiteral("antigravity"), QStringLiteral("Gemini 3.1 Pro (High)"),
        prompt, QStringLiteral("C:/tools/agy.exe"), logPath, 30);
    QVERIFY(!antigravity.promptViaStdin);
    QCOMPARE(antigravity.arguments.mid(0, 2),
             QStringList({QStringLiteral("--log-file"), logPath}));
    QVERIFY(antigravity.arguments.contains(QStringLiteral("--sandbox")));
    QVERIFY(antigravity.arguments.contains(
        QStringLiteral("--dangerously-skip-permissions")));
    QVERIFY(antigravity.arguments.contains(QStringLiteral("30s")));
    QCOMPARE(antigravity.arguments.constLast(), prompt);
    QCOMPARE(antigravity.diagnosticLogPath, logPath);
}

void TestDubbingProject::classifiesCliDiagnostics()
{
    QCOMPARE(
        DubbingTranslationFixService::cliFailureMessage(
            QStringLiteral("antigravity"), {}, {},
            QByteArray("RESOURCE_EXHAUSTED (code 429): Individual quota reached")),
        QStringLiteral("Antigravity quota is exhausted for the selected model. Choose another model in LA Studio or wait for the quota to reset."));
    QCOMPARE(
        DubbingTranslationFixService::cliFailureMessage(
            QStringLiteral("antigravity"), {},
            QByteArray("a tool required the \"command\" permission that headless mode cannot prompt for")),
        QStringLiteral("The CLI requested an interactive tool permission that cannot be approved in headless mode. Update the CLI and retry with the sandboxed non-interactive integration."));
    QCOMPARE(
        DubbingTranslationFixService::cliFailureMessage(
            QStringLiteral("antigravity"),
            QByteArray("Authentication required. Please visit the URL"), {}),
        QStringLiteral("Antigravity authentication is required. Open a terminal, run agy once, complete Google sign-in, then retry."));

    const QByteArray agySilentAuthLog(
        "Failed to poll ListExperiments: You are not logged into Antigravity.\n"
        "Print mode: not authenticated, trying silent auth\n"
        "keyringAuth: loaded token, expired=false\n"
        "ChainedAuth: authenticated via keyring (effective: keyring)\n"
        "OAuth: authenticated successfully as user@example.com\n"
        "Print mode: silent auth succeeded\n");
    QCOMPARE(
        DubbingTranslationFixService::cliFailureMessage(
            QStringLiteral("antigravity"), QByteArray("OK\n"), {},
            agySilentAuthLog),
        QString());
}

void TestDubbingProject::discoversCliModelsFromLocalConfiguration()
{
    QTemporaryDir home;
    QVERIFY(home.isValid());
    QVERIFY(QDir().mkpath(home.filePath(QStringLiteral(".claude"))));
    QVERIFY(QDir().mkpath(home.filePath(QStringLiteral(".codex"))));
    QVERIFY(QDir().mkpath(
        home.filePath(QStringLiteral(".gemini/antigravity-cli"))));

    auto writeFile = [](const QString &path, const QByteArray &content) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        return file.write(content) == content.size();
    };
    QVERIFY(writeFile(
        home.filePath(QStringLiteral(".claude/settings.json")),
        QByteArray(R"({"model":"opus"})")));
    QVERIFY(writeFile(
        home.filePath(QStringLiteral(".claude/stats-cache.json")),
        QByteArray(R"({"modelUsage":{"claude-opus-4-6":{},"claude-sonnet-4-6":{}}})")));
    QVERIFY(writeFile(
        home.filePath(QStringLiteral(".codex/config.toml")),
        QByteArray("model = \"gpt-5.6-sol\"\n")));
    QVERIFY(writeFile(
        home.filePath(QStringLiteral(".codex/models_cache.json")),
        QByteArray(R"({"models":[{"slug":"gpt-5.6-sol","display_name":"GPT-5.6-Sol","visibility":"list"},{"slug":"internal","display_name":"Internal","visibility":"hidden"}]})")));
    QVERIFY(writeFile(
        home.filePath(
            QStringLiteral(".gemini/antigravity-cli/settings.json")),
        QByteArray(R"({"model":"gemini-3.6-flash-low"})")));

    const auto valuesFor = [&home](const QString &agent) {
        QStringList values;
        const QVariantList options =
            DubbingTranslationFixService::cliModelOptions(agent, home.path());
        for (const QVariant &option : options)
            values.append(
                option.toMap().value(QStringLiteral("value")).toString());
        return values;
    };

    const QStringList claude = valuesFor(QStringLiteral("claude"));
    QCOMPARE(claude.constFirst(), QStringLiteral("default"));
    QVERIFY(claude.contains(QStringLiteral("opus")));
    QVERIFY(claude.contains(QStringLiteral("claude-sonnet-4-6")));

    const QStringList codex = valuesFor(QStringLiteral("codex"));
    QVERIFY(codex.contains(QStringLiteral("gpt-5.6-sol")));
    QVERIFY(!codex.contains(QStringLiteral("internal")));

    const QStringList antigravity =
        valuesFor(QStringLiteral("antigravity"));
    QVERIFY(antigravity.contains(QStringLiteral("gemini-3.6-flash-low")));
    QVERIFY(antigravity.contains(QStringLiteral("claude-sonnet-4-6")));
}

void TestDubbingProject::fixesOnlyTranslationsOverPhonemeLimit()
{
    const QString text = QStringLiteral("Đây là một câu dịch để kiểm tra.");
    const int phonemes =
        DubbingDurationPlanner::countPhonemes(text, QStringLiteral("vi"));
    if (phonemes <= 1) {
        QSKIP("eSpeak NG runtime is unavailable; phoneme integration is validated in the staged release tier.");
    }
    QVERIFY(phonemes > 1);

    const QVariantMap overBudget{
        {QStringLiteral("targetText"), text},
        {QStringLiteral("durationBudget"),
         QVariantMap{{QStringLiteral("minUnits"), 1},
                     {QStringLiteral("maxUnits"), phonemes - 1}}}};
    const QVariantMap underBudget{
        {QStringLiteral("targetText"), text},
        {QStringLiteral("durationBudget"),
         QVariantMap{{QStringLiteral("minUnits"), phonemes + 1},
                     {QStringLiteral("maxUnits"), phonemes + 5}}}};
    const QVariantMap withinBudget{
        {QStringLiteral("targetText"), text},
        {QStringLiteral("durationBudget"),
         QVariantMap{{QStringLiteral("minUnits"), phonemes},
                     {QStringLiteral("maxUnits"), phonemes}}}};

    QCOMPARE(DubbingTranslationFixService::eligibleSegmentCount(
                 {overBudget, underBudget, withinBudget}, QStringLiteral("vi")),
             1);
}

void TestDubbingProject::ranksPartialTranslationFixesByBudgetDistance()
{
    QVERIFY(DubbingTranslationFixService::isCloserToBudget(97, 59, 47, 57));
    QVERIFY(DubbingTranslationFixService::isCloserToBudget(68, 60, 47, 57));
    QVERIFY(!DubbingTranslationFixService::isCloserToBudget(68, 80, 47, 57));
    QVERIFY(!DubbingTranslationFixService::isCloserToBudget(59, 45, 47, 57));
    QVERIFY(DubbingTranslationFixService::isCloserToBudget(59, 55, 47, 57));
}

void TestDubbingProject::roundTripsVersionedJson()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DubbingProject original;
    original.projectPath = dir.filePath(QStringLiteral("demo.ladub.json"));
    original.sourceMediaPath = QStringLiteral("C:/media/demo.mp4");
    original.sourceLanguage = QStringLiteral("en");
    original.targetLanguage = QStringLiteral("vi");
    original.dubbingQuality = QStringLiteral("custom");
    original.workflowEntryMode = QStringLiteral("automatic");
    original.cloneVoicePresetId = QStringLiteral("saved-clone-voice");
    original.durationControl.insert(QStringLiteral("autoRewrite"), false);
    original.workflowNodeConfigurations.insert(
        QStringLiteral("translate"),
        QVariantMap{{QStringLiteral("familyId"), QStringLiteral("nllb-200")},
                    {QStringLiteral("runtimeId"), QStringLiteral("crispasr")}});
    original.customRewriteConfiguration = {
        {QStringLiteral("provider"), QStringLiteral("cli")},
        {QStringLiteral("cliAgent"), QStringLiteral("codex")},
        {QStringLiteral("model"), QStringLiteral("default")},
        {QStringLiteral("configured"), true}
    };
    original.speakers.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("speaker-1")} });
    original.segments.append(QVariantMap{{QStringLiteral("startMs"), 1000},
                                         {QStringLiteral("endMs"), 2400},
                                         {QStringLiteral("sourceText"), QStringLiteral("Hello")} });

    QString error;
    QVERIFY2(original.save(&error), qPrintable(error));
    QVERIFY(QFileInfo::exists(original.projectPath));

    DubbingProject loaded;
    QVERIFY2(DubbingProject::load(original.projectPath, loaded, &error), qPrintable(error));
    QCOMPARE(loaded.sourceMediaPath, original.sourceMediaPath);
    QCOMPARE(loaded.targetLanguage, original.targetLanguage);
    QCOMPARE(loaded.dubbingQuality, QStringLiteral("custom"));
    QCOMPARE(loaded.workflowEntryMode, QStringLiteral("automatic"));
    QCOMPARE(loaded.cloneVoicePresetId, QStringLiteral("saved-clone-voice"));
    QVERIFY(!loaded.durationControl.value(QStringLiteral("autoRewrite")).toBool());
    QCOMPARE(loaded.workflowNodeConfigurations.value(QStringLiteral("translate")).toMap()
                 .value(QStringLiteral("familyId")).toString(),
             QStringLiteral("nllb-200"));
    QCOMPARE(loaded.customRewriteConfiguration.value(QStringLiteral("provider")).toString(),
             QStringLiteral("cli"));
    QCOMPARE(loaded.segments.size(), 1);
    QCOMPARE(loaded.segments.first().toMap().value(QStringLiteral("startMs")).toLongLong(), qint64(1000));
}

void TestDubbingProject::migratesLegacyProjectsToLlmRewritePipeline()
{
    DubbingProject migrated;
    QString error;
    const QJsonObject legacy{
        {QStringLiteral("schemaVersion"), 3},
        {QStringLiteral("durationControl"),
         QJsonObject{{QStringLiteral("enabled"), true},
                     {QStringLiteral("autoRewrite"), false}}}
    };
    QVERIFY2(DubbingProject::fromJson(legacy, migrated, &error), qPrintable(error));
    QVERIFY(migrated.durationControl.value(QStringLiteral("autoRewrite")).toBool());
    QCOMPARE(migrated.dubbingQuality, QStringLiteral("adaptive"));

    DubbingProject current;
    const QJsonObject explicitOptOut{
        {QStringLiteral("schemaVersion"), DubbingProject::CurrentSchemaVersion},
        {QStringLiteral("durationControl"),
         QJsonObject{{QStringLiteral("enabled"), true},
                     {QStringLiteral("autoRewrite"), false}}}
    };
    QVERIFY2(DubbingProject::fromJson(explicitOptOut, current, &error), qPrintable(error));
    QVERIFY(!current.durationControl.value(QStringLiteral("autoRewrite")).toBool());
}

void TestDubbingProject::rejectsUnknownSchema()
{
    DubbingProject project;
    QString error;
    QVERIFY(!DubbingProject::fromJson(QJsonObject{{QStringLiteral("schemaVersion"), 99}}, project, &error));
    QVERIFY(error.contains(QStringLiteral("Unsupported")));
}

void TestDubbingProject::mergesSegmentPatchesByStableId()
{
    const QVariantList source{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("a")}, {QStringLiteral("startMs"), 10}, {QStringLiteral("endMs"), 15}, {QStringLiteral("speakerId"), QStringLiteral("s1")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("b")}, {QStringLiteral("startMs"), 20}, {QStringLiteral("endMs"), 30}}
    };
    QVariantList merged;
    QString error;
    QVERIFY(DubbingProject::mergeSegmentPatches(source,
        {QVariantMap{{QStringLiteral("id"), QStringLiteral("b")}, {QStringLiteral("targetText"), QStringLiteral("Xin chao")}}},
        merged, &error));
    QCOMPARE(merged.size(), 2);
    QCOMPARE(merged.at(0).toMap().value(QStringLiteral("speakerId")).toString(), QStringLiteral("s1"));
    QCOMPARE(merged.at(1).toMap().value(QStringLiteral("targetText")).toString(), QStringLiteral("Xin chao"));
}

void TestDubbingProject::rejectsUnknownAndDuplicateSegmentPatches()
{
    const QVariantList source{QVariantMap{{QStringLiteral("id"), QStringLiteral("a")}, {QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 1}}};
    QVariantList merged;
    QString error;
    QVERIFY(!DubbingProject::mergeSegmentPatches(source,
        {QVariantMap{{QStringLiteral("id"), QStringLiteral("missing")}}}, merged, &error));
    QVERIFY(error.contains(QStringLiteral("unknown")));
    QVERIFY(!DubbingProject::mergeSegmentPatches(source,
        {QVariantMap{{QStringLiteral("id"), QStringLiteral("a")} }, QVariantMap{{QStringLiteral("id"), QStringLiteral("a")}}},
        merged, &error));
    QVERIFY(error.contains(QStringLiteral("duplicate")));
}

void TestDubbingProject::importingMediaDoesNotStartProcessing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath(QStringLiteral("source.mp4"));
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QVERIFY(media.write("video-placeholder") > 0);
    media.close();

    DubbingController controller(nullptr, nullptr);
    QVERIFY(controller.newProject(dir.filePath(QStringLiteral("project.ladub.json"))));
    QVERIFY(controller.importMedia(mediaPath));
    QCOMPARE(controller.sourceMediaPath(), QFileInfo(mediaPath).absoluteFilePath());
    QVERIFY(!controller.processing());
    QVERIFY(controller.normalizedAudioPath().isEmpty());
    QVERIFY(controller.vocalsPath().isEmpty());
    QCOMPARE(controller.workflowMode(), QStringLiteral("idle"));
    QCOMPARE(controller.currentStepId(), QStringLiteral("ingest"));
}

void TestDubbingProject::automaticWorkflowDoesNotStartWithUnresolvedPreflight()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath(QStringLiteral("source.mp4"));
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QVERIFY(media.write("video-placeholder") > 0);
    media.close();

    DubbingController controller(nullptr, nullptr);
    QVERIFY(controller.newProject(dir.filePath(QStringLiteral("project.ladub.json"))));
    QVERIFY(controller.importMedia(mediaPath));
    QVERIFY(!controller.automaticPreflight().value(QStringLiteral("ready")).toBool());
    QVERIFY(!controller.approveAutomaticPreflight());
    QVERIFY(!controller.startAutomaticWorkflow(dir.filePath(QStringLiteral("dubbed.mp4"))));
    QVERIFY(!controller.processing());
    QVERIFY(!controller.settingsLocked());
    QCOMPARE(controller.workflowMode(), QStringLiteral("idle"));
    QVERIFY(controller.lastError().contains(QStringLiteral("Automatic preflight")));
}

void TestDubbingProject::dubbingEntryGatePersistsChoiceWithoutMutatingProject()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString projectPath = dir.filePath(QStringLiteral("project.ladub.json"));
    DubbingController controller(nullptr, nullptr);
    QVERIFY(controller.newProject(projectPath));
    controller.addSegment(0, 1000, QStringLiteral("preserved transcript"));
    const QVariantList beforeSegments = controller.segments();

    controller.beginDubbingEntry();
    QVERIFY(controller.dubbingEntryGateActive());
    QVERIFY(!controller.runCurrentStep());
    QVERIFY(controller.lastError().contains(QStringLiteral("entry mode")));
    QVERIFY(controller.chooseDubbingEntryMode(QStringLiteral("automatic")));
    QVERIFY(!controller.dubbingEntryGateActive());
    QCOMPARE(controller.savedDubbingEntryMode(), QStringLiteral("automatic"));
    QCOMPARE(controller.segments(), beforeSegments);
    QCOMPARE(controller.workflowMode(), QStringLiteral("idle"));
    QVERIFY(!controller.processing());

    controller.beginDubbingEntry();
    QVERIFY(controller.chooseDubbingEntryMode(QStringLiteral("step")));
    controller.startStepByStep();
    QCOMPARE(controller.workflowMode(), QStringLiteral("step"));
    QCOMPARE(controller.currentStepId(), QStringLiteral("media-input"));
    QCOMPARE(controller.segments(), beforeSegments);
    QVERIFY(controller.saveProject());

    DubbingProject reopened;
    QString error;
    QVERIFY2(DubbingProject::load(projectPath, reopened, &error), qPrintable(error));
    QCOMPARE(reopened.workflowEntryMode, QStringLiteral("step"));
    QCOMPARE(reopened.segments, beforeSegments);
}

void TestDubbingProject::automaticPreflightUsesPersistedLanguageSingleSourceOfTruth()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    DubbingController controller(nullptr, nullptr);
    QVERIFY(controller.newProject(dir.filePath(QStringLiteral("project.ladub.json"))));
    controller.setSourceLanguage(QStringLiteral("zh"));
    controller.setTargetLanguage(QStringLiteral("vi"));
    const QVariantMap preflight = controller.automaticPreflight();
    QCOMPARE(preflight.value(QStringLiteral("sourceLanguage")).toString(), QStringLiteral("zh"));
    QCOMPARE(preflight.value(QStringLiteral("targetLanguage")).toString(), QStringLiteral("vi"));
    bool sawTranscribe = false;
    bool sawTranslate = false;
    bool sawTts = false;
    for (const QVariant &entry : preflight.value(QStringLiteral("stages")).toList()) {
        const QVariantMap node = entry.toMap();
        const QString id = node.value(QStringLiteral("id")).toString();
        if (id == QStringLiteral("transcribe")) {
            sawTranscribe = true;
            QCOMPARE(node.value(QStringLiteral("languageSummary")).toString(), QStringLiteral("zh"));
        } else if (id == QStringLiteral("translate")) {
            sawTranslate = true;
            QCOMPARE(node.value(QStringLiteral("languageSummary")).toString(), QStringLiteral("zh -> vi"));
        } else if (id == QStringLiteral("tts")) {
            sawTts = true;
            QCOMPARE(node.value(QStringLiteral("languageSummary")).toString(), QStringLiteral("vi"));
        } else if (id == QStringLiteral("normalize") || id == QStringLiteral("isolator")) {
            QVERIFY(!node.value(QStringLiteral("requiresLanguage")).toBool());
        }
    }
    QVERIFY(sawTranscribe);
    QVERIFY(sawTranslate);
    QVERIFY(sawTts);

    const QString mediaPath = dir.filePath(QStringLiteral("source.mp4"));
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QVERIFY(media.write("video-placeholder") > 0);
    media.close();
    QVERIFY(controller.importMedia(mediaPath));
    QVERIFY(!controller.automaticPreflight().value(QStringLiteral("ready")).toBool());
    QVERIFY(!controller.approveAutomaticPreflight());
    controller.setSourceLanguage(QStringLiteral("ja"));
    QVERIFY(!controller.startAutomaticWorkflow(dir.filePath(QStringLiteral("dubbed.mp4"))));
    QVERIFY(controller.lastError().contains(QStringLiteral("Review Automatic preflight")));
}

void TestDubbingProject::automaticPreflightExposesActionableSourceAndStageStates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    DubbingController controller(nullptr, nullptr);
    QVERIFY(controller.newProject(dir.filePath(QStringLiteral("project.ladub.json"))));

    const QVariantMap missing = controller.automaticPreflight();
    QCOMPARE(missing.value(QStringLiteral("sourceMediaPath")).toString(), QString());
    const QVariantList missingIssues = missing.value(QStringLiteral("issues")).toList();
    QVERIFY(!missingIssues.isEmpty());
    QCOMPARE(missingIssues.constFirst().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("source-media"));
    QCOMPARE(missingIssues.constFirst().toMap().value(QStringLiteral("page")).toInt(), 0);
    QCOMPARE(missingIssues.constFirst().toMap().value(QStringLiteral("focus")).toString(),
             QStringLiteral("source-media"));

    bool sawMediaNeedsInput = false;
    bool sawDownstreamReason = false;
    for (const QVariant &entry : missing.value(QStringLiteral("stages")).toList()) {
        const QVariantMap node = entry.toMap();
        const QString id = node.value(QStringLiteral("id")).toString();
        if (id == QStringLiteral("import")) {
            sawMediaNeedsInput = true;
            QCOMPARE(node.value(QStringLiteral("preflightState")).toString(),
                     QStringLiteral("needs-input"));
            QCOMPARE(node.value(QStringLiteral("setupAction")).toString(),
                     QStringLiteral("source"));
        } else if (id == QStringLiteral("normalize")) {
            sawDownstreamReason = true;
            QCOMPARE(node.value(QStringLiteral("preflightState")).toString(),
                     QStringLiteral("blocked-previous"));
            QCOMPARE(node.value(QStringLiteral("setupAction")).toString(),
                     QStringLiteral("normalize"));
            QVERIFY(node.value(QStringLiteral("setupHint")).toString().contains(
                QStringLiteral("preprocessing")));
        }
    }
    QVERIFY(sawMediaNeedsInput);
    QVERIFY(sawDownstreamReason);

    const QString mediaPath = dir.filePath(QStringLiteral("source.mp4"));
    QVERIFY(writeFixtureFile(mediaPath, QByteArrayLiteral("media fixture")));
    QVERIFY(controller.importMedia(mediaPath));
    const QVariantMap afterImport = controller.automaticPreflight();
    QCOMPARE(afterImport.value(QStringLiteral("sourceMediaPath")).toString(), mediaPath);
    for (const QVariant &issueValue : afterImport.value(QStringLiteral("issues")).toList())
        QVERIFY(issueValue.toMap().value(QStringLiteral("id")).toString() != QStringLiteral("source-media"));
}

void TestDubbingProject::automaticPreflightFixTargetsAndNoOpConfigurationsAreExplicit()
{
    DubbingController controller(nullptr, nullptr);
    const QVariantMap preflight = controller.automaticPreflight();
    QHash<QString, QVariantMap> nodes;
    for (const QVariant &entry : preflight.value(QStringLiteral("stages")).toList()) {
        const QVariantMap node = entry.toMap();
        nodes.insert(node.value(QStringLiteral("id")).toString(), node);
    }
    QCOMPARE(nodes.value(QStringLiteral("isolator")).value(QStringLiteral("setupAction")).toString(),
             QStringLiteral("node-model"));
    QCOMPARE(nodes.value(QStringLiteral("transcribe")).value(QStringLiteral("setupAction")).toString(),
             QStringLiteral("node-model"));
    QCOMPARE(nodes.value(QStringLiteral("translate")).value(QStringLiteral("setupAction")).toString(),
             QStringLiteral("node-model"));
    QCOMPARE(nodes.value(QStringLiteral("tts")).value(QStringLiteral("setupAction")).toString(),
             QStringLiteral("node-model"));
    QCOMPARE(nodes.value(QStringLiteral("alignment-subtitle")).value(QStringLiteral("setupAction")).toString(),
             QStringLiteral("alignment"));
    QCOMPARE(nodes.value(QStringLiteral("export")).value(QStringLiteral("setupAction")).toString(),
             QStringLiteral("export"));
    QVERIFY(nodes.value(QStringLiteral("normalize")).value(
        QStringLiteral("configurationSummary")).toString().contains(QStringLiteral("no model required")));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(controller.newProject(dir.filePath(QStringLiteral("colab.ladub.json"))));
    QVERIFY(controller.setWorkflowNodeParameters(QStringLiteral("transcribe"), {
        {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
        {QStringLiteral("modelId"), QStringLiteral("whisper.cpp")}
    }));
    bool sawColabIssue = false;
    for (const QVariant &issueValue : controller.automaticPreflight().value(QStringLiteral("issues")).toList()) {
        const QVariantMap issue = issueValue.toMap();
        if (issue.value(QStringLiteral("id")).toString() == QStringLiteral("colab-transcribe")) {
            sawColabIssue = true;
            QCOMPARE(issue.value(QStringLiteral("page")).toInt(), 2);
        }
    }
    QVERIFY(sawColabIssue);
}

void TestDubbingProject::automaticPreflightReadinessMatrixRejectsFalseReadyStates()
{
    const auto stageFor = [](const QVariantMap &preflight, const QString &id) {
        for (const QVariant &entry : preflight.value(QStringLiteral("stages")).toList()) {
            const QVariantMap stage = entry.toMap();
            if (stage.value(QStringLiteral("id")).toString() == id) return stage;
        }
        return QVariantMap{};
    };

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath(QStringLiteral("source.wav"));
    QVERIFY(writeFixtureFile(mediaPath, QByteArrayLiteral("audio fixture")));
    DubbingController controller(nullptr, nullptr);
    QVERIFY(controller.newProject(dir.filePath(QStringLiteral("matrix.ladub.json"))));

    QVariantMap preflight = controller.automaticPreflight();
    QCOMPARE(stageFor(preflight, QStringLiteral("normalize"))
                 .value(QStringLiteral("preflightState")).toString(),
             QStringLiteral("blocked-previous"));
    QCOMPARE(stageFor(preflight, QStringLiteral("isolator"))
                 .value(QStringLiteral("preflightState")).toString(),
             QStringLiteral("blocked-previous"));

    QVERIFY(controller.importMedia(mediaPath));
    preflight = controller.automaticPreflight();
    const QVariantMap normalize = stageFor(preflight, QStringLiteral("normalize"));
    QCOMPARE(normalize.value(QStringLiteral("preflightState")).toString(), QStringLiteral("ready"));
    QVERIFY(!normalize.value(QStringLiteral("modelRequired")).toBool());
    QCOMPARE(stageFor(preflight, QStringLiteral("isolator"))
                 .value(QStringLiteral("preflightState")).toString(),
             QStringLiteral("needs-setup"));

    const QString localFamily = QStringLiteral("sherpa-onnx-spleeter-2stems-fp16");
    const QString localRuntime = QStringLiteral("sherpa-onnx-win-x86_64-cpu");
    QVERIFY(controller.setWorkflowNodeParameters(QStringLiteral("source-separate"), {
        {QStringLiteral("executionProvider"), QStringLiteral("local-dev")},
        {QStringLiteral("modelId"), localFamily}
    }));
    QCOMPARE(stageFor(controller.automaticPreflight(), QStringLiteral("isolator"))
                 .value(QStringLiteral("preflightState")).toString(),
             QStringLiteral("needs-setup"));
    QVERIFY(controller.setWorkflowNodeModel(QStringLiteral("source-separate"), localFamily,
                                            localRuntime, QStringLiteral("v1.13.4")));
    QCOMPARE(stageFor(controller.automaticPreflight(), QStringLiteral("isolator"))
                 .value(QStringLiteral("preflightState")).toString(),
             QStringLiteral("ready"));

    const QString colabModel = QStringLiteral("sherpa-onnx-spleeter-2stems-fp16");
    QVERIFY(controller.setWorkflowNodeParameters(QStringLiteral("source-separate"), {
        {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
        {QStringLiteral("modelId"), colabModel}
    }));
    QCOMPARE(stageFor(controller.automaticPreflight(), QStringLiteral("isolator"))
                 .value(QStringLiteral("preflightState")).toString(),
             QStringLiteral("needs-worker"));

    ExactRouteWorkerMock worker(QStringLiteral("voice-isolation"), colabModel);
    QVERIFY(worker.start());
    AppController *app = AppController::instance();
    QVERIFY(app && app->colabSeparationSession());
    QString routeError;
    QVERIFY2(app->colabSeparationSession()->beginVerifiedSession(
        worker.workerUrl(), QStringLiteral("test-token"), QStringLiteral("voice-isolation"),
        colabModel, &routeError, true), qPrintable(routeError));
    // beginVerifiedSession starts the network health check.  Do not mistake a
    // successfully submitted check for a verified route: preflight must only
    // become Ready after the exact capability/model response is received.
    QTRY_VERIFY(app->colabSeparationSession()->hasVerifiedRoute(
        QStringLiteral("voice-isolation"), colabModel, &routeError));
    preflight = controller.automaticPreflight();
    QCOMPARE(stageFor(preflight, QStringLiteral("isolator"))
                 .value(QStringLiteral("preflightState")).toString(),
             QStringLiteral("ready"));
    QCOMPARE(preflight.value(QStringLiteral("selectedWorkers")).toList().size(), 1);
    app->colabSeparationSession()->disconnectTemporaryWorker();
}

void TestDubbingProject::automaticSetupKeepsVerifiedDirectColabRouteAndReportsCurrentStage()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString mediaPath = directory.filePath(QStringLiteral("source.wav"));
    const QVector<float> samples(1600, 0.1F);
    QVERIFY(WavIO::saveFloat(mediaPath, samples.constData(), samples.size(), 16000));

    AppController *app = AppController::instance();
    QVERIFY(app != nullptr);
    QVERIFY(app->settings() != nullptr);
    const bool previousRemoteFirst = app->settings()->remoteFirstMode();
    app->settings()->setRemoteFirstMode(false);

    const QString separationModel = QStringLiteral("sherpa-onnx-spleeter-2stems-fp16");
    const QString sttModel = QStringLiteral("whisper.cpp");
    const QString translationModel = QStringLiteral("m2m100-418m");
    const QString ttsModel = QStringLiteral("kokoro");
    ExactRouteWorkerMock separationWorker(QStringLiteral("voice-isolation"), separationModel);
    ExactRouteWorkerMock sttWorker(QStringLiteral("stt"), sttModel);
    ExactRouteWorkerMock translationWorker(QStringLiteral("translation"), translationModel);
    ExactRouteWorkerMock ttsWorker(QStringLiteral("tts"), ttsModel);
    QVERIFY(separationWorker.start());
    QVERIFY(sttWorker.start());
    QVERIFY(translationWorker.start());
    QVERIFY(ttsWorker.start());

    ColabSessionReset resetSeparation(app->colabSeparationSession());
    ColabSessionReset resetStt(app->colabSttSession());
    ColabSessionReset resetTranslation(app->colabTranslationSession());
    ColabSessionReset resetTts(app->colabTtsSession());
    QString routeError;
    QVERIFY2(app->colabSeparationSession()->beginVerifiedSession(
        separationWorker.workerUrl(), QStringLiteral("test-token"), QStringLiteral("voice-isolation"),
        separationModel, &routeError, true), qPrintable(routeError));
    QVERIFY2(app->colabSttSession()->beginVerifiedSession(
        sttWorker.workerUrl(), QStringLiteral("test-token"), QStringLiteral("stt"),
        sttModel, &routeError, true), qPrintable(routeError));
    QVERIFY2(app->colabTranslationSession()->beginVerifiedSession(
        translationWorker.workerUrl(), QStringLiteral("test-token"), QStringLiteral("translation"),
        translationModel, &routeError, true), qPrintable(routeError));
    QVERIFY2(app->colabTtsSession()->beginVerifiedSession(
        ttsWorker.workerUrl(), QStringLiteral("test-token"), QStringLiteral("tts"),
        ttsModel, &routeError, true), qPrintable(routeError));
    QTRY_VERIFY(app->colabSeparationSession()->hasVerifiedRoute(
        QStringLiteral("voice-isolation"), separationModel, &routeError));
    QTRY_VERIFY(app->colabSttSession()->hasVerifiedRoute(
        QStringLiteral("stt"), sttModel, &routeError));
    QTRY_VERIFY(app->colabTranslationSession()->hasVerifiedRoute(
        QStringLiteral("translation"), translationModel, &routeError));
    QTRY_VERIFY(app->colabTtsSession()->hasVerifiedRoute(
        QStringLiteral("tts"), ttsModel, &routeError));

    DubbingController controller(app->sttSession(), app->tts(), app->translationEngine(),
                                 app->models(), app->runtimes());
    controller.setRemoteServices(app->settings(), app->colabTranslationSession(),
                                 app->colabTtsSession(), app->colabVoiceCloneSession(),
                                 app->colabSeparationSession(), app->colabAlignmentSession());
    const QVariantMap priorAdaptiveConfiguration = controller.translationFixConfiguration();
    QVERIFY(controller.newProject(directory.filePath(QStringLiteral("direct-colab.ladub.json"))));
    controller.setDubbingQuality(QStringLiteral("fast"));
    QVERIFY(controller.importMedia(mediaPath));
    controller.setSourceLanguage(QStringLiteral("en"));
    controller.setTargetLanguage(QStringLiteral("vi"));
    for (const auto &configuration : {
             qMakePair(QStringLiteral("source-separate"), separationModel),
             qMakePair(QStringLiteral("transcribe"), sttModel),
             qMakePair(QStringLiteral("translate"), translationModel),
             qMakePair(QStringLiteral("synthesize"), ttsModel)}) {
        QVERIFY(controller.setWorkflowNodeParameters(configuration.first, {
            {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
            {QStringLiteral("modelId"), configuration.second}
        }));
    }
    QVERIFY2(controller.automaticPreflight().value(QStringLiteral("ready")).toBool(),
             qPrintable(controller.automaticPreflight().value(QStringLiteral("issues")).toString()));
    QVERIFY(controller.approveAutomaticPreflight());

    WorkflowActivityManager activity(nullptr, nullptr, nullptr, nullptr, &controller);
    QVERIFY(controller.startAutomaticWorkflow(directory.filePath(QStringLiteral("dubbed.mp4"))));
    const QVariantList setupRows = activity.activeWorkflows();
    QVERIFY(!setupRows.isEmpty());
    const QVariantMap setup = setupRows.constFirst().toMap();
    QCOMPARE(setup.value(QStringLiteral("title")).toString(), QStringLiteral("Dubbing — Isolator"));
    QVERIFY(setup.value(QStringLiteral("stageLabel")).toString().contains(QStringLiteral("Isolator (3/8)")));
    QCOMPARE(setup.value(QStringLiteral("executionRoute")).toString(), QStringLiteral("Direct Colab GPU"));

    // This is the regression boundary: with remote-first disabled, the old
    // implementation enqueued the local sherpa runtime and remained in
    // model-setup.  A verified selected worker now advances without a local
    // download or silently changing the route.
    QTRY_VERIFY_WITH_TIMEOUT(!controller.automaticSetupActive(), 3000);
    bool sawDirectPreparation = false;
    for (const QVariant &event : controller.automaticEvents()) {
        const QVariantMap value = event.toMap();
        sawDirectPreparation = sawDirectPreparation
            || (value.value(QStringLiteral("nodeId")).toString() == QStringLiteral("source-separate")
                && value.value(QStringLiteral("message")).toString().contains(QStringLiteral("Direct Colab worker ready")));
    }
    QVERIFY(sawDirectPreparation);
    controller.cancelProcessing();
    controller.setAdaptiveConfiguration(priorAdaptiveConfiguration);
    app->settings()->setRemoteFirstMode(previousRemoteFirst);
}

void TestDubbingProject::independentAuditDirectColabPurgesLocalStateAcrossDubbingStages()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString mediaPath = directory.filePath(QStringLiteral("audit-source.wav"));
    const QVector<float> samples(1600, 0.1F);
    QVERIFY(WavIO::saveFloat(mediaPath, samples.constData(), samples.size(), 16000));

    AppController *app = AppController::instance();
    QVERIFY(app && app->settings());
    const bool previousRemoteFirst = app->settings()->remoteFirstMode();
    app->settings()->setRemoteFirstMode(false);

    const struct Stage {
        QString nodeId;
        QString capability;
        QString model;
        ColabSession *session;
    } stages[] = {
        {QStringLiteral("source-separate"), QStringLiteral("voice-isolation"),
         QStringLiteral("sherpa-onnx-spleeter-2stems-fp16"), app->colabSeparationSession()},
        {QStringLiteral("transcribe"), QStringLiteral("stt"),
         QStringLiteral("whisper.cpp"), app->colabSttSession()},
        {QStringLiteral("translate"), QStringLiteral("translation"),
         QStringLiteral("m2m100-418m"), app->colabTranslationSession()},
        {QStringLiteral("synthesize"), QStringLiteral("tts"),
         QStringLiteral("kokoro"), app->colabTtsSession()},
    };
    for (const Stage &stage : stages) QVERIFY(stage.session != nullptr);
    ExactRouteWorkerMock separationWorker(stages[0].capability, stages[0].model);
    ExactRouteWorkerMock sttWorker(stages[1].capability, stages[1].model);
    ExactRouteWorkerMock translationWorker(stages[2].capability, stages[2].model);
    ExactRouteWorkerMock ttsWorker(stages[3].capability, stages[3].model);
    QVERIFY(separationWorker.start());
    QVERIFY(sttWorker.start());
    QVERIFY(translationWorker.start());
    QVERIFY(ttsWorker.start());
    const ExactRouteWorkerMock *workers[] = {
        &separationWorker, &sttWorker, &translationWorker, &ttsWorker};

    ColabSessionReset resetSeparation(stages[0].session);
    ColabSessionReset resetStt(stages[1].session);
    ColabSessionReset resetTranslation(stages[2].session);
    ColabSessionReset resetTts(stages[3].session);

    // Seed every executable Dubbing stage with the root and nested Local
    // metadata that older projects could carry across a route change.
    DubbingProject project;
    project.projectPath = directory.filePath(QStringLiteral("independent-audit.ladub.json"));
    project.sourceMediaPath = mediaPath;
    // The independent route audit begins after Normalize.  This keeps the
    // fixture focused on the four GPU-capable Dubbing stages instead of the
    // package-only FFmpeg discovery path.
    project.masterAudioPath = mediaPath;
    for (const Stage &stage : stages) {
        project.workflowNodeConfigurations.insert(stage.nodeId, QVariantMap{
            {QStringLiteral("executionProvider"), QStringLiteral("local-dev")},
            {QStringLiteral("modelId"), stage.model},
            {QStringLiteral("familyId"), QStringLiteral("stale-local-family")},
            {QStringLiteral("runtimeId"), QStringLiteral("stale-local-runtime")},
            {QStringLiteral("runtimeVersion"), QStringLiteral("stale-local-version")},
            {QStringLiteral("selectedFiles"), QVariantMap{{QStringLiteral("model"), QStringLiteral("stale.gguf")}}},
            {QStringLiteral("configurationSignature"), QStringLiteral("stale-local-signature")},
            {QStringLiteral("parameters"), QVariantMap{
                {QStringLiteral("executionProvider"), QStringLiteral("local-dev")},
                {QStringLiteral("runtimeId"), QStringLiteral("stale-local-runtime")},
                {QStringLiteral("selectedFiles"), QVariantMap{{QStringLiteral("model"), QStringLiteral("stale.gguf")}}}}}
        });
    }
    QString saveError;
    QVERIFY2(project.save(&saveError), qPrintable(saveError));

    DubbingController controller(app->sttSession(), app->tts(), app->translationEngine(),
                                 app->models(), app->runtimes());
    controller.setRemoteServices(app->settings(), app->colabTranslationSession(),
                                 app->colabTtsSession(), app->colabVoiceCloneSession(),
                                 app->colabSeparationSession(), app->colabAlignmentSession());
    QVERIFY2(controller.openProject(project.projectPath), qPrintable(controller.lastError()));
    controller.setDubbingQuality(QStringLiteral("fast"));
    controller.setSourceLanguage(QStringLiteral("en"));
    controller.setTargetLanguage(QStringLiteral("vi"));

    QString routeError;
    for (int index = 0; index < 4; ++index) {
        const Stage &stage = stages[index];
        QVERIFY2(stage.session->beginVerifiedSession(
            workers[index]->workerUrl(), QStringLiteral("independent-audit-token"),
            stage.capability, stage.model, &routeError, true), qPrintable(routeError));
        QTRY_VERIFY(stage.session->hasVerifiedRoute(stage.capability, stage.model, &routeError));

        QVERIFY(controller.setWorkflowNodeParameters(stage.nodeId, {
            {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
            {QStringLiteral("modelId"), stage.model}
        }));
        const QVariantMap selected = controller.workflowNodeConfigurations().value(stage.nodeId).toMap();
        QCOMPARE(selected.value(QStringLiteral("executionProvider")).toString(),
                 QStringLiteral("colab-direct"));
        QCOMPARE(selected.value(QStringLiteral("modelId")).toString(), stage.model);
        const QVariantMap parameters = selected.value(QStringLiteral("parameters")).toMap();
        QCOMPARE(parameters.value(QStringLiteral("executionProvider")).toString(),
                 QStringLiteral("colab-direct"));
        for (const QString &localKey : {QStringLiteral("familyId"), QStringLiteral("runtimeId"),
                                        QStringLiteral("runtimeVersion"), QStringLiteral("selectedFiles"),
                                        QStringLiteral("configurationSignature")}) {
            QVERIFY(!selected.contains(localKey));
            QVERIFY(!parameters.contains(localKey));
        }
    }

    const QVariantMap preflight = controller.automaticPreflight();
    QVERIFY2(preflight.value(QStringLiteral("ready")).toBool(),
             qPrintable(preflight.value(QStringLiteral("issues")).toString()));
    const QVariantList workersForWorkflow = preflight.value(QStringLiteral("selectedWorkers")).toList();
    QCOMPARE(workersForWorkflow.size(), 4);
    QSet<QString> verifiedNodes;
    for (const QVariant &entry : workersForWorkflow) {
        const QVariantMap worker = entry.toMap();
        QVERIFY(worker.value(QStringLiteral("verified")).toBool());
        QVERIFY(!worker.value(QStringLiteral("capability")).toString().isEmpty());
        QVERIFY(!worker.value(QStringLiteral("modelId")).toString().isEmpty());
        verifiedNodes.insert(worker.value(QStringLiteral("id")).toString());
    }
    const QSet<QString> expectedDirectNodes{
        QStringLiteral("source-separate"), QStringLiteral("transcribe"),
        QStringLiteral("translate"), QStringLiteral("synthesize")};
    QCOMPARE(verifiedNodes, expectedDirectNodes);

    QVERIFY(controller.approveAutomaticPreflight());
    WorkflowActivityManager activity(nullptr, nullptr, nullptr, nullptr, &controller);
    QVERIFY(controller.startAutomaticWorkflow(directory.filePath(QStringLiteral("dubbed.mp4"))));
    const QVariantList activityRows = activity.activeWorkflows();
    QVERIFY(!activityRows.isEmpty());
    QCOMPARE(activityRows.constFirst().toMap().value(QStringLiteral("executionRoute")).toString(),
             QStringLiteral("Direct Colab GPU"));
    // Stop before the external-media stage. The direct worker mocks in this
    // unit test deliberately implement only health/capability verification;
    // actual transfer, cancellation and artifact handling are exercised by
    // the dedicated Colab runner tests below in the audit matrix.
    controller.cancelProcessing();
    QVERIFY(!controller.automaticSetupActive());

    QFile persisted(project.projectPath);
    QVERIFY(persisted.open(QIODevice::ReadOnly));
    const QByteArray savedProject = persisted.readAll();
    QVERIFY(!savedProject.contains("independent-audit-token"));
    for (const Stage &stage : stages)
        QVERIFY(!savedProject.contains(stage.session->endpoint().toString().toUtf8()));
    app->settings()->setRemoteFirstMode(previousRemoteFirst);
}

void TestDubbingProject::independentAuditDirectColabFailureNeverFallsBackToLocal()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString audioPath = directory.filePath(QStringLiteral("source.wav"));
    QVERIFY(writeFixtureFile(audioPath, QByteArrayLiteral("audio fixture")));

    DubbingJobRunner runner(nullptr, nullptr);
    const QVariantMap exactColab{{QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
                                 {QStringLiteral("modelId"), QStringLiteral("sherpa-onnx-spleeter-2stems-fp16")}};
    runner.startSourceSeparation(audioPath, exactColab);
    QVERIFY(!runner.processing());
    QCOMPARE(runner.lastError(),
             QStringLiteral("Connect a Colab GPU worker before running this Voice Isolation node."));

    ColabSession unverifiedSession;
    runner.setRemoteServices(nullptr, nullptr, nullptr, nullptr, &unverifiedSession, nullptr, nullptr);
    runner.startSourceSeparation(audioPath, {{QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
                                             {QStringLiteral("modelId"), QStringLiteral("not-a-colab-model")}});
    QVERIFY(!runner.processing());
    QVERIFY(runner.lastError().contains(QStringLiteral("Select an exact Colab voice-isolation model")));

    runner.startSourceSeparation(audioPath, {{QStringLiteral("executionProvider"), QStringLiteral("api-gateway")},
                                             {QStringLiteral("modelId"), QStringLiteral("anything")}});
    QVERIFY(!runner.processing());
    QCOMPARE(runner.lastError(),
             QStringLiteral("Source separation is not available through API Gateway. Select Local Dev or Colab GPU."));
}

void TestDubbingProject::directColabAdaptiveLlmClearsLocalStateAndNeverFallsBack()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString mediaPath = directory.filePath(QStringLiteral("source.wav"));
    const QVector<float> samples(1600, 0.1F);
    QVERIFY(WavIO::saveFloat(mediaPath, samples.constData(), samples.size(), 16000));

    AppController *app = AppController::instance();
    QVERIFY(app && app->settings());
    const bool previousRemoteFirst = app->settings()->remoteFirstMode();
    app->settings()->setRemoteFirstMode(false);

    const QString separationModel = QStringLiteral("sherpa-onnx-spleeter-2stems-fp16");
    const QString sttModel = QStringLiteral("whisper.cpp");
    const QString translationModel = QStringLiteral("m2m100-418m");
    const QString ttsModel = QStringLiteral("kokoro");
    const QString llmModel = QStringLiteral("qwen3.5-2b");
    ExactRouteWorkerMock separationWorker(QStringLiteral("voice-isolation"), separationModel);
    ExactRouteWorkerMock sttWorker(QStringLiteral("stt"), sttModel);
    ExactRouteWorkerMock translationWorker(QStringLiteral("translation"), translationModel);
    ExactRouteWorkerMock ttsWorker(QStringLiteral("tts"), ttsModel);
    ExactRouteWorkerMock llmWorker(QStringLiteral("llm-chat"), llmModel);
    QVERIFY(separationWorker.start());
    QVERIFY(sttWorker.start());
    QVERIFY(translationWorker.start());
    QVERIFY(ttsWorker.start());
    QVERIFY(llmWorker.start());

    ColabSessionReset resetSeparation(app->colabSeparationSession());
    ColabSessionReset resetStt(app->colabSttSession());
    ColabSessionReset resetTranslation(app->colabTranslationSession());
    ColabSessionReset resetTts(app->colabTtsSession());
    ColabSessionReset resetChat(app->colabChatSession());

    // Reopen a legacy Local selection containing the exact root fields that
    // previously survived a switch to Colab and later triggered a download.
    DubbingProject legacy;
    legacy.projectPath = directory.filePath(QStringLiteral("adaptive-colab.ladub.json"));
    legacy.sourceMediaPath = mediaPath;
    legacy.dubbingQuality = QStringLiteral("adaptive");
    legacy.workflowNodeConfigurations.insert(QStringLiteral("source-separate"), QVariantMap{
        {QStringLiteral("executionProvider"), QStringLiteral("local-dev")},
        {QStringLiteral("modelId"), separationModel},
        {QStringLiteral("familyId"), separationModel},
        {QStringLiteral("runtimeId"), QStringLiteral("sherpa-onnx-win-x86_64-cpu")},
        {QStringLiteral("runtimeVersion"), QStringLiteral("v1.13.4")},
        {QStringLiteral("selectedFiles"), QVariantMap{{QStringLiteral("model"), QStringLiteral("stale.gguf")}}},
        {QStringLiteral("configurationSignature"), QStringLiteral("stale-local-signature")},
        {QStringLiteral("parameters"), QVariantMap{{QStringLiteral("executionProvider"), QStringLiteral("local-dev")}}}
    });
    QString saveError;
    QVERIFY2(legacy.save(&saveError), qPrintable(saveError));

    DubbingController controller(app->sttSession(), app->tts(), app->translationEngine(),
                                 app->models(), app->runtimes());
    controller.setRemoteServices(app->settings(), app->colabTranslationSession(),
                                 app->colabTtsSession(), app->colabVoiceCloneSession(),
                                 app->colabSeparationSession(), app->colabAlignmentSession());
    const QVariantMap priorAdaptiveConfiguration = controller.translationFixConfiguration();
    QVERIFY2(controller.openProject(legacy.projectPath), qPrintable(controller.lastError()));
    controller.setSourceLanguage(QStringLiteral("en"));
    controller.setTargetLanguage(QStringLiteral("vi"));

    QVERIFY(controller.setWorkflowNodeParameters(QStringLiteral("source-separate"), {
        {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
        {QStringLiteral("modelId"), separationModel}
    }));
    const QVariantMap migrated = controller.workflowNodeConfigurations()
        .value(QStringLiteral("source-separate")).toMap();
    QCOMPARE(migrated.value(QStringLiteral("executionProvider")).toString(), QStringLiteral("colab-direct"));
    QCOMPARE(migrated.value(QStringLiteral("modelId")).toString(), separationModel);
    QVERIFY(!migrated.contains(QStringLiteral("familyId")));
    QVERIFY(!migrated.contains(QStringLiteral("runtimeId")));
    QVERIFY(!migrated.contains(QStringLiteral("selectedFiles")));
    QVERIFY(!migrated.contains(QStringLiteral("configurationSignature")));
    const QVariantMap migratedParameters = migrated.value(QStringLiteral("parameters")).toMap();
    QVERIFY(!migratedParameters.contains(QStringLiteral("runtimeId")));
    QVERIFY(!migratedParameters.contains(QStringLiteral("selectedFiles")));

    // Re-selecting an already Direct Colab route must apply the same cleanup.
    // This covers a project that was once saved with a remote root but stale
    // Local runtime metadata still nested in its parameters.
    QVERIFY(controller.setWorkflowNodeParameters(QStringLiteral("source-separate"), {
        {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
        {QStringLiteral("runtimeId"), QStringLiteral("stale-runtime")},
        {QStringLiteral("selectedFiles"), QVariantMap{{QStringLiteral("model"), QStringLiteral("stale.gguf")}}}
    }));
    const QVariantMap reselectedRemote = controller.workflowNodeConfigurations()
        .value(QStringLiteral("source-separate")).toMap();
    QVERIFY(!reselectedRemote.value(QStringLiteral("parameters")).toMap()
                 .contains(QStringLiteral("runtimeId")));
    QVERIFY(!reselectedRemote.value(QStringLiteral("parameters")).toMap()
                 .contains(QStringLiteral("selectedFiles")));

    for (const auto &configuration : {
             qMakePair(QStringLiteral("transcribe"), sttModel),
             qMakePair(QStringLiteral("translate"), translationModel),
             qMakePair(QStringLiteral("synthesize"), ttsModel)}) {
        QVERIFY(controller.setWorkflowNodeParameters(configuration.first, {
            {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
            {QStringLiteral("modelId"), configuration.second}
        }));
    }
    QVERIFY(controller.selectWorkflowColabModel(QStringLiteral("adaptive-llm"), llmModel));

    QString routeError;
    const auto begin = [&routeError](ColabSession *session, ExactRouteWorkerMock &worker,
                                     const QString &capability, const QString &model) {
        return session->beginVerifiedSession(worker.workerUrl(), QStringLiteral("test-token"),
                                             capability, model, &routeError, true);
    };
    QVERIFY2(begin(app->colabSeparationSession(), separationWorker, QStringLiteral("voice-isolation"), separationModel), qPrintable(routeError));
    QVERIFY2(begin(app->colabSttSession(), sttWorker, QStringLiteral("stt"), sttModel), qPrintable(routeError));
    QVERIFY2(begin(app->colabTranslationSession(), translationWorker, QStringLiteral("translation"), translationModel), qPrintable(routeError));
    QVERIFY2(begin(app->colabTtsSession(), ttsWorker, QStringLiteral("tts"), ttsModel), qPrintable(routeError));
    QVERIFY2(begin(app->colabChatSession(), llmWorker, QStringLiteral("llm-chat"), llmModel), qPrintable(routeError));
    QTRY_VERIFY(app->colabChatSession()->hasVerifiedRoute(QStringLiteral("llm-chat"), llmModel, &routeError));
    QTRY_VERIFY(app->colabSeparationSession()->hasVerifiedRoute(QStringLiteral("voice-isolation"), separationModel, &routeError));
    QTRY_VERIFY(app->colabSttSession()->hasVerifiedRoute(QStringLiteral("stt"), sttModel, &routeError));
    QTRY_VERIFY(app->colabTranslationSession()->hasVerifiedRoute(QStringLiteral("translation"), translationModel, &routeError));
    QTRY_VERIFY(app->colabTtsSession()->hasVerifiedRoute(QStringLiteral("tts"), ttsModel, &routeError));

    const QVariantMap adaptive = controller.translationFixConfiguration();
    QCOMPARE(adaptive.value(QStringLiteral("provider")).toString(), QStringLiteral("colab-direct"));
    QCOMPARE(adaptive.value(QStringLiteral("model")).toString(), llmModel);
    QCOMPARE(adaptive.value(QStringLiteral("runtimeId")).toString(), QString());
    QVERIFY(adaptive.value(QStringLiteral("selectedFiles")).toMap().isEmpty());
    QVERIFY(controller.adaptiveReady());

    DubbingProject persisted;
    QString loadError;
    QVERIFY2(DubbingProject::load(legacy.projectPath, persisted, &loadError), qPrintable(loadError));
    const QVariantMap persistedSeparation = persisted.workflowNodeConfigurations
        .value(QStringLiteral("source-separate")).toMap();
    QCOMPARE(persistedSeparation.value(QStringLiteral("executionProvider")).toString(), QStringLiteral("colab-direct"));
    QVERIFY(!persistedSeparation.contains(QStringLiteral("runtimeId")));
    QCOMPARE(persisted.customRewriteConfiguration.value(QStringLiteral("provider")).toString(),
             QStringLiteral("colab-direct"));
    QVERIFY(!persisted.customRewriteConfiguration.contains(QStringLiteral("apiKey")));
    QVERIFY(!persisted.customRewriteConfiguration.contains(QStringLiteral("serverUrl")));

    const QVariantMap preflight = controller.automaticPreflight();
    QVERIFY2(preflight.value(QStringLiteral("ready")).toBool(),
             qPrintable(preflight.value(QStringLiteral("issues")).toString()));
    bool sawAdaptiveWorker = false;
    for (const QVariant &worker : preflight.value(QStringLiteral("selectedWorkers")).toList()) {
        const QVariantMap card = worker.toMap();
        if (card.value(QStringLiteral("id")).toString() == QStringLiteral("adaptive-llm")) {
            sawAdaptiveWorker = true;
            QCOMPARE(card.value(QStringLiteral("parentStageId")).toString(), QStringLiteral("translate"));
            QVERIFY(card.value(QStringLiteral("verified")).toBool());
        }
    }
    QVERIFY(sawAdaptiveWorker);
    QVERIFY(controller.approveAutomaticPreflight());
    QVERIFY(controller.startAutomaticWorkflow(directory.filePath(QStringLiteral("dubbed.mp4"))));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.automaticSetupActive(), 3000);
    for (const QVariant &event : controller.automaticEvents()) {
        QVERIFY(!event.toMap().value(QStringLiteral("message")).toString()
                     .contains(QStringLiteral("Downloading the default Adaptive LLM")));
    }
    controller.cancelProcessing();
    controller.setAdaptiveConfiguration(priorAdaptiveConfiguration);
    app->settings()->setRemoteFirstMode(previousRemoteFirst);
}

void TestDubbingProject::automaticWorkflowRequiresFreshPreflightApproval()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath(QStringLiteral("source.mp4"));
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QVERIFY(media.write("video-placeholder") > 0);
    media.close();

    DubbingController controller(nullptr, nullptr);
    QVERIFY(controller.newProject(dir.filePath(QStringLiteral("project.ladub.json"))));
    QVERIFY(controller.importMedia(mediaPath));
    const QString output = dir.filePath(QStringLiteral("dubbed.mp4"));

    QVERIFY(!controller.startAutomaticWorkflow(output));
    QVERIFY(controller.lastError().contains(QStringLiteral("Automatic preflight")));

    QVERIFY(!controller.approveAutomaticPreflight());
    QVERIFY(controller.setWorkflowNodeParameters(QStringLiteral("translate"), {
        {QStringLiteral("executionProvider"), QStringLiteral("api-gateway")},
        {QStringLiteral("modelId"), QStringLiteral("review-model")}
    }));
    QVERIFY(!controller.startAutomaticWorkflow(output));
    QVERIFY(controller.lastError().contains(QStringLiteral("Automatic preflight")));

    QVERIFY(!controller.approveAutomaticPreflight());
    QVERIFY(!controller.startAutomaticWorkflow(output));
}

void TestDubbingProject::customWorkflowOpensFirstMissingNodeSetup()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath(QStringLiteral("source.mp4"));
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QVERIFY(media.write("video-placeholder") > 0);
    media.close();

    DubbingController controller(nullptr, nullptr);
    QVERIFY(controller.newProject(dir.filePath(QStringLiteral("project.ladub.json"))));
    QVERIFY(controller.importMedia(mediaPath));
    controller.setDubbingQuality(QStringLiteral("custom"));

    const QVariantMap preflight = controller.automaticPreflight();
    QVERIFY(!preflight.value(QStringLiteral("ready")).toBool());
    QVERIFY(preflight.value(QStringLiteral("issues")).toList().constFirst().toMap()
                .value(QStringLiteral("message")).toString().contains(QStringLiteral("Configure")));
    QSignalSpy setupSpy(&controller, &DubbingController::workflowSetupRequired);
    QVERIFY(!controller.startAutomaticWorkflow(
        dir.filePath(QStringLiteral("dubbed.mp4"))));
    QCOMPARE(setupSpy.count(), 0);
    QVERIFY(controller.lastError().contains(QStringLiteral("Automatic preflight")));
    QVERIFY(!controller.processing());
    QCOMPARE(controller.workflowMode(), QStringLiteral("idle"));
}

void TestDubbingProject::qualityModesExposeExpectedDefaultVoiceModel()
{
    DubbingController controller(nullptr, nullptr);
    QCOMPARE(controller.defaultWorkflowModelFamily(QStringLiteral("synthesize")),
             QStringLiteral("omnivoice"));

    controller.setDubbingQuality(QStringLiteral("fast"));
    QCOMPARE(controller.defaultWorkflowModelFamily(QStringLiteral("synthesize")),
             QStringLiteral("vieneu-tts-v2-turbo"));
    for (const QVariant &nodeValue : controller.workflowNodes()) {
        const QVariantMap node = nodeValue.toMap();
        if (node.value(QStringLiteral("id")).toString() == QStringLiteral("synthesize"))
            QCOMPARE(node.value(QStringLiteral("defaultFamilyId")).toString(),
                     QStringLiteral("vieneu-tts-v2-turbo"));
    }

    controller.setDubbingQuality(QStringLiteral("adaptive"));
    QCOMPARE(controller.defaultWorkflowModelFamily(QStringLiteral("synthesize")),
             QStringLiteral("omnivoice"));
}

void TestDubbingProject::standardModesPreserveExplicitNodeModelsOnOpen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QVariantMap savedNode{
        {QStringLiteral("familyId"), QStringLiteral("manually-selected")},
        {QStringLiteral("runtimeId"), QStringLiteral("runtime")}
    };

    DubbingProject adaptive;
    adaptive.projectPath = dir.filePath(QStringLiteral("adaptive.ladub.json"));
    adaptive.dubbingQuality = QStringLiteral("adaptive");
    adaptive.workflowNodeConfigurations.insert(QStringLiteral("synthesize"), savedNode);
    QString error;
    QVERIFY2(adaptive.save(&error), qPrintable(error));

    DubbingController controller(nullptr, nullptr);
    QVERIFY(controller.openProject(adaptive.projectPath));
    QCOMPARE(controller.workflowNodeConfigurations()
                 .value(QStringLiteral("synthesize")).toMap()
                 .value(QStringLiteral("familyId")).toString(),
             QStringLiteral("manually-selected"));

    DubbingProject custom = adaptive;
    custom.projectPath = dir.filePath(QStringLiteral("custom.ladub.json"));
    custom.dubbingQuality = QStringLiteral("custom");
    QVERIFY2(custom.save(&error), qPrintable(error));
    QVERIFY(controller.openProject(custom.projectPath));
    QCOMPARE(controller.workflowNodeConfigurations()
                 .value(QStringLiteral("synthesize")).toMap()
                 .value(QStringLiteral("familyId")).toString(),
             QStringLiteral("manually-selected"));
}

void TestDubbingProject::sourceSeparationExposesModelSelection()
{
    DubbingController controller(nullptr, nullptr);
    QVariantMap sourceSeparationNode;
    for (const QVariant &value : controller.workflowNodes()) {
        const QVariantMap node = value.toMap();
        if (node.value(QStringLiteral("id")).toString() == QStringLiteral("source-separate")) {
            sourceSeparationNode = node;
            break;
        }
    }

    QVERIFY(!sourceSeparationNode.isEmpty());
    QVERIFY(sourceSeparationNode.value(QStringLiteral("configurable")).toBool());
    QCOMPARE(sourceSeparationNode.value(QStringLiteral("capabilityId")).toString(),
             QStringLiteral("voice-isolation"));
}

void TestDubbingProject::workflowStagesExposeEightProductionBackedSteps()
{
    DubbingController controller(nullptr, nullptr);
    const QVariantList stages = controller.workflowStages();
    QCOMPARE(stages.size(), 8);
    const QStringList expectedIds{
        QStringLiteral("import"), QStringLiteral("normalize"), QStringLiteral("isolator"),
        QStringLiteral("transcribe"), QStringLiteral("alignment-subtitle"),
        QStringLiteral("translate"), QStringLiteral("tts"), QStringLiteral("export")};
    const QStringList expectedTitles{
        QStringLiteral("Import/Download"), QStringLiteral("Normalize"), QStringLiteral("Isolator"),
        QStringLiteral("Transcribe/STT"), QStringLiteral("Alignment/Subtitle"),
        QStringLiteral("Translate"), QStringLiteral("TTS"), QStringLiteral("Export/Output")};
    QSet<QString> productionNodes;
    for (int index = 0; index < stages.size(); ++index) {
        const QVariantMap stage = stages.at(index).toMap();
        QCOMPARE(stage.value(QStringLiteral("id")).toString(), expectedIds.at(index));
        QCOMPARE(stage.value(QStringLiteral("title")).toString(), expectedTitles.at(index));
        QVERIFY2(!stage.value(QStringLiteral("productionNodeIds")).toList().isEmpty(),
                 "Every presentation stage must remain backed by a production node.");
        QVERIFY(!stage.value(QStringLiteral("actionNodeId")).toString().isEmpty());
        for (const QVariant &node : stage.value(QStringLiteral("productionNodeIds")).toList())
            QVERIFY2(!productionNodes.contains(node.toString()), "A production node may have only one presentation parent.");
        for (const QVariant &node : stage.value(QStringLiteral("productionNodeIds")).toList())
            productionNodes.insert(node.toString());
    }
    QCOMPARE(productionNodes.size(), controller.workflowNodes().size());
    const QVariantMap alignment = stages.at(4).toMap();
    QVERIFY(alignment.value(QStringLiteral("productionNodeIds")).toList()
                .contains(QStringLiteral("review-transcript")));
    QVERIFY(alignment.value(QStringLiteral("productionNodeIds")).toList()
                .contains(QStringLiteral("fit-timing")));
    const QVariantMap exportStage = stages.at(7).toMap();
    QVERIFY(exportStage.value(QStringLiteral("productionNodeIds")).toList()
                .contains(QStringLiteral("mix")));
}

void TestDubbingProject::colabSourceSeparationDoesNotFallbackToLocal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audioPath = dir.filePath(QStringLiteral("source.wav"));
    QFile file(audioPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("audio-placeholder") > 0);
    file.close();

    DubbingJobRunner runner(nullptr, nullptr);
    runner.startSourceSeparation(audioPath,
        QVariantMap{{QStringLiteral("executionProvider"), QStringLiteral("colab-direct")}});

    QVERIFY(!runner.processing());
    QCOMPARE(runner.lastError(),
             QStringLiteral("Connect a Colab GPU worker before running this Voice Isolation node."));
}

void TestDubbingProject::dubbingDirectColabVoiceCloneReusesProfileAcrossSegments()
{
    DubbingVoiceCloneWorkerMock worker;
    QVERIFY(worker.start());
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString referencePath = dir.filePath(QStringLiteral("owned-reference.wav"));
    QVector<float> referenceSamples(24000 * 3, 0.05F);
    QVERIFY(WavIO::saveFloat(referencePath, referenceSamples.constData(), referenceSamples.size(), 24000));

    const QVariantList segments{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-1")},
                    {QStringLiteral("speakerId"), QStringLiteral("speaker-a")},
                    {QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 1000},
                    {QStringLiteral("targetText"), QStringLiteral("First dubbed segment")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-2")},
                    {QStringLiteral("speakerId"), QStringLiteral("speaker-b")},
                    {QStringLiteral("startMs"), 1000}, {QStringLiteral("endMs"), 2000},
                    {QStringLiteral("targetText"), QStringLiteral("Second dubbed segment")}}
    };
    const auto settings = [&referencePath](const QString &presetId, const QString &cloneModel) {
        return QVariantMap{{QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
                           {QStringLiteral("modelId"), QStringLiteral("kokoro")},
                           {QStringLiteral("lang"), QStringLiteral("vi")},
                           {QStringLiteral("voiceCloningEnabled"), true},
                           {QStringLiteral("voiceCloneConsentConfirmed"), true},
                           {QStringLiteral("voiceCloneModelId"), cloneModel},
                           {QStringLiteral("cloneVoicePreset"),
                            QVariantMap{{QStringLiteral("id"), presetId},
                                        {QStringLiteral("name"), QStringLiteral("Owned saved voice")},
                                        {QStringLiteral("familyId"), cloneModel},
                                        {QStringLiteral("audioPath"), referencePath},
                                        {QStringLiteral("referenceText"), QStringLiteral("Exact owned transcript")}}}};
    };

    ColabSession session;
    QString error;
    QVERIFY(session.setSession(worker.workerUrl(), QStringLiteral("loopback-token"), &error, true));
    DubbingSynthesisJob job(nullptr);
    job.setRemoteServices(nullptr, nullptr, &session);
    QSignalSpy completed(&job, &DubbingSynthesisJob::completed);
    QSignalSpy failures(&job, &DubbingSynthesisJob::failed);
    const QString projectPath = dir.filePath(QStringLiteral("project.ladub.json"));

    QVERIFY(job.start(segments, projectPath, settings(QStringLiteral("preset-a"), QStringLiteral("omnivoice")),
                       QStringLiteral("first-run")));
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 10000);
    QCOMPARE(failures.count(), 0);
    QCOMPARE(worker.profileCreations(), 1);
    QCOMPARE(worker.generationCreations(), 2);
    QCOMPARE(worker.profileRequests().size(), 1);
    QVERIFY(worker.profileRequests().constFirst().contains("omnivoice"));
    QVERIFY(worker.profileRequests().constFirst().contains("Exact owned transcript"));
    QCOMPARE(worker.generationRequests().size(), 2);
    for (const QByteArray &request : worker.generationRequests()) {
        QVERIFY(request.contains("\"profile_id\":\"profile-1\""));
        QVERIFY(request.contains("\"language\":\"vi\""));
        QVERIFY(request.contains("\"model\":\"omnivoice\""));
    }
    const QVariantList firstResult = completed.constFirst().constFirst().toList();
    QCOMPARE(firstResult.size(), 2);
    for (const QVariant &entry : firstResult) {
        const QVariantMap segment = entry.toMap();
        QCOMPARE(segment.value(QStringLiteral("cloneVoicePresetId")).toString(), QStringLiteral("preset-a"));
        QCOMPARE(segment.value(QStringLiteral("voiceReferencePath")).toString(),
                 QFileInfo(referencePath).absoluteFilePath());
        QCOMPARE(segment.value(QStringLiteral("voiceReferenceText")).toString(),
                 QStringLiteral("Exact owned transcript"));
    }

    // Exact model changes are not compatible with an in-memory worker profile.
    QVERIFY(job.start(segments, projectPath, settings(QStringLiteral("preset-a"), QStringLiteral("voxcpm2")),
                       QStringLiteral("model-change")));
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 2, 10000);
    QCOMPARE(worker.profileCreations(), 2);
    QCOMPARE(worker.generationCreations(), 4);
    QVERIFY(worker.profileRequests().constLast().contains("voxcpm2"));

    // A changed worker session during a job must fail/cancel rather than use
    // the old profile. Re-running after reconnect creates a fresh profile.
    worker.setHoldGeneration(true);
    QVERIFY(job.start(segments, projectPath, settings(QStringLiteral("preset-a"), QStringLiteral("voxcpm2")),
                       QStringLiteral("session-change")));
    QTRY_COMPARE_WITH_TIMEOUT(worker.generationCreations(), 5, 5000);
    QVERIFY(session.setSession(worker.workerUrl(), QStringLiteral("loopback-token-reconnected"), &error, true));
    QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 5000);
    QVERIFY(failures.constFirst().at(0).toString().contains(
        QStringLiteral("worker session changed while voice cloning")));
    worker.setHoldGeneration(false);
    // The runner is synchronous on its own thread. Let the cancelled remote
    // request observe its token and drain before queueing the next run; this
    // mirrors the UI's terminal-error boundary rather than racing an abort
    // against a newly queued request on the same worker object.
    QTest::qWait(600);
    QVERIFY(job.start(segments, projectPath, settings(QStringLiteral("preset-a"), QStringLiteral("voxcpm2")),
                       QStringLiteral("after-reconnect")));
    QTRY_VERIFY_WITH_TIMEOUT(completed.count() == 3 || failures.count() > 1 || !job.running(), 10000);
    QVERIFY2(completed.count() == 3,
             qPrintable(QStringLiteral("recovery completed=%1 failures=%2 profiles=%3 generations=%4 running=%5 lastError=%6")
                            .arg(completed.count()).arg(failures.count())
                            .arg(worker.profileCreations()).arg(worker.generationCreations())
                            .arg(job.running())
                            .arg(failures.size() > 1 ? failures.constLast().at(0).toString()
                                                     : QStringLiteral("<none>"))));
    QCOMPARE(worker.profileCreations(), 3);
    QCOMPARE(failures.count(), 1);
}

void TestDubbingProject::unavailableLocalSourceSeparationDoesNotUseOriginalAudio()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audioPath = dir.filePath(QStringLiteral("source.wav"));
    QFile file(audioPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("audio-placeholder") > 0);
    file.close();

    DubbingJobRunner runner(nullptr, nullptr);
    QSignalSpy completed(&runner, &DubbingJobRunner::stageCompleted);
    QSignalSpy separationFinished(&runner, &DubbingJobRunner::sourceSeparationFinished);
    runner.startSourceSeparation(audioPath,
        QVariantMap{{QStringLiteral("executionProvider"), QStringLiteral("local-dev")}});

    QVERIFY(!runner.processing());
    QCOMPARE(completed.count(), 0);
    QCOMPARE(separationFinished.count(), 0);
    QVERIFY(runner.lastError().contains(QStringLiteral("will not be used as a substitute")));
}

void TestDubbingProject::failedSeparationBackendDoesNotUseOriginalAudio()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audioPath = dir.filePath(QStringLiteral("source.wav"));
    const QVector<float> samples(1600, 0.1F);
    QVERIFY(WavIO::saveFloat(audioPath, samples.constData(), samples.size(), 16000));
    const QString fakeRuntime = dir.filePath(QStringLiteral("fake-sherpa.dll"));
    const QString fakeModel = dir.filePath(QStringLiteral("fake-model.onnx"));
    for (const QString &path : {fakeRuntime, fakeModel}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("not-a-runtime") > 0);
    }
    const ScopedEnvironmentValue runtimeOverride("SHERPA_ONNX_RUNTIME", fakeRuntime.toUtf8());
    const ScopedEnvironmentValue modelOverride("SHERPA_ONNX_UVR_MODEL", fakeModel.toUtf8());

    DubbingJobRunner runner(nullptr, nullptr);
    QSignalSpy completed(&runner, &DubbingJobRunner::stageCompleted);
    QSignalSpy separationFinished(&runner, &DubbingJobRunner::sourceSeparationFinished);
    runner.startSourceSeparation(audioPath,
        QVariantMap{{QStringLiteral("executionProvider"), QStringLiteral("local-dev")}});
    QTRY_VERIFY_WITH_TIMEOUT(!runner.processing(), 5000);
    QCOMPARE(completed.count(), 0);
    QCOMPARE(separationFinished.count(), 0);
    QVERIFY(!runner.lastError().isEmpty());
}

void TestDubbingProject::incompleteSeparationStemsDoNotCompleteTheNode()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audioPath = dir.filePath(QStringLiteral("source.wav"));
    const QVector<float> samples(1600, 0.1F);
    QVERIFY(WavIO::saveFloat(audioPath, samples.constData(), samples.size(), 16000));
    const QString fakeRuntime = dir.filePath(QStringLiteral("fake-sherpa.dll"));
    const QString fakeModel = dir.filePath(QStringLiteral("fake-model.onnx"));
    const QString vocalsPath = dir.filePath(QStringLiteral("vocals.wav"));
    for (const QString &path : {fakeRuntime, fakeModel, vocalsPath}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("not-a-runtime") > 0);
    }
    const ScopedEnvironmentValue runtimeOverride("SHERPA_ONNX_RUNTIME", fakeRuntime.toUtf8());
    const ScopedEnvironmentValue modelOverride("SHERPA_ONNX_UVR_MODEL", fakeModel.toUtf8());

    DubbingJobRunner runner(nullptr, nullptr);
    QSignalSpy completed(&runner, &DubbingJobRunner::stageCompleted);
    QSignalSpy separationFinished(&runner, &DubbingJobRunner::sourceSeparationFinished);
    runner.startSourceSeparation(audioPath,
        QVariantMap{{QStringLiteral("executionProvider"), QStringLiteral("local-dev")}});
    QVERIFY(runner.processing());

    SeparationResult incomplete;
    incomplete.success = true;
    incomplete.stems.append({QStringLiteral("vocals"), vocalsPath, 16000, 1});
    QVERIFY(QMetaObject::invokeMethod(&runner, "onSourceSeparationFinished", Qt::DirectConnection,
                                      Q_ARG(LAStudio::SeparationResult, incomplete)));
    QVERIFY(!runner.processing());
    QCOMPARE(completed.count(), 0);
    QCOMPARE(separationFinished.count(), 0);
    QVERIFY(runner.lastError().contains(QStringLiteral("both required vocals and background stems")));
}

void TestDubbingProject::dubbingRejectsAConnectedColabWorkerForTheWrongModel()
{
    ExactRouteWorkerMock worker(QStringLiteral("tts"), QStringLiteral("kokoro"));
    QVERIFY(worker.start());

    ColabSession session;
    QSignalSpy verification(&session, &ColabSession::verificationFinished);
    QString error;
    QVERIFY2(session.beginVerifiedSession(worker.workerUrl(), QStringLiteral("test-token"),
                                          QStringLiteral("tts"), QStringLiteral("kokoro"),
                                          &error, true), qPrintable(error));
    QTRY_COMPARE(verification.count(), 1);
    QVERIFY(verification.constFirst().at(0).toBool());

    const QVariantList segments{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-1")},
                    {QStringLiteral("targetText"), QStringLiteral("Xin chao")},
                    {QStringLiteral("startMs"), 0},
                    {QStringLiteral("endMs"), 1000}}
    };
    DubbingSynthesisJob job(nullptr);
    job.setRemoteServices(nullptr, &session, nullptr);
    QSignalSpy failures(&job, &DubbingSynthesisJob::failed);
    QVERIFY(!job.start(segments, QStringLiteral("C:/temp/project.ladub.json"),
                        QVariantMap{{QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
                                    {QStringLiteral("modelId"), QStringLiteral("vibevoice")}},
                        QStringLiteral("wrong-model")));
    QCOMPARE(failures.count(), 1);
    const QString message = failures.constFirst().at(0).toString();
    QVERIFY(message.contains(QStringLiteral("Wrong Colab worker")));
    QVERIFY(message.contains(QStringLiteral("tts / vibevoice")));
    QVERIFY(message.contains(QStringLiteral("tts / kokoro")));
}

void TestDubbingProject::remoteDubbingWorkflowIsReadyWithoutLocalModels()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath(QStringLiteral("source.wav"));
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QVERIFY(media.write("audio-placeholder") > 0);
    media.close();

    const auto remoteNode = [](const QString &provider, const QString &modelId) {
        const QVariantMap parameters{{QStringLiteral("executionProvider"), provider},
                                     {QStringLiteral("modelId"), modelId}};
        return QVariantMap{{QStringLiteral("executionProvider"), provider},
                           {QStringLiteral("modelId"), modelId},
                           {QStringLiteral("parameters"), parameters}};
    };
    DubbingProject project;
    project.projectPath = dir.filePath(QStringLiteral("remote.ladub.json"));
    project.sourceMediaPath = mediaPath;
    project.targetLanguage = QStringLiteral("vi");
    project.dubbingQuality = QStringLiteral("custom");
    project.durationControl.insert(QStringLiteral("enabled"), false);
    project.durationControl.insert(QStringLiteral("autoRewrite"), false);
    project.workflowNodeConfigurations.insert(
        QStringLiteral("source-separate"), remoteNode(
            QStringLiteral("colab-direct"),
            QStringLiteral("sherpa-onnx-spleeter-2stems-fp16")));
    project.workflowNodeConfigurations.insert(
        QStringLiteral("transcribe"), remoteNode(
            // Simulate an older workflow template after the project has already
            // persisted its actual transcript route in transcriptConfiguration.
            QStringLiteral("local-dev"), QString()));
    project.transcriptConfiguration.insert(QStringLiteral("transcriptSource"), QStringLiteral("stt"));
    project.transcriptConfiguration.insert(QStringLiteral("sttExecutionProvider"),
                                           QStringLiteral("colab-direct"));
    project.transcriptConfiguration.insert(QStringLiteral("sttModelId"),
                                           QStringLiteral("whisper.cpp"));
    project.workflowNodeConfigurations.insert(
        QStringLiteral("translate"), remoteNode(QStringLiteral("api-gateway"), QStringLiteral("gateway-translate")));
    project.workflowNodeConfigurations.insert(
        QStringLiteral("synthesize"), remoteNode(QStringLiteral("colab-direct"), QStringLiteral("kokoro")));
    QString error;
    QVERIFY2(project.save(&error), qPrintable(error));

    DubbingController controller(nullptr, nullptr);
    QVERIFY2(controller.openProject(project.projectPath), qPrintable(controller.lastError()));
    QVERIFY(controller.workflowReady());
}

void TestDubbingProject::dubbingColabModelsMapToExactNotebooks()
{
    const QStringList nodes{
        QStringLiteral("source-separate"),
        QStringLiteral("transcribe"),
        QStringLiteral("subtitle-ocr"),
        QStringLiteral("translate"),
        QStringLiteral("synthesize"),
        QStringLiteral("alignment")
    };
    int routeCount = 0;
    for (const QString &nodeId : nodes) {
        const QVariantList options =
            DubbingColabModelRoutes::optionsForNode(nodeId);
        QVERIFY2(!options.isEmpty(), qPrintable(nodeId));
        const QString defaultModel =
            DubbingColabModelRoutes::defaultModelForNode(nodeId);
        QVERIFY2(DubbingColabModelRoutes::supports(nodeId, defaultModel),
                 qPrintable(nodeId));
        for (const QVariant &entry : options) {
            const QVariantMap option = entry.toMap();
            const QString model =
                option.value(QStringLiteral("modelId")).toString();
            const QString notebook =
                option.value(QStringLiteral("notebook")).toString();
            QVERIFY2(!model.isEmpty(), qPrintable(nodeId));
            QVERIFY2(notebook.startsWith(QStringLiteral("LA_STUDIO_"))
                         && notebook.endsWith(QStringLiteral("_GPU.ipynb")),
                     qPrintable(notebook));
            QCOMPARE(DubbingColabModelRoutes::notebookForModel(nodeId, model),
                     notebook);
            QVERIFY2(QFileInfo(QStringLiteral(LASTUDIO_SOURCE_DIR)
                               + QStringLiteral("/notebooks/") + notebook).isFile(),
                     qPrintable(notebook));
            ++routeCount;
        }
    }
    QCOMPARE(routeCount, 22);
    QVERIFY(DubbingColabModelRoutes::notebookForModel(
                QStringLiteral("transcribe"),
                QStringLiteral("not-a-model")).isEmpty());
}

void TestDubbingProject::dubbingUiUsesExactModelWorkers()
{
    QFile settingsPanel(
        QStringLiteral(LASTUDIO_SOURCE_DIR)
        + QStringLiteral("/qml/components/dubbing/DubbingNodeSettingsPanel.qml"));
    QVERIFY(settingsPanel.open(QIODevice::ReadOnly));
    const QString settingsSource = QString::fromUtf8(settingsPanel.readAll());
    QVERIFY(settingsSource.contains(
        QStringLiteral("dubbing.colabModelOptionsForNode(root.nodeId)")));
    QVERIFY(settingsSource.contains(
        QStringLiteral("dubbing.selectWorkflowColabModel(root.nodeId")));
    QVERIFY(settingsSource.contains(
        QStringLiteral("dubbing.colabNotebookForNode(root.nodeId")));
    QVERIFY(!settingsSource.contains(
        QStringLiteral("LA_STUDIO_SPEECH_GPU.ipynb")));
    QVERIFY(!settingsSource.contains(
        QStringLiteral("LA_STUDIO_LANGUAGE_GPU.ipynb")));
    QVERIFY(!settingsSource.contains(
        QStringLiteral("LA_STUDIO_VOICE_GPU.ipynb")));

    QFile inspector(
        QStringLiteral(LASTUDIO_SOURCE_DIR)
        + QStringLiteral("/qml/components/dubbing/DubbingNodeInspector.qml"));
    QVERIFY(inspector.open(QIODevice::ReadOnly));
    const QString inspectorSource = QString::fromUtf8(inspector.readAll());
    QVERIFY(inspectorSource.contains(QStringLiteral("TTS / Text to Speech")));
    QVERIFY(inspectorSource.contains(QStringLiteral("dubbing.ttsVoiceOptions")));
    QVERIFY(inspectorSource.contains(QStringLiteral("dubbing.selectTtsVoice")));
    QVERIFY(!inspectorSource.contains(QStringLiteral("Create or import clone voice")));
    QVERIFY(inspectorSource.contains(
        QStringLiteral("Saved voice-design presets are not available for Dubbing yet.")));
    QVERIFY(inspectorSource.contains(
        QStringLiteral("will not silently substitute a designed voice")));
    QVERIFY(!inspectorSource.contains(
        QStringLiteral("Auto-select a clean voice reference")));
    QVERIFY(inspectorSource.contains(
        QStringLiteral("\"alignment\", root.alignmentModelId")));
    QVERIFY(inspectorSource.contains(
        QStringLiteral("AppController.colabAlignmentSession.connectTemporaryWorker")));

    QFile colabSetup(
        QStringLiteral(LASTUDIO_SOURCE_DIR)
        + QStringLiteral("/qml/components/dubbing/DubbingColabSetupDialog.qml"));
    QVERIFY(colabSetup.open(QIODevice::ReadOnly));
    const QString colabSetupSource = QString::fromUtf8(colabSetup.readAll());
    QVERIFY(colabSetupSource.contains(
        QStringLiteral("dubbing.connectWorkflowColabStage")));
    QVERIFY(colabSetupSource.contains(
        QStringLiteral("dubbing.checkWorkflowColabStage")));
    QVERIFY(colabSetupSource.contains(
        QStringLiteral("dubbing.validateAllWorkflowColabStages")));
    QVERIFY(colabSetupSource.contains(
        QStringLiteral("ColabSessionStatus")));
    QVERIFY(colabSetupSource.contains(
        QStringLiteral("subtitle-ocr\") return AppController.colabSubtitleOcrSession")));
    QVERIFY(colabSetupSource.contains(
        QStringLiteral("onCheckRequested:")));
    QVERIFY(colabSetupSource.contains(
        QStringLiteral("stageCard.setupDialog.dubbing.checkWorkflowColabStage(stageCard.stageId)")));
    QVERIFY(colabSetupSource.contains(QStringLiteral("id: stageRepeater")));
    QVERIFY(colabSetupSource.contains(QStringLiteral("readonly property var setupDialog: stageRepeater.setupDialog")));

    QFile qualityDialog(
        QStringLiteral(LASTUDIO_SOURCE_DIR)
        + QStringLiteral("/qml/components/dubbing/DubbingQualityDialog.qml"));
    QVERIFY(qualityDialog.open(QIODevice::ReadOnly));
    const QString qualityDialogSource = QString::fromUtf8(qualityDialog.readAll());
    QVERIFY(!qualityDialogSource.contains(QStringLiteral("root.modelField")));
    QVERIFY(qualityDialogSource.contains(QStringLiteral("modelField.text = \"qwen3.5-2b\"")));

    QFile automaticPreflight(
        QStringLiteral(LASTUDIO_SOURCE_DIR)
        + QStringLiteral("/qml/components/dubbing/DubbingAutomaticPreflightDialog.qml"));
    QVERIFY(automaticPreflight.open(QIODevice::ReadOnly));
    const QString automaticPreflightSource = QString::fromUtf8(automaticPreflight.readAll());
    QVERIFY(automaticPreflightSource.contains(QStringLiteral("Source & language")));
    QVERIFY(automaticPreflightSource.contains(QStringLiteral("Stages, routes & models")));
    QVERIFY(automaticPreflightSource.contains(QStringLiteral("Colab workers")));
    QVERIFY(automaticPreflightSource.contains(QStringLiteral("Start Automatic Dubbing")));
    QVERIFY(automaticPreflightSource.contains(QStringLiteral("approveAutomaticPreflight")));
    QVERIFY(automaticPreflightSource.contains(QStringLiteral("fixed notebook config")));
    QVERIFY(automaticPreflightSource.contains(QStringLiteral("preflight.stages")));
    QVERIFY(!automaticPreflightSource.contains(QStringLiteral("workflow default")));
    QVERIFY(automaticPreflightSource.contains(QStringLiteral("required property int index")));

    QFile dubbingController(
        QStringLiteral(LASTUDIO_SOURCE_DIR)
        + QStringLiteral("/src/controllers/dubbing/DubbingController.cpp"));
    QVERIFY(dubbingController.open(QIODevice::ReadOnly));
    const QString controllerSource = QString::fromUtf8(dubbingController.readAll());
    QVERIFY(controllerSource.contains(
        QStringLiteral("snapshotSelectedColabStagesForWorkflow")));
    QVERIFY(controllerSource.contains(
        QStringLiteral("hasVerifiedRoute(capability, model")));
    QVERIFY(controllerSource.contains(
        QStringLiteral("m_automaticPreflightFingerprint")));
    QVERIFY(controllerSource.contains(
        QStringLiteral("automaticPreflightFingerprint")));

    QFile voiceLibrary(
        QStringLiteral(LASTUDIO_SOURCE_DIR)
        + QStringLiteral("/qml/components/shared/VoiceLibraryDialog.qml"));
    QVERIFY(voiceLibrary.open(QIODevice::ReadOnly));
    const QString voiceLibrarySource = QString::fromUtf8(voiceLibrary.readAll());
    QVERIFY(voiceLibrarySource.contains(QStringLiteral("validationError")));
    QVERIFY(voiceLibrarySource.contains(QStringLiteral("modelData.valid !== false")));

    QFile synthesisJob(
        QStringLiteral(LASTUDIO_SOURCE_DIR)
        + QStringLiteral("/src/controllers/dubbing/DubbingSynthesisJob.cpp"));
    QVERIFY(synthesisJob.open(QIODevice::ReadOnly));
    const QString synthesisSource = QString::fromUtf8(synthesisJob.readAll());
    QVERIFY(synthesisSource.contains(
        QStringLiteral("request.model = model;")));
    QVERIFY(synthesisSource.contains(
        QStringLiteral("savedTtsVoicePreset")));
    QVERIFY(synthesisSource.contains(
        QStringLiteral("effectiveVoiceCloneModel")));
    QVERIFY(synthesisSource.contains(
        QStringLiteral("%1|%2|%3|%4|%5|%6")));
    QVERIFY(synthesisSource.contains(
        QStringLiteral("m_colabVoiceProfileId.clear")));
    QVERIFY(!synthesisSource.contains(
        QStringLiteral("cloneVoiceProfileId")));

    QFile transcriptionJob(
        QStringLiteral(LASTUDIO_SOURCE_DIR)
        + QStringLiteral("/src/controllers/dubbing/DubbingTranscriptionJob.cpp"));
    QVERIFY(transcriptionJob.open(QIODevice::ReadOnly));
    const QString transcriptionSource =
        QString::fromUtf8(transcriptionJob.readAll());
    QVERIFY(transcriptionSource.contains(
        QStringLiteral("refineAlignmentWithColab")));
    QVERIFY(transcriptionSource.contains(
        QStringLiteral("completeWithoutAlignment")));
    QVERIFY(transcriptionSource.contains(
        QStringLiteral("beginTranscriptionAfterInputReady")));
    QVERIFY(transcriptionSource.contains(
        QStringLiteral("m_inputLoadStarted = true")));
    QVERIFY(transcriptionSource.contains(
        QStringLiteral("m_inputLoadStarted = false")));

    QFile dubbingPage(
        QStringLiteral(LASTUDIO_SOURCE_DIR)
        + QStringLiteral("/qml/pages/DubbingPage.qml"));
    QVERIFY(dubbingPage.open(QIODevice::ReadOnly));
    const QString dubbingPageSource = QString::fromUtf8(dubbingPage.readAll());
    QVERIFY(dubbingPageSource.contains(
        QStringLiteral("objectName: \"dubbingTranscriptSourceMode\"")));
    QVERIFY(dubbingPageSource.contains(QStringLiteral("{ id: \"stt\"")));
    QVERIFY(dubbingPageSource.contains(QStringLiteral("{ id: \"ocr\"")));
    QVERIFY(dubbingPageSource.contains(QStringLiteral("{ id: \"stt+ocr\"")));
    QVERIFY(dubbingPageSource.contains(
        QStringLiteral("dubbing.resolveTranscriptConflict(index, \"stt\")")));
    QVERIFY(dubbingPageSource.contains(
        QStringLiteral("dubbing.resolveTranscriptConflict(index, \"ocr\")")));

    QFile transcriptionRunner(
        QStringLiteral(LASTUDIO_SOURCE_DIR)
        + QStringLiteral("/src/controllers/dubbing/DubbingJobRunner.cpp"));
    QVERIFY(transcriptionRunner.open(QIODevice::ReadOnly));
    const QString runnerSource = QString::fromUtf8(transcriptionRunner.readAll());
    QVERIFY(runnerSource.contains(
        QStringLiteral("setSubtitleOcrController")));
    QVERIFY(runnerSource.contains(
        QStringLiteral("Subtitle OCR controller is unavailable.")));
    QVERIFY(runnerSource.contains(
        QStringLiteral("DubbingTranscriptFusionService::fuse")));
}

void TestDubbingProject::dubbingEntryAndAutomaticSetupCannotBypassConfiguration()
{
    QFile entryGate(QStringLiteral(LASTUDIO_SOURCE_DIR)
                    + QStringLiteral("/qml/components/dubbing/DubbingEntryGateDialog.qml"));
    QVERIFY(entryGate.open(QIODevice::ReadOnly));
    const QString gate = QString::fromUtf8(entryGate.readAll());
    QVERIFY(gate.contains(QStringLiteral("closePolicy: Popup.NoAutoClose")));
    QVERIFY(gate.contains(QStringLiteral("Leave Dubbing")));
    QVERIFY(gate.contains(QStringLiteral("Automatic")));
    QVERIFY(gate.contains(QStringLiteral("Review one by one")));
    QVERIFY(!gate.contains(QStringLiteral("name: \"close\"")));

    QFile preflight(QStringLiteral(LASTUDIO_SOURCE_DIR)
                    + QStringLiteral("/qml/components/dubbing/DubbingAutomaticPreflightDialog.qml"));
    QVERIFY(preflight.open(QIODevice::ReadOnly));
    const QString wizard = QString::fromUtf8(preflight.readAll());
    QVERIFY(wizard.contains(QStringLiteral("closePolicy: Popup.NoAutoClose")));
    QVERIFY(wizard.contains(QStringLiteral("Back to mode selection")));
    QVERIFY(wizard.contains(QStringLiteral("Spoken/source language *")));
    QVERIFY(wizard.contains(QStringLiteral("Output/target language *")));
    QVERIFY(wizard.contains(QStringLiteral("root.dubbing.sourceLanguage = currentValue")));
    QVERIFY(wizard.contains(QStringLiteral("root.dubbing.targetLanguage = currentValue")));
    QVERIFY(wizard.contains(QStringLiteral("function advanceFromCurrentPage()")));
    QVERIFY(wizard.contains(QStringLiteral("sourceLanguageBox.forceActiveFocus()")));
    QVERIFY(wizard.contains(QStringLiteral("targetLanguageBox.forceActiveFocus()")));
    QVERIFY(wizard.contains(QStringLiteral("configurationSummary")));
    QVERIFY(wizard.contains(QStringLiteral("preflight.stages")));
    QVERIFY(wizard.contains(QStringLiteral("routeSetupDialog")));
    QVERIFY(wizard.contains(QStringLiteral("preflightModelDialog")));
    QVERIFY(wizard.contains(QStringLiteral("visible: (root.preflight.selectedWorkers || []).length > 0")));
    QVERIFY(wizard.contains(QStringLiteral("Reviewed stage configuration")));
    QVERIFY(wizard.contains(QStringLiteral("modelData.detail ||")));
    QVERIFY(!wizard.contains(QStringLiteral("workflow default")));
    QVERIFY(!wizard.contains(QStringLiteral("CloseOnEscape")));

    QFile page(QStringLiteral(LASTUDIO_SOURCE_DIR) + QStringLiteral("/qml/pages/DubbingPage.qml"));
    QVERIFY(page.open(QIODevice::ReadOnly));
    const QString pageSource = QString::fromUtf8(page.readAll());
    QVERIFY(pageSource.contains(QStringLiteral("dubbing.beginDubbingEntry()")));
    QVERIFY(pageSource.contains(QStringLiteral("dubbingEntryGate.openGate()")));
    QVERIFY(pageSource.contains(QStringLiteral("chooseDubbingEntryMode(\"automatic\")")));
    QVERIFY(pageSource.contains(QStringLiteral("chooseDubbingEntryMode(\"step\")")));
    QVERIFY(!pageSource.contains(QStringLiteral("Component.onCompleted: dubbing.resetStandardWorkflowNodeModels()")));
}

void TestDubbingProject::dubbingTranscriptionWaitsForFreshDecodedAudio()
{
    DubbingSttWorkerMock worker;
    QVERIFY(worker.start());

    AppController *app = AppController::instance();
    QVERIFY(app != nullptr);
    ColabSession *session = app->colabSttSession();
    SttSessionController *stt = app->sttSession();
    QVERIFY(session != nullptr);
    QVERIFY(stt != nullptr);
    ColabSessionReset resetSession(session);

    QString sessionError;
    QVERIFY2(session->setSession(worker.workerUrl(), QStringLiteral("test-token"),
                                 &sessionError, true), qPrintable(sessionError));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audioPath = dir.filePath(QStringLiteral("input.wav"));
    QVector<float> samples(1600, 0.1F);
    QVERIFY(WavIO::saveFloat(audioPath, samples.constData(), samples.size(), 16000));

    DubbingTranscriptionJob job(stt, nullptr, nullptr);
    const QVariantMap configuration{
        {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
        {QStringLiteral("modelId"), QStringLiteral("whisper.cpp")}
    };
    QVERIFY(job.start(QStringLiteral("en"), audioPath, {}, configuration));

    // The worker deliberately rejects the request. What matters here is the
    // real boundary: an upload is issued only after the fresh WAV decode ends.
    QTRY_VERIFY_WITH_TIMEOUT(!worker.request().isEmpty(), 5000);
    QVERIFY(worker.request().startsWith("POST /v2/uploads/stt HTTP/1.1"));
    job.cancel();
    stt->cancelProcessing();
}

void TestDubbingProject::targetLanguageUpdatesVoiceNodeLanguage()
{
    DubbingController controller(nullptr, nullptr);
    QVERIFY(controller.setWorkflowNodeParameters(
        QStringLiteral("synthesize"),
        QVariantMap{{QStringLiteral("lang"), QStringLiteral("en")}}));

    controller.setTargetLanguage(QStringLiteral("ja"));

    QCOMPARE(controller.targetLanguage(), QStringLiteral("ja"));
    const QVariantMap synthesis = controller.workflowNodeConfigurations()
                                      .value(QStringLiteral("synthesize")).toMap();
    QCOMPARE(synthesis.value(QStringLiteral("parameters")).toMap()
                 .value(QStringLiteral("lang")).toString(),
             QStringLiteral("ja"));
}

void TestDubbingProject::rejectsRerunningUnsupportedStep()
{
    DubbingController controller(nullptr, nullptr);
    QCOMPARE(controller.workflowMode(), QStringLiteral("idle"));
    QCOMPARE(controller.currentStepId(), QStringLiteral("import"));

    QVERIFY(!controller.rerunStep(QStringLiteral("import")));
    QVERIFY(!controller.rerunStep(QStringLiteral("unknown")));
    QCOMPARE(controller.workflowMode(), QStringLiteral("idle"));
    QCOMPARE(controller.currentStepId(), QStringLiteral("import"));
}

void TestDubbingProject::transcriptionRequiresReadyModel()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath(QStringLiteral("source.wav"));
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QVERIFY(media.write("audio-placeholder") > 0);
    media.close();

    Settings *settings = AppController::instance()->settings();
    QVERIFY(settings != nullptr);
    const bool originalRemoteFirst = settings->remoteFirstMode();
    settings->setRemoteFirstMode(false);
    AppController::instance()->stt()->unloadModel();
    DubbingJobRunner runner(AppController::instance()->sttSession(), nullptr);
    runner.startTranscription(QStringLiteral("en"), mediaPath);

    const QString error = runner.lastError();
    settings->setRemoteFirstMode(originalRemoteFirst);
    QVERIFY(!runner.processing());
    QVERIFY(error.contains(QStringLiteral("not ready")));
}

void TestDubbingProject::alignmentRefinementFallsBackWithoutDependencies()
{
    const QVariantList input{QVariantMap{{QStringLiteral("id"), QStringLiteral("s1")},
                                         {QStringLiteral("startMs"), 1000},
                                         {QStringLiteral("endMs"), 2000},
                                         {QStringLiteral("sourceText"), QStringLiteral("Hello")}}};
    const AlignmentRefinementResult result = AlignmentRefinementService::refine(
        QStringLiteral("missing-analysis.wav"), QStringLiteral("en"), input, nullptr, nullptr);

    QCOMPARE(result.status, QStringLiteral("skipped"));
    QCOMPARE(result.segments.size(), input.size());
    const QVariantMap fallback = result.segments.first().toMap();
    QCOMPARE(fallback.value(QStringLiteral("sourceText")).toString(), QStringLiteral("Hello"));
    QCOMPARE(fallback.value(QStringLiteral("startMs")).toLongLong(), qint64(1000));
    QCOMPARE(fallback.value(QStringLiteral("timingSource")).toString(), QStringLiteral("asr"));
    QCOMPARE(fallback.value(QStringLiteral("alignmentStatus")).toString(), QStringLiteral("skipped"));
    QVERIFY(!result.attempted);
}

void TestDubbingProject::audioGenerationWaitsForCompletedSynthesis()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    TtsEngine tts;
    tts.loadModel(QStringLiteral("mock-model.onnx"));
    DubbingJobRunner runner(nullptr, &tts);
    QSignalSpy completedSpy(&runner, &DubbingJobRunner::stageCompleted);
    QSignalSpy errorSpy(&runner, &DubbingJobRunner::errorOccurred);

    const QVariantList segments{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("s1")},
                    {QStringLiteral("startMs"), 0},
                    {QStringLiteral("endMs"), 1000},
                    {QStringLiteral("targetText"), QStringLiteral("Xin chào")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("s2")},
                    {QStringLiteral("startMs"), 1000},
                    {QStringLiteral("endMs"), 2000},
                    {QStringLiteral("targetText"), QStringLiteral("Thế giới")}}
    };
    runner.startAudioGeneration(segments, dir.filePath(QStringLiteral("project.ladub.json")));

    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.size(), 1, 3000);
    QCOMPARE(errorSpy.size(), 0);
    QVERIFY(!runner.processing());

    const QVariantMap outputs = completedSpy.constFirst().at(1).toMap();
    const QVariantList timeline = outputs.value(QStringLiteral("timeline")).toList();
    QCOMPARE(timeline.size(), 2);
    for (const QVariant &entry : timeline) {
        const QVariantMap segment = entry.toMap();
        QVERIFY(!segment.value(QStringLiteral("clipPath")).toString().isEmpty());
        QVERIFY(QFileInfo::exists(segment.value(QStringLiteral("clipPath")).toString()));
        QVERIFY(!segment.value(QStringLiteral("waveformSamples")).toList().isEmpty());
    }
}

void TestDubbingProject::audioGenerationUsesSelectedVoiceForEverySegment()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    TtsEngine tts;
    tts.loadModel(QStringLiteral("mock-model.onnx"));
    DubbingJobRunner runner(nullptr, &tts);
    QSignalSpy completedSpy(&runner, &DubbingJobRunner::stageCompleted);
    QSignalSpy errorSpy(&runner, &DubbingJobRunner::errorOccurred);

    const QVariantList segments{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("s1")},
                    {QStringLiteral("startMs"), 0},
                    {QStringLiteral("endMs"), 10},
                    {QStringLiteral("targetText"), QStringLiteral("Một")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("s2")},
                    {QStringLiteral("startMs"), 10},
                    {QStringLiteral("endMs"), 20},
                    {QStringLiteral("targetText"), QStringLiteral("Hai")}}
    };
    runner.startAudioGeneration(
        segments,
        dir.filePath(QStringLiteral("project.ladub.json")),
        QVariantMap{{QStringLiteral("voice"), QStringLiteral("preset-a")}});

    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.size(), 1, 3000);
    QCOMPARE(errorSpy.size(), 0);
    const QVariantList timeline = completedSpy.constFirst().at(1).toMap()
                                      .value(QStringLiteral("timeline")).toList();
    QCOMPARE(timeline.size(), 2);
    for (const QVariant &entry : timeline) {
        const WavIO::WavData wav = WavIO::loadAsFloat(
            entry.toMap().value(QStringLiteral("clipPath")).toString());
        QVERIFY(!wav.samples.isEmpty());
        QVERIFY(qAbs(wav.samples.first() - 0.2f) < 0.001f);
    }
}

void TestDubbingProject::selectsBestAutomaticVoiceReference()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    constexpr int sampleRate = 24000;
    QVector<float> samples(sampleRate * 6);
    for (int i = 0; i < sampleRate * 3; ++i) samples[i] = 1.0f;
    constexpr double pi = 3.14159265358979323846;
    for (int i = sampleRate * 3; i < samples.size(); ++i)
        samples[i] = 0.1f * qSin(2.0 * pi * 220.0 * i / sampleRate);
    const QString sourcePath = dir.filePath(QStringLiteral("source.wav"));
    QVERIFY(WavIO::saveFloat(sourcePath, samples.constData(), samples.size(), sampleRate));

    const QVariantList segments{
        QVariantMap{{QStringLiteral("startMs"), 0},
                    {QStringLiteral("endMs"), 3000},
                    {QStringLiteral("sourceText"), QStringLiteral("Clipped speech")}},
        QVariantMap{{QStringLiteral("startMs"), 3000},
                    {QStringLiteral("endMs"), 6000},
                    {QStringLiteral("sourceText"), QStringLiteral("Clean speech")}}
    };
    const QString projectPath = dir.filePath(QStringLiteral("project.ladub.json"));
    const DubbingVoiceReference selected =
        DubbingVoiceReferenceSelector::select(sourcePath, segments, projectPath);

    QVERIFY2(selected.isValid(), qPrintable(selected.error));
    QCOMPARE(selected.startMs, qint64(3000));
    QCOMPARE(selected.endMs, qint64(6000));
    QCOMPARE(selected.referenceText, QStringLiteral("Clean speech"));
    QVERIFY(QFileInfo::exists(selected.audioPath));
}

void TestDubbingProject::audioGenerationUsesSavedCloneVoiceForEverySegment()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    constexpr int sampleRate = 24000;
    QVector<float> source(sampleRate * 4);
    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i < source.size(); ++i)
        source[i] = 0.1f * qSin(2.0 * pi * 180.0 * i / sampleRate);
    const QString sourcePath = dir.filePath(QStringLiteral("source.wav"));
    QVERIFY(WavIO::saveFloat(sourcePath, source.constData(), source.size(), sampleRate));

    TtsEngine tts;
    tts.setFamilyConfig({{QStringLiteral("id"), QStringLiteral("qwen3-tts-1.7b-customvoice")}});
    tts.loadModel(QStringLiteral("mock-model.onnx"));
    DubbingJobRunner runner(nullptr, &tts);
    QSignalSpy completedSpy(&runner, &DubbingJobRunner::stageCompleted);
    QSignalSpy errorSpy(&runner, &DubbingJobRunner::errorOccurred);
    const QVariantList segments{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("s1")},
                    {QStringLiteral("startMs"), 0},
                    {QStringLiteral("endMs"), 2000},
                    {QStringLiteral("speakerId"), QStringLiteral("speaker-a")},
                    {QStringLiteral("sourceText"), QStringLiteral("Original reference words")},
                    {QStringLiteral("targetText"), QStringLiteral("Translated speech")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("s2")},
                    {QStringLiteral("startMs"), 2000},
                    {QStringLiteral("endMs"), 4000},
                    {QStringLiteral("speakerId"), QStringLiteral("speaker-b")},
                    {QStringLiteral("sourceText"), QStringLiteral("Other source speaker")},
                    {QStringLiteral("targetText"), QStringLiteral("Second translated speech")}}
    };
    runner.startAudioGeneration(
        segments, dir.filePath(QStringLiteral("project.ladub.json")),
        QVariantMap{{QStringLiteral("voiceCloningEnabled"), true},
                    {QStringLiteral("cloneVoicePreset"),
                     QVariantMap{{QStringLiteral("id"), QStringLiteral("preset-source")},
                                 {QStringLiteral("name"), QStringLiteral("Saved source")},
                                 {QStringLiteral("familyId"), QStringLiteral("omnivoice")},
                                 {QStringLiteral("audioPath"), sourcePath},
                                 {QStringLiteral("referenceText"), QStringLiteral("Original reference words")}}}});

    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.size(), 1, 3000);
    QCOMPARE(errorSpy.size(), 0);
    const QVariantList timeline = completedSpy.constFirst().at(1).toMap()
                                      .value(QStringLiteral("timeline")).toList();
    QCOMPARE(timeline.size(), 2);
    for (const QVariant &entry : timeline) {
        const QVariantMap generated = entry.toMap();
        QCOMPARE(generated.value(QStringLiteral("cloneVoicePresetId")).toString(),
                 QStringLiteral("preset-source"));
        QCOMPARE(generated.value(QStringLiteral("voiceReferenceText")).toString(),
                 QStringLiteral("Original reference words"));
        QCOMPARE(generated.value(QStringLiteral("voiceReferencePath")).toString(),
                 QFileInfo(sourcePath).absoluteFilePath());
    }
    QCOMPARE(tts.lastGenerationMode(), QStringLiteral("tts"));
}

void TestDubbingProject::zeroCloneVoicePresetBlocksSynthesisWithoutFallback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    TtsEngine tts;
    tts.loadModel(QStringLiteral("mock-model.onnx"));
    DubbingJobRunner runner(nullptr, &tts);
    QSignalSpy errorSpy(&runner, &DubbingJobRunner::errorOccurred);
    const QVariantList segments{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("s1")},
                    {QStringLiteral("startMs"), 0},
                    {QStringLiteral("endMs"), 1000},
                    {QStringLiteral("targetText"), QStringLiteral("No fallback")}}
    };

    runner.startAudioGeneration(
        segments, dir.filePath(QStringLiteral("project.ladub.json")),
        QVariantMap{{QStringLiteral("voiceCloningEnabled"), true}});

    QCOMPARE(errorSpy.size(), 1);
    QVERIFY(errorSpy.constFirst().at(0).toString().contains(
        QStringLiteral("Select a saved clone voice")));
    QVERIFY(!runner.processing());
    QVERIFY(tts.lastGenerationMode() != QStringLiteral("voice-cloning"));
}

void TestDubbingProject::localSavedVoiceRequiresPersistentProfile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVector<float> samples(24000, 0.02F);
    const QString referencePath = dir.filePath(QStringLiteral("reference.wav"));
    QVERIFY(WavIO::saveFloat(referencePath, samples.constData(), samples.size(), 24000));

    TtsEngine tts;
    tts.setFamilyConfig({{QStringLiteral("id"), QStringLiteral("omnivoice")}});
    tts.loadModel(QStringLiteral("mock-model.onnx"));
    DubbingJobRunner runner(nullptr, &tts);
    QSignalSpy errors(&runner, &DubbingJobRunner::errorOccurred);
    runner.startAudioGeneration(
        {QVariantMap{{QStringLiteral("id"), QStringLiteral("s1")},
                     {QStringLiteral("startMs"), 0},
                     {QStringLiteral("endMs"), 1000},
                     {QStringLiteral("targetText"), QStringLiteral("No local reclone")}}},
        dir.filePath(QStringLiteral("project.ladub.json")),
        QVariantMap{{QStringLiteral("voiceCloningEnabled"), true},
                    {QStringLiteral("cloneVoicePreset"),
                     QVariantMap{{QStringLiteral("id"), QStringLiteral("preset-source")},
                                 {QStringLiteral("name"), QStringLiteral("Saved source")},
                                 {QStringLiteral("familyId"), QStringLiteral("omnivoice")},
                                 {QStringLiteral("audioPath"), referencePath},
                                 {QStringLiteral("referenceText"), QStringLiteral("Approved reference")}}}});

    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.constFirst().at(0).toString().contains(
        QStringLiteral("will not clone the voice again")));
    QCOMPARE(tts.lastGenerationMode(), QStringLiteral("tts"));
}

void TestDubbingProject::cloneVoicePresetSelectionPersistsAndMissingPresetBlocks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    constexpr int sampleRate = 24000;
    QVector<float> samples(sampleRate, 0.05F);
    const QString sourcePath = dir.filePath(QStringLiteral("reference.wav"));
    QVERIFY(WavIO::saveFloat(sourcePath, samples.constData(), samples.size(), sampleRate));

    // Selecting or persisting a managed reference must not load a local
    // clone/TTS model. Dubbing voice cloning is a Direct Colab contract.
    TtsEngine tts;
    VoiceClonePresetService presets;
    DubbingController controller(nullptr, &tts);
    controller.setVoiceClonePresetService(&presets);
    const QString familyId = controller.cloneVoicePresetFamily();
    const QString presetName = QStringLiteral("Dubbing regression %1")
        .arg(QDateTime::currentMSecsSinceEpoch());
    QVERIFY(presets.addPreset(familyId, presetName, sourcePath,
                              QStringLiteral("Stable reference")));

    QString presetId;
    for (const QVariant &entry : presets.presetsForFamily(familyId)) {
        const QVariantMap preset = entry.toMap();
        if (preset.value(QStringLiteral("name")).toString() == presetName) {
            presetId = preset.value(QStringLiteral("id")).toString();
            break;
        }
    }
    QVERIFY(!presetId.isEmpty());
    QVERIFY(controller.newProject(dir.filePath(QStringLiteral("project.ladub.json"))));
    bool listed = false;
    for (const QVariant &entry : controller.cloneVoicePresets()) {
        if (entry.toMap().value(QStringLiteral("id")).toString() == presetId) {
            listed = true;
            break;
        }
    }
    QVERIFY(listed);
    QVERIFY(controller.selectCloneVoicePreset(presetId));
    QVERIFY(controller.cloneVoiceSelectionValid());
    QVERIFY(controller.saveProject());

    DubbingController reloaded(nullptr, &tts);
    reloaded.setVoiceClonePresetService(&presets);
    QVERIFY(reloaded.openProject(dir.filePath(QStringLiteral("project.ladub.json"))));
    QCOMPARE(reloaded.selectedCloneVoicePresetId(), presetId);
    QVERIFY(reloaded.cloneVoiceSelectionValid());

    // A saved voice remains visible after the TTS family changes so the user
    // can see why it cannot run; it must be marked incompatible and never
    // selected as a silent fallback.
    QVERIFY(reloaded.setWorkflowNodeParameters(
        QStringLiteral("synthesize"),
        QVariantMap{{QStringLiteral("voiceCloneModelId"), QStringLiteral("voxcpm2")}}));
    QCOMPARE(reloaded.cloneVoicePresetFamily(), QStringLiteral("voxcpm2"));
    QVERIFY(!reloaded.cloneVoicePresets().isEmpty());
    QCOMPARE(reloaded.cloneVoicePresets().first().toMap()
                 .value(QStringLiteral("compatible")).toBool(), false);
    QVERIFY(!reloaded.cloneVoiceSelectionValid());
    QVERIFY(!reloaded.selectCloneVoicePreset(presetId));
    QVERIFY(reloaded.lastError().contains(QStringLiteral("incompatible")));

    QVERIFY(presets.deletePreset(presetId));
    QVERIFY(!reloaded.cloneVoiceSelectionValid());
    QCOMPARE(reloaded.selectedCloneVoicePresetId(), presetId);
    QVERIFY(reloaded.cloneVoiceSelectionError().contains(
        QStringLiteral("no longer available")));
}

void TestDubbingProject::changingCloneVoicePresetAppliesToEntireNextRun()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    constexpr int sampleRate = 24000;
    QVector<float> firstReference(sampleRate * 2, 0.04F);
    QVector<float> secondReference(sampleRate * 2, 0.08F);
    const QString firstPath = dir.filePath(QStringLiteral("first.wav"));
    const QString secondPath = dir.filePath(QStringLiteral("second.wav"));
    QVERIFY(WavIO::saveFloat(firstPath, firstReference.constData(), firstReference.size(), sampleRate));
    QVERIFY(WavIO::saveFloat(secondPath, secondReference.constData(), secondReference.size(), sampleRate));

    TtsEngine tts;
    tts.setFamilyConfig({{QStringLiteral("id"), QStringLiteral("qwen3-tts-1.7b-customvoice")}});
    tts.loadModel(QStringLiteral("mock-model.onnx"));
    DubbingJobRunner runner(nullptr, &tts);
    QSignalSpy completedSpy(&runner, &DubbingJobRunner::stageCompleted);
    QSignalSpy errorSpy(&runner, &DubbingJobRunner::errorOccurred);
    const QVariantList segments{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("s1")},
                    {QStringLiteral("speakerId"), QStringLiteral("speaker-a")},
                    {QStringLiteral("startMs"), 0},
                    {QStringLiteral("endMs"), 1000},
                    {QStringLiteral("targetText"), QStringLiteral("First")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("s2")},
                    {QStringLiteral("speakerId"), QStringLiteral("speaker-b")},
                    {QStringLiteral("startMs"), 1000},
                    {QStringLiteral("endMs"), 2000},
                    {QStringLiteral("targetText"), QStringLiteral("Second")}}
    };
    const auto cloneSettings = [](const QString &id, const QString &path) {
        return QVariantMap{{QStringLiteral("voiceCloningEnabled"), true},
                           {QStringLiteral("cloneVoicePreset"),
                            QVariantMap{{QStringLiteral("id"), id},
                                        {QStringLiteral("name"), id},
                                        {QStringLiteral("familyId"), QStringLiteral("omnivoice")},
                                        {QStringLiteral("audioPath"), path},
                                        {QStringLiteral("referenceText"), id}}}};
    };

    runner.startAudioGeneration(segments, dir.filePath(QStringLiteral("project.ladub.json")),
                                cloneSettings(QStringLiteral("preset-a"), firstPath));
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.size(), 1, 3000);
    runner.startAudioGeneration(segments, dir.filePath(QStringLiteral("project.ladub.json")),
                                cloneSettings(QStringLiteral("preset-b"), secondPath));
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.size(), 2, 3000);
    QCOMPARE(errorSpy.size(), 0);

    const QVariantList secondTimeline = completedSpy.at(1).at(1).toMap()
        .value(QStringLiteral("timeline")).toList();
    QCOMPARE(secondTimeline.size(), 2);
    for (const QVariant &entry : secondTimeline) {
        const QVariantMap generated = entry.toMap();
        QCOMPARE(generated.value(QStringLiteral("cloneVoicePresetId")).toString(),
                 QStringLiteral("preset-b"));
        QCOMPARE(generated.value(QStringLiteral("voiceReferencePath")).toString(),
                 QFileInfo(secondPath).absoluteFilePath());
    }
}

void TestDubbingProject::voiceClonePresetLibraryPersistsAtomicallyAndProtectsSource()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    constexpr int sampleRate = 24000;
    QVector<float> samples(sampleRate, 0.03F);
    const QString sourcePath = dir.filePath(QStringLiteral("externally-imported.wav"));
    QVERIFY(WavIO::saveFloat(sourcePath, samples.constData(), samples.size(), sampleRate));

    VoiceClonePresetService service;
    const QString familyId = QStringLiteral("library-regression-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QVERIFY(service.addPreset(familyId, QStringLiteral("Durable voice"), sourcePath,
                              QStringLiteral("Reference transcript")));

    const QVariantList initial = service.presetsForFamily(familyId);
    QCOMPARE(initial.size(), 1);
    QVariantMap preset = initial.constFirst().toMap();
    const QString presetId = preset.value(QStringLiteral("id")).toString();
    const QString storedPath = preset.value(QStringLiteral("audioPath")).toString();
    QVERIFY(!presetId.isEmpty());
    QVERIFY(QFileInfo(storedPath).isFile());
    QVERIFY(preset.value(QStringLiteral("valid")).toBool());
    QCOMPARE(preset.value(QStringLiteral("referenceSha256")).toString().size(), 64);
    QVERIFY(preset.value(QStringLiteral("referenceBytes")).toLongLong() > 0);

    const QString metadataPath = PathUtils::dataDir()
        + QStringLiteral("/presets/voice_clone_presets.json");
    QFile metadata(metadataPath);
    QVERIFY(metadata.open(QIODevice::ReadOnly));
    const QJsonDocument metadataDocument = QJsonDocument::fromJson(metadata.readAll());
    QVERIFY(metadataDocument.isObject());
    QCOMPARE(metadataDocument.object().value(QStringLiteral("schemaVersion")).toInt(), 1);
    QVERIFY(metadataDocument.object().value(QStringLiteral("presets")).isArray());
    metadata.close();

    // A fresh service simulates an application restart. It must read the
    // committed metadata and the app-owned reference, not the source path.
    VoiceClonePresetService restarted;
    const QVariantList reloaded = restarted.presetsForFamily(familyId);
    QCOMPARE(reloaded.size(), 1);
    QCOMPARE(reloaded.constFirst().toMap().value(QStringLiteral("id")).toString(), presetId);
    QVERIFY(restarted.updatePreset(presetId, QStringLiteral("Renamed durable voice"),
                                   storedPath, QStringLiteral("Updated transcript")));
    QCOMPARE(restarted.presetsForFamily(familyId).constFirst().toMap()
                 .value(QStringLiteral("name")).toString(),
             QStringLiteral("Renamed durable voice"));

    QFile corrupt(storedPath);
    QVERIFY(corrupt.open(QIODevice::Append));
    QVERIFY(corrupt.write("corruption") > 0);
    corrupt.close();
    const QVariantMap invalid = restarted.presetsForFamily(familyId).constFirst().toMap();
    QVERIFY(!invalid.value(QStringLiteral("valid")).toBool());
    QVERIFY(invalid.value(QStringLiteral("validationError")).toString().contains(
        QStringLiteral("checksum mismatch")));

    QVERIFY(restarted.deletePreset(presetId));
    QVERIFY(QFileInfo(sourcePath).isFile());
    QVERIFY(!QFileInfo(storedPath).exists());
}

void TestDubbingProject::voiceClonePresetLibraryMigratesLegacyArrayOnEdit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString familyId = QStringLiteral("legacy-library-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString storageDir = PathUtils::dataDir()
        + QStringLiteral("/presets/voice_clone_refs");
    QVERIFY(QDir().mkpath(storageDir));
    const QString legacyAudio = QDir(storageDir).filePath(QStringLiteral("legacy-owned.wav"));
    QVector<float> samples(24000, 0.02F);
    QVERIFY(WavIO::saveFloat(legacyAudio, samples.constData(), samples.size(), 24000));

    // Libraries from before schemaVersion=1 were an array.  Keep a real
    // app-owned reference in that legacy data, then prove a normal edit
    // upgrades the file atomically to the current envelope.
    const QString metadataPath = PathUtils::dataDir()
        + QStringLiteral("/presets/voice_clone_presets.json");
    QVERIFY(QDir().mkpath(QFileInfo(metadataPath).absolutePath()));
    const QJsonArray legacyArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("legacy-preset")},
                                              {QStringLiteral("familyId"), familyId},
                                              {QStringLiteral("name"), QStringLiteral("Legacy voice")},
                                              {QStringLiteral("audioPath"), legacyAudio},
                                              {QStringLiteral("referenceText"), QStringLiteral("Legacy reference")}}};
    QSaveFile legacyFile(metadataPath);
    QVERIFY(legacyFile.open(QIODevice::WriteOnly));
    const QByteArray legacyJson = QJsonDocument(legacyArray).toJson(QJsonDocument::Compact);
    QCOMPARE(legacyFile.write(legacyJson), legacyJson.size());
    QVERIFY(legacyFile.commit());

    VoiceClonePresetService service;
    const QVariantList legacyPresets = service.presetsForFamily(familyId);
    QCOMPARE(legacyPresets.size(), 1);
    QCOMPARE(legacyPresets.constFirst().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("legacy-preset"));
    QVERIFY(legacyPresets.constFirst().toMap().value(QStringLiteral("valid")).toBool());

    const QString importedAudio = dir.filePath(QStringLiteral("new-external.wav"));
    QVERIFY(WavIO::saveFloat(importedAudio, samples.constData(), samples.size(), 24000));
    QVERIFY(service.addPreset(familyId, QStringLiteral("Current voice"), importedAudio,
                              QStringLiteral("Current reference")));

    QFile metadata(metadataPath);
    QVERIFY(metadata.open(QIODevice::ReadOnly));
    const QJsonDocument migrated = QJsonDocument::fromJson(metadata.readAll());
    QVERIFY(migrated.isObject());
    QCOMPARE(migrated.object().value(QStringLiteral("schemaVersion")).toInt(), 1);
    const QJsonArray migratedPresets = migrated.object().value(QStringLiteral("presets")).toArray();
    QCOMPARE(migratedPresets.size(), 2);
    QVERIFY(std::any_of(migratedPresets.cbegin(), migratedPresets.cend(), [](const QJsonValue &entry) {
        return entry.toObject().value(QStringLiteral("id")).toString()
            == QStringLiteral("legacy-preset");
    }));
}

void TestDubbingProject::exportsSelfContainedCapCutDraftWithUnverifiedImportStatus()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    constexpr int sampleRate = 24000;
    QVector<float> samples(sampleRate, 0.02F);
    const QString sourcePath = dir.filePath(QStringLiteral("nguon-goc-private.mp4"));
    const QString masterPath = dir.filePath(QStringLiteral("original.wav"));
    const QString vocalsPath = dir.filePath(QStringLiteral("vocals.wav"));
    const QString backgroundPath = dir.filePath(QStringLiteral("background.wav"));
    const QString mixPath = dir.filePath(QStringLiteral("dubbed-mix.wav"));
    const QString clipPath = dir.filePath(QStringLiteral("clip-01.wav"));
    const QString secondClipPath = dir.filePath(QStringLiteral("clip-02.wav"));
    const QString customFontPath = dir.filePath(QStringLiteral("private-subtitle-font.ttf"));
    QVERIFY(WavIO::saveFloat(sourcePath, samples.constData(), samples.size(), sampleRate));
    QVERIFY(WavIO::saveFloat(masterPath, samples.constData(), samples.size(), sampleRate));
    QVERIFY(WavIO::saveFloat(vocalsPath, samples.constData(), samples.size(), sampleRate));
    QVERIFY(WavIO::saveFloat(backgroundPath, samples.constData(), samples.size(), sampleRate));
    QVERIFY(WavIO::saveFloat(mixPath, samples.constData(), samples.size(), sampleRate));
    QVERIFY(WavIO::saveFloat(clipPath, samples.constData(), samples.size(), sampleRate));
    QVERIFY(WavIO::saveFloat(secondClipPath, samples.constData(), samples.size(), sampleRate));
    QFile customFontFile(customFontPath);
    QVERIFY(customFontFile.open(QIODevice::WriteOnly));
    QCOMPARE(customFontFile.write("test subtitle font"), qint64(18));
    customFontFile.close();

    const QVariantList segments{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-01")},
                    {QStringLiteral("startMs"), 0},
                    {QStringLiteral("endMs"), 850},
                    {QStringLiteral("speakerId"), QStringLiteral("speaker-1")},
                    {QStringLiteral("sourceText"), QStringLiteral("Original text")},
                    {QStringLiteral("targetText"), QString::fromUtf8("Bản dịch tiếng Việt")},
                    {QStringLiteral("clipPath"), clipPath},
                    {QStringLiteral("volume"), 0.85},
                    {QStringLiteral("colabToken"), QStringLiteral("temporary-colab-secret")},
                    {QStringLiteral("gatewayApiKey"), QStringLiteral("gateway-secret")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-02")},
                    {QStringLiteral("startMs"), 850},
                    {QStringLiteral("endMs"), 1800},
                    {QStringLiteral("speakerId"), QStringLiteral("speaker-2")},
                    {QStringLiteral("sourceText"), QString::fromUtf8("Câu gốc thứ hai")},
                    {QStringLiteral("targetText"), QString::fromUtf8("Câu dịch thứ hai")},
                    {QStringLiteral("clipPath"), secondClipPath},
                    {QStringLiteral("rippleOriginalStartMs"), 700},
                    {QStringLiteral("rippleOriginalEndMs"), 1400},
                    {QStringLiteral("rippleOffsetMs"), 150}}
    };
    QVariantMap subtitleStyle = DubbingSubtitleService::defaultStyle();
    subtitleStyle.insert(QStringLiteral("fontFamily"), QStringLiteral("Noto Sans CJK"));
    subtitleStyle.insert(QStringLiteral("fontSize"), 52);
    subtitleStyle.insert(QStringLiteral("textColor"), QStringLiteral("#FF80D8FF"));
    subtitleStyle.insert(QStringLiteral("outlineWidth"), 3);
    subtitleStyle.insert(QStringLiteral("lineSpacing"), 1.2);
    subtitleStyle.insert(QStringLiteral("fontFile"), customFontPath);
    const QVariantMap subtitleConfiguration{{QStringLiteral("source"), QStringLiteral("imported-vtt")},
                                            {QStringLiteral("style"), subtitleStyle}};
    const QVariantMap timingConfiguration{{QStringLiteral("mode"), QStringLiteral("ripple")},
                                          {QStringLiteral("minimumGapMs"), 100}};
    QString draftPath;
    QString warning;
    QString error;
    QVERIFY2(CapCutDraftExporter::exportDraft(
                 dir.path(), QString::fromUtf8("Dự án kiểm thử"), sourcePath, masterPath,
                 backgroundPath, mixPath, true, 1800, segments, vocalsPath, subtitleConfiguration,
                 timingConfiguration, &draftPath, &warning, &error),
             qPrintable(error));
    QVERIFY(QFileInfo(draftPath).isDir());
    QVERIFY(QFileInfo(QDir(draftPath).filePath(QStringLiteral("draft_content.json"))).isFile());
    QVERIFY(QFileInfo(QDir(draftPath).filePath(QStringLiteral("draft_meta_info.json"))).isFile());
    QVERIFY(QFileInfo(QDir(draftPath).filePath(QStringLiteral("assets/clips/0001.wav"))).isFile());
    QVERIFY(QFileInfo(QDir(draftPath).filePath(QStringLiteral("assets/clips/0002.wav"))).isFile());
    QVERIFY(QFileInfo(QDir(draftPath).filePath(QStringLiteral("assets/source-vocals.wav"))).isFile());
    QVERIFY(QFileInfo(QDir(draftPath).filePath(QStringLiteral("assets/fonts/subtitle-font.ttf"))).isFile());
    QVERIFY(QFileInfo(QDir(draftPath).filePath(QStringLiteral("subtitles/dubbed.srt"))).isFile());
    QVERIFY(QFileInfo(QDir(draftPath).filePath(QStringLiteral("LA_STUDIO_EDITABLE_MANIFEST.json"))).isFile());
    QVERIFY(warning.contains(QStringLiteral("not yet verified")));

    QFile contentFile(QDir(draftPath).filePath(QStringLiteral("draft_content.json")));
    QVERIFY(contentFile.open(QIODevice::ReadOnly));
    const QByteArray contentBytes = contentFile.readAll();
    const QJsonDocument content = QJsonDocument::fromJson(contentBytes);
    QVERIFY(content.isObject());
    const QJsonObject materials = content.object().value(QStringLiteral("materials")).toObject();
    QCOMPARE(materials.value(QStringLiteral("videos")).toArray().size(), 1);
    QVERIFY(materials.value(QStringLiteral("audios")).toArray().size() >= 6);
    QCOMPARE(materials.value(QStringLiteral("texts")).toArray().size(), 2);
    QVERIFY(content.object().value(QStringLiteral("tracks")).toArray().size() >= 7);
    for (const QJsonValue &audio : materials.value(QStringLiteral("audios")).toArray()) {
        const QString asset = audio.toObject().value(QStringLiteral("path")).toString();
        QVERIFY(QFileInfo(asset).isFile());
        QVERIFY(QDir::cleanPath(asset).startsWith(QDir::cleanPath(draftPath) + QLatin1Char('/')));
    }
    const QJsonObject textMaterial = materials.value(QStringLiteral("texts")).toArray().first().toObject();
    const QJsonObject textContent = QJsonDocument::fromJson(
        textMaterial.value(QStringLiteral("content")).toString().toUtf8()).object();
    QVERIFY(!textContent.value(QStringLiteral("text")).toString().isEmpty());
    QCOMPARE(textContent.value(QStringLiteral("styles")).toArray().first().toObject()
                 .value(QStringLiteral("font")).toObject().value(QStringLiteral("path")).toString(),
             QDir(draftPath).filePath(QStringLiteral("assets/fonts/subtitle-font.ttf")));
    QVERIFY(!contentBytes.contains(customFontPath.toUtf8()));
    contentFile.close();

    QFile segmentFile(QDir(draftPath).filePath(QStringLiteral("segments.json")));
    QVERIFY(segmentFile.open(QIODevice::ReadOnly));
    const QByteArray segmentBytes = segmentFile.readAll();
    const QJsonArray exportedSegments = QJsonDocument::fromJson(segmentBytes).object()
        .value(QStringLiteral("segments")).toArray();
    QCOMPARE(exportedSegments.size(), 2);
    QCOMPARE(exportedSegments.at(1).toObject().value(QStringLiteral("startMs")).toInt(), 850);
    QCOMPARE(exportedSegments.at(1).toObject().value(QStringLiteral("endMs")).toInt(), 1800);
    QCOMPARE(exportedSegments.at(1).toObject().value(QStringLiteral("rippleOffsetMs")).toInt(), 150);
    QVERIFY(!exportedSegments.at(1).toObject().value(QStringLiteral("subtitleMaterialId")).toString().isEmpty());
    QVERIFY(!segmentBytes.contains("temporary-colab-secret"));
    QVERIFY(!segmentBytes.contains("gateway-secret"));
    QFile subtitlesFile(QDir(draftPath).filePath(QStringLiteral("subtitles/dubbed.srt")));
    QVERIFY(subtitlesFile.open(QIODevice::ReadOnly));
    const QByteArray subtitlesBytes = subtitlesFile.readAll();
    QVERIFY(subtitlesBytes.contains("00:00:00,850 --> 00:00:01,800"));
    QVERIFY(subtitlesBytes.contains(QString::fromUtf8("Câu dịch thứ hai").toUtf8()));

    QFile editableManifestFile(QDir(draftPath).filePath(QStringLiteral("LA_STUDIO_EDITABLE_MANIFEST.json")));
    QVERIFY(editableManifestFile.open(QIODevice::ReadOnly));
    const QByteArray editableManifestBytes = editableManifestFile.readAll();
    const QJsonObject editableManifest = QJsonDocument::fromJson(editableManifestBytes).object();
    QCOMPARE(editableManifest.value(QStringLiteral("capCutImportStatus")).toString(),
             QStringLiteral("structurally-validated-manual-import-pending"));
    QCOMPARE(editableManifest.value(QStringLiteral("subtitle")).toObject()
                 .value(QStringLiteral("source")).toString(), QStringLiteral("imported-vtt"));
    QCOMPARE(editableManifest.value(QStringLiteral("subtitle")).toObject()
                 .value(QStringLiteral("style")).toObject().value(QStringLiteral("fontSize")).toInt(), 52);
    QCOMPARE(editableManifest.value(QStringLiteral("subtitle")).toObject()
                 .value(QStringLiteral("style")).toObject().value(QStringLiteral("fontFile")).toString(),
             QStringLiteral("assets/fonts/subtitle-font.ttf"));
    QCOMPARE(editableManifest.value(QStringLiteral("timing")).toObject()
                 .value(QStringLiteral("mode")).toString(), QStringLiteral("ripple"));
    QCOMPARE(editableManifest.value(QStringLiteral("subtitle")).toObject()
                 .value(QStringLiteral("editableTextSegmentCount")).toInt(), 2);
    QVERIFY(!editableManifestBytes.contains("temporary-colab-secret"));
    QVERIFY(!editableManifestBytes.contains("gateway-secret"));
    QVERIFY(!editableManifestBytes.contains(customFontPath.toUtf8()));

    QString secondDraft;
    QVERIFY2(CapCutDraftExporter::exportDraft(
                 dir.path(), QStringLiteral("collision-safe"), sourcePath, masterPath,
                 backgroundPath, mixPath, true, 1800, segments, vocalsPath, subtitleConfiguration,
                 timingConfiguration, &secondDraft, nullptr, &error),
             qPrintable(error));
    QVERIFY(secondDraft != draftPath);
    QVERIFY(QFileInfo(secondDraft).isDir());

    QVariantList missingAssetSegments = segments;
    QVariantMap missingAsset = missingAssetSegments.first().toMap();
    missingAsset.insert(QStringLiteral("clipPath"), dir.filePath(QStringLiteral("missing.wav")));
    missingAssetSegments[0] = missingAsset;
    QString rejectedDraft;
    QVERIFY(!CapCutDraftExporter::exportDraft(
        dir.path(), QStringLiteral("missing-asset"), sourcePath, masterPath, backgroundPath,
        mixPath, true, 1800, missingAssetSegments, vocalsPath, subtitleConfiguration,
        timingConfiguration, &rejectedDraft, nullptr, &error));
    QVERIFY(rejectedDraft.isEmpty());
    QVERIFY(error.contains(QStringLiteral("valid generated audio clip")));
}

void TestDubbingProject::capCutExportDoesNotMislabelUnseparatedAnalysisAudioAsVocals()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    constexpr int sampleRate = 24000;
    QVector<float> samples(sampleRate, 0.02F);
    const QString sourcePath = dir.filePath(QStringLiteral("original.mp4"));
    const QString masterPath = dir.filePath(QStringLiteral("master.wav"));
    const QString analysisPath = dir.filePath(QStringLiteral("analysis-mono.wav"));
    const QString clipPath = dir.filePath(QStringLiteral("voice-01.wav"));
    const QString previewPath = dir.filePath(QStringLiteral("preview.wav"));
    for (const QString &path : {sourcePath, masterPath, analysisPath, clipPath, previewPath})
        QVERIFY(WavIO::saveFloat(path, samples.constData(), samples.size(), sampleRate));

    DubbingProject project;
    project.projectPath = dir.filePath(QStringLiteral("unseparated.ladub.json"));
    project.sourceMediaPath = sourcePath;
    project.masterAudioPath = masterPath;
    project.analysisAudioPath = analysisPath;
    project.sourceDurationMs = 1000;
    project.sourceSampleRate = sampleRate;
    project.sourceChannels = 1;
    project.sourceIsVideo = true;
    project.subtitleConfiguration = {{QStringLiteral("source"), QStringLiteral("segments")},
                                     {QStringLiteral("style"), DubbingSubtitleService::defaultStyle()}};
    project.segments = {QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-01")},
                                    {QStringLiteral("startMs"), 0},
                                    {QStringLiteral("endMs"), 1000},
                                    {QStringLiteral("sourceText"), QStringLiteral("Original")},
                                    {QStringLiteral("targetText"), QStringLiteral("Translated")},
                                    {QStringLiteral("clipPath"), clipPath}}};
    QString error;
    QVERIFY2(project.save(&error), qPrintable(error));

    DubbingController controller(nullptr, nullptr);
    QVERIFY2(controller.openProject(project.projectPath), qPrintable(controller.lastError()));
    QVERIFY2(controller.exportCapCutDraft(dir.path()), qPrintable(controller.lastError()));
    const QString draftPath = controller.capCutDraftPath();
    QVERIFY(QFileInfo(draftPath).isDir());

    QFile manifestFile(QDir(draftPath).filePath(QStringLiteral("LA_STUDIO_EDITABLE_MANIFEST.json")));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    QVERIFY(manifest.value(QStringLiteral("assets")).toObject()
                .value(QStringLiteral("sourceVocals")).toString().isEmpty());
    const QJsonArray tracks = manifest.value(QStringLiteral("tracks")).toArray();
    QVERIFY(std::none_of(tracks.cbegin(), tracks.cend(), [](const QJsonValue &entry) {
        return entry.toObject().value(QStringLiteral("role")).toString()
            == QStringLiteral("source-vocals");
    }));
}

void TestDubbingProject::audioMixRunsAsynchronously()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    TtsEngine tts;
    tts.loadModel(QStringLiteral("mock-model.onnx"));
    DubbingJobRunner runner(nullptr, &tts);
    QSignalSpy completedSpy(&runner, &DubbingJobRunner::stageCompleted);

    const QVariantList segments{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("s1")},
                    {QStringLiteral("startMs"), 0},
                    {QStringLiteral("endMs"), 1000},
                    {QStringLiteral("targetText"), QStringLiteral("Xin chao")}}
    };
    runner.startAudioGeneration(segments, dir.filePath(QStringLiteral("project.ladub.json")));
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.size(), 1, 3000);

    const QVariantList timeline = completedSpy.constFirst().at(1).toMap()
                                      .value(QStringLiteral("timeline")).toList();
    const QString previewPath = dir.filePath(QStringLiteral("preview.wav"));
    QVERIFY(runner.renderPreview(timeline,
                                 dir.filePath(QStringLiteral("project.ladub.json")),
                                 previewPath));
    QVERIFY(runner.processing());
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.size(), 2, 3000);
    QCOMPARE(completedSpy.at(1).at(0).toString(), QStringLiteral("mix"));
    QVERIFY(QFileInfo::exists(previewPath));
    QVERIFY(!runner.processing());
}

void TestDubbingProject::audioMixCreatesIndependentVocalStem()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    constexpr int sampleRate = 48000;
    QVector<float> clipSamples(sampleRate, 0.25f);
    QVector<float> backgroundSamples(sampleRate, 0.5f);
    const QString clipPath = dir.filePath(QStringLiteral("clip.wav"));
    const QString backgroundPath = dir.filePath(QStringLiteral("background.wav"));
    const QString previewPath = dir.filePath(QStringLiteral("preview.wav"));
    const QString vocalPath = AudioTimelineMixer::vocalStemPath(previewPath);
    QVERIFY(WavIO::saveFloat(clipPath, clipSamples.constData(), clipSamples.size(), sampleRate));
    QVERIFY(WavIO::saveFloat(backgroundPath, backgroundSamples.constData(),
                             backgroundSamples.size(), sampleRate));

    const QVariantList segments{
        QVariantMap{{QStringLiteral("startMs"), 0},
                    {QStringLiteral("endMs"), 1000},
                    {QStringLiteral("clipPath"), clipPath}}
    };
    QString error;
    QVERIFY2(AudioTimelineMixer::mixSegments(segments, previewPath, backgroundPath,
                                             vocalPath, &error), qPrintable(error));

    const WavIO::WavData vocals = WavIO::loadAsFloat(vocalPath);
    const WavIO::WavData mixed = WavIO::loadAsFloat(previewPath);
    QVERIFY(!vocals.samples.isEmpty());
    QVERIFY(!mixed.samples.isEmpty());
    QVERIFY(qAbs(vocals.samples.constFirst() - 0.25f) < 0.01f);
    QVERIFY(qAbs(mixed.samples.constFirst() - 0.425f) < 0.01f);
}

void TestDubbingProject::commitsMediaExportAtomically()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString destination = dir.filePath(QStringLiteral("export.mp4"));
    const QString staging = dir.filePath(QStringLiteral("export.staging"));
    QFile oldFile(destination);
    QVERIFY(oldFile.open(QIODevice::WriteOnly));
    QVERIFY(oldFile.write("old") == 3);
    oldFile.close();
    QFile stagedFile(staging);
    QVERIFY(stagedFile.open(QIODevice::WriteOnly));
    QVERIFY(stagedFile.write("new") == 3);
    stagedFile.close();

    QString error;
    QVERIFY2(AtomicMediaCommit::commit(staging, destination, &error), qPrintable(error));
    QFile result(destination);
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(result.readAll(), QByteArray("new"));
}

void TestDubbingProject::sourceTextEditInvalidatesWordTiming()
{
    DubbingController controller(nullptr, nullptr);
    controller.addSegment(0, 1000, QStringLiteral("Hello"));
    controller.updateSegment(0, QVariantMap{
        {QStringLiteral("words"), QVariantList{QVariantMap{{QStringLiteral("text"), QStringLiteral("Hello")},
                                                            {QStringLiteral("startMs"), 0},
                                                            {QStringLiteral("endMs"), 500}}}},
        {QStringLiteral("timingSource"), QStringLiteral("ctc")},
        {QStringLiteral("alignmentStatus"), QStringLiteral("aligned")}
    });
    QVERIFY(!controller.segments().at(0).toMap().value(QStringLiteral("words")).toList().isEmpty());

    controller.updateSegment(0, QVariantMap{{QStringLiteral("sourceText"), QStringLiteral("Hi")}});
    const QVariantMap updated = controller.segments().at(0).toMap();
    QVERIFY(updated.value(QStringLiteral("words")).toList().isEmpty());
    QCOMPARE(updated.value(QStringLiteral("timingSource")).toString(), QStringLiteral("asr"));
    QCOMPARE(updated.value(QStringLiteral("alignmentStatus")).toString(), QStringLiteral("pending"));
}

void TestDubbingProject::unchangedTextEditPreservesTranslationMetadata()
{
    DubbingController controller(nullptr, nullptr);
    controller.addSegment(0, 1000, QStringLiteral("Hello"));
    controller.updateSegment(
        0, QVariantMap{{QStringLiteral("targetText"), QStringLiteral("Xin chao")}});
    controller.updateSegment(0, QVariantMap{
        {QStringLiteral("state"), QStringLiteral("translated")},
        {QStringLiteral("durationBudget"),
         QVariantMap{{QStringLiteral("targetUnits"), 8},
                     {QStringLiteral("minUnits"), 6},
                     {QStringLiteral("maxUnits"), 10}}},
        {QStringLiteral("durationUnits"), 8},
        {QStringLiteral("durationStatus"), QStringLiteral("within-budget")}
    });

    controller.updateSegment(0, QVariantMap{{QStringLiteral("sourceText"), QStringLiteral("Hello")}});
    controller.updateSegment(0, QVariantMap{{QStringLiteral("targetText"), QStringLiteral("Xin chao")}});

    const QVariantMap updated = controller.segments().at(0).toMap();
    QCOMPARE(updated.value(QStringLiteral("state")).toString(), QStringLiteral("translated"));
    QCOMPARE(updated.value(QStringLiteral("durationUnits")).toInt(), 8);
    QCOMPARE(updated.value(QStringLiteral("durationStatus")).toString(),
             QStringLiteral("within-budget"));
    QVERIFY(updated.contains(QStringLiteral("durationBudget")));
}

void TestDubbingProject::targetTextEditRefreshesDurationMetadata()
{
    DubbingController controller(nullptr, nullptr);
    controller.setTargetLanguage(QStringLiteral("vi"));
    controller.addSegment(0, 1000, QStringLiteral("Hello"));
    const QString original = QStringLiteral("Xin chao");
    const QString replacement = QStringLiteral("Xin chao ban");
    const int replacementUnits = DubbingDurationPlanner::countPhonemes(
        replacement, QStringLiteral("vi"));
    if (replacementUnits <= 0) {
        QSKIP("eSpeak NG runtime is unavailable; phoneme integration is validated in the staged release tier.");
    }
    QVERIFY(replacementUnits > 0);
    controller.updateSegment(0, QVariantMap{
        {QStringLiteral("targetText"), original},
        {QStringLiteral("durationBudget"),
         QVariantMap{{QStringLiteral("targetUnits"), replacementUnits},
                     {QStringLiteral("minUnits"), replacementUnits},
                     {QStringLiteral("maxUnits"), replacementUnits}}},
        {QStringLiteral("durationUnits"), 1},
        {QStringLiteral("durationStatus"), QStringLiteral("needs-review")}
    });

    controller.updateSegment(
        0, QVariantMap{{QStringLiteral("targetText"), replacement}});

    const QVariantMap updated = controller.segments().at(0).toMap();
    QCOMPARE(updated.value(QStringLiteral("state")).toString(), QStringLiteral("stale"));
    QCOMPARE(updated.value(QStringLiteral("durationUnits")).toInt(), replacementUnits);
    QCOMPARE(updated.value(QStringLiteral("durationStatus")).toString(),
             QStringLiteral("within-budget"));
}

void TestDubbingProject::exportsSubtitlesAndReviewPackage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DubbingController controller(nullptr, nullptr);
    const QString projectPath = dir.filePath(QStringLiteral("demo.ladub.json"));
    QVERIFY(controller.newProject(projectPath));
    controller.addSegment(1200, 3450, QStringLiteral("Hello"));
    controller.updateSegment(0, QVariantMap{{QStringLiteral("targetText"), QStringLiteral("Xin chao")}});

    const QString subtitlePath = dir.filePath(QStringLiteral("dubbed.srt"));
    QVERIFY(controller.exportSubtitles(subtitlePath, true));
    QFile subtitleFile(subtitlePath);
    QVERIFY(subtitleFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString subtitle = QString::fromUtf8(subtitleFile.readAll());
    QVERIFY(subtitle.contains(QStringLiteral("00:00:01,200 --> 00:00:03,450")));
    QVERIFY(subtitle.contains(QStringLiteral("Xin chao")));

    const QString packagePath = dir.filePath(QStringLiteral("review-package"));
    QVERIFY(controller.exportPackage(packagePath));
    QVERIFY(QFileInfo(packagePath + QStringLiteral("/manifest.json")).isFile());
    QVERIFY(QFileInfo(packagePath + QStringLiteral("/project.ladub.json")).isFile());
    QVERIFY(QFileInfo(packagePath + QStringLiteral("/source.srt")).isFile());
    QVERIFY(QFileInfo(packagePath + QStringLiteral("/dubbed.srt")).isFile());
}

void TestDubbingProject::segmentNormalizerSplitsLongAsrTranscript()
{
    const QVariantList input{QVariantMap{
        {QStringLiteral("id"), QStringLiteral("long-source")},
        {QStringLiteral("startMs"), 320},
        {QStringLiteral("endMs"), 44560},
        {QStringLiteral("sourceText"), QStringLiteral(
            "Iberia is the reconquista where Christian kingdoms in Spain fought Muslim states. "
            "This war lasted seven hundred and eighty one years before the final kingdom fell.")},
        {QStringLiteral("targetText"), QString()},
        {QStringLiteral("speakerId"), QStringLiteral("speaker-1")},
        {QStringLiteral("timingSource"), QStringLiteral("asr")}
    }};

    const QVariantList normalized = DubbingSegmentNormalizer::normalize(input);
    QVERIFY(normalized.size() >= 3);
    QCOMPARE(normalized.constFirst().toMap().value(QStringLiteral("startMs")).toLongLong(), qint64(320));
    QCOMPARE(normalized.constLast().toMap().value(QStringLiteral("endMs")).toLongLong(), qint64(44560));
    qint64 previousEnd = 320;
    for (const QVariant &entry : normalized) {
        const QVariantMap segment = entry.toMap();
        QCOMPARE(segment.value(QStringLiteral("startMs")).toLongLong(), previousEnd);
        QVERIFY(segment.value(QStringLiteral("endMs")).toLongLong() > previousEnd);
        QVERIFY(segment.value(QStringLiteral("endMs")).toLongLong() - previousEnd <= 13000);
        QCOMPARE(segment.value(QStringLiteral("derivedFromSegmentId")).toString(), QStringLiteral("long-source"));
        QCOMPARE(segment.value(QStringLiteral("timingSource")).toString(), QStringLiteral("asr-interpolated"));
        previousEnd = segment.value(QStringLiteral("endMs")).toLongLong();
    }
}

void TestDubbingProject::segmentNormalizerUsesAlignedWordBoundaries()
{
    const QVariantList words{
        QVariantMap{{QStringLiteral("text"), QStringLiteral("First")}, {QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 900}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("sentence")}, {QStringLiteral("startMs"), 950}, {QStringLiteral("endMs"), 2300}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("Second")}, {QStringLiteral("startMs"), 4000}, {QStringLiteral("endMs"), 5200}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("long")}, {QStringLiteral("startMs"), 5250}, {QStringLiteral("endMs"), 6500}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("sentence")}, {QStringLiteral("startMs"), 6550}, {QStringLiteral("endMs"), 8000}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("ends")}, {QStringLiteral("startMs"), 8050}, {QStringLiteral("endMs"), 9300}}
    };
    const QVariantList input{QVariantMap{{QStringLiteral("id"), QStringLiteral("aligned-source")},
                                         {QStringLiteral("startMs"), 0},
                                         {QStringLiteral("endMs"), 9300},
                                         {QStringLiteral("sourceText"), QStringLiteral("First sentence. Second long sentence ends.")},
                                         {QStringLiteral("words"), words},
                                         {QStringLiteral("timingSource"), QStringLiteral("ctc")}}};

    const QVariantList normalized = DubbingSegmentNormalizer::normalize(input);
    QCOMPARE(normalized.size(), 2);
    QCOMPARE(normalized.at(0).toMap().value(QStringLiteral("endMs")).toLongLong(), qint64(2300));
    QCOMPARE(normalized.at(1).toMap().value(QStringLiteral("startMs")).toLongLong(), qint64(4000));
    QCOMPARE(normalized.at(0).toMap().value(QStringLiteral("sourceText")).toString(), QStringLiteral("First sentence."));
    QCOMPARE(normalized.at(1).toMap().value(QStringLiteral("words")).toList().size(), 4);
}

void TestDubbingProject::segmentNormalizerRebuildsAcrossAsrBoundaries()
{
    const QVariantList firstWords{
        QVariantMap{{QStringLiteral("text"), QStringLiteral("This")}, {QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 600}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("war")}, {QStringLiteral("startMs"), 650}, {QStringLiteral("endMs"), 1200}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("lasted")}, {QStringLiteral("startMs"), 1250}, {QStringLiteral("endMs"), 1900}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("seven")}, {QStringLiteral("startMs"), 1950}, {QStringLiteral("endMs"), 2600}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("hundred")}, {QStringLiteral("startMs"), 2650}, {QStringLiteral("endMs"), 3400}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("eighty")}, {QStringLiteral("startMs"), 3450}, {QStringLiteral("endMs"), 4200}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("years.")}, {QStringLiteral("startMs"), 4250}, {QStringLiteral("endMs"), 5000}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("At")}, {QStringLiteral("startMs"), 5050}, {QStringLiteral("endMs"), 5400}}
    };
    const QVariantList secondWords{
        QVariantMap{{QStringLiteral("text"), QStringLiteral("number")}, {QStringLiteral("startMs"), 5450}, {QStringLiteral("endMs"), 5900}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("five,")}, {QStringLiteral("startMs"), 5950}, {QStringLiteral("endMs"), 6400}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("it")}, {QStringLiteral("startMs"), 6450}, {QStringLiteral("endMs"), 6800}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("is")}, {QStringLiteral("startMs"), 6850}, {QStringLiteral("endMs"), 7200}},
        QVariantMap{{QStringLiteral("text"), QStringLiteral("Vietnam.")}, {QStringLiteral("startMs"), 7250}, {QStringLiteral("endMs"), 8000}}
    };
    const QVariantList input{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("asr-1")},
                    {QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 5400},
                    {QStringLiteral("sourceText"), QStringLiteral("This war lasted seven hundred eighty years. At")},
                    {QStringLiteral("words"), firstWords}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("asr-2")},
                    {QStringLiteral("startMs"), 5450}, {QStringLiteral("endMs"), 8000},
                    {QStringLiteral("sourceText"), QStringLiteral("number five, it is Vietnam.")},
                    {QStringLiteral("words"), secondWords}}
    };

    const QVariantList normalized = DubbingSegmentNormalizer::normalize(input);
    QCOMPARE(normalized.size(), 2);
    QCOMPARE(normalized.at(0).toMap().value(QStringLiteral("sourceText")).toString(),
             QStringLiteral("This war lasted seven hundred eighty years."));
    QVERIFY(normalized.at(1).toMap().value(QStringLiteral("sourceText")).toString()
                .startsWith(QStringLiteral("At number five")));
}

void TestDubbingProject::countsVietnameseSyllablesAndPlansBudget()
{
    QCOMPARE(DubbingDurationPlanner::countVietnameseSyllables(QStringLiteral("Xin chào, thế giới!")), 4);
    const QVariantMap segment{{QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 2000}};
    const DubbingSpeechBudget budget = DubbingDurationPlanner::plan(segment, 10.0);
    QCOMPARE(budget.slotMs, qint64(2000));
    QVERIFY(budget.targetUnits > 0);
    QVERIFY(budget.minUnits <= budget.targetUnits);
    QVERIFY(budget.targetUnits <= budget.maxUnits);

    DubbingDurationSettings asymmetric;
    asymmetric.lowerToleranceRatio = 0.25;
    asymmetric.upperToleranceRatio = 0.50;
    const DubbingSpeechBudget asymmetricBudget =
        DubbingDurationPlanner::plan(segment, 10.0, asymmetric);
    QCOMPARE(asymmetricBudget.targetUnits, 20);
    QCOMPARE(asymmetricBudget.minUnits, 15);
    QCOMPARE(asymmetricBudget.maxUnits, 30);
}

void TestDubbingProject::selectsImprovingDurationCandidate()
{
    const QString reference = QStringLiteral("Cuoc chien keo dai 100 nam");
    const QString current =
        QStringLiteral("Cuoc chien nay da keo dai trong suot 100 nam");
    const int predicted = DubbingDurationPlanner::countPhonemes(
        reference, QStringLiteral("vi"));
    if (predicted <= 0) {
        QSKIP("eSpeak NG runtime is unavailable; phoneme integration is validated in the staged release tier.");
    }
    const QString selected = DubbingDurationPlanner::selectBestCandidate(
        QStringLiteral("The war lasted 100 years."),
        reference,
        current,
        {QStringLiteral("Cuoc chien keo dai 100 nam"),
         QStringLiteral("Cuoc chien rat dai"),
         QStringLiteral("Ban dich dai hon rat nhieu va van co 100 nam")},
        predicted,
        predicted,
        predicted,
        {QStringLiteral("100")},
        QStringLiteral("vi"));
    QCOMPARE(selected, reference);
    QVERIFY(DubbingDurationPlanner::phonemeDistance(
                selected, predicted, QStringLiteral("vi"))
            < DubbingDurationPlanner::phonemeDistance(
                current, predicted, QStringLiteral("vi")));
}

void TestDubbingProject::prefersWithinBudgetDurationCandidate()
{
    const QString reference =
        QStringLiteral("Cuoc chien nay da keo dai trong suot 100 nam");
    const QString withinBudget = QStringLiteral("Cuoc chien keo dai 100 nam");
    const QString closerToReference = QStringLiteral("Cuoc chien nay keo dai 100 nam");
    const int predicted = DubbingDurationPlanner::countPhonemes(
        withinBudget, QStringLiteral("vi"));
    if (predicted <= 0) {
        QSKIP("eSpeak NG runtime is unavailable; phoneme integration is validated in the staged release tier.");
    }

    const QString selected = DubbingDurationPlanner::selectBestCandidate(
        QStringLiteral("The war lasted 100 years."),
        reference,
        reference,
        {closerToReference, withinBudget},
        predicted,
        predicted,
        predicted,
        {QStringLiteral("100")},
        QStringLiteral("vi"));

    QCOMPARE(selected, withinBudget);
}

void TestDubbingProject::prefersClosestRepairCandidateOutsideBudget()
{
    const QString reference = QStringLiteral(
        "Cuoc chien nay da keo dai trong suot mot tram nam va gay ra nhieu ton that");
    const QString semanticButLong =
        QStringLiteral("Cuoc chien nay keo dai trong suot mot tram nam");
    const QString closest = QStringLiteral("Chien tranh tram nam");
    const int predicted = DubbingDurationPlanner::countPhonemes(
                               closest, QStringLiteral("vi"))
        + 1;
    if (predicted <= 1) {
        QSKIP("eSpeak NG runtime is unavailable; phoneme integration is validated in the staged release tier.");
    }

    const QString selected = DubbingDurationPlanner::selectBestCandidate(
        QStringLiteral("The war lasted one hundred years."),
        reference,
        reference,
        {semanticButLong, closest},
        predicted,
        predicted,
        predicted,
        {},
        QStringLiteral("vi"));

    QCOMPARE(selected, closest);
}

void TestDubbingProject::buildsPauseAlignedTtsChunks()
{
    const QVariantList pauses{
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("leading")},
                    {QStringLiteral("durationMs"), 100}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("internal")},
                    {QStringLiteral("durationMs"), 450}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("trailing")},
                    {QStringLiteral("durationMs"), 200}}};
    const QVariantList chunks = DubbingDurationPlanner::pauseChunks(
        QStringLiteral("Xin chao [[PAUSE]] the gioi"), pauses);
    QCOMPARE(chunks.size(), 2);
    QCOMPARE(
        chunks.at(0).toMap().value(QStringLiteral("leadingPauseMs")).toLongLong(),
        qint64(100));
    QCOMPARE(
        chunks.at(0).toMap().value(QStringLiteral("pauseAfterMs")).toLongLong(),
        qint64(450));
    QCOMPARE(
        chunks.at(1).toMap().value(QStringLiteral("pauseAfterMs")).toLongLong(),
        qint64(200));
}

void TestDubbingProject::extractsAlignedPauses()
{
    const QVariantMap segment{{QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 3000},
        {QStringLiteral("words"), QVariantList{
            QVariantMap{{QStringLiteral("startMs"), 100}, {QStringLiteral("endMs"), 500}},
            QVariantMap{{QStringLiteral("startMs"), 1100}, {QStringLiteral("endMs"), 1500}},
            QVariantMap{{QStringLiteral("startMs"), 1600}, {QStringLiteral("endMs"), 1900}}
        }}};
    const QVariantList pauses = DubbingDurationPlanner::extractPauses(segment);
    QCOMPARE(pauses.size(), 2);
    QCOMPARE(pauses.at(0).toMap().value(QStringLiteral("durationMs")).toLongLong(), qint64(600));
}

void TestDubbingProject::roundTripsDurationSettings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    DubbingProject project;
    project.projectPath = dir.filePath(QStringLiteral("duration.ladub.json"));
    project.durationControl.insert(QStringLiteral("enabled"), true);
    project.durationControl.insert(QStringLiteral("maxPreTtsIterations"), 3);
    QString error;
    QVERIFY2(project.save(&error), qPrintable(error));
    DubbingProject loaded;
    QVERIFY2(DubbingProject::load(project.projectPath, loaded, &error), qPrintable(error));
    QCOMPARE(loaded.durationControl.value(QStringLiteral("maxPreTtsIterations")).toInt(), 3);
}

void TestDubbingProject::importsDubbingSubtitleFormatsWithoutInventingTiming()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString projectPath = dir.filePath(QStringLiteral("subtitle-import.ladub.json"));
    DubbingController controller(nullptr, nullptr);
    QVERIFY(controller.newProject(projectPath));

    const QString vttPath = dir.filePath(QStringLiteral("source.vtt"));
    QVERIFY(writeFixtureFile(vttPath,
        QByteArray("WEBVTT\n\n00:00:01.000 --> 00:00:02.400\nViá»‡t / ä¸­æ–‡\n\n")));
    QVERIFY2(controller.importSubtitles(vttPath), qPrintable(controller.lastError()));
    QCOMPARE(controller.segments().size(), 1);
    QCOMPARE(controller.segments().first().toMap().value(QStringLiteral("startMs")).toLongLong(), qint64(1000));
    QCOMPARE(controller.segments().first().toMap().value(QStringLiteral("endMs")).toLongLong(), qint64(2400));
    QCOMPARE(controller.segments().first().toMap().value(QStringLiteral("sourceText")).toString(),
             QString::fromUtf8("Viá»‡t / ä¸­æ–‡"));
    QCOMPARE(controller.subtitleConfiguration().value(QStringLiteral("source")).toString(),
             QStringLiteral("imported-vtt"));

    const QString assPath = dir.filePath(QStringLiteral("source.ass"));
    QVERIFY(writeFixtureFile(assPath,
        QByteArray("[Events]\nDialogue: 0,0:00:03.00,0:00:04.25,Default,,0,0,0,,{\\i1}ã“ã‚“ã«ã¡ã¯\\NäŸ©ë…•í•˜ì„¸ìš”\n")));
    QVERIFY2(controller.importSubtitles(assPath), qPrintable(controller.lastError()));
    QCOMPARE(controller.segments().first().toMap().value(QStringLiteral("startMs")).toLongLong(), qint64(3000));
    QCOMPARE(controller.segments().first().toMap().value(QStringLiteral("sourceText")).toString(),
             QString::fromUtf8("ã“ã‚“ã«ã¡ã¯\näŸ©ë…•í•˜ì„¸ìš”"));

    controller.addSegment(5000, 6000, QStringLiteral("Anchor 2"));
    const QVariantList timelineBefore = controller.segments();
    const QString txtPath = dir.filePath(QStringLiteral("untimed.txt"));
    QVERIFY(writeFixtureFile(txtPath, QByteArray("Má»™t dÃ²ng\nDÃ²ng hai\n")));
    QVERIFY2(controller.importSubtitles(txtPath, QStringLiteral("existing-segment")),
             qPrintable(controller.lastError()));
    QCOMPARE(controller.segments().size(), 2);
    QCOMPARE(controller.segments().at(0).toMap().value(QStringLiteral("startMs")).toLongLong(),
             timelineBefore.at(0).toMap().value(QStringLiteral("startMs")).toLongLong());
    QCOMPARE(controller.segments().at(1).toMap().value(QStringLiteral("endMs")).toLongLong(),
             timelineBefore.at(1).toMap().value(QStringLiteral("endMs")).toLongLong());

    QVERIFY(writeFixtureFile(txtPath, QByteArray("Too few lines\n")));
    QVERIFY(!controller.importSubtitles(txtPath, QStringLiteral("existing-segment")));
    QVERIFY(controller.lastError().contains(QStringLiteral("exactly one non-empty line")));
    QVERIFY(!controller.importSubtitles(txtPath, QStringLiteral("alignment")));
    QVERIFY(controller.lastError().contains(QStringLiteral("will not invent timing")));
}

void TestDubbingProject::persistsDubbingSubtitleStyleAndExportsUnicodeAss()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    DubbingController controller(nullptr, nullptr);
    const QString projectPath = dir.filePath(QStringLiteral("subtitle-style.ladub.json"));
    QVERIFY(controller.newProject(projectPath));
    controller.addSegment(0, 1800, QString::fromUtf8("Tiáº¿ng Viá»‡t ä¸­æ–‡ æ—¥æœ¬ì–´ í•œêµ­ì–´"));
    QVariantMap style = DubbingSubtitleService::defaultStyle();
    style.insert(QStringLiteral("fontFamily"), QStringLiteral("Noto Sans CJK"));
    style.insert(QStringLiteral("fontSize"), 54);
    style.insert(QStringLiteral("alignment"), QStringLiteral("custom"));
    style.insert(QStringLiteral("safeMargin"), 0.11);
    style.insert(QStringLiteral("positionX"), 0.25);
    style.insert(QStringLiteral("positionY"), 0.30);
    QVERIFY(controller.setSubtitleStyle(style));
    QVERIFY(controller.setSubtitleTextSource(QStringLiteral("source")));
    QVERIFY(!controller.setSubtitleTextSource(QStringLiteral("unknown")));
    QVERIFY(controller.setSubtitleBurnIn(true));
    QVERIFY(controller.saveProject());

    DubbingController reopened(nullptr, nullptr);
    QVERIFY2(reopened.openProject(projectPath), qPrintable(reopened.lastError()));
    QCOMPARE(reopened.subtitleConfiguration().value(QStringLiteral("burnIn")).toBool(), true);
    QCOMPARE(reopened.subtitleConfiguration().value(QStringLiteral("style")).toMap()
                 .value(QStringLiteral("fontSize")).toInt(), 54);
    QCOMPARE(reopened.subtitleConfiguration().value(QStringLiteral("textSource")).toString(),
             QStringLiteral("source"));

    const QString assPath = dir.filePath(QStringLiteral("styled.ass"));
    QString error;
    QVERIFY2(DubbingSubtitleService::writeAss(reopened.segments(),
                                                reopened.subtitleConfiguration().value(QStringLiteral("style")).toMap(),
                                                assPath, false, &error), qPrintable(error));
    QFile assFile(assPath);
    QVERIFY(assFile.open(QIODevice::ReadOnly));
    const QByteArray bytes = assFile.readAll();
    QVERIFY(bytes.contains("Style: LAStudio,Noto Sans CJK,54"));
    QVERIFY(bytes.contains(",2,173,173,119,1"));
    QVERIFY(bytes.contains("{\\pos(480,324)}"));
    QVERIFY(bytes.contains(QString::fromUtf8("Tiáº¿ng Viá»‡t ä¸­æ–‡ æ—¥æœ¬ì–´ í•œêµ­ì–´").toUtf8()));
}

void TestDubbingProject::preservesConfiguredLineSpacingInBurnInAss()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QVariantList segments{QVariantMap{{QStringLiteral("startMs"), 0},
                                            {QStringLiteral("endMs"), 1000},
                                            {QStringLiteral("sourceText"),
                                             QStringLiteral("First reviewed line\nSecond reviewed line")}}};
    QVariantMap compact = DubbingSubtitleService::defaultStyle();
    compact.insert(QStringLiteral("fontSize"), 48);
    compact.insert(QStringLiteral("lineSpacing"), 1.0);
    QVariantMap expanded = compact;
    expanded.insert(QStringLiteral("lineSpacing"), 1.8);
    const QString compactPath = dir.filePath(QStringLiteral("compact.ass"));
    const QString expandedPath = dir.filePath(QStringLiteral("expanded.ass"));
    QString error;
    QVERIFY2(DubbingSubtitleService::writeAss(segments, compact, compactPath, false, &error),
             qPrintable(error));
    QVERIFY2(DubbingSubtitleService::writeAss(segments, expanded, expandedPath, false, &error),
             qPrintable(error));

    const auto positions = [](const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return QList<int>{};
        const QString content = QString::fromUtf8(file.readAll());
        QRegularExpression expression(QStringLiteral("\\\\pos\\(960,(\\d+)\\)"));
        QList<int> values;
        QRegularExpressionMatchIterator matches = expression.globalMatch(content);
        while (matches.hasNext()) values.append(matches.next().captured(1).toInt());
        return values;
    };
    const QList<int> compactPositions = positions(compactPath);
    const QList<int> expandedPositions = positions(expandedPath);
    QCOMPARE(compactPositions.size(), 2);
    QCOMPARE(expandedPositions.size(), 2);
    QVERIFY(compactPositions.at(1) > compactPositions.at(0));
    QVERIFY(expandedPositions.at(1) > expandedPositions.at(0));
    QVERIFY(expandedPositions.at(1) - expandedPositions.at(0)
            > compactPositions.at(1) - compactPositions.at(0));
}

void TestDubbingProject::dubbingSubtitleUiWiresImportPreviewAndBurnIn()
{
    const QDir sourceRoot(QStringLiteral(LASTUDIO_SOURCE_DIR));
    QFile page(sourceRoot.filePath(QStringLiteral("qml/pages/DubbingPage.qml")));
    QFile panel(sourceRoot.filePath(QStringLiteral("qml/components/dubbing/DubbingSourceMediaPanel.qml")));
    QFile editor(sourceRoot.filePath(QStringLiteral("qml/components/dubbing/DubbingSubtitleEditor.qml")));
    QVERIFY(page.open(QIODevice::ReadOnly));
    QVERIFY(panel.open(QIODevice::ReadOnly));
    QVERIFY(editor.open(QIODevice::ReadOnly));
    const QString pageSource = QString::fromUtf8(page.readAll());
    const QString panelSource = QString::fromUtf8(panel.readAll());
    const QString editorSource = QString::fromUtf8(editor.readAll());
    QVERIFY(pageSource.contains(QStringLiteral("DubbingSubtitleEditor")));
    QVERIFY(panelSource.contains(QStringLiteral("dubbingSubtitlePreviewOverlay")));
    QVERIFY(panelSource.contains(QStringLiteral("FontLoader")));
    QVERIFY(panelSource.contains(QStringLiteral("dubbingSubtitleEditorButton")));
    QVERIFY(editorSource.contains(QStringLiteral("*.srt *.vtt *.ass *.ssa *.txt *.md *.markdown")));
    QVERIFY(editorSource.contains(QStringLiteral("timestamps are never invented")));
    QVERIFY(editorSource.contains(QStringLiteral("Burn the styled subtitles into rendered MP4")));
    QVERIFY(editorSource.contains(QStringLiteral("subtitleTextSourceBox")));
    QVERIFY(editorSource.contains(QStringLiteral("setSubtitleTextSource(currentValue)")));
    QVERIFY(editorSource.contains(QStringLiteral("Reviewed source text (STT, OCR, or imported)")));
    QVERIFY(editorSource.contains(QStringLiteral("safeMargin")));
    QVERIFY(editorSource.contains(QStringLiteral("positionX")));
    QVERIFY(editorSource.contains(QStringLiteral("preview and rendered MP4")));
}

void TestDubbingProject::resolvesGlobalTimingConflictsWithRippleAndUndo()
{
    const QVariantList segments{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("a")},
                    {QStringLiteral("speakerId"), QStringLiteral("speaker-1")},
                    {QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 800},
                    {QStringLiteral("durationMs"), 1000}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("b")},
                    {QStringLiteral("speakerId"), QStringLiteral("speaker-2")},
                    {QStringLiteral("startMs"), 600}, {QStringLiteral("endMs"), 1200},
                    {QStringLiteral("durationMs"), 900},
                    {QStringLiteral("words"), QVariantList{
                        QVariantMap{{QStringLiteral("startMs"), 620}, {QStringLiteral("endMs"), 840}}}}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("c")},
                    {QStringLiteral("speakerId"), QStringLiteral("speaker-1")},
                    {QStringLiteral("startMs"), 1300}, {QStringLiteral("endMs"), 1600},
                    {QStringLiteral("durationMs"), 350}}
    };
    const QVariantMap analysis = DubbingTimingService::analyzeSpeechOverlaps(segments, 100);
    QCOMPARE(analysis.value(QStringLiteral("blockingConflictCount")).toInt(), 2);
    QCOMPARE(analysis.value(QStringLiteral("conflicts")).toList().size(), 2);

    const QVariantList exactBoundary{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("boundary-a")},
                    {QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 1000},
                    {QStringLiteral("durationMs"), 1000}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("boundary-b")},
                    {QStringLiteral("startMs"), 1100}, {QStringLiteral("endMs"), 2100},
                    {QStringLiteral("durationMs"), 1000}}
    };
    const QVariantMap boundaryAnalysis =
        DubbingTimingService::analyzeSpeechOverlaps(exactBoundary, 100);
    QCOMPARE(boundaryAnalysis.value(QStringLiteral("blockingConflictCount")).toInt(), 0);
    QVERIFY(boundaryAnalysis.value(QStringLiteral("conflicts")).toList().isEmpty());

    QVariantMap rippleReport;
    QString error;
    const QVariantList rippled = DubbingTimingService::rippleForward(segments, 100, &rippleReport, &error);
    QVERIFY2(!rippled.isEmpty(), qPrintable(error));
    QCOMPARE(rippled.at(0).toMap().value(QStringLiteral("endMs")).toLongLong(), qint64(1000));
    QCOMPARE(rippled.at(1).toMap().value(QStringLiteral("startMs")).toLongLong(), qint64(1100));
    QCOMPARE(rippled.at(1).toMap().value(QStringLiteral("endMs")).toLongLong(), qint64(2000));
    QCOMPARE(rippled.at(1).toMap().value(QStringLiteral("words")).toList().first().toMap()
                 .value(QStringLiteral("startMs")).toLongLong(), qint64(1120));
    QCOMPARE(rippled.at(2).toMap().value(QStringLiteral("startMs")).toLongLong(), qint64(2100));
    QCOMPARE(rippleReport.value(QStringLiteral("blockingConflictCount")).toInt(), 0);
    QCOMPARE(rippleReport.value(QStringLiteral("originalConflicts")).toList().size(), 2);
    QCOMPARE(rippleReport.value(QStringLiteral("durationIncreaseMs")).toLongLong(), qint64(800));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    DubbingController controller(nullptr, nullptr);
    const QString projectPath = dir.filePath(QStringLiteral("timing.ladub.json"));
    QVERIFY(controller.newProject(projectPath));
    controller.addSegment(0, 800, QStringLiteral("First"));
    controller.addSegment(600, 1200, QStringLiteral("Second"));
    controller.updateSegment(0, {{QStringLiteral("durationMs"), 1000}});
    controller.updateSegment(1, {{QStringLiteral("durationMs"), 900}});
    DubbingJobRunner *runner = controller.findChild<DubbingJobRunner *>();
    QVERIFY(runner);
    runner->setPreviewPath(QStringLiteral("stale-preview.wav"));
    runner->setExportPath(QStringLiteral("stale-export.mp4"));
    const QVariantMap staleMixOutput{{QStringLiteral("audio"),
                                      QStringLiteral("stale-preview.wav")}};
    const QVariantMap staleExportOutput{{QStringLiteral("video"),
                                         QStringLiteral("stale-export.mp4")}};
    QVERIFY(QMetaObject::invokeMethod(runner, "stageCompleted", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("mix")),
                                      Q_ARG(QVariantMap, staleMixOutput)));
    QVERIFY(QMetaObject::invokeMethod(runner, "stageCompleted", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("export")),
                                      Q_ARG(QVariantMap, staleExportOutput)));
    QVERIFY(!controller.stepOutput(QStringLiteral("mix")).isEmpty());
    QVERIFY(!controller.stepOutput(QStringLiteral("export")).isEmpty());
    QVERIFY(!controller.timingConflicts().isEmpty());
    runner->setProcessingState(true, QStringLiteral("translation"), 42);
    QVERIFY(controller.processing());
    QVERIFY(controller.previewTimingResolution(QStringLiteral("ripple"), 100).isEmpty());
    QVERIFY(!controller.applyTimingResolution(QStringLiteral("ripple"), 100));
    QVERIFY(controller.processing());
    QCOMPARE(controller.stage(), QStringLiteral("translation"));
    QCOMPARE(controller.progress(), 42);
    QVERIFY(controller.lastError().contains(QStringLiteral("Wait for the current Dubbing operation")));
    QCOMPARE(controller.segments().at(1).toMap().value(QStringLiteral("startMs")).toLongLong(), qint64(600));
    runner->setProcessingState(false, QStringLiteral("ready"), 100);
    const QVariantMap preview = controller.previewTimingResolution(QStringLiteral("ripple"), 100);
    QVERIFY(!preview.isEmpty());
    QVERIFY(controller.applyTimingResolution(QStringLiteral("ripple"), 100));
    QCOMPARE(controller.segments().at(1).toMap().value(QStringLiteral("startMs")).toLongLong(), qint64(1100));
    QVERIFY(controller.stepOutput(QStringLiteral("mix")).isEmpty());
    QVERIFY(controller.stepOutput(QStringLiteral("export")).isEmpty());
    QVERIFY(controller.previewPath().isEmpty());
    QVERIFY(controller.exportPath().isEmpty());
    QVERIFY(controller.timingUndoAvailable());
    QVERIFY(controller.undoTimingResolution());
    QCOMPARE(controller.segments().at(1).toMap().value(QStringLiteral("startMs")).toLongLong(), qint64(600));
    QCOMPARE(controller.timingConfiguration().value(QStringLiteral("mode")).toString(),
             QStringLiteral("ripple"));

    DubbingController reopened(nullptr, nullptr);
    QVERIFY2(reopened.openProject(projectPath), qPrintable(reopened.lastError()));
    QCOMPARE(reopened.timingConfiguration().value(QStringLiteral("minimumGapMs")).toInt(), 100);
}

void TestDubbingProject::dubbingTimingUiWiresPreviewApplyAndUndo()
{
    const QDir sourceRoot(QStringLiteral(LASTUDIO_SOURCE_DIR));
    QFile page(sourceRoot.filePath(QStringLiteral("qml/pages/DubbingPage.qml")));
    QFile review(sourceRoot.filePath(QStringLiteral("qml/components/dubbing/DubbingVoiceClipReview.qml")));
    QVERIFY(page.open(QIODevice::ReadOnly));
    QVERIFY(review.open(QIODevice::ReadOnly));
    const QString pageSource = QString::fromUtf8(page.readAll());
    const QString reviewSource = QString::fromUtf8(review.readAll());
    QVERIFY(pageSource.contains(QStringLiteral("dubbingVoiceClipReview.qmlSmokeTimingResolutionCheck")));
    QVERIFY(reviewSource.contains(QStringLiteral("dubbingTimingResolutionPanel")));
    QVERIFY(reviewSource.contains(QStringLiteral("previewTimingResolution")));
    QVERIFY(reviewSource.contains(QStringLiteral("applyTimingResolution")));
    QVERIFY(reviewSource.contains(QStringLiteral("undoTimingResolution")));
    QVERIFY(reviewSource.contains(QStringLiteral("setIntentionalTimingOverlap")));
    QVERIFY(reviewSource.contains(QStringLiteral("measured duration")));
    QVERIFY(reviewSource.contains(QStringLiteral("Timeline: %1 ms → %2 ms")));
    QVERIFY(reviewSource.contains(QStringLiteral("originalStartMs")));
}

void TestDubbingProject::dubbingExportUiSeparatesMp4AndEditableCapCutDraft()
{
    const QDir sourceRoot(QStringLiteral(LASTUDIO_SOURCE_DIR));
    QFile page(sourceRoot.filePath(QStringLiteral("qml/pages/DubbingPage.qml")));
    QFile dialog(sourceRoot.filePath(QStringLiteral("qml/components/dubbing/DubbingExportDialog.qml")));
    QVERIFY(page.open(QIODevice::ReadOnly));
    QVERIFY(dialog.open(QIODevice::ReadOnly));
    const QString pageSource = QString::fromUtf8(page.readAll());
    const QString dialogSource = QString::fromUtf8(dialog.readAll());
    QVERIFY(pageSource.contains(QStringLiteral("exportOptionsDialog.qmlSmokeExportRoutesCheck")));
    QVERIFY(dialogSource.contains(QStringLiteral("Rendered Video (MP4)")));
    QVERIFY(dialogSource.contains(QStringLiteral("Export rendered MP4")));
    QVERIFY(dialogSource.contains(QStringLiteral("Editable CapCut Draft")));
    QVERIFY(dialogSource.contains(QStringLiteral("Export editable draft")));
    QVERIFY(dialogSource.contains(QStringLiteral("editable subtitle text segments")));
    QVERIFY(dialogSource.contains(QStringLiteral("CapCut import unverified")));
}

void TestDubbingProject::normalizesOcrOnlyTranscriptWithProvenance()
{
    const QVariantList normalized = DubbingTranscriptFusionService::normalizeOcrSegments({
        QVariantMap{{QStringLiteral("id"), QStringLiteral("ocr-vi")},
                    {QStringLiteral("startMs"), 120},
                    {QStringLiteral("endMs"), 980},
                    {QStringLiteral("text"), QStringLiteral("Xin chào thế giới")},
                    {QStringLiteral("confidence"), 0.91}},
        QVariantMap{{QStringLiteral("startMs"), 1000},
                    {QStringLiteral("endMs"), 1000},
                    {QStringLiteral("text"), QStringLiteral("invalid")}}
    });

    QCOMPARE(normalized.size(), 1);
    const QVariantMap segment = normalized.constFirst().toMap();
    QCOMPARE(segment.value(QStringLiteral("id")).toString(), QStringLiteral("ocr-vi"));
    QCOMPARE(segment.value(QStringLiteral("sourceText")).toString(), QStringLiteral("Xin chào thế giới"));
    QCOMPARE(segment.value(QStringLiteral("timingSource")).toString(), QStringLiteral("subtitle-ocr"));
    QCOMPARE(segment.value(QStringLiteral("ocrConfidence")).toDouble(), 0.91);
    const QVariantMap evidence = segment.value(QStringLiteral("transcriptProvenance"))
                                     .toList().constFirst().toMap();
    QCOMPARE(evidence.value(QStringLiteral("source")).toString(), QStringLiteral("ocr"));
}

void TestDubbingProject::fusesMatchingAndShiftedTranscriptWithoutDuplicates()
{
    const QVariantList stt{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("stt-1")},
                    {QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 1000},
                    {QStringLiteral("sourceText"), QStringLiteral("Xin chào thế giới")},
                    {QStringLiteral("sttConfidence"), 0.82}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("stt-2")},
                    {QStringLiteral("startMs"), 1800}, {QStringLiteral("endMs"), 2800},
                    {QStringLiteral("sourceText"), QStringLiteral("你好，世界")},
                    {QStringLiteral("sttConfidence"), 0.78}}
    };
    const QVariantList ocr{
        QVariantMap{{QStringLiteral("startMs"), 40}, {QStringLiteral("endMs"), 940},
                    {QStringLiteral("text"), QStringLiteral("Xin chào thế giới")},
                    {QStringLiteral("confidence"), 0.88}},
        // Center is shifted but stays within the deterministic timestamp/text threshold.
        QVariantMap{{QStringLiteral("startMs"), 2230}, {QStringLiteral("endMs"), 3230},
                    {QStringLiteral("text"), QStringLiteral("你好，世界")},
                    {QStringLiteral("confidence"), 0.90}}
    };

    const QVariantList fused = DubbingTranscriptFusionService::fuse(stt, ocr);
    QCOMPARE(fused.size(), 2);
    QCOMPARE(fused.at(0).toMap().value(QStringLiteral("startMs")).toLongLong(), qint64(0));
    QCOMPARE(fused.at(0).toMap().value(QStringLiteral("endMs")).toLongLong(), qint64(1000));
    QCOMPARE(fused.at(0).toMap().value(QStringLiteral("fusionStatus")).toString(), QStringLiteral("matched"));
    QCOMPARE(fused.at(1).toMap().value(QStringLiteral("startMs")).toLongLong(), qint64(1800));
    QCOMPARE(fused.at(1).toMap().value(QStringLiteral("fusionStatus")).toString(), QStringLiteral("matched"));
}

void TestDubbingProject::exposesConflictEvidenceWithoutSilentChoice()
{
    const QVariantList fused = DubbingTranscriptFusionService::fuse(
        {QVariantMap{{QStringLiteral("id"), QStringLiteral("stt-conflict")},
                     {QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 1200},
                     {QStringLiteral("sourceText"), QStringLiteral("the red car")},
                     {QStringLiteral("sttConfidence"), 0.70}}},
        {QVariantMap{{QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 1200},
                     {QStringLiteral("text"), QStringLiteral("the blue house")},
                     {QStringLiteral("confidence"), 0.98}}});

    QCOMPARE(fused.size(), 1);
    const QVariantMap conflict = fused.constFirst().toMap();
    QCOMPARE(conflict.value(QStringLiteral("fusionStatus")).toString(), QStringLiteral("conflict"));
    QVERIFY(conflict.value(QStringLiteral("fusionNeedsReview")).toBool());
    QCOMPARE(conflict.value(QStringLiteral("fusionChoice")).toString(), QStringLiteral("pending"));
    QCOMPARE(conflict.value(QStringLiteral("fusionPolicy")).toString(), QStringLiteral("ask"));
    QCOMPARE(conflict.value(QStringLiteral("sourceText")).toString(), QStringLiteral("the red car"));
    QCOMPARE(conflict.value(QStringLiteral("fusionSttText")).toString(), QStringLiteral("the red car"));
    QCOMPARE(conflict.value(QStringLiteral("fusionOcrText")).toString(), QStringLiteral("the blue house"));
    QCOMPARE(conflict.value(QStringLiteral("transcriptProvenance")).toList().size(), 2);
}

void TestDubbingProject::preservesFusionAndTranscriptSettingsAcrossProjectReload()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    DubbingProject project;
    project.projectPath = dir.filePath(QStringLiteral("fusion.ladub.json"));
    project.transcriptConfiguration = {
        {QStringLiteral("transcriptSource"), QStringLiteral("stt+ocr")},
        {QStringLiteral("ocrLanguage"), QStringLiteral("chi_sim")},
        {QStringLiteral("ocrExecutionRoute"), QStringLiteral("colab-gpu")},
        {QStringLiteral("ocrLocalEngineId"), QStringLiteral("paddleocr-ppocrv6-tiny")},
        {QStringLiteral("ocrLocalEngineVersion"), QStringLiteral("3.7.0")},
        {QStringLiteral("ocrColabModelId"), QStringLiteral("pp-ocrv5-multilingual-3.1")},
        {QStringLiteral("ocrRoi"), QVariantMap{{QStringLiteral("x"), 0.1},
                                                 {QStringLiteral("y"), 0.70},
                                                 {QStringLiteral("width"), 0.8},
                                                 {QStringLiteral("height"), 0.2}}},
        {QStringLiteral("ocrSampleIntervalMs"), 700},
        {QStringLiteral("ocrMinimumConfidence"), 0.60}
    };
    project.segments = DubbingTranscriptFusionService::fuse(
        {QVariantMap{{QStringLiteral("id"), QStringLiteral("stable")},
                     {QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 1000},
                     {QStringLiteral("sourceText"), QStringLiteral("alpha")},
                     {QStringLiteral("sttConfidence"), 0.6}}},
        {QVariantMap{{QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 1000},
                     {QStringLiteral("text"), QStringLiteral("beta")},
                     {QStringLiteral("confidence"), 0.9}}});
    QString error;
    QVERIFY2(project.save(&error), qPrintable(error));
    DubbingProject restored;
    QVERIFY2(DubbingProject::load(project.projectPath, restored, &error), qPrintable(error));
    QCOMPARE(restored.transcriptConfiguration.value(QStringLiteral("transcriptSource")).toString(),
             QStringLiteral("stt+ocr"));
    QCOMPARE(restored.transcriptConfiguration.value(QStringLiteral("ocrRoi")).toMap()
                 .value(QStringLiteral("y")).toDouble(), 0.70);
    QCOMPARE(restored.transcriptConfiguration.value(QStringLiteral("ocrExecutionRoute")).toString(),
             QStringLiteral("colab-gpu"));
    QCOMPARE(restored.transcriptConfiguration.value(QStringLiteral("ocrLocalEngineId")).toString(),
             QStringLiteral("paddleocr-ppocrv6-tiny"));
    QCOMPARE(restored.segments.constFirst().toMap().value(QStringLiteral("fusionStatus")).toString(),
             QStringLiteral("conflict"));
    QCOMPARE(restored.segments.constFirst().toMap().value(QStringLiteral("fusionOcrText")).toString(),
             QStringLiteral("beta"));

    SubtitleOcrController subtitleOcr(nullptr, nullptr);
    DubbingController controller(nullptr, nullptr);
    controller.setSubtitleOcrController(&subtitleOcr);
    QVERIFY2(controller.openProject(project.projectPath), qPrintable(controller.lastError()));
    QCOMPARE(subtitleOcr.executionRoute(), QStringLiteral("colab-gpu"));
    QCOMPARE(subtitleOcr.localEngineId(), QStringLiteral("paddleocr-ppocrv6-tiny"));
    QCOMPARE(subtitleOcr.localEngineVersion(), QStringLiteral("3.7.0"));
    QCOMPARE(subtitleOcr.colabModelId(), QStringLiteral("pp-ocrv5-multilingual-3.1"));
    QVERIFY(!controller.customReady());
    QVERIFY(controller.customStatusText().contains(QStringLiteral("Connect and check")));
}

void TestDubbingProject::ocrOnlyTranscriptUsesTheSharedSubtitleOcrController()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source video.mp4"));
    const QString ffprobe = directory.filePath(QStringLiteral("ffprobe.cmd"));
    const QString ffmpeg = directory.filePath(QStringLiteral("ffmpeg.cmd"));
    const QString tesseract = directory.filePath(QStringLiteral("tesseract.cmd"));
    QVERIFY(writeFixtureFile(source, QByteArrayLiteral("video fixture")));
    QVERIFY(writeFixtureFile(ffprobe, QByteArrayLiteral(
        "@echo off\r\necho {\"streams\":[{\"width\":1920,\"height\":1080}],\"format\":{\"duration\":\"4.0\"}}\r\n")));
    QVERIFY(writeFixtureFile(ffmpeg, batchSubtitleOcrFfmpegScript()));
    QVERIFY(writeFixtureFile(tesseract, QByteArrayLiteral(
        "@echo off\r\nif /I \"%~1\"==\"--list-langs\" (\r\n  echo List of available languages ^(1^):\r\n  echo eng\r\n  exit /b 0\r\n)\r\necho level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\r\necho 5\t1\t1\t1\t1\t1\t0\t0\t10\t10\t96\tShared\r\necho 5\t1\t1\t1\t1\t2\t10\t0\t10\t10\t94\tOCR\r\n")));

    ScopedEnvironmentValue ffmpegEnvironment("LASTUDIO_FFMPEG", ffmpeg.toUtf8());
    ScopedEnvironmentValue ffprobeEnvironment("LASTUDIO_FFPROBE", ffprobe.toUtf8());
    ScopedEnvironmentValue tesseractEnvironment("LASTUDIO_TESSERACT", tesseract.toUtf8());
    ScopedEnvironmentValue localEngineEnvironment("LASTUDIO_SUBTITLE_OCR_ENGINE",
                                                  QByteArrayLiteral("tesseract-baseline"));
    SubtitleOcrController ocr(nullptr, nullptr);
    DubbingJobRunner runner(nullptr, nullptr, static_cast<ModelManager *>(nullptr),
                            static_cast<RuntimeManager *>(nullptr));
    runner.setSubtitleOcrController(&ocr);
    QSignalSpy completed(&runner, &DubbingJobRunner::segmentsUpdated);
    QSignalSpy failed(&runner, &DubbingJobRunner::errorOccurred);

    runner.startTranscription(QStringLiteral("en"), QString(), QString(),
                              {{QStringLiteral("parameters"), QVariantMap{
                                  {QStringLiteral("transcriptSource"), QStringLiteral("ocr")},
                                  {QStringLiteral("ocrSourceMedia"), source},
                                  {QStringLiteral("ocrLanguage"), QStringLiteral("eng")},
                                  {QStringLiteral("ocrSampleIntervalMs"), 1000},
                                  {QStringLiteral("ocrMinimumConfidence"), 0.90}}}});
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 10000);
    QCOMPARE(failed.count(), 0);
    const QVariantList transcript = completed.constFirst().constFirst().toList();
    QCOMPARE(transcript.size(), 1);
    const QVariantMap segment = transcript.constFirst().toMap();
    QCOMPARE(segment.value(QStringLiteral("sourceText")).toString(), QStringLiteral("Shared OCR"));
    QCOMPARE(segment.value(QStringLiteral("timingSource")).toString(), QStringLiteral("subtitle-ocr"));
    QCOMPARE(segment.value(QStringLiteral("transcriptProvenance")).toList().constFirst().toMap()
                 .value(QStringLiteral("source")).toString(), QStringLiteral("ocr"));
    QVERIFY(!runner.processing());
}

void TestDubbingProject::sttOnlyTranscriptDoesNotRequireOcrRuntime()
{
    DubbingSttWorkerMock worker(true);
    QVERIFY(worker.start());

    AppController *app = AppController::instance();
    QVERIFY(app != nullptr);
    ColabSession *session = app->colabSttSession();
    SttSessionController *stt = app->sttSession();
    QVERIFY(session != nullptr);
    QVERIFY(stt != nullptr);
    ColabSessionReset resetSession(session);
    QString sessionError;
    QVERIFY2(session->setSession(worker.workerUrl(), QStringLiteral("stt-only-token"),
                                 &sessionError, true), qPrintable(sessionError));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString audioPath = directory.filePath(QStringLiteral("source.wav"));
    const QVector<float> samples(16000, 0.1F);
    QVERIFY(WavIO::saveFloat(audioPath, samples.constData(), samples.size(), 16000));

    // No SubtitleOcrController is injected: the STT-only route must still
    // complete, proving it does not gate on an OCR runtime or language pack.
    DubbingJobRunner runner(stt, nullptr, static_cast<ModelManager *>(nullptr),
                            static_cast<RuntimeManager *>(nullptr));
    QSignalSpy completed(&runner, &DubbingJobRunner::segmentsUpdated);
    QSignalSpy failed(&runner, &DubbingJobRunner::errorOccurred);
    runner.startTranscription(QStringLiteral("en"), audioPath, {}, {
        {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
        {QStringLiteral("modelId"), QStringLiteral("whisper.cpp")},
        {QStringLiteral("parameters"), QVariantMap{
            {QStringLiteral("transcriptSource"), QStringLiteral("stt")}}}
    });

    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 10000);
    QCOMPARE(failed.count(), 0);
    const QVariantList transcript = completed.constFirst().constFirst().toList();
    QCOMPARE(transcript.size(), 1);
    QCOMPARE(transcript.constFirst().toMap().value(QStringLiteral("sourceText")).toString(),
             QStringLiteral("Shared OCR"));
    QVERIFY(worker.requests().contains("POST /v2/uploads/stt HTTP/1.1\r\n"));
    QVERIFY(!runner.processing());
}

void TestDubbingProject::combinedTranscriptRunsSttAndSharedOcrWithoutFallback()
{
    DubbingSttWorkerMock worker(true);
    QVERIFY(worker.start());

    AppController *app = AppController::instance();
    QVERIFY(app != nullptr);
    ColabSession *session = app->colabSttSession();
    SttSessionController *stt = app->sttSession();
    QVERIFY(session != nullptr);
    QVERIFY(stt != nullptr);
    ColabSessionReset resetSession(session);
    QString sessionError;
    QVERIFY2(session->setSession(worker.workerUrl(), QStringLiteral("combined-stt-token"),
                                 &sessionError, true), qPrintable(sessionError));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString audioPath = directory.filePath(QStringLiteral("source.wav"));
    const QVector<float> samples(16000, 0.1F);
    QVERIFY(WavIO::saveFloat(audioPath, samples.constData(), samples.size(), 16000));
    const QString videoPath = directory.filePath(QStringLiteral("source video.mp4"));
    const QString ffprobe = directory.filePath(QStringLiteral("ffprobe.cmd"));
    const QString ffmpeg = directory.filePath(QStringLiteral("ffmpeg.cmd"));
    const QString tesseract = directory.filePath(QStringLiteral("tesseract.cmd"));
    QVERIFY(writeFixtureFile(videoPath, QByteArrayLiteral("video fixture")));
    QVERIFY(writeFixtureFile(ffprobe, QByteArrayLiteral(
        "@echo off\r\necho {\"streams\":[{\"width\":1920,\"height\":1080}],\"format\":{\"duration\":\"4.0\"}}\r\n")));
    QVERIFY(writeFixtureFile(ffmpeg, batchSubtitleOcrFfmpegScript()));
    QVERIFY(writeFixtureFile(tesseract, QByteArrayLiteral(
        "@echo off\r\nif /I \"%~1\"==\"--list-langs\" (\r\n  echo List of available languages ^(1^):\r\n  echo eng\r\n  exit /b 0\r\n)\r\necho level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\r\necho 5\t1\t1\t1\t1\t1\t0\t0\t10\t10\t96\tShared\r\necho 5\t1\t1\t1\t1\t2\t10\t0\t10\t10\t94\tOCR\r\n")));
    ScopedEnvironmentValue ffmpegEnvironment("LASTUDIO_FFMPEG", ffmpeg.toUtf8());
    ScopedEnvironmentValue ffprobeEnvironment("LASTUDIO_FFPROBE", ffprobe.toUtf8());
    ScopedEnvironmentValue tesseractEnvironment("LASTUDIO_TESSERACT", tesseract.toUtf8());
    ScopedEnvironmentValue localEngineEnvironment("LASTUDIO_SUBTITLE_OCR_ENGINE",
                                                  QByteArrayLiteral("tesseract-baseline"));

    SubtitleOcrController ocr(nullptr, nullptr);
    DubbingJobRunner runner(stt, nullptr, static_cast<ModelManager *>(nullptr),
                            static_cast<RuntimeManager *>(nullptr));
    runner.setSubtitleOcrController(&ocr);
    QSignalSpy completed(&runner, &DubbingJobRunner::segmentsUpdated);
    QSignalSpy failed(&runner, &DubbingJobRunner::errorOccurred);
    const QVariantMap configuration{
        {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
        {QStringLiteral("modelId"), QStringLiteral("whisper.cpp")},
        {QStringLiteral("parameters"), QVariantMap{
            {QStringLiteral("transcriptSource"), QStringLiteral("stt+ocr")},
            {QStringLiteral("ocrSourceMedia"), videoPath},
            {QStringLiteral("ocrLanguage"), QStringLiteral("eng")},
            {QStringLiteral("ocrSampleIntervalMs"), 1000},
            {QStringLiteral("ocrMinimumConfidence"), 0.90}}}
    };
    runner.startTranscription(QStringLiteral("en"), audioPath, {}, configuration);

    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 15000);
    QCOMPARE(failed.count(), 0);
    const QVariantList transcript = completed.constFirst().constFirst().toList();
    QCOMPARE(transcript.size(), 1);
    const QVariantMap segment = transcript.constFirst().toMap();
    QCOMPARE(segment.value(QStringLiteral("sourceText")).toString(), QStringLiteral("Shared OCR"));
    QCOMPARE(segment.value(QStringLiteral("fusionStatus")).toString(), QStringLiteral("matched"));
    QCOMPARE(segment.value(QStringLiteral("timingSource")).toString(),
             QStringLiteral("asr-with-ocr-text"));
    QCOMPARE(segment.value(QStringLiteral("transcriptProvenance")).toList().size(), 2);
    QVERIFY(worker.requests().contains("POST /v2/uploads/stt HTTP/1.1\r\n"));
    QVERIFY(worker.requests().contains("POST /v2/uploads/stt/dubbing-stt-upload/commit HTTP/1.1\r\n"));
    QVERIFY(worker.requests().contains("GET /v2/jobs/transcriptions/dubbing-stt-job HTTP/1.1\r\n"));
    QVERIFY(!runner.processing());
}

void TestDubbingProject::combinedTranscriptReportsOcrFailureWithoutSttFallback()
{
    DubbingSttWorkerMock worker(true);
    QVERIFY(worker.start());

    AppController *app = AppController::instance();
    QVERIFY(app != nullptr);
    ColabSession *session = app->colabSttSession();
    SttSessionController *stt = app->sttSession();
    QVERIFY(session != nullptr);
    QVERIFY(stt != nullptr);
    ColabSessionReset resetSession(session);
    QString sessionError;
    QVERIFY2(session->setSession(worker.workerUrl(), QStringLiteral("combined-error-token"),
                                 &sessionError, true), qPrintable(sessionError));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString audioPath = directory.filePath(QStringLiteral("source.wav"));
    const QVector<float> samples(16000, 0.1F);
    QVERIFY(WavIO::saveFloat(audioPath, samples.constData(), samples.size(), 16000));

    // Deliberately do not inject SubtitleOcrController. The combined route
    // must name the OCR failure and stop; it may not silently publish STT.
    DubbingJobRunner runner(stt, nullptr, static_cast<ModelManager *>(nullptr),
                            static_cast<RuntimeManager *>(nullptr));
    QSignalSpy completed(&runner, &DubbingJobRunner::segmentsUpdated);
    QSignalSpy failed(&runner, &DubbingJobRunner::errorOccurred);
    runner.startTranscription(QStringLiteral("en"), audioPath, {}, {
        {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
        {QStringLiteral("modelId"), QStringLiteral("whisper.cpp")},
        {QStringLiteral("parameters"), QVariantMap{
            {QStringLiteral("transcriptSource"), QStringLiteral("stt+ocr")},
            {QStringLiteral("ocrSourceMedia"), directory.filePath(QStringLiteral("source.mp4"))}}}
    });

    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    QCOMPARE(completed.count(), 0);
    const QString error = failed.constFirst().constFirst().toString();
    QVERIFY(error.contains(QStringLiteral("OCR transcript failed")));
    QVERIFY(error.contains(QStringLiteral("Subtitle OCR controller is unavailable")));
    QVERIFY(!runner.processing());
}

void TestDubbingProject::reviewerMustResolveFusionConflictExplicitly()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DubbingController dubbing(nullptr, nullptr, static_cast<ModelManager *>(nullptr),
                              static_cast<RuntimeManager *>(nullptr));
    QVERIFY(dubbing.newProject(directory.filePath(QStringLiteral("review.ladub.json"))));
    dubbing.addSegment(0, 1000, QStringLiteral("source from STT"));
    dubbing.updateSegment(0, {{QStringLiteral("fusionStatus"), QStringLiteral("conflict")},
                              {QStringLiteral("fusionNeedsReview"), true},
                              {QStringLiteral("fusionSttText"), QStringLiteral("source from STT")},
                              {QStringLiteral("fusionOcrText"), QStringLiteral("source from OCR")},
                              {QStringLiteral("sttConfidence"), 0.65},
                              {QStringLiteral("ocrConfidence"), 0.91}});
    QVERIFY(!dubbing.resolveTranscriptConflict(0, QStringLiteral("automatic")));
    QVERIFY(dubbing.resolveTranscriptConflict(0, QStringLiteral("ocr")));
    const QVariantMap resolved = dubbing.segments().constFirst().toMap();
    QCOMPARE(resolved.value(QStringLiteral("sourceText")).toString(), QStringLiteral("source from OCR"));
    QCOMPARE(resolved.value(QStringLiteral("fusionChoice")).toString(), QStringLiteral("ocr"));
    QCOMPARE(resolved.value(QStringLiteral("fusionStatus")).toString(), QStringLiteral("resolved"));
    QVERIFY(!resolved.value(QStringLiteral("fusionNeedsReview")).toBool());

    DubbingProject reopened;
    QString error;
    QVERIFY2(DubbingProject::load(dubbing.projectPath(), reopened, &error), qPrintable(error));
    QCOMPARE(reopened.segments.constFirst().toMap().value(QStringLiteral("fusionChoice")).toString(),
             QStringLiteral("ocr"));
}

void TestDubbingProject::transcriptModePersistsAndColabCardsUseOnlyActiveSourceAndRoute()
{
    const auto stage = [](const QVariantList &stages, const QString &id) {
        for (const QVariant &entry : stages) {
            const QVariantMap value = entry.toMap();
            if (value.value(QStringLiteral("id")).toString() == id) return value;
        }
        return QVariantMap{};
    };

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString projectPath = directory.filePath(QStringLiteral("transcript-modes.ladub.json"));
    DubbingController controller(nullptr, nullptr, static_cast<ModelManager *>(nullptr),
                              static_cast<RuntimeManager *>(nullptr));
    QVERIFY(controller.newProject(projectPath));
    QVERIFY(controller.setWorkflowNodeParameters(QStringLiteral("transcribe"), {
        {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
        {QStringLiteral("modelId"), QStringLiteral("whisper.cpp")},
        {QStringLiteral("transcriptSource"), QStringLiteral("stt+ocr")},
        {QStringLiteral("fusionPolicy"), QStringLiteral("ask")},
        {QStringLiteral("ocrExecutionRoute"), QStringLiteral("colab-gpu")},
        {QStringLiteral("ocrColabModelId"), QStringLiteral("pp-ocrv5-multilingual-3.1")}
    }));
    QCOMPARE(controller.transcriptConfiguration().value(QStringLiteral("transcriptSource")).toString(),
             QStringLiteral("stt+ocr"));
    QCOMPARE(controller.transcriptConfiguration().value(QStringLiteral("sttExecutionProvider")).toString(),
             QStringLiteral("colab-direct"));
    QCOMPARE(controller.transcriptConfiguration().value(QStringLiteral("sttModelId")).toString(),
             QStringLiteral("whisper.cpp"));

    QVariantList stages = controller.colabSetupStages();
    QVERIFY(stage(stages, QStringLiteral("transcribe")).value(QStringLiteral("activeForTranscriptSource")).toBool());
    QVERIFY(stage(stages, QStringLiteral("transcribe")).value(QStringLiteral("selectedForDirectColab")).toBool());
    QVERIFY(stage(stages, QStringLiteral("subtitle-ocr")).value(QStringLiteral("activeForTranscriptSource")).toBool());
    QVERIFY(stage(stages, QStringLiteral("subtitle-ocr")).value(QStringLiteral("selectedForDirectColab")).toBool());

    QVERIFY(controller.setWorkflowNodeParameters(QStringLiteral("transcribe"), {
        {QStringLiteral("transcriptSource"), QStringLiteral("ocr")}}));
    stages = controller.colabSetupStages();
    QVERIFY(!stage(stages, QStringLiteral("transcribe")).value(QStringLiteral("activeForTranscriptSource")).toBool());
    QVERIFY(!stage(stages, QStringLiteral("transcribe")).value(QStringLiteral("selectedForDirectColab")).toBool());
    QVERIFY(stage(stages, QStringLiteral("subtitle-ocr")).value(QStringLiteral("activeForTranscriptSource")).toBool());
    QVERIFY(stage(stages, QStringLiteral("subtitle-ocr")).value(QStringLiteral("selectedForDirectColab")).toBool());

    QVERIFY(controller.setWorkflowNodeParameters(QStringLiteral("transcribe"), {
        {QStringLiteral("executionProvider"), QStringLiteral("local-dev")},
        {QStringLiteral("transcriptSource"), QStringLiteral("stt")}}));
    stages = controller.colabSetupStages();
    QVERIFY(!stage(stages, QStringLiteral("transcribe")).value(QStringLiteral("selectedForDirectColab")).toBool());
    QVERIFY(!stage(stages, QStringLiteral("subtitle-ocr")).value(QStringLiteral("activeForTranscriptSource")).toBool());
    // An inactive OCR route is not allowed to make Check all selected fail.
    QVERIFY(controller.validateAllWorkflowColabStages());

    QVERIFY(controller.setWorkflowNodeParameters(QStringLiteral("transcribe"), {
        {QStringLiteral("transcriptSource"), QStringLiteral("ocr")}}));
    QVERIFY(!controller.validateAllWorkflowColabStages());

    QVERIFY(controller.setWorkflowNodeParameters(QStringLiteral("transcribe"), {
        {QStringLiteral("executionProvider"), QStringLiteral("colab-direct")},
        {QStringLiteral("modelId"), QStringLiteral("whisper.cpp")},
        {QStringLiteral("transcriptSource"), QStringLiteral("stt+ocr")}}));
    QVERIFY(controller.saveProject());

    DubbingController reopened(nullptr, nullptr, static_cast<ModelManager *>(nullptr),
                               static_cast<RuntimeManager *>(nullptr));
    QVERIFY2(reopened.openProject(projectPath), qPrintable(reopened.lastError()));
    QCOMPARE(reopened.transcriptConfiguration().value(QStringLiteral("transcriptSource")).toString(),
             QStringLiteral("stt+ocr"));
    QCOMPARE(reopened.transcriptConfiguration().value(QStringLiteral("sttExecutionProvider")).toString(),
             QStringLiteral("colab-direct"));
    QCOMPARE(reopened.transcriptConfiguration().value(QStringLiteral("sttModelId")).toString(),
             QStringLiteral("whisper.cpp"));
    QCOMPARE(stage(reopened.colabSetupStages(), QStringLiteral("transcribe"))
                 .value(QStringLiteral("modelId")).toString(), QStringLiteral("whisper.cpp"));
}

void TestDubbingProject::fusionPoliciesAndBulkResolutionPreserveOriginalEvidence()
{
    const QVariantList stt{QVariantMap{{QStringLiteral("id"), QStringLiteral("policy-stt")},
                                       {QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 1000},
                                       {QStringLiteral("sourceText"), QStringLiteral("one red car")},
                                       {QStringLiteral("sttConfidence"), 0.61}}};
    const QVariantList ocr{QVariantMap{{QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 1000},
                                       {QStringLiteral("text"), QStringLiteral("one blue house")},
                                       {QStringLiteral("confidence"), 0.92}}};
    const QVariantMap asked = DubbingTranscriptFusionService::fuse(stt, ocr, QStringLiteral("ask"))
                                  .constFirst().toMap();
    QCOMPARE(asked.value(QStringLiteral("fusionChoice")).toString(), QStringLiteral("pending"));
    QCOMPARE(asked.value(QStringLiteral("fusionStatus")).toString(), QStringLiteral("conflict"));
    QVERIFY(asked.value(QStringLiteral("fusionNeedsReview")).toBool());
    const QVariantMap preferred = DubbingTranscriptFusionService::fuse(
        stt, ocr, QStringLiteral("prefer-ocr")).constFirst().toMap();
    QCOMPARE(preferred.value(QStringLiteral("sourceText")).toString(), QStringLiteral("one blue house"));
    QCOMPARE(preferred.value(QStringLiteral("fusionResolutionPolicy")).toString(),
             QStringLiteral("prefer-ocr"));
    QCOMPARE(preferred.value(QStringLiteral("fusionSttText")).toString(), QStringLiteral("one red car"));
    QCOMPARE(preferred.value(QStringLiteral("fusionOcrText")).toString(), QStringLiteral("one blue house"));
    QCOMPARE(preferred.value(QStringLiteral("transcriptProvenance")).toList().size(), 2);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DubbingController controller(nullptr, nullptr, static_cast<ModelManager *>(nullptr),
                              static_cast<RuntimeManager *>(nullptr));
    QVERIFY(controller.newProject(directory.filePath(QStringLiteral("bulk-review.ladub.json"))));
    controller.addSegment(0, 1000, QStringLiteral("STT one"));
    controller.addSegment(1100, 2100, QStringLiteral("STT two"));
    for (int index = 0; index < 2; ++index) {
        controller.updateSegment(index, {
            {QStringLiteral("fusionStatus"), QStringLiteral("conflict")},
            {QStringLiteral("fusionNeedsReview"), true},
            {QStringLiteral("fusionSttText"), index == 0 ? QStringLiteral("STT one") : QStringLiteral("STT two")},
            {QStringLiteral("fusionOcrText"), index == 0 ? QStringLiteral("OCR one") : QStringLiteral("OCR two")},
            {QStringLiteral("sttConfidence"), 0.60}, {QStringLiteral("ocrConfidence"), 0.93},
            {QStringLiteral("transcriptProvenance"), QVariantList{QVariantMap{{QStringLiteral("source"), QStringLiteral("stt")}},
                                                                      QVariantMap{{QStringLiteral("source"), QStringLiteral("ocr")}}}}
        });
    }
    QCOMPARE(controller.unresolvedTranscriptConflictCount(), 2);
    QVERIFY(controller.resolveAllTranscriptConflicts(QStringLiteral("ocr")));
    QCOMPARE(controller.unresolvedTranscriptConflictCount(), 0);
    for (const QVariant &entry : controller.segments()) {
        const QVariantMap resolved = entry.toMap();
        QVERIFY(resolved.value(QStringLiteral("sourceText")).toString().startsWith(QStringLiteral("OCR")));
        QCOMPARE(resolved.value(QStringLiteral("fusionResolutionPolicy")).toString(),
                 QStringLiteral("bulk-manual"));
        QCOMPARE(resolved.value(QStringLiteral("transcriptProvenance")).toList().size(), 2);
    }
    QVERIFY(controller.saveProject());
    DubbingProject restored;
    QString error;
    QVERIFY2(DubbingProject::load(controller.projectPath(), restored, &error), qPrintable(error));
    QCOMPARE(restored.segments.constFirst().toMap().value(QStringLiteral("fusionSttText")).toString(),
             QStringLiteral("STT one"));
    QCOMPARE(restored.segments.constFirst().toMap().value(QStringLiteral("fusionOcrText")).toString(),
             QStringLiteral("OCR one"));
}

void TestDubbingProject::unresolvedTranscriptConflictsBlockTranslationAndManualReviewPersists()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString mediaPath = directory.filePath(QStringLiteral("source.mp4"));
    QVERIFY(writeFixtureFile(mediaPath, QByteArrayLiteral("media fixture")));
    DubbingProject project;
    project.projectPath = directory.filePath(QStringLiteral("translation-block.ladub.json"));
    project.sourceMediaPath = mediaPath;
    project.sourceLanguage = QStringLiteral("en");
    project.targetLanguage = QStringLiteral("vi");
    project.segments = {QVariantMap{{QStringLiteral("id"), QStringLiteral("blocked-conflict")},
                                    {QStringLiteral("startMs"), 0}, {QStringLiteral("endMs"), 1000},
                                    {QStringLiteral("sourceText"), QStringLiteral("STT original")},
                                    {QStringLiteral("fusionStatus"), QStringLiteral("conflict")},
                                    {QStringLiteral("fusionNeedsReview"), true},
                                    {QStringLiteral("fusionSttText"), QStringLiteral("STT original")},
                                    {QStringLiteral("fusionOcrText"), QStringLiteral("OCR original")},
                                    {QStringLiteral("sttConfidence"), 0.52},
                                    {QStringLiteral("ocrConfidence"), 0.88}}};
    QString error;
    QVERIFY2(project.save(&error), qPrintable(error));

    DubbingController controller(nullptr, nullptr, static_cast<ModelManager *>(nullptr),
                              static_cast<RuntimeManager *>(nullptr));
    QVERIFY2(controller.openProject(project.projectPath), qPrintable(controller.lastError()));
    controller.translateSource();
    QVERIFY(controller.lastError().contains(QStringLiteral("Resolve 1 STT/OCR conflict")));
    QCOMPARE(controller.unresolvedTranscriptConflictCount(), 1);
    controller.updateSegment(0, {{QStringLiteral("sourceText"), QStringLiteral("Reviewer final source")}});
    QCOMPARE(controller.unresolvedTranscriptConflictCount(), 0);
    const QVariantMap resolved = controller.segments().constFirst().toMap();
    QCOMPARE(resolved.value(QStringLiteral("fusionChoice")).toString(), QStringLiteral("manual"));
    QCOMPARE(resolved.value(QStringLiteral("fusionSttText")).toString(), QStringLiteral("STT original"));
    QCOMPARE(resolved.value(QStringLiteral("fusionOcrText")).toString(), QStringLiteral("OCR original"));
    QVERIFY(controller.saveProject());
    DubbingProject restored;
    QVERIFY2(DubbingProject::load(project.projectPath, restored, &error), qPrintable(error));
    QCOMPARE(restored.segments.constFirst().toMap().value(QStringLiteral("sourceText")).toString(),
             QStringLiteral("Reviewer final source"));
    QCOMPARE(restored.segments.constFirst().toMap().value(QStringLiteral("fusionStatus")).toString(),
             QStringLiteral("resolved"));
}

void TestDubbingProject::aiReconciliationCapabilityAndReviewDecisionsPreserveEvidence()
{
    QString reason;
    QVERIFY(!DubbingTranslationFixService::reconciliationAvailable({
        {QStringLiteral("configured"), true}, {QStringLiteral("provider"), QStringLiteral("local")},
        {QStringLiteral("supportsStructuredReconciliation"), true}}, &reason));
    QVERIFY(reason.contains(QStringLiteral("translation runtime"), Qt::CaseInsensitive));
    QVERIFY(!DubbingTranslationFixService::reconciliationAvailable({
        {QStringLiteral("configured"), true}, {QStringLiteral("provider"), QStringLiteral("api")},
        {QStringLiteral("serverUrl"), QStringLiteral("https://example.invalid")},
        {QStringLiteral("model"), QStringLiteral("unverified-model")}}, &reason));
    QVERIFY(reason.contains(QStringLiteral("explicitly marked"), Qt::CaseInsensitive));
    QVERIFY(DubbingTranslationFixService::reconciliationAvailable({
        {QStringLiteral("configured"), true}, {QStringLiteral("provider"), QStringLiteral("api")},
        {QStringLiteral("serverUrl"), QStringLiteral("https://example.invalid")},
        {QStringLiteral("model"), QStringLiteral("structured-text-llm")},
        {QStringLiteral("supportsStructuredReconciliation"), true}}, &reason));
    QVERIFY(reason.isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DubbingController controller(nullptr, nullptr, static_cast<ModelManager *>(nullptr),
                              static_cast<RuntimeManager *>(nullptr));
    QVERIFY(controller.newProject(directory.filePath(QStringLiteral("ai-review.ladub.json"))));
    controller.addSegment(0, 1000, QStringLiteral("STT first"));
    controller.addSegment(1100, 2100, QStringLiteral("STT second"));
    for (int index = 0; index < 2; ++index) {
        controller.updateSegment(index, {
            {QStringLiteral("fusionStatus"), QStringLiteral("conflict")},
            {QStringLiteral("fusionNeedsReview"), true},
            {QStringLiteral("fusionSttText"), index == 0 ? QStringLiteral("STT first") : QStringLiteral("STT second")},
            {QStringLiteral("fusionOcrText"), index == 0 ? QStringLiteral("OCR first") : QStringLiteral("OCR second")},
            {QStringLiteral("fusionAiSuggestion"), index == 0 ? QStringLiteral("AI proposed first") : QStringLiteral("AI proposed second")},
            {QStringLiteral("fusionAiSuggestionStatus"), QStringLiteral("pending")},
            {QStringLiteral("fusionAiSuggestionLanguage"), QStringLiteral("en")},
            {QStringLiteral("fusionAiSuggestionEvidence"), QVariantMap{{QStringLiteral("sttText"), index == 0 ? QStringLiteral("STT first") : QStringLiteral("STT second")},
                                                                           {QStringLiteral("ocrText"), index == 0 ? QStringLiteral("OCR first") : QStringLiteral("OCR second")}}}
        });
    }
    QVERIFY(controller.acceptTranscriptConflictAiSuggestion(0));
    const QVariantMap accepted = controller.segments().at(0).toMap();
    QCOMPARE(accepted.value(QStringLiteral("sourceText")).toString(), QStringLiteral("AI proposed first"));
    QCOMPARE(accepted.value(QStringLiteral("fusionAiSuggestionLanguage")).toString(), QStringLiteral("en"));
    QCOMPARE(accepted.value(QStringLiteral("fusionSttText")).toString(), QStringLiteral("STT first"));
    QCOMPARE(accepted.value(QStringLiteral("fusionOcrText")).toString(), QStringLiteral("OCR first"));
    QVERIFY(controller.rejectTranscriptConflictAiSuggestion(1));
    const QVariantMap rejected = controller.segments().at(1).toMap();
    QCOMPARE(rejected.value(QStringLiteral("fusionAiSuggestionStatus")).toString(), QStringLiteral("rejected"));
    QVERIFY(rejected.value(QStringLiteral("fusionNeedsReview")).toBool());
    QCOMPARE(controller.unresolvedTranscriptConflictCount(), 1);
    QVERIFY(controller.saveProject());
    DubbingProject restored;
    QString error;
    QVERIFY2(DubbingProject::load(controller.projectPath(), restored, &error), qPrintable(error));
    QCOMPARE(restored.segments.at(0).toMap().value(QStringLiteral("fusionSttText")).toString(),
             QStringLiteral("STT first"));
    QCOMPARE(restored.segments.at(1).toMap().value(QStringLiteral("fusionAiSuggestionStatus")).toString(),
             QStringLiteral("rejected"));
}

void TestDubbingProject::transcriptConflictUiAndColabSetupWireProductionController()
{
    const QDir sourceRoot(QStringLiteral(LASTUDIO_SOURCE_DIR));
    QFile page(sourceRoot.filePath(QStringLiteral("qml/pages/DubbingPage.qml")));
    QFile setup(sourceRoot.filePath(QStringLiteral("qml/components/dubbing/DubbingColabSetupDialog.qml")));
    QFile controller(sourceRoot.filePath(QStringLiteral("src/controllers/dubbing/DubbingController.cpp")));
    QFile service(sourceRoot.filePath(QStringLiteral("src/controllers/dubbing/DubbingTranslationFixService.cpp")));
    QFile adapter(sourceRoot.filePath(QStringLiteral("src/dubbing/workflow/DubbingWorkflowAdapter.cpp")));
    QVERIFY(page.open(QIODevice::ReadOnly));
    QVERIFY(setup.open(QIODevice::ReadOnly));
    QVERIFY(controller.open(QIODevice::ReadOnly));
    QVERIFY(service.open(QIODevice::ReadOnly));
    QVERIFY(adapter.open(QIODevice::ReadOnly));
    const QString pageSource = QString::fromUtf8(page.readAll());
    const QString setupSource = QString::fromUtf8(setup.readAll());
    const QString controllerSource = QString::fromUtf8(controller.readAll());
    const QString serviceSource = QString::fromUtf8(service.readAll());
    const QString adapterSource = QString::fromUtf8(adapter.readAll());
    QVERIFY(pageSource.contains(QStringLiteral("dubbingTranscriptSourceMode")));
    QVERIFY(pageSource.contains(QString::fromUtf8("Chỉ STT")));
    QVERIFY(pageSource.contains(QString::fromUtf8("Chỉ OCR")));
    QVERIFY(pageSource.contains(QStringLiteral("setTranscriptFusionPolicy")));
    QVERIFY(pageSource.contains(QStringLiteral("resolveAllTranscriptConflicts")));
    QVERIFY(pageSource.contains(QStringLiteral("acceptTranscriptConflictAiSuggestion")));
    QVERIFY(setupSource.contains(QStringLiteral("dubbingColabTranscriptSourceMode")));
    QVERIFY(setupSource.contains(QString::fromUtf8("Không dùng")));
    QVERIFY(setupSource.contains(QStringLiteral("activeForTranscriptSource")));
    QVERIFY(setupSource.contains(QStringLiteral("notUsedReason")));
    QVERIFY(controllerSource.contains(QStringLiteral("snapshotSelectedColabStagesForWorkflow")));
    QVERIFY(controllerSource.contains(QStringLiteral("Resolve %1 STT/OCR conflict")));
    QVERIFY(serviceSource.contains(QStringLiteral("supportsStructuredReconciliation")));
    QVERIFY(serviceSource.contains(QStringLiteral("buildReconciliationPrompt")));
    QVERIFY(adapterSource.contains(QStringLiteral("unresolvedConflicts")));
}


} // namespace LAStudio
