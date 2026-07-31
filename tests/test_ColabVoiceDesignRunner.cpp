#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QPointer>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>

#include <cstring>

#include "controllers/tts/ColabVoiceDesignController.h"
#include "controllers/shared/VoiceDesignPresetService.h"
#include "remote/ColabSession.h"
#include "tts/ColabVoiceDesignRunner.h"
#include "test_ColabVoiceDesignRunner.h"

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

class VoiceDesignMock final : public QObject
{
public:
    VoiceDesignMock()
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

void TestColabVoiceDesignRunner::testPostsIndependentVoiceDesignContract()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    const QByteArray previousDataDir = qgetenv("LASTUDIO_DATA_DIR");
    const bool hadDataDir = qEnvironmentVariableIsSet("LASTUDIO_DATA_DIR");
    qputenv("LASTUDIO_DATA_DIR", profile.path().toUtf8());
    const auto restoreDataDir = qScopeGuard([hadDataDir, previousDataDir] {
        if (hadDataDir) qputenv("LASTUDIO_DATA_DIR", previousDataDir);
        else qunsetenv("LASTUDIO_DATA_DIR");
    });
    const QString family = QStringLiteral("qwen3-tts-1.7b-voicedesign");
    const QString description = QStringLiteral("Warm low female voice");
    VoiceDesignPresetService presets;
    QVERIFY(presets.addPreset(family, QStringLiteral("Saved warm voice"), description));
    const QVariantMap saved = presets.presetsForFamily(family).constFirst().toMap();
    QVERIFY(!saved.value(QStringLiteral("id")).toString().isEmpty());
    VoiceDesignPresetService restarted;
    const QVariantMap reloaded = restarted.presetsForFamily(family).constFirst().toMap();
    QCOMPARE(reloaded.value(QStringLiteral("id")).toString(), saved.value(QStringLiteral("id")).toString());
    QCOMPARE(reloaded.value(QStringLiteral("description")).toString(), description);

    VoiceDesignMock server;
    QVERIFY(server.start());
    qRegisterMetaType<ColabVoiceDesignRequest>("ColabVoiceDesignRequest");
    qRegisterMetaType<QVector<float>>("QVector<float>");
    QThread workerThread;
    auto *runner = new ColabVoiceDesignRunner;
    runner->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, runner, &QObject::deleteLater);
    workerThread.start();
    QSignalSpy finished(runner, &ColabVoiceDesignRunner::finished);
    QSignalSpy failures(runner, &ColabVoiceDesignRunner::failed);
    QSignalSpy progress(runner, &ColabVoiceDesignRunner::progress);

    ColabVoiceDesignRequest request;
    request.workerUrl = QUrl(server.baseUrl());
    request.bearerToken = QStringLiteral("colab-design-token");
    request.model = family;
    request.text = QStringLiteral("A short design line.");
    request.voiceDescription = reloaded.value(QStringLiteral("description")).toString();
    request.style = QStringLiteral("Calm and intimate");
    request.language = QStringLiteral("en");
    request.temperature = 0.7F;
    request.seed = 4242;
    request.allowInsecureLocalhost = true;
    QVERIFY(QMetaObject::invokeMethod(runner, "generate", Qt::QueuedConnection,
                                      Q_ARG(ColabVoiceDesignRequest, request)));

    QVERIFY2(finished.wait(5000), "Colab VoiceDesign worker did not finish.");
    QCOMPARE(failures.count(), 0);
    const QList<QVariant> result = finished.takeFirst();
    QCOMPARE(result.at(0).toByteArray().size(), 4);
    QCOMPARE(result.at(1).value<QVector<float>>().size(), 2);
    QCOMPARE(result.at(2).toInt(), 24000);
    QCOMPARE(progress.count(), 1);
    QCOMPARE(progress.constFirst().at(0).toInt(), 100);
    const QByteArray body = server.request();
    QVERIFY(body.startsWith("POST /v1/audio/voice_designs HTTP/1.1\r\n"));
    QVERIFY(body.toLower().contains("authorization: bearer colab-design-token"));
    const QJsonObject payload = QJsonDocument::fromJson(body.mid(body.indexOf("\r\n\r\n") + 4)).object();
    QCOMPARE(payload.value(QStringLiteral("model")).toString(), QStringLiteral("qwen3-tts-1.7b-voicedesign"));
    QCOMPARE(payload.value(QStringLiteral("input")).toString(), QStringLiteral("A short design line."));
    QCOMPARE(payload.value(QStringLiteral("voice_description")).toString(), QStringLiteral("Warm low female voice"));
    QCOMPARE(payload.value(QStringLiteral("style")).toString(), QStringLiteral("Calm and intimate"));
    QCOMPARE(payload.value(QStringLiteral("language")).toString(), QStringLiteral("en"));
    QVERIFY(qAbs(payload.value(QStringLiteral("temperature")).toDouble() - 0.7) < 0.001);
    QCOMPARE(payload.value(QStringLiteral("seed")).toInteger(), 4242);
    QVERIFY(!payload.contains(QStringLiteral("profile_id")));
    QVERIFY(!payload.contains(QStringLiteral("ref_audio")));
    workerThread.quit();
    QVERIFY(workerThread.wait(5000));
}

