#include <QtTest>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <cstring>

#include "controllers/separation/ColabVoiceIsolatorController.h"
#include "controllers/separation/VoiceCloneReferenceIsolatorController.h"
#include "controllers/separation/VoiceIsolatorController.h"
#include "core/Settings.h"
#include "remote/ColabSession.h"
#include "separation/ColabSeparationRunner.h"
#include "test_ColabSeparationRunner.h"

namespace LAStudio {
namespace {

QByteArray tinyWav()
{
    QByteArray wav(48, '\0');
    std::memcpy(wav.data(), "RIFF", 4);
    const quint32 size = 40, fmtSize = 16, rate = 16000, byteRate = 32000, dataSize = 4;
    const quint16 format = 1, channels = 1, align = 2, bits = 16;
    std::memcpy(wav.data() + 4, &size, 4); std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    std::memcpy(wav.data() + 16, &fmtSize, 4); std::memcpy(wav.data() + 20, &format, 2);
    std::memcpy(wav.data() + 22, &channels, 2); std::memcpy(wav.data() + 24, &rate, 4);
    std::memcpy(wav.data() + 28, &byteRate, 4); std::memcpy(wav.data() + 32, &align, 2);
    std::memcpy(wav.data() + 34, &bits, 2); std::memcpy(wav.data() + 36, "data", 4);
    std::memcpy(wav.data() + 40, &dataSize, 4);
    return wav;
}

QByteArray tinyFlac()
{
    // Enough for the transfer contract.  It deliberately is not a decodable
    // media stream, so the controller regression also verifies that a bad
    // manually supplied FLAC cannot block the UI waveform preview.
    return QByteArrayLiteral("fLaC");
}

class SeparationMock final : public QObject
{
public:
    explicit SeparationMock(bool permanentlyQueued = false, bool cudaFailure = false,
                            bool stuckAtNinety = false, bool legacyWav = false)
        : m_permanentlyQueued(permanentlyQueued), m_cudaFailure(cudaFailure),
          m_stuckAtNinety(stuckAtNinety), m_legacyWav(legacyWav)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { consume(socket); });
            }
        });
    }
    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    QByteArray requests() const { return m_requests; }
    bool sawStatus() const { return m_requests.contains("GET /v1/audio/separations/job-direct HTTP/1.1"); }

private:
    void consume(QTcpSocket *socket)
    {
        QByteArray &pending = m_pending[socket];
        pending += socket->readAll();
        const int headerEnd = pending.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const auto match = QRegularExpression(QStringLiteral("Content-Length: (\\d+)"), QRegularExpression::CaseInsensitiveOption)
                               .match(QString::fromLatin1(pending.left(headerEnd)));
        const int contentLength = match.hasMatch() ? match.captured(1).toInt() : 0;
        if (pending.size() < headerEnd + 4 + contentLength) return;
        const QByteArray request = pending.left(headerEnd + 4 + contentLength);
        m_requests += request + "\n---\n";
        QByteArray body;
        QByteArray contentType = "application/json";
        if (request.startsWith("POST /v1/audio/separations ")) {
            body = m_legacyWav
                ? R"({"job_id":"job-direct","status":"queued","progress":10})"
                : R"({"job_id":"job-direct","status":"queued","progress":10,"artifact_format":"flac"})";
        } else if (request.startsWith("GET /v1/audio/separations/job-direct/artifacts/vocals ")
                   || request.startsWith("GET /v1/audio/separations/job-direct/artifacts/background ")) {
            body = m_legacyWav ? tinyWav() : tinyFlac();
            contentType = m_legacyWav ? "audio/wav" : "audio/flac";
        } else if (request.startsWith("GET /v1/audio/separations/job-direct ")) {
            body = m_cudaFailure
                ? R"({"job_id":"job-direct","status":"failed","progress":0,"detail":"RuntimeError: CUDNN_FE failure 8: HEURISTIC_QUERY_FAILED; a deliberately long remote CUDA trace follows"})"
                : (m_stuckAtNinety ? R"({"job_id":"job-direct","status":"running","progress":90,"detail":"Writing separated CUDA stems"})"
                   : (m_permanentlyQueued ? R"({"job_id":"job-direct","status":"running","progress":30})"
                                       : (m_legacyWav
                                              ? R"({"job_id":"job-direct","status":"ready","progress":100,"artifacts_ready":true})"
                                              : R"({"job_id":"job-direct","status":"ready","progress":100,"artifact_format":"flac","artifacts_ready":true})")));
        } else if (request.startsWith("DELETE /v1/audio/separations/job-direct ")) {
            body = R"({"status":"cancelled"})";
        } else {
            body = R"({"detail":"unexpected request"})";
        }
        const QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: ") + contentType
            + QByteArrayLiteral("\r\nContent-Length: ") + QByteArray::number(body.size())
            + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
        socket->write(response);
        socket->disconnectFromHost();
        m_pending.remove(socket);
    }
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_pending;
    QByteArray m_requests;
    bool m_permanentlyQueued = false;
    bool m_cudaFailure = false;
    bool m_stuckAtNinety = false;
    bool m_legacyWav = false;
};

