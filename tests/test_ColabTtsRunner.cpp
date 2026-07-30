#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <cstring>

#include "tts/ColabTtsRunner.h"
#include "controllers/tts/ColabTtsController.h"
#include "test_ColabTtsRunner.h"

namespace LAStudio {
namespace {

QByteArray pcm16Wav()
{
    QByteArray wav(48, '\0');
    std::memcpy(wav.data(), "RIFF", 4);
    const quint32 size = 40;
    std::memcpy(wav.data() + 4, &size, sizeof(size));
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    const quint32 fmtSize = 16;
    const quint16 format = 1;
    const quint16 channels = 1;
    const quint32 rate = 24000;
    const quint32 byteRate = 48000;
    const quint16 align = 2;
    const quint16 bits = 16;
    const quint32 dataSize = 4;
    std::memcpy(wav.data() + 16, &fmtSize, sizeof(fmtSize));
    std::memcpy(wav.data() + 20, &format, sizeof(format));
    std::memcpy(wav.data() + 22, &channels, sizeof(channels));
    std::memcpy(wav.data() + 24, &rate, sizeof(rate));
    std::memcpy(wav.data() + 28, &byteRate, sizeof(byteRate));
    std::memcpy(wav.data() + 32, &align, sizeof(align));
    std::memcpy(wav.data() + 34, &bits, sizeof(bits));
    std::memcpy(wav.data() + 36, "data", 4);
    std::memcpy(wav.data() + 40, &dataSize, sizeof(dataSize));
    const qint16 samples[] = {4096, -4096};
    std::memcpy(wav.data() + 44, samples, sizeof(samples));
    return wav;
}

class ColabTtsMock final : public QObject
{
public:
    ColabTtsMock()
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
        const QByteArray audio = pcm16Wav();
        const QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: ")
            + QByteArray::number(audio.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + audio;
        m_socket->write(response);
        m_socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QPointer<QTcpSocket> m_socket;
    QByteArray m_pending;
    QByteArray m_request;
};

} // namespace

void TestColabTtsRunner::testPostsDirectWorkerSpeechRequest()
{
    ColabTtsMock server;
    QVERIFY(server.start());
    qRegisterMetaType<ColabTtsRequest>("ColabTtsRequest");
    qRegisterMetaType<QVector<float>>("QVector<float>");
    QThread workerThread;
    auto *runner = new ColabTtsRunner;
    runner->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, runner, &QObject::deleteLater);
    workerThread.start();
    QSignalSpy finished(runner, &ColabTtsRunner::finished);
    QSignalSpy failures(runner, &ColabTtsRunner::failed);
    QSignalSpy progress(runner, &ColabTtsRunner::progress);

    ColabTtsRequest request;
    request.workerUrl = QUrl(server.baseUrl());
    request.bearerToken = QStringLiteral("colab-tts-token");
    request.model = QStringLiteral("kokoro-vn");
    request.text = QStringLiteral("Xin chào Việt Nam");
    request.voice = QStringLiteral("female-01");
    request.language = QStringLiteral("vi");
    request.speed = 1.15F;
    request.settings.insert(QStringLiteral("seed"), 1234);
    request.allowInsecureLocalhost = true;
    QVERIFY(QMetaObject::invokeMethod(runner, "synthesize", Qt::QueuedConnection,
                                      Q_ARG(ColabTtsRequest, request)));

