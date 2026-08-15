#include "test_SttSession.h"
#include <QtTest>
#include <QSignalSpy>
#include <QPointer>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QThreadPool>
#include <QTemporaryDir>
#include <QUrl>

#include "controllers/app/AppController.h"
#include "controllers/stt/SttSessionController.h"
#include "controllers/stt/SttAudioDecoder.h"
#include "audio/WavIO.h"
#include "controllers/models/ModelLifecycleController.h"
#include "core/Settings.h"
#include "core/StudioCapabilityRegistry.h"
#include "stt/SttEngine.h"
#include "stt/ColabSttRunner.h"
#include "stt/GatewaySttRunner.h"

namespace LAStudio {
namespace {

class ColabSttMock final : public QObject
{
public:
    ColabSttMock()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this,
                        [this, socket] { consume(socket); });
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
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
        const auto match = QRegularExpression(QStringLiteral("Content-Length: (\\d+)"),
                                              QRegularExpression::CaseInsensitiveOption)
                               .match(QString::fromLatin1(pending.left(headerEnd)));
        // GET and DELETE requests do not carry a body and normally omit
        // Content-Length.  Treat a missing header as a zero-length body,
        // just like a real HTTP server does.
        const int contentLength = match.hasMatch() ? match.captured(1).toInt() : 0;
        const int requestLength = headerEnd + 4 + contentLength;
        if (pending.size() < requestLength) return;
        m_request = pending.left(requestLength);
        m_requests += m_request;
        m_pending.remove(socket);
        QByteArray response;
        if (m_request.startsWith("POST /v2/uploads/stt HTTP/1.1")) {
            response = jsonResponse(QByteArrayLiteral("201 Created"), QByteArrayLiteral(
                "{\"upload_id\":\"stt-upload-1\",\"chunk_bytes\":2097152}"));
        } else if (m_request.startsWith("PUT /v2/uploads/stt/stt-upload-1/chunks/0 HTTP/1.1")) {
            m_uploadedBytes = contentLength;
            response = jsonResponse(QByteArrayLiteral("200 OK"),
                                    QByteArrayLiteral("{\"received_bytes\":")
                                        + QByteArray::number(m_uploadedBytes)
                                        + QByteArrayLiteral(",\"next_chunk\":1}"));
        } else if (m_request.startsWith("POST /v2/uploads/stt/stt-upload-1/commit HTTP/1.1")) {
            response = jsonResponse(QByteArrayLiteral("202 Accepted"), QByteArrayLiteral(
                "{\"job_id\":\"stt-job-1\",\"status\":\"queued\",\"progress\":5,\"model\":\"qwen3-asr-0.6b\"}"));
        } else if (m_request.startsWith("GET /v2/jobs/transcriptions/stt-job-1 HTTP/1.1")) {
            ++m_statusRequests;
            response = m_statusRequests == 1
                ? jsonResponse(QByteArrayLiteral("200 OK"), QByteArrayLiteral(
                    "{\"job_id\":\"stt-job-1\",\"status\":\"running\",\"progress\":50}"))
                : jsonResponse(QByteArrayLiteral("200 OK"), QByteArrayLiteral(
                    "{\"job_id\":\"stt-job-1\",\"status\":\"succeeded\",\"progress\":100,\"result\":{\"text\":\"Hello world\",\"segments\":[{\"id\":0,\"start\":0.0,\"end\":1.5,\"text\":\"Hello world\"}]}}"));
        } else if (m_request.startsWith("POST /v1/audio/transcriptions HTTP/1.1")) {
            response = jsonResponse(QByteArrayLiteral("200 OK"), QByteArrayLiteral(
                "{\"text\":\"Hello world\",\"segments\":[{\"id\":0,\"start\":0.0,\"end\":1.5,\"text\":\"Hello world\"}]}"));
        } else {
            response = jsonResponse(QByteArrayLiteral("404 Not Found"), QByteArrayLiteral(
                "{\"detail\":\"Unexpected test endpoint\"}"));
        }
        socket->write(response);
        socket->disconnectFromHost();
        m_pending.remove(socket);
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_pending;
    QByteArray m_request;
    QByteArray m_requests;
    int m_statusRequests = 0;
    int m_uploadedBytes = 0;
};

} // namespace