QString sourceFile(QTemporaryDir *directory)
{
    const QString path = directory->filePath(QStringLiteral("source.wav"));
    QFile file(path); if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(tinyWav()); file.close(); return path;
}

ColabSeparationRequest makeRequest(const QString &url, const QString &source, const QString &output,
                                   const std::shared_ptr<std::atomic_bool> &flag = {})
{
    ColabSeparationRequest request;
    request.workerUrl = QUrl(url); request.bearerToken = QStringLiteral("colab-separation-token");
    request.audioPath = source; request.outputRoot = output; request.allowInsecureLocalhost = true;
    request.cancellation = InferenceCancellationToken(flag);
    return request;
}

} // namespace

void TestColabSeparationRunner::testUsesDirectJobAndArtifactContract()
{
    SeparationMock server; QVERIFY(server.start());
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString source = sourceFile(&directory); QVERIFY(!source.isEmpty());
    const QString output = directory.filePath(QStringLiteral("output"));
    qRegisterMetaType<ColabSeparationRequest>("ColabSeparationRequest");
    qRegisterMetaType<ColabSeparationResult>("ColabSeparationResult");
    QThread thread; auto *runner = new ColabSeparationRunner; runner->moveToThread(&thread);
    connect(&thread, &QThread::finished, runner, &QObject::deleteLater); thread.start();
    QSignalSpy finished(runner, &ColabSeparationRunner::finished); QSignalSpy failures(runner, &ColabSeparationRunner::failed);
    QSignalSpy progress(runner, &ColabSeparationRunner::progress);
    QSignalSpy phases(runner, &ColabSeparationRunner::phaseChanged);
    QSignalSpy transfers(runner, &ColabSeparationRunner::artifactTransferProgress);
    QVERIFY(QMetaObject::invokeMethod(runner, "separate", Qt::QueuedConnection,
                                      Q_ARG(ColabSeparationRequest, makeRequest(server.baseUrl(), source, output))));
    QVERIFY2(finished.wait(5000), "Colab separation worker did not finish.");
    QCOMPARE(failures.count(), 0);
    const ColabSeparationResult result = finished.takeFirst().at(0).value<ColabSeparationResult>();
    QVERIFY(QFileInfo::exists(result.vocalsPath)); QVERIFY(QFileInfo::exists(result.backgroundPath));
    QVERIFY(result.vocalsPath.endsWith(QStringLiteral("vocals.flac")));
    QVERIFY(result.backgroundPath.endsWith(QStringLiteral("background.flac")));
    // This mock completes the remote job on its first status poll. The
    // runner must not manufacture an intermediate percentage; it reports
    // 100 only after both artifacts are downloaded and committed locally.
    QCOMPARE(progress.count(), 1);
    QCOMPARE(progress.at(0).at(0).toInt(), 100);
    QVERIFY(phases.count() >= 4);
    bool sawArtifactsReadyPhase = false;
    for (const QList<QVariant> &entry : phases) {
        if (!entry.isEmpty()
            && entry.constFirst().toString() == QStringLiteral("Colab created both FLAC stems; downloading them now")) {
            sawArtifactsReadyPhase = true;
            break;
        }
    }
    QVERIFY(sawArtifactsReadyPhase);
    QVERIFY(transfers.count() >= 2);
    const QString firstArtifact = transfers.at(0).at(0).toString();
    const qint64 firstReceived = transfers.at(0).at(1).toLongLong();
    const qint64 firstTotal = transfers.at(0).at(2).toLongLong();
    QCOMPARE(firstArtifact, QStringLiteral("vocals"));
    QCOMPARE(firstReceived, firstTotal);
    QVERIFY(firstTotal >= 4);
    const QByteArray requests = server.requests();
    QVERIFY(requests.startsWith("POST /v1/audio/separations HTTP/1.1\r\n"));
    QVERIFY(requests.toLower().contains("authorization: bearer colab-separation-token"));
    QVERIFY(requests.contains("name=\"stems\"")); QVERIFY(requests.contains("vocals,background"));
    QVERIFY(requests.contains("name=\"output_format\"")); QVERIFY(requests.contains("flac"));
    QVERIFY(requests.contains("name=\"model\""));
    QVERIFY(requests.contains("sherpa-onnx-spleeter-2stems-fp16"));
    QVERIFY(requests.contains("name=\"file\"; filename=\"source.wav\""));
    QVERIFY(requests.toLower().contains("content-type: audio/wav"));
    QVERIFY(requests.contains("GET /v1/audio/separations/job-direct/artifacts/vocals HTTP/1.1"));
    QVERIFY(requests.contains("GET /v1/audio/separations/job-direct/artifacts/background HTTP/1.1"));
    QVERIFY(!requests.contains("gateway")); QVERIFY(!requests.contains("chat/completions"));
    thread.quit(); QVERIFY(thread.wait(5000));
}