void TestColabVoiceDesignRunner::exactModelMappingMatchesCatalogAndNotebooks()
{
    const QList<QPair<QString, QString>> mappings{
        {QStringLiteral("omnivoice"), QStringLiteral("LA_STUDIO_VOICE_DESIGN_OMNIVOICE_GPU.ipynb")},
        {QStringLiteral("qwen3-tts-1.7b-voicedesign"), QStringLiteral("LA_STUDIO_VOICE_DESIGN_QWEN3_1_7B_GPU.ipynb")},
        {QStringLiteral("voxcpm2"), QStringLiteral("LA_STUDIO_VOICE_DESIGN_VOXCPM2_GPU.ipynb")},
    };
    ColabVoiceDesignController controller(nullptr, nullptr, nullptr, nullptr, nullptr);
    for (const auto &[model, notebook] : mappings) {
        QCOMPARE(controller.notebookForColabModel(model), notebook);
        QVERIFY(controller.selectColabModel(model));
        QCOMPARE(controller.model(), model);
        QCOMPARE(controller.colabNotebookFile(), notebook);

        const QString path = QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
            .filePath(QStringLiteral("notebooks/") + notebook);
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        QVERIFY(document.isObject());
        const QJsonObject root = document.object();
        QCOMPARE(root.value(QStringLiteral("nbformat")).toInt(), 4);
        QCOMPARE(root.value(QStringLiteral("metadata")).toObject()
                     .value(QStringLiteral("la_studio")).toObject()
                     .value(QStringLiteral("family_id")).toString(), model);

        QString source;
        const QJsonArray cells = root.value(QStringLiteral("cells")).toArray();
        QVERIFY(cells.size() >= 4);
        for (const QJsonValue &cellValue : cells) {
            const QJsonArray lines = cellValue.toObject().value(QStringLiteral("source")).toArray();
            for (const QJsonValue &line : lines) source += line.toString();
        }
        QVERIFY(source.contains(QStringLiteral("MODEL_ID = \"%1\"").arg(model)));
        QVERIFY(source.contains(QStringLiteral("if not torch.cuda.is_available()")));
        QVERIFY(source.contains(QStringLiteral("@app.post(\"/v1/audio/voice_designs\")")));
        QVERIFY(source.contains(QStringLiteral("@app.get(\"/v1/capabilities\")")));
        QVERIFY(source.contains(QStringLiteral("\"id\": \"voice-design\"")));
        QVERIFY(source.contains(QStringLiteral("status_code=429")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_VOICE_DESIGN_URL")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_VOICE_DESIGN_TOKEN")));
        QVERIFY(source.contains(QStringLiteral("cloudflared")));
        QVERIFY(!source.contains(QStringLiteral("API_GATEWAY")));
        QVERIFY(!source.contains(QStringLiteral("GATEWAY_BASE_URL")));
    }
    QVERIFY(controller.notebookForColabModel(QStringLiteral("not-a-model")).isEmpty());

    ColabSession session;
    QString error;
    QVERIFY(session.setSession(QStringLiteral("http://127.0.0.1:3924"),
                               QStringLiteral("temporary-token"), &error, true));
    ColabVoiceDesignController sessionController(&session, nullptr, nullptr, nullptr, nullptr);
    QVERIFY(sessionController.selectColabModel(QStringLiteral("omnivoice")));
    QVERIFY2(!session.isActive(), "Changing the design model must discard the previous model worker.");

    QFile output(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
                     .filePath(QStringLiteral("qml/components/voicedesign/VoiceDesignStudioView.qml")));
    QVERIFY(output.open(QIODevice::ReadOnly));
    QVERIFY(output.readAll().contains("progressEstimated: root.colabActive ? true"));
}

} // namespace LAStudio
