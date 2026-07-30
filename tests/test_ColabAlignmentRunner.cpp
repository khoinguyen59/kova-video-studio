#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include "alignment/ColabAlignmentRunner.h"
#include "controllers/alignment/ColabAlignmentController.h"
#include "remote/ColabSession.h"
#include "test_ColabAlignmentRunner.h"

namespace LAStudio {
namespace {

class AlignmentMock final : public QObject
{
public:
    explicit AlignmentMock(QByteArray responseJson, bool respond = true)
        : m_responseJson(std::move(responseJson)), m_respond(respond)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                m_socket = socket;
                connect(socket, &QTcpSocket::readyRead, this, [this] { consume(); });
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    QByteArray request() const { return m_request; }
    bool received() const { return !m_request.isEmpty(); }

private:
    void consume()
    {
        if (!m_socket) return;
        m_pending += m_socket->readAll();
        const int headerEnd = m_pending.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const auto match = QRegularExpression(QStringLiteral("Content-Length: (\\d+)"),
                                              QRegularExpression::CaseInsensitiveOption)
                               .match(QString::fromLatin1(m_pending.left(headerEnd)));
        if (!match.hasMatch()) return;
        const int requestLength = headerEnd + 4 + match.captured(1).toInt();
        if (m_pending.size() < requestLength) return;
        m_request = m_pending.left(requestLength);
        if (!m_respond) return;
        const QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
            + QByteArray::number(m_responseJson.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + m_responseJson;
        m_socket->write(response);
        m_socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QPointer<QTcpSocket> m_socket;
    QByteArray m_pending;
    QByteArray m_request;
    QByteArray m_responseJson;
    bool m_respond = true;
};

QString temporaryAudio(QTemporaryDir *directory)
{
    const QString path = directory->filePath(QStringLiteral("alignment.wav"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write("not-a-real-wav-but-a-real-upload");
    file.close();
    return path;
}

ColabAlignmentRequest makeRequest(const QString &baseUrl, const QString &audioPath,
                                  const std::shared_ptr<std::atomic_bool> &cancellation = {})
{
    ColabAlignmentRequest request;
    request.workerUrl = QUrl(baseUrl);
    request.bearerToken = QStringLiteral("colab-alignment-token");
    request.audioPath = audioPath;
    request.transcript = QStringLiteral("Hello direct Colab alignment");
    request.language = QStringLiteral("en");
    request.outputFormat = QStringLiteral("srt");
    request.model = QStringLiteral("qwen3-forced-aligner-0.6b");
    request.allowInsecureLocalhost = true;
    request.cancellation = InferenceCancellationToken(cancellation);
    return request;
}

} // namespace

void TestColabAlignmentRunner::testPostsDirectAlignmentContractAndValidatesSpans()
{
    const QByteArray response = R"({"duration":2.0,"segments":[{"text":"Hello","start":0.0,"end":0.6,"score":0.9,"kind":"word"},{"text":"direct","start":0.6,"end":1.2,"score":0.8,"kind":"word"}],"unaligned_tokens":["Colab"]})";
    AlignmentMock server(response);
    QVERIFY(server.start());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString audioPath = temporaryAudio(&directory);
    QVERIFY(!audioPath.isEmpty());

    qRegisterMetaType<ColabAlignmentRequest>("ColabAlignmentRequest");
    qRegisterMetaType<ColabAlignmentResult>("ColabAlignmentResult");
    QThread workerThread;
    auto *runner = new ColabAlignmentRunner;
    runner->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, runner, &QObject::deleteLater);
    workerThread.start();
    QSignalSpy finished(runner, &ColabAlignmentRunner::finished);
    QSignalSpy failures(runner, &ColabAlignmentRunner::failed);
    QSignalSpy progress(runner, &ColabAlignmentRunner::progress);

    QVERIFY(QMetaObject::invokeMethod(runner, "align", Qt::QueuedConnection,
                                      Q_ARG(ColabAlignmentRequest, makeRequest(server.baseUrl(), audioPath))));
    QVERIFY2(finished.wait(5000), "Colab alignment worker did not finish.");
    QCOMPARE(failures.count(), 0);
    const ColabAlignmentResult result = finished.takeFirst().at(0).value<ColabAlignmentResult>();
    QCOMPARE(result.segments.size(), 2);
    QCOMPARE(result.unalignedTokens, QVariantList{QStringLiteral("Colab")});
    QCOMPARE(result.duration, 2.0);
    QVERIFY(result.output.contains(QStringLiteral("00:00:00,000 --> 00:00:00,600")));
    QCOMPARE(progress.count(), 1);
    QCOMPARE(progress.constFirst().at(0).toInt(), 100);

    const QByteArray body = server.request();
    QVERIFY(body.startsWith("POST /v1/audio/alignments HTTP/1.1\r\n"));
    QVERIFY(body.toLower().contains("authorization: bearer colab-alignment-token"));
    QVERIFY(body.contains("name=\"model\""));
    QVERIFY(body.contains("qwen3-forced-aligner-0.6b"));
    QVERIFY(body.contains("name=\"transcript\""));
    QVERIFY(body.contains("Hello direct Colab alignment"));
    QVERIFY(body.contains("name=\"language\""));
    QVERIFY(body.contains("name=\"audio\"; filename=\"alignment.wav\""));
    QVERIFY(body.toLower().contains("content-type: audio/wav"));
    QVERIFY(!body.contains("gateway"));
    QVERIFY(!body.contains("/v1/chat/completions"));
    workerThread.quit();
    QVERIFY(workerThread.wait(5000));
}

void TestColabAlignmentRunner::testRejectsNonMonotonicAndCancelledResponses()
{
    const QByteArray badResponse = R"({"duration":2.0,"segments":[{"text":"later","start":1.1,"end":1.4},{"text":"earlier","start":0.9,"end":1.3}]})";
    AlignmentMock invalidServer(badResponse);
    QVERIFY(invalidServer.start());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString audioPath = temporaryAudio(&directory);

    qRegisterMetaType<ColabAlignmentRequest>("ColabAlignmentRequest");
    QThread workerThread;
    auto *runner = new ColabAlignmentRunner;
    runner->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, runner, &QObject::deleteLater);
    workerThread.start();
    QSignalSpy finished(runner, &ColabAlignmentRunner::finished);
    QSignalSpy failures(runner, &ColabAlignmentRunner::failed);
    QVERIFY(QMetaObject::invokeMethod(runner, "align", Qt::QueuedConnection,
                                      Q_ARG(ColabAlignmentRequest, makeRequest(invalidServer.baseUrl(), audioPath))));
    QVERIFY2(failures.wait(5000), "Invalid direct alignment response was not rejected.");
    QCOMPARE(finished.count(), 0);
    QVERIFY(failures.takeFirst().at(0).toString().contains(QStringLiteral("non-monotonic")));

    AlignmentMock stalledServer({}, false);
    QVERIFY(stalledServer.start());
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    QSignalSpy cancelled(runner, &ColabAlignmentRunner::failed);
    QVERIFY(QMetaObject::invokeMethod(runner, "align", Qt::QueuedConnection,
                                      Q_ARG(ColabAlignmentRequest, makeRequest(stalledServer.baseUrl(), audioPath, cancellation))));
    QTRY_VERIFY(stalledServer.received());
    cancellation->store(true, std::memory_order_relaxed);
    QVERIFY(QMetaObject::invokeMethod(runner, "cancel", Qt::QueuedConnection));
    QVERIFY2(cancelled.wait(5000), "Cancelled direct alignment request did not terminate.");
    QCOMPARE(finished.count(), 0);
    QVERIFY(cancelled.takeFirst().at(0).toString().contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
    workerThread.quit();
    QVERIFY(workerThread.wait(5000));
}

void TestColabAlignmentRunner::alignmentNotebookMatchesDirectColabContract()
{
    struct Expectation {
        QString model;
        QString notebook;
        QString upstream;
        QString adapterNeedle;
    };
    const QList<Expectation> expectations{
        {QStringLiteral("wav2vec2-aligner-zh"), QStringLiteral("LA_STUDIO_ALIGNMENT_WAV2VEC2_ZH_GPU.ipynb"),
         QStringLiteral("cstr/wav2vec2-large-xlsr-53-chinese-zh-cn-GGUF"), QStringLiteral("--align-only")},
        {QStringLiteral("canary-ctc-aligner"), QStringLiteral("LA_STUDIO_ALIGNMENT_CANARY_CTC_GPU.ipynb"),
         QStringLiteral("cstr/canary-ctc-aligner-GGUF"), QStringLiteral("canary-ctc-aligner-q4_k.gguf")},
        {QStringLiteral("mms-forced-aligner-onnx"), QStringLiteral("LA_STUDIO_ALIGNMENT_MMS_ONNX_GPU.ipynb"),
         QStringLiteral("onnx-community/mms-300m-1130-forced-aligner-ONNX"), QStringLiteral("ort.InferenceSession")},
        {QStringLiteral("qwen3-forced-aligner-0.6b"), QStringLiteral("LA_STUDIO_ALIGNMENT_QWEN3_0_6B_GPU.ipynb"),
         QStringLiteral("Qwen/Qwen3-ForcedAligner-0.6B"), QStringLiteral("Qwen3ForcedAligner.from_pretrained")},
    };

    ColabAlignmentController controller(nullptr, nullptr);
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
        QCOMPARE(metadata.value(QStringLiteral("capability")).toString(), QStringLiteral("forced-alignment"));
        QCOMPARE(metadata.value(QStringLiteral("family_id")).toString(), expected.model);
        QCOMPARE(metadata.value(QStringLiteral("upstream_model")).toString(), expected.upstream);
        QCOMPARE(metadata.value(QStringLiteral("device")).toString(), QStringLiteral("cuda"));
        QVERIFY(!metadata.value(QStringLiteral("cpu_fallback")).toBool(true));

        QString source;
        const QJsonArray cells = root.value(QStringLiteral("cells")).toArray();
        QVERIFY(cells.size() >= 4);
        for (const QJsonValue &cellValue : cells) {
            const QJsonArray lines = cellValue.toObject().value(QStringLiteral("source")).toArray();
            for (const QJsonValue &line : lines) source += line.toString();
        }
        QVERIFY2(source.contains(expected.adapterNeedle), qPrintable(expected.notebook));
        QVERIFY(source.contains(QStringLiteral("MODEL_ID = \"%1\"").arg(expected.model)));
        QVERIFY(source.contains(QStringLiteral("if not torch.cuda.is_available()")));
        QVERIFY(source.contains(QStringLiteral("@app.post(\"/v1/audio/alignments\")")));
        QVERIFY(source.contains(QStringLiteral("@app.get(\"/v1/capabilities\")")));
        QVERIFY(source.contains(QStringLiteral("\"id\": \"forced-alignment\"")));
        QVERIFY(source.contains(QStringLiteral("\"device\": \"cuda\"")));
        QVERIFY(source.contains(QStringLiteral("require_exact_model(model)")));
        QVERIFY(source.contains(QStringLiteral("status_code=409")));
        QVERIFY(source.contains(QStringLiteral("MAX_UPLOAD_BYTES = 512 * 1024 * 1024")));
        QVERIFY(source.contains(QStringLiteral("MAX_AUDIO_SECONDS = 300")));
        QVERIFY(source.contains(QStringLiteral("REQUEST_SLOTS = threading.BoundedSemaphore(1)")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_ALIGNMENT_URL")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_ALIGNMENT_TOKEN")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_ALIGNMENT_MODEL")));
        QVERIFY(source.contains(QStringLiteral("cloudflared")));
        QVERIFY(!source.contains(QStringLiteral("API_GATEWAY")));
    }
    QVERIFY(controller.notebookForColabModel(QStringLiteral("not-a-model")).isEmpty());

    ColabSession session;
    QString error;
    QVERIFY(session.setSession(QStringLiteral("http://127.0.0.1:3923"),
                               QStringLiteral("temporary-token"), &error, true));
    ColabAlignmentController sessionController(&session, nullptr);
    QVERIFY(sessionController.selectColabModel(QStringLiteral("canary-ctc-aligner")));
    QVERIFY2(!session.isActive(), "Changing the alignment model must discard the previous model worker.");

    QFile page(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR)).filePath(QStringLiteral("qml/pages/AlignmentPage.qml")));
    QVERIFY(page.open(QIODevice::ReadOnly));
    const QByteArray pageSource = page.readAll();
    QVERIFY(pageSource.contains("colabModelSelectionEnabled: true"));
    QVERIFY(pageSource.contains("AppController.colabAlignment.selectColabModel(familyId)"));

    QFile settings(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
                       .filePath(QStringLiteral("qml/components/alignment/AlignmentSetupPanel.qml")));
    QVERIFY(settings.open(QIODevice::ReadOnly));
    const QByteArray settingsSource = settings.readAll();
    QVERIFY(settingsSource.contains("AppController.colabAlignment.colabNotebookFile"));
    QVERIFY(settingsSource.contains("Selected Colab model"));
}

} // namespace LAStudio
