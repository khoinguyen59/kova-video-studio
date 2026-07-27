#include "test_SttSession.h"
#include <QtTest>
#include <QSignalSpy>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QThreadPool>
#include <QUrl>

#include "controllers/app/AppController.h"
#include "controllers/stt/SttSessionController.h"
#include "controllers/stt/SttAudioDecoder.h"
#include "controllers/models/ModelLifecycleController.h"
#include "core/StudioCapabilityRegistry.h"
#include "stt/SttEngine.h"
#include "stt/ColabSttRunner.h"

namespace LAStudio {
namespace {

class ColabSttMock final : public QObject
{
public:
    ColabSttMock()
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
        const QByteArray response = QByteArrayLiteral(
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n"
            "{\"text\":\"Hello world\",\"segments\":[{\"id\":0,\"start\":0.0,\"end\":1.5,\"text\":\"Hello world\"}]}");
        m_socket->write(response);
        m_socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QPointer<QTcpSocket> m_socket;
    QByteArray m_pending;
    QByteArray m_request;
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

void TestSttSession::testColabSttRunnerPostsKovaCompatibleMultipart()
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

    ColabSttRequest request;
    request.workerUrl = QUrl(server.baseUrl());
    request.bearerToken = QStringLiteral("colab-test-token");
    request.samples = {0.0F, 0.25F, -0.25F, 0.0F};
    request.language = QStringLiteral("en");
    request.allowInsecureLocalhost = true;
    QVERIFY(QMetaObject::invokeMethod(runner, "transcribe", Qt::QueuedConnection,
                                      Q_ARG(ColabSttRequest, request)));

    QVERIFY2(finished.wait(5000), "Colab STT worker did not finish.");
    QCOMPARE(failures.count(), 0);
    QCOMPARE(finished.takeFirst().at(0).toString(), QStringLiteral("Hello world"));
    const QByteArray body = server.request();
    QVERIFY(body.startsWith("POST /v1/audio/transcriptions HTTP/1.1\r\n"));
    QVERIFY(body.toLower().contains("authorization: bearer colab-test-token"));
    QVERIFY(body.contains("name=\"response_format\""));
    QVERIFY(body.contains("verbose_json"));
    QVERIFY(body.contains("name=\"file\"; filename=\"audio.wav\""));
    QVERIFY(body.contains("RIFF"));
    workerThread.quit();
    QVERIFY(workerThread.wait(5000));
}

} // namespace LAStudio