void TestColabSeparationRunner::testCancellationDiscardsPartialArtifacts()
{
    SeparationMock server(true); QVERIFY(server.start());
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto flag = std::make_shared<std::atomic_bool>(false);
    qRegisterMetaType<ColabSeparationRequest>("ColabSeparationRequest");
    QThread thread; auto *runner = new ColabSeparationRunner; runner->moveToThread(&thread);
    connect(&thread, &QThread::finished, runner, &QObject::deleteLater); thread.start();
    QSignalSpy finished(runner, &ColabSeparationRunner::finished); QSignalSpy failures(runner, &ColabSeparationRunner::failed);
    QVERIFY(QMetaObject::invokeMethod(runner, "separate", Qt::QueuedConnection,
                                      Q_ARG(ColabSeparationRequest, makeRequest(server.baseUrl(), sourceFile(&directory), directory.filePath(QStringLiteral("cancelled")), flag))));
    QTRY_VERIFY(server.sawStatus());
    flag->store(true, std::memory_order_relaxed);
    QVERIFY2(failures.wait(5000), "Cancelled direct separation request did not terminate.");
    QCOMPARE(finished.count(), 0);
    QVERIFY(failures.takeFirst().at(0).toString().contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
    QVERIFY(server.requests().contains("DELETE /v1/audio/separations/job-direct HTTP/1.1"));
    thread.quit(); QVERIFY(thread.wait(5000));
}

void TestColabSeparationRunner::finalizingAtNinetyTimesOutWithNoLocalFallback()
{
    SeparationMock server(false, false, true); QVERIFY(server.start());
    QTemporaryDir directory; QVERIFY(directory.isValid());
    qRegisterMetaType<ColabSeparationRequest>("ColabSeparationRequest");
    QThread thread; auto *runner = new ColabSeparationRunner; runner->moveToThread(&thread);
    connect(&thread, &QThread::finished, runner, &QObject::deleteLater); thread.start();
    QSignalSpy finished(runner, &ColabSeparationRunner::finished);
    QSignalSpy failures(runner, &ColabSeparationRunner::failed);
    QSignalSpy phases(runner, &ColabSeparationRunner::phaseChanged);
    ColabSeparationRequest request = makeRequest(server.baseUrl(), sourceFile(&directory),
                                                  directory.filePath(QStringLiteral("timeout")));
    request.finalizeTimeoutMs = 40;
    request.statusPollIntervalMs = 5;
    QVERIFY(QMetaObject::invokeMethod(runner, "separate", Qt::QueuedConnection,
                                      Q_ARG(ColabSeparationRequest, request)));
    QVERIFY2(failures.wait(5000), "A worker stuck at 90% did not fail within its bounded finalize timeout.");
    QCOMPARE(finished.count(), 0);
    const QString message = failures.takeFirst().at(0).toString();
    QVERIFY(message.contains(QStringLiteral("stayed at 90%")));
    QVERIFY(message.contains(QStringLiteral("No local model was started")));
    QVERIFY(server.requests().contains("DELETE /v1/audio/separations/job-direct HTTP/1.1"));
    QVERIFY(phases.count() >= 2);
    thread.quit(); QVERIFY(thread.wait(5000));
}

void TestColabSeparationRunner::cudaWorkerFailureIsActionableAndDoesNotDumpRuntimeTrace()
{
    SeparationMock server(false, true); QVERIFY(server.start());
    QTemporaryDir directory; QVERIFY(directory.isValid());
    qRegisterMetaType<ColabSeparationRequest>("ColabSeparationRequest");
    QThread thread; auto *runner = new ColabSeparationRunner; runner->moveToThread(&thread);
    connect(&thread, &QThread::finished, runner, &QObject::deleteLater); thread.start();
    QSignalSpy failures(runner, &ColabSeparationRunner::failed);
    QVERIFY(QMetaObject::invokeMethod(runner, "separate", Qt::QueuedConnection,
                                      Q_ARG(ColabSeparationRequest, makeRequest(
                                          server.baseUrl(), sourceFile(&directory),
                                          directory.filePath(QStringLiteral("failure"))))));
    QVERIFY2(failures.wait(5000), "CUDA worker failure was not surfaced");
    const QString message = failures.takeFirst().at(0).toString();
    QVERIFY(message.contains(QStringLiteral("Direct Colab")));
    QVERIFY(message.contains(QStringLiteral("No local model was started")));
    QVERIFY(!message.contains(QStringLiteral("HEURISTIC_QUERY_FAILED")));
    thread.quit(); QVERIFY(thread.wait(5000));
}

void TestColabSeparationRunner::voiceCloneReferenceUsesCachedVocalsOnly()
{
    // This branch intentionally emulates the old WAV-only notebook.  It
    // proves the new FLAC transfer default remains backward compatible for a
    // previously configured worker while the direct-runner test covers FLAC.
    SeparationMock server(false, false, false, true);
    QVERIFY(server.start());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = sourceFile(&directory);
    QVERIFY(!source.isEmpty());

    ColabSession session;
    QString error;
    QVERIFY2(session.setSession(server.baseUrl(), QStringLiteral("reference-isolator-token"),
                                &error, true), qPrintable(error));
    Settings settings;
    VoiceIsolatorController local;
    ColabVoiceIsolatorController colab(&session, &settings);
    colab.useColab();
    QVERIFY(colab.colabActive());

    VoiceCloneReferenceIsolatorController reference(&local, &colab);
    reference.setSourcePath(source);
    reference.setEnabled(true);
    QVERIFY2(reference.start(), qPrintable(reference.lastError()));
    QTRY_VERIFY_WITH_TIMEOUT(!reference.processing(), 5000);
    QVERIFY2(reference.resultReady(), qPrintable(reference.lastError()));
    QVERIFY(QFileInfo(reference.vocalsPath()).isFile());
    QVERIFY(QFileInfo(reference.backgroundPath()).isFile());
    QCOMPARE(reference.cloneReferencePath(), reference.vocalsPath());
    QVERIFY(reference.cloneReferencePath() != source);
    QVERIFY(reference.cloneReferencePath() != reference.backgroundPath());
    const QByteArray firstRunRequests = server.requests();
    QVERIFY(firstRunRequests.contains("POST /v1/audio/separations HTTP/1.1"));

    // Cache identity is source fingerprint + selected route/model.  Clearing
    // the presentation state must reuse the durable Vocals artifact rather
    // than dispatch a second separation job.
    reference.clearResult();
    QVERIFY2(reference.start(), qPrintable(reference.lastError()));
    QVERIFY(reference.resultReady());
    QCOMPARE(server.requests(), firstRunRequests);

    // Turning the option off is the only path that permits the original
    // reference.  With cleanup enabled, Background is never returned to the
    // clone boundary.
    reference.setEnabled(false);
    QCOMPARE(reference.cloneReferencePath(), source);
    reference.setEnabled(true);
    QVERIFY(reference.resultReady());
    QCOMPARE(reference.cloneReferencePath(), reference.vocalsPath());

    colab.selectColabModel(QStringLiteral("sherpa-onnx-uvr-vocals-ft"));
    QVERIFY(!reference.resultReady());
}

void TestColabSeparationRunner::separationNotebookMatchesDirectColabContract()
{
    struct Expectation {
        QString model;
        QString notebook;
        QString upstream;
        QString artifactUrl;
        QString adapterNeedle;
    };
    const QList<Expectation> expectations{
        {QStringLiteral("sherpa-onnx-spleeter-2stems-fp16"),
         QStringLiteral("LA_STUDIO_SEPARATION_SPLEETER_2STEMS_GPU.ipynb"),
         QStringLiteral("k2-fsa/sherpa-onnx-spleeter-2stems-fp16"),
         QStringLiteral("https://github.com/k2-fsa/sherpa-onnx/releases/download/source-separation-models/sherpa-onnx-spleeter-2stems-fp16.tar.bz2"),
          QStringLiteral("CUDAExecutionProvider")},
        {QStringLiteral("sherpa-onnx-uvr-vocals-ft"),
         QStringLiteral("LA_STUDIO_SEPARATION_UVR_VOCALS_GPU.ipynb"),
         QStringLiteral("k2-fsa/sherpa-onnx-uvr-vocals-ft"),
         QStringLiteral("https://github.com/k2-fsa/sherpa-onnx/releases/download/source-separation-models/UVR-MDX-NET-Voc_FT.onnx"),
         QStringLiteral("OfflineSourceSeparationUvrModelConfig")},
    };

    ColabVoiceIsolatorController controller(nullptr, nullptr);
    for (const Expectation &expected : expectations) {
        QCOMPARE(controller.notebookForColabModel(expected.model), expected.notebook);
        QVERIFY2(controller.selectColabModel(expected.model), qPrintable(expected.model));
        QCOMPARE(controller.model(), expected.model);
        QCOMPARE(controller.colabNotebookFile(), expected.notebook);

        const QString path = QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
            .filePath(QStringLiteral("notebooks/") + expected.notebook);
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        QVERIFY2(document.isObject(), qPrintable(expected.notebook));
        const QJsonObject root = document.object();
        QCOMPARE(root.value(QStringLiteral("nbformat")).toInt(), 4);
        const QJsonObject metadata = root.value(QStringLiteral("metadata")).toObject()
                                         .value(QStringLiteral("la_studio")).toObject();
        QCOMPARE(metadata.value(QStringLiteral("capability")).toString(), QStringLiteral("voice-isolation"));
        QCOMPARE(metadata.value(QStringLiteral("family_id")).toString(), expected.model);
        QCOMPARE(metadata.value(QStringLiteral("upstream_model")).toString(), expected.upstream);
        QCOMPARE(metadata.value(QStringLiteral("artifact_url")).toString(), expected.artifactUrl);
        QCOMPARE(metadata.value(QStringLiteral("device")).toString(), QStringLiteral("cuda"));
        QVERIFY(!metadata.value(QStringLiteral("cpu_fallback")).toBool(true));

        QString source;
        const QJsonArray cells = root.value(QStringLiteral("cells")).toArray();
        QVERIFY(cells.size() >= 4);
        for (const QJsonValue &cellValue : cells) {
            const QJsonArray lines = cellValue.toObject().value(QStringLiteral("source")).toArray();
            for (const QJsonValue &line : lines) source += line.toString();
        }
        for (const QJsonValue &workerTemplate : metadata.value(QStringLiteral("worker_templates")).toArray()) {
            QFile worker(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
                             .filePath(QStringLiteral("notebooks/") + workerTemplate.toString()));
            QVERIFY2(worker.open(QIODevice::ReadOnly), qPrintable(worker.fileName()));
            source += QString::fromUtf8(worker.readAll());
        }
        QVERIFY2(source.contains(expected.adapterNeedle), qPrintable(expected.notebook));
        QVERIFY2(source.contains(expected.artifactUrl), qPrintable(expected.notebook));
        QVERIFY(source.contains(QStringLiteral("MODEL_ID = \"%1\"").arg(expected.model)));
        if (expected.model == QStringLiteral("sherpa-onnx-spleeter-2stems-fp16")) {
            const QRegularExpression pinnedWorkerCommit(
                QStringLiteral(R"(WORKER_COMMIT = "[0-9a-f]{40}")"));
            QVERIFY2(pinnedWorkerCommit.match(source).hasMatch(),
                     "The Spleeter notebook must download audited worker files from an immutable commit.");
            QVERIFY(!source.contains(QStringLiteral("WORKER_COMMIT = \"main\"")));
            QVERIFY(source.contains(QStringLiteral("CUDAExecutionProvider")));
        } else {
            QVERIFY(source.contains(QStringLiteral("provider=\"cuda\"")));
        }
        QVERIFY(source.contains(QStringLiteral("@app.post(\"/v1/audio/separations\")")));
        QVERIFY(source.contains(QStringLiteral("@app.get(\"/v1/audio/separations/{job_id}\")")));
        QVERIFY(source.contains(QStringLiteral("@app.get(\"/v1/capabilities\")")));
        QVERIFY(source.contains(QStringLiteral("\"id\": \"voice-isolation\"")));
        if (expected.model == QStringLiteral("sherpa-onnx-spleeter-2stems-fp16")) {
            QVERIFY(source.contains(QStringLiteral("cudnn_conv_algo_search\": \"DEFAULT")));
            QVERIFY(source.contains(QStringLiteral("CORE_SECONDS = 20.0")));
            QVERIFY(source.contains(QStringLiteral("startup_probe")));
            QVERIFY(source.contains(QStringLiteral("No local model was started")));
            QVERIFY2(source.contains(QStringLiteral("def cloudflared_ready() -> bool:")),
                     "The hardened Spleeter launcher must probe an absent cloudflared safely.");
            QVERIFY2(source.contains(QStringLiteral("except OSError:")),
                     "A fresh Colab runtime must not crash with FileNotFoundError before cloudflared installs.");
            QVERIFY2(source.contains(QStringLiteral("or not cloudflared_ready()")),
                     "The launcher must verify cloudflared after installation before creating a tunnel.");
        }
        QVERIFY(source.contains(QStringLiteral("\"device\": \"cuda\"")));
        QVERIFY(source.contains(QStringLiteral("require_exact_model(model)")));
        QVERIFY(source.contains(QStringLiteral("status_code=409")));
        QVERIFY(source.contains(QStringLiteral("MAX_UPLOAD_BYTES = 512 * 1024 * 1024")));
        QVERIFY(source.contains(QStringLiteral("MAX_AUDIO_SECONDS = 30 * 60")));
        QVERIFY(source.contains(QStringLiteral("ARTIFACT_TTL_SECONDS = 1800")));
        QVERIFY(source.contains(QStringLiteral("JOB_SLOTS = threading.BoundedSemaphore(1)")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_SEPARATION_URL")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_SEPARATION_TOKEN")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_SEPARATION_MODEL")));
        QVERIFY(source.contains(QStringLiteral("cloudflared")));
        QVERIFY(!source.contains(QStringLiteral("API_GATEWAY")));
        QVERIFY(!source.contains(QStringLiteral("htdemucs")));
    }
    QVERIFY(controller.notebookForColabModel(QStringLiteral("not-a-model")).isEmpty());

    ColabSession session;
    QString error;
    QVERIFY(session.setSession(QStringLiteral("http://127.0.0.1:3924"),
                               QStringLiteral("temporary-token"), &error, true));
    ColabVoiceIsolatorController sessionController(&session, nullptr);
    QVERIFY(sessionController.selectColabModel(QStringLiteral("sherpa-onnx-uvr-vocals-ft")));
    QVERIFY2(!session.isActive(), "Changing the separation model must discard the previous model worker.");

    QFile page(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR)).filePath(QStringLiteral("qml/pages/VoiceIsolatorPage.qml")));
    QVERIFY(page.open(QIODevice::ReadOnly));
    const QByteArray pageSource = page.readAll();
    QVERIFY(pageSource.contains("colabModelSelectionEnabled: true"));
    QVERIFY(pageSource.contains("AppController.colabVoiceIsolator.selectColabModel(familyId)"));

    QFile settings(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
                       .filePath(QStringLiteral("qml/components/voiceisolator/VoiceIsolatorStudioView.qml")));
    QVERIFY(settings.open(QIODevice::ReadOnly));
    const QByteArray settingsSource = settings.readAll();
    QVERIFY(settingsSource.contains("AppController.colabVoiceIsolator.colabNotebookFile"));
    QVERIFY(settingsSource.contains("Selected Colab model"));
    QVERIFY(settingsSource.contains("worker reports phases, not a measurable percentage"));
    QVERIFY(settingsSource.contains("root.isolator.processing && !root.colabSelected"));
}

} // namespace LAStudio