void TestSttSession::cleanupTestCase()
{
    QThreadPool::globalInstance()->waitForDone();
}

void TestSttSession::testSttAudioDecoder()
{
    qDebug() << "--- START: testSttAudioDecoder ---";
    SttAudioDecoder decoder;
    QSignalSpy spyError(&decoder, &SttAudioDecoder::errorOccurred);

    decoder.startDecode(QStringLiteral("nonexistent.wav"));
    QVERIFY(spyError.size() > 0 || spyError.wait(1000));
}

void TestSttSession::testSttAudioDecoderResamplesStereoWavOffThread()
{
    qDebug() << "--- START: testSttAudioDecoderResamplesStereoWavOffThread ---";
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // Use a stereo 44.1 kHz source so the public decoder contract verifies
    // both downmixing and 16 kHz normalization.  This is the same decoder
    // chain used for Colab FLAC stems after their container is decoded.
    const QString sourcePath = directory.filePath(QStringLiteral("stereo-reference.wav"));
    QVector<float> stereo(44100 * 2);
    for (int frame = 0; frame < 44100; ++frame) {
        stereo[frame * 2] = frame % 200 < 100 ? 0.5f : -0.5f;
        stereo[frame * 2 + 1] = frame % 200 < 100 ? -0.25f : 0.25f;
    }
    QVERIFY(WavIO::saveFloat(sourcePath, stereo.constData(), stereo.size(), 44100, 2));

    SttAudioDecoder decoder;
    QVector<float> decoded;
    QString error;
    connect(&decoder, &SttAudioDecoder::finished, this,
            [&decoded](const QVector<float> &samples) { decoded = samples; });
    connect(&decoder, &SttAudioDecoder::errorOccurred, this,
            [&error](const QString &message) { error = message; });

    decoder.startDecode(sourcePath);
    QTRY_VERIFY_WITH_TIMEOUT(!decoded.isEmpty() || !error.isEmpty(), 5000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // One input second, normalized to the STT engine's mono 16 kHz input.
    QVERIFY(decoded.size() >= 15990 && decoded.size() <= 16010);
}

void TestSttSession::testSttSessionPendingLoads()
{
    qDebug() << "--- START: testSttSessionPendingLoads ---";

    QList<QString> startedLoads;
    ModelLifecycleController lifecycle(
        [](const StudioConfiguration &config) {
            SessionConfiguration resolved;
            resolved.capabilityId = config.capabilityId;
            resolved.selection = config;
            resolved.signature = config.selectedFiles.value(QStringLiteral("model")).toString();
            return std::optional<SessionConfiguration>(resolved);
        },
        [&startedLoads](const SessionConfiguration &config) {
            startedLoads.append(config.signature);
        },
        []() {});

    StudioConfiguration first;
    first.capabilityId = QStringLiteral("stt");
    first.familyId = QStringLiteral("whisper.cpp");
    first.selectedFiles.insert(QStringLiteral("model"), QStringLiteral("ggml-tiny.bin"));
    StudioConfiguration second = first;
    second.selectedFiles.insert(QStringLiteral("model"), QStringLiteral("ggml-base.bin"));

    lifecycle.requestLoad(QStringLiteral("stt"), first);
    lifecycle.requestLoad(QStringLiteral("stt"), second);
    QCOMPARE(startedLoads, QList<QString>{QStringLiteral("ggml-tiny.bin")});

    lifecycle.onLoadSuccess();
    QVERIFY(lifecycle.activeConfiguration().has_value());
    QCOMPARE(lifecycle.activeSignature(), QStringLiteral("ggml-tiny.bin"));
    QCOMPARE(startedLoads, QList<QString>({QStringLiteral("ggml-tiny.bin"), QStringLiteral("ggml-base.bin")}));

    lifecycle.onLoadSuccess();
    QCOMPARE(lifecycle.activeSignature(), QStringLiteral("ggml-base.bin"));
}

void TestSttSession::testSttSessionHistoryRoundTrip()
{
    qDebug() << "--- START: testSttSessionHistoryRoundTrip ---";
    SttSessionController session;

    QString savedText = QStringLiteral("Saved history transcription text");
    QString savedPath = QStringLiteral("E:/saved_audio.wav");
    session.loadHistoryItem(savedText, savedPath);

    // Verify transcript is restored
    QCOMPARE(session.transcript(), savedText);

    // Verify file input path and normalized URL are set
    QCOMPARE(session.inputPath(), QStringLiteral("E:/saved_audio.wav"));
    QCOMPARE(session.inputUrl(), QUrl::fromLocalFile(QStringLiteral("E:/saved_audio.wav")));
}

void TestSttSession::testSttSessionUrlPreview()
{
    qDebug() << "--- START: testSttSessionUrlPreview ---";
    SttSessionController session;

    // Windows local path
    session.selectFileInput(QStringLiteral("C:/audio.wav"));
    QCOMPARE(session.inputUrl(), QUrl::fromLocalFile(QStringLiteral("C:/audio.wav")));

    // Standard file URL
    session.selectFileInput(QStringLiteral("file:///D:/folder/audio.wav"));
    QCOMPARE(session.inputUrl(), QUrl::fromLocalFile(QStringLiteral("D:/folder/audio.wav")));
}

void TestSttSession::testSttSessionQmlNotifications()
{
    qDebug() << "--- START: testSttSessionQmlNotifications ---";
    SttSessionController session;
    SttEngine* engine = AppController::instance()->stt();
    QVERIFY(engine != nullptr);

    QSignalSpy spyProcessing(&session, &SttSessionController::processingChanged);
    QSignalSpy spyTranscript(&session, &SttSessionController::transcriptChanged);

    // 1. Verify transcriptChanged when cleared
    session.clearTranscript();
    QCOMPARE(spyTranscript.size(), 1);

    // 2. Verify processingChanged when transcribing
    engine->transcribeSamples({0.1f});
    QTRY_COMPARE_WITH_TIMEOUT(spyProcessing.size(), 1, 500);
}

void TestSttSession::testSttRecordingSourceSelection()
{
    SttSessionController session;
    AudioRecorder *recorder = AppController::instance()->recorder();
    QVERIFY(recorder != nullptr);

    session.startRecording(true);
    QVERIFY(recorder->recordSystemAudio());

    session.startRecording(false);
    QVERIFY(!recorder->recordSystemAudio());
}

void TestSttSession::testExplicitProviderRoutingDoesNotFallback()
{
    SttSessionController session;
    AppController::instance()->settings()->setGatewayUrl(
        QStringLiteral("https://gateway.example.test/v1"));
    QString gatewayError;
    QString colabError;

    QVERIFY(!session.canTranscribeForProvider(ExecutionProvider::ApiGateway,
                                              QStringLiteral("gateway-stt"), &gatewayError));
    QVERIFY(!session.canTranscribeForProvider(ExecutionProvider::ColabDirect,
                                              QString(), &colabError));
    QVERIFY(gatewayError.contains(QStringLiteral("Gateway")));
    QVERIFY(colabError.contains(QStringLiteral("Colab")));
    QVERIFY(gatewayError != colabError);
}

void TestSttSession::testRemoteFirstBlocksLocalStt()
{
    SttSessionController session;
    Settings *settings = AppController::instance()->settings();
    QVERIFY(settings != nullptr);
    const bool original = settings->remoteFirstMode();
    settings->setRemoteFirstMode(true);

    QString error;
    QVERIFY(!session.canTranscribeForProvider(ExecutionProvider::LocalDev, QString(), &error));
    QVERIFY(error.contains(QStringLiteral("Remote-first")));

    settings->setRemoteFirstMode(original);
}

void TestSttSession::testSttRouteSelectionDoesNotFallbackAcrossGatewayAndColab()
{
    SttSessionController session;
    Settings *settings = AppController::instance()->settings();
    QVERIFY(settings != nullptr);
    const bool originalRemoteFirst = settings->remoteFirstMode();
    const QString originalGatewayUrl = settings->gatewayUrl();
    const QString originalGatewayKey = settings->gatewayApiKey();
    const QString originalGatewayModel = settings->gatewaySttModel();
    settings->setRemoteFirstMode(false);
    settings->setGatewayUrl(QStringLiteral("https://gateway.example.test/v1"));
    settings->setGatewayApiKey(QStringLiteral("gateway-test-token"));
    settings->setGatewaySttModel(QStringLiteral("gateway-stt"));

    QVERIFY(session.selectColabModel(QStringLiteral("qwen3-asr-0.6b")));
    QVERIFY(session.connectColab(QStringLiteral("https://worker.example.test"),
                                 QStringLiteral("temporary-colab-token")));
    ColabSession *workerSession = AppController::instance()->colabSttSession();
    QVERIFY(workerSession);
    QVERIFY(workerSession->isChecking());
    QVERIFY(!session.colabPaired());
    QVERIFY(!session.colabActive());

    // The route-selection assertions below do not need a network worker.
    // Install a trusted in-memory contract session after proving that the
    // production connect path remains inactive while verification is pending.
    workerSession->clear();
    QString workerError;
    QVERIFY2(workerSession->setSession(
                 QStringLiteral("https://worker.example.test"),
                 QStringLiteral("temporary-colab-token"), &workerError),
             qPrintable(workerError));
    session.useColab();
    QVERIFY(session.colabPaired());
    QVERIFY(session.colabActive());

    session.useGateway();
    QVERIFY(session.gatewayActive());
    QVERIFY(session.colabPaired());
    QVERIFY(!session.colabActive());

    session.disconnectGateway();
    QVERIFY(!session.gatewayActive());
    QVERIFY(session.colabPaired());
    QVERIFY(!session.colabActive());

    session.useColab();
    QVERIFY(session.colabActive());
    session.disconnectColab();

    settings->setGatewayUrl(originalGatewayUrl);
    settings->setGatewayApiKey(originalGatewayKey);
    settings->setGatewaySttModel(originalGatewayModel);
    settings->setRemoteFirstMode(originalRemoteFirst);
}

void TestSttSession::testColabSttModelNotebookMapping()
{
    SttSessionController session;
    const QList<QPair<QString, QString>> models{
        {QStringLiteral("nemotron-3.5-asr-streaming-0.6b"),
         QStringLiteral("LA_STUDIO_STT_NEMOTRON_3_5_0_6B_GPU.ipynb")},
        {QStringLiteral("whisper.cpp"),
         QStringLiteral("LA_STUDIO_STT_WHISPER_GPU.ipynb")},
        {QStringLiteral("qwen3-asr-0.6b"),
         QStringLiteral("LA_STUDIO_STT_QWEN3_ASR_0_6B_GPU.ipynb")},
        {QStringLiteral("qwen3-asr-1.7b"),
         QStringLiteral("LA_STUDIO_STT_QWEN3_ASR_1_7B_GPU.ipynb")},
    };

    for (const auto &[model, notebook] : models) {
        QVERIFY2(session.selectColabModel(model), qPrintable(model));
        QCOMPARE(session.colabModel(), model);
        QCOMPARE(session.colabNotebookFile(), notebook);
        QCOMPARE(session.notebookForColabModel(model), notebook);
    }
    QSignalSpy failures(&session, &SttSessionController::transcriptionFailed);
    QVERIFY(!session.selectColabModel(QStringLiteral("unknown-stt")));
    QCOMPARE(failures.count(), 1);

    // colabModel is a writable QML property. It must enforce the same exact
    // notebook mapping as the explicit selector so no UI binding can leave
    // STT configured with a model that has no matching Colab worker.
    const QString selectedModel = session.colabModel();
    session.setColabModel(QStringLiteral("unknown-stt-from-binding"));
    QCOMPARE(session.colabModel(), selectedModel);
    QCOMPARE(failures.count(), 2);
}

void TestSttSession::testColabSttRunnerUsesAsynchronousJobContract()
{
    ColabSttMock server;
    QVERIFY(server.start());
    qRegisterMetaType<ColabSttRequest>("ColabSttRequest");
    QThread workerThread;
    auto *runner = new ColabSttRunner;
    runner->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, runner, &QObject::deleteLater);
    workerThread.start();
    QSignalSpy finished(runner, &ColabSttRunner::finished);
    QSignalSpy failures(runner, &ColabSttRunner::failed);
    QSignalSpy progress(runner, &ColabSttRunner::progress);

    ColabSttRequest request;
    request.workerUrl = QUrl(server.baseUrl());
    request.bearerToken = QStringLiteral("colab-test-token");
    request.model = QStringLiteral("qwen3-asr-0.6b");
    request.samples = {0.0F, 0.25F, -0.25F, 0.0F};
    request.language = QStringLiteral("en");
    request.allowInsecureLocalhost = true;
    QVERIFY(QMetaObject::invokeMethod(runner, "transcribe", Qt::QueuedConnection,
                                      Q_ARG(ColabSttRequest, request)));

    const bool completed = finished.wait(5000) || failures.count() > 0;
    if (!completed) {
        QMetaObject::invokeMethod(runner, "cancel", Qt::QueuedConnection);
    }
    workerThread.quit();
    QVERIFY2(workerThread.wait(5000), "Colab STT worker thread did not stop.");
    QVERIFY2(completed, qPrintable(QStringLiteral("Colab STT worker did not finish. Requests: %1")
                                   .arg(QString::fromLatin1(server.requests()))));
    QCOMPARE(failures.count(), 0);
    QCOMPARE(finished.takeFirst().at(0).toString(), QStringLiteral("Hello world"));
    // The desktop must not manufacture phase percentages.  The mock worker
    // reports a measured 50% while running and completion is the only local
    // terminal value the runner may add.
    QCOMPARE(progress.count(), 2);
    QCOMPARE(progress.at(0).at(0).toInt(), 50);
    QCOMPARE(progress.at(1).at(0).toInt(), 100);
    const QByteArray requests = server.requests();
    QVERIFY(requests.contains("POST /v2/uploads/stt HTTP/1.1\r\n"));
    QVERIFY(requests.contains("PUT /v2/uploads/stt/stt-upload-1/chunks/0 HTTP/1.1\r\n"));
    QVERIFY(requests.contains("POST /v2/uploads/stt/stt-upload-1/commit HTTP/1.1\r\n"));
    QVERIFY(!requests.contains("POST /v2/jobs/transcriptions HTTP/1.1\r\n"));
    QVERIFY(!requests.contains("POST /v1/audio/transcriptions HTTP/1.1\r\n"));
    QVERIFY(requests.toLower().contains("authorization: bearer colab-test-token"));
    QVERIFY(requests.contains("qwen3-asr-0.6b"));
    QVERIFY(requests.contains("verbose_json"));
    QVERIFY(requests.contains("RIFF"));
    QCOMPARE(requests.count("GET /v2/jobs/transcriptions/stt-job-1 HTTP/1.1\r\n"), 2);
}