    QVERIFY2(finished.wait(5000), "Colab TTS worker did not finish.");
    QCOMPARE(failures.count(), 0);
    const QList<QVariant> result = finished.takeFirst();
    QCOMPARE(result.at(0).toByteArray().size(), 4);
    QCOMPARE(result.at(1).value<QVector<float>>().size(), 2);
    QCOMPARE(result.at(2).toInt(), 24000);
    QCOMPARE(progress.count(), 1);
    QCOMPARE(progress.constFirst().at(0).toInt(), 100);
    const QByteArray body = server.request();
    QVERIFY(body.startsWith("POST /v1/audio/speech HTTP/1.1\r\n"));
    QVERIFY(body.toLower().contains("authorization: bearer colab-tts-token"));
    QVERIFY(body.contains("\"model\":\"kokoro-vn\""));
    QVERIFY(body.contains("Xin chào Việt Nam"));
    QVERIFY(body.contains("\"voice\":\"female-01\""));
    QVERIFY(body.contains("\"language\":\"vi\""));
    QVERIFY(body.contains("\"seed\":1234"));
    workerThread.quit();
    QVERIFY(workerThread.wait(5000));
}

void TestColabTtsRunner::testTtsModelNotebookMapping()
{
    ColabTtsController controller(nullptr, nullptr, nullptr, nullptr, nullptr);
    const QList<QPair<QString, QString>> models{
        {QStringLiteral("kokoro"), QStringLiteral("LA_STUDIO_TTS_KOKORO_GPU.ipynb")},
        {QStringLiteral("kokoro-vietnamese"), QStringLiteral("LA_STUDIO_TTS_KOKORO_VIETNAMESE_GPU.ipynb")},
        {QStringLiteral("omnivoice"), QStringLiteral("LA_STUDIO_TTS_OMNIVOICE_GPU.ipynb")},
        {QStringLiteral("qwen3-tts-1.7b-customvoice"), QStringLiteral("LA_STUDIO_TTS_QWEN3_CUSTOMVOICE_1_7B_GPU.ipynb")},
        {QStringLiteral("vibevoice"), QStringLiteral("LA_STUDIO_TTS_VIBEVOICE_0_5B_GPU.ipynb")},
        {QStringLiteral("vieneu-tts-v2-turbo"), QStringLiteral("LA_STUDIO_TTS_VIENEU_V2_TURBO_GPU.ipynb")},
        {QStringLiteral("vieneu-tts-v3-turbo"), QStringLiteral("LA_STUDIO_TTS_VIENEU_V3_TURBO_GPU.ipynb")},
        {QStringLiteral("voxcpm2"), QStringLiteral("LA_STUDIO_TTS_VOXCPM2_GPU.ipynb")},
    };
    for (const auto &[model, notebook] : models) {
        QVERIFY2(controller.selectColabModel(model), qPrintable(model));
        QCOMPARE(controller.colabModel(), model);
        QCOMPARE(controller.colabNotebookFile(), notebook);
        QCOMPARE(controller.notebookForColabModel(model), notebook);
    }
    QSignalSpy failures(&controller, &ColabTtsController::errorOccurred);
    QVERIFY(!controller.selectColabModel(QStringLiteral("unknown-tts")));
    QCOMPARE(failures.count(), 1);
}

void TestColabTtsRunner::ttsNotebookMatchesDirectColabContract()
{
    struct NotebookExpectation {
        QString fileName;
        QString familyId;
        QString upstreamModel;
        QString adapterNeedle;
    };
    const QList<NotebookExpectation> expectations{
        {QStringLiteral("LA_STUDIO_TTS_KOKORO_GPU.ipynb"), QStringLiteral("kokoro"),
         QStringLiteral("hexgrad/Kokoro-82M"), QStringLiteral("KPipeline")},
        {QStringLiteral("LA_STUDIO_TTS_KOKORO_VIETNAMESE_GPU.ipynb"), QStringLiteral("kokoro-vietnamese"),
         QStringLiteral("contextboxai/Kokoro-Vietnamese"), QStringLiteral("KokoroVietnameseONNX")},
        {QStringLiteral("LA_STUDIO_TTS_OMNIVOICE_GPU.ipynb"), QStringLiteral("omnivoice"),
         QStringLiteral("k2-fsa/OmniVoice"), QStringLiteral("OmniVoice.from_pretrained")},
        {QStringLiteral("LA_STUDIO_TTS_QWEN3_CUSTOMVOICE_1_7B_GPU.ipynb"), QStringLiteral("qwen3-tts-1.7b-customvoice"),
         QStringLiteral("Qwen/Qwen3-TTS-12Hz-1.7B-CustomVoice"), QStringLiteral("generate_custom_voice")},
        {QStringLiteral("LA_STUDIO_TTS_VIBEVOICE_0_5B_GPU.ipynb"), QStringLiteral("vibevoice"),
         QStringLiteral("microsoft/VibeVoice-Realtime-0.5B"), QStringLiteral("VibeVoiceStreamingForConditionalGenerationInference")},
        {QStringLiteral("LA_STUDIO_TTS_VIENEU_V2_TURBO_GPU.ipynb"), QStringLiteral("vieneu-tts-v2-turbo"),
         QStringLiteral("pnnbao-ump/VieNeu-TTS-v2-Turbo"), QStringLiteral("mode=\"turbo_gpu\"")},
        {QStringLiteral("LA_STUDIO_TTS_VIENEU_V3_TURBO_GPU.ipynb"), QStringLiteral("vieneu-tts-v3-turbo"),
         QStringLiteral("pnnbao-ump/VieNeu-TTS-v3-Turbo"), QStringLiteral("mode=\"v3turbo\"")},
        {QStringLiteral("LA_STUDIO_TTS_VOXCPM2_GPU.ipynb"), QStringLiteral("voxcpm2"),
         QStringLiteral("openbmb/VoxCPM2"), QStringLiteral("VoxCPM.from_pretrained")},
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
        QCOMPARE(metadata.value(QStringLiteral("capability")).toString(), QStringLiteral("tts"));
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
        QVERIFY2(source.contains(expected.adapterNeedle), qPrintable(expected.fileName));
        QVERIFY(source.contains(QStringLiteral("if not torch.cuda.is_available()")));
        QVERIFY(source.contains(QStringLiteral("@app.post(\"/v1/audio/speech\")")));
        QVERIFY(source.contains(QStringLiteral("@app.get(\"/v1/capabilities\")")));
        QVERIFY(source.contains(QStringLiteral("\"id\": \"tts\"")));
        QVERIFY(source.contains(QStringLiteral("\"device\": \"cuda\"")));
        QVERIFY(source.contains(QStringLiteral("MAX_INPUT_CHARS = 4000")));
        QVERIFY(source.contains(QStringLiteral("MAX_OUTPUT_SECONDS = 300")));
        QVERIFY(source.contains(QStringLiteral("REQUEST_SLOTS = threading.BoundedSemaphore(1)")));
        QVERIFY(source.contains(QStringLiteral("if request.model.strip().lower() != MODEL_ID")));
        QVERIFY(source.contains(QStringLiteral("status_code=409")));
        QVERIFY(source.contains(QStringLiteral("status_code=429")));
        QVERIFY(source.contains(QStringLiteral("status_code=413")));
        QVERIFY(source.contains(QStringLiteral("REQUEST_SLOTS.release()")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_TTS_URL")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_TTS_TOKEN")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_TTS_MODEL")));
        QVERIFY(source.contains(QStringLiteral("cloudflared")));
        QVERIFY2(source.contains(QStringLiteral("STARTUP_TIMEOUT_SECONDS = 20 * 60")),
                 "Exact-model TTS notebook must allow a cold CUDA model download to finish.");
        QVERIFY2(source.contains(QStringLiteral("WORKER_LOG")),
                 "Exact-model TTS notebook must retain startup logs when a worker fails.");
        QVERIFY2(source.contains(QStringLiteral("LA Studio worker log")),
                 "Exact-model TTS notebook must return the root worker error instead of a generic timeout.");
        QVERIFY2(source.contains(QStringLiteral("str(health.get(\"model\", \"\")).strip().lower() == MODEL_ID")),
                 "Exact-model TTS notebook must validate that the ready worker is the selected model.");
        if (expected.familyId.startsWith(QStringLiteral("vieneu-tts-"))) {
            QVERIFY2(source.contains(QStringLiteral("torchvision==0.23.0")),
                     "VieNeu TTS must replace Colab's potentially incompatible torchvision build.");
            QVERIFY2(source.contains(QStringLiteral("Qwen3ForCausalLM")),
                     "VieNeu TTS must validate the Qwen3 Transformers import before starting its worker.");
            QVERIFY2(source.contains(QStringLiteral("PreTrainedModel")),
                     "VieNeu TTS must validate the v3 Transformers import before starting its worker.");
        }
        QVERIFY(!source.contains(QStringLiteral("API_GATEWAY")));
    }

    QFile page(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
                   .filePath(QStringLiteral("qml/pages/TtsPage.qml")));
    QVERIFY(page.open(QIODevice::ReadOnly));
    const QByteArray pageSource = page.readAll();
    QVERIFY(pageSource.contains("colabModelSelectionEnabled: true"));
    QVERIFY(pageSource.contains("AppController.colabTts.selectColabModel(familyId)"));

    QFile settings(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
                       .filePath(QStringLiteral("qml/components/tts/TtsSettingsPanel.qml")));
    QVERIFY(settings.open(QIODevice::ReadOnly));
    const QByteArray settingsSource = settings.readAll();
    QVERIFY(settingsSource.contains("AppController.colabTts.colabNotebookFile"));
    QVERIFY(settingsSource.contains("Selected Colab model"));

    QFile output(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
                     .filePath(QStringLiteral("qml/components/shared/GeneratedAudioOutput.qml")));
    QVERIFY(output.open(QIODevice::ReadOnly));
    const QByteArray outputSource = output.readAll();
    QVERIFY(outputSource.contains("progress unavailable from this provider"));
    QVERIFY(outputSource.contains("root.progressEstimated ? qsTr(\"Working\")"));
}

} // namespace LAStudio