void TestSttSession::testSpeechNotebookMatchesDirectColabSttContract()
{
    struct NotebookExpectation {
        QString fileName;
        QString familyId;
        QString upstreamModel;
        QString loaderNeedle;
    };
    const QList<NotebookExpectation> expectations{
        {QStringLiteral("LA_STUDIO_STT_NEMOTRON_3_5_0_6B_GPU.ipynb"),
         QStringLiteral("nemotron-3.5-asr-streaming-0.6b"),
         QStringLiteral("nvidia/nemotron-3.5-asr-streaming-0.6b"),
         QStringLiteral("AutoModelForRNNT.from_pretrained")},
        {QStringLiteral("LA_STUDIO_STT_WHISPER_GPU.ipynb"),
         QStringLiteral("whisper.cpp"), QStringLiteral("large-v3"),
         QStringLiteral("WhisperModel(UPSTREAM_MODEL, device=\"cuda\"")},
        {QStringLiteral("LA_STUDIO_STT_QWEN3_ASR_0_6B_GPU.ipynb"),
         QStringLiteral("qwen3-asr-0.6b"), QStringLiteral("Qwen/Qwen3-ASR-0.6B"),
         QStringLiteral("Qwen3ASRModel.from_pretrained")},
        {QStringLiteral("LA_STUDIO_STT_QWEN3_ASR_1_7B_GPU.ipynb"),
         QStringLiteral("qwen3-asr-1.7b"), QStringLiteral("Qwen/Qwen3-ASR-1.7B"),
         QStringLiteral("Qwen3ASRModel.from_pretrained")},
    };

    for (const NotebookExpectation &expected : expectations) {
        const QString path = QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
            .filePath(QStringLiteral("notebooks/") + expected.fileName);
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        QVERIFY2(document.isObject(), qPrintable(expected.fileName));
        const QJsonObject root = document.object();
        QCOMPARE(root.value(QStringLiteral("nbformat")).toInt(), 4);
        const QJsonObject metadata = root.value(QStringLiteral("metadata")).toObject()
                                         .value(QStringLiteral("la_studio")).toObject();
        QCOMPARE(metadata.value(QStringLiteral("family_id")).toString(), expected.familyId);
        QCOMPARE(metadata.value(QStringLiteral("upstream_model")).toString(), expected.upstreamModel);
        QCOMPARE(metadata.value(QStringLiteral("device")).toString(), QStringLiteral("cuda"));
        QVERIFY(!metadata.value(QStringLiteral("cpu_fallback")).toBool(true));

        QString source;
        const QJsonArray cells = root.value(QStringLiteral("cells")).toArray();
        QVERIFY(cells.size() >= 4);
        for (const QJsonValue &cellValue : cells) {
            const QJsonArray lines = cellValue.toObject().value(QStringLiteral("source")).toArray();
            for (const QJsonValue &line : lines) source += line.toString();
        }
        QVERIFY2(source.contains(expected.loaderNeedle), qPrintable(expected.fileName));
        QVERIFY(source.contains(QStringLiteral("if not torch.cuda.is_available()")));
        QVERIFY(source.contains(QStringLiteral("@app.post(\"/v1/audio/transcriptions\")")));
        QVERIFY(source.contains(QStringLiteral("@app.post(\"/v2/jobs/transcriptions\", status_code=202)")));
        QVERIFY(source.contains(QStringLiteral("@app.post(\"/v2/uploads/stt\", status_code=201)")));
        QVERIFY(source.contains(QStringLiteral("@app.put(\"/v2/uploads/stt/{upload_id}/chunks/{chunk_index}\")")));
        QVERIFY(source.contains(QStringLiteral("@app.post(\"/v2/uploads/stt/{upload_id}/commit\", status_code=202)")));
        QVERIFY(source.contains(QStringLiteral("@app.get(\"/v2/jobs/transcriptions/{job_id}\")")));
        QVERIFY(source.contains(QStringLiteral("@app.delete(\"/v2/jobs/transcriptions/{job_id}\")")));
        QVERIFY(source.contains(QStringLiteral("asyncio.create_task(run_job(job_id))")));
        QVERIFY(source.contains(QStringLiteral("\"transcription_jobs\": \"/v2/jobs/transcriptions\"")));
        QVERIFY(source.contains(QStringLiteral("\"chunked_transcription_uploads\": \"/v2/uploads/stt\"")));
        QVERIFY(source.contains(QStringLiteral("@app.get(\"/v1/capabilities\")")));
        QVERIFY(source.contains(QStringLiteral("\"contract_version\": 1")));
        QVERIFY(source.contains(QStringLiteral("\"worker_revision\": WORKER_REVISION")));
        QVERIFY(source.contains(QStringLiteral("if model.strip().lower() != MODEL_ID")));
        QVERIFY(source.contains(QStringLiteral("status_code=409")));
        QVERIFY(source.contains(QStringLiteral("MAX_UPLOAD_BYTES = 512 * 1024 * 1024")));
        QVERIFY(source.contains(QStringLiteral("MAX_AUDIO_SECONDS = 30 * 60")));
        QVERIFY(source.contains(QStringLiteral("REQUEST_SLOTS = threading.BoundedSemaphore(1)")));
        QVERIFY(source.contains(QStringLiteral("CHUNK_UPLOAD_BYTES = 2 * 1024 * 1024")));
        QVERIFY(source.contains(QStringLiteral("status_code=415")));
        QVERIFY(source.contains(QStringLiteral("status_code=429")));
        QVERIFY(source.contains(QStringLiteral("await file.read(1024 * 1024)")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_STT_URL")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_STT_TOKEN")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_STT_MODEL")));
        QVERIFY(source.contains(QStringLiteral("cloudflared")));
    }

    QFile gallery(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
                      .filePath(QStringLiteral("qml/components/shared/CapabilityGallery.qml")));
    QVERIFY(gallery.open(QIODevice::ReadOnly));
    const QByteArray gallerySource = gallery.readAll();
    QVERIFY(gallerySource.contains("Select for Colab"));
    QVERIFY(gallerySource.contains("Select + open notebook"));
    QVERIFY(gallerySource.contains("localRuntimeOptions"));
    QVERIFY(gallerySource.contains("readonly property bool hasFamily: root.hasFamilyValue(f)"));
    QVERIFY(gallerySource.contains("property var family: detailPanel.hasFamily"));
    QVERIFY(gallerySource.contains("detailPanel.hasFamily ? detailPanel.f.familyId : \"\""));

    QFile remoteInferenceTab(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
                                  .filePath(QStringLiteral("qml/pages/settings/RemoteInferenceTab.qml")));
    QVERIFY(remoteInferenceTab.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString remoteInferenceSource = QString::fromUtf8(remoteInferenceTab.readAll());
    QVERIFY(remoteInferenceSource.contains("readonly property var safeEntry: entry"));
    QVERIFY(remoteInferenceSource.contains("typeof modelData === \"undefined\""));

    QFile settings(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
                       .filePath(QStringLiteral("qml/components/stt/SttSettingsPanel.qml")));
    QVERIFY(settings.open(QIODevice::ReadOnly));
    const QByteArray settingsSource = settings.readAll();
    QVERIFY(settingsSource.contains("root.sttSession.colabNotebookFile"));
    QVERIFY(settingsSource.contains("root.sttSession.colabModel"));
}

void TestSttSession::testGatewaySttRunnerPostsOpenAiCompatibleMultipart()
{
    ColabSttMock server;
    QVERIFY(server.start());
    qRegisterMetaType<GatewaySttRequest>("GatewaySttRequest");
    QThread workerThread;
    auto *runner = new GatewaySttRunner;
    runner->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, runner, &QObject::deleteLater);
    workerThread.start();
    QSignalSpy finished(runner, &GatewaySttRunner::finished);
    QSignalSpy failures(runner, &GatewaySttRunner::failed);

    GatewaySttRequest request;
    request.gatewayUrl = server.baseUrl() + QStringLiteral("/v1");
    request.apiKey = QStringLiteral("gateway-stt-test-key");
    request.model = QStringLiteral("gateway-stt-model");
    request.samples = {0.0F, 0.25F, -0.25F, 0.0F};
    request.language = QStringLiteral("en");
    request.allowInsecureLocalhost = true;
    QVERIFY(QMetaObject::invokeMethod(runner, "transcribe", Qt::QueuedConnection,
                                      Q_ARG(GatewaySttRequest, request)));

    QVERIFY2(finished.wait(5000), "Gateway STT worker did not finish.");
    QCOMPARE(failures.count(), 0);
    QCOMPARE(finished.takeFirst().at(0).toString(), QStringLiteral("Hello world"));
    const QByteArray body = server.request();
    QVERIFY(body.startsWith("POST /v1/audio/transcriptions HTTP/1.1\r\n"));
    QVERIFY(body.toLower().contains("authorization: bearer gateway-stt-test-key"));
    QVERIFY(body.contains("gateway-stt-model"));
    QVERIFY(body.contains("name=\"file\"; filename=\"audio.wav\""));
    workerThread.quit();
    QVERIFY(workerThread.wait(5000));
}

} // namespace LAStudio
