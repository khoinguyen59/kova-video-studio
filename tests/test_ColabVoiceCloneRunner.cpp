#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>

#include <cstring>

#include "controllers/shared/VoiceClonePresetService.h"
#include "controllers/tts/ColabVoiceCloneController.h"
#include "remote/ColabSession.h"
#include "tts/ColabVoiceCloneRunner.h"
#include "test_ColabVoiceCloneRunner.h"

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

QByteArray referenceWav()
{
    constexpr quint32 frameCount = 3 * 24000;
    constexpr quint32 dataSize = frameCount * sizeof(qint16);
    QByteArray wav(static_cast<int>(44 + dataSize), '\0');
    std::memcpy(wav.data(), "RIFF", 4);
    const quint32 size = 36 + dataSize;
    std::memcpy(wav.data() + 4, &size, sizeof(size));
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    const quint32 fmtSize = 16;
    const quint16 format = 1;
    const quint16 channels = 1;
    const quint32 rate = 24000;
    const quint32 byteRate = 48000;
    const quint16 align = 2;
    const quint16 bits = 16;
    std::memcpy(wav.data() + 16, &fmtSize, sizeof(fmtSize));
    std::memcpy(wav.data() + 20, &format, sizeof(format));
    std::memcpy(wav.data() + 22, &channels, sizeof(channels));
    std::memcpy(wav.data() + 24, &rate, sizeof(rate));
    std::memcpy(wav.data() + 28, &byteRate, sizeof(byteRate));
    std::memcpy(wav.data() + 32, &align, sizeof(align));
    std::memcpy(wav.data() + 34, &bits, sizeof(bits));
    std::memcpy(wav.data() + 36, "data", 4);
    std::memcpy(wav.data() + 40, &dataSize, sizeof(dataSize));
    return wav;
}

class VoiceCloneMock final : public QObject
{
public:
    VoiceCloneMock()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { consume(socket); });
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    QList<QByteArray> requests() const { return m_requests; }

    int requestCount(const QByteArray &prefix) const
    {
        int count = 0;
        for (const QByteArray &request : m_requests) {
            if (request.startsWith(prefix)) ++count;
        }
        return count;
    }

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
            response = jsonResponse(202, QByteArrayLiteral("{\"id\":\"profile-job\",\"status\":\"queued\",\"percent\":0}"));
        } else if (firstLine.startsWith("GET /v2/jobs/profile-job ")) {
            response = jsonResponse(200, QByteArrayLiteral("{\"id\":\"profile-job\",\"status\":\"succeeded\",\"percent\":100,\"result\":{\"id\":\"profile-1\"}}"));
        } else if (firstLine.startsWith("POST /v2/jobs/generation ")) {
            response = jsonResponse(202, QByteArrayLiteral("{\"id\":\"generation-job\",\"status\":\"queued\",\"percent\":0}"));
        } else if (firstLine.startsWith("GET /v2/jobs/generation-job/audio ")) {
            const QByteArray audio = pcm16Wav();
            response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: ")
                + QByteArray::number(audio.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + audio;
        } else if (firstLine.startsWith("GET /v2/jobs/generation-job ")) {
            response = jsonResponse(200, QByteArrayLiteral("{\"id\":\"generation-job\",\"status\":\"succeeded\",\"percent\":100}"));
        } else if (firstLine.startsWith("DELETE /v1/profiles/profile-1 ")) {
            response = jsonResponse(200, QByteArrayLiteral("{\"deleted\":true}"));
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
        const auto match = QRegularExpression(QStringLiteral("Content-Length: (\\d+)"),
                                              QRegularExpression::CaseInsensitiveOption)
                               .match(QString::fromLatin1(pending.left(headerEnd)));
        const int bodyLength = match.hasMatch() ? match.captured(1).toInt() : 0;
        const int requestLength = headerEnd + 4 + bodyLength;
        if (pending.size() < requestLength) return;
        const QByteArray request = pending.left(requestLength);
        m_requests.append(request);
        pending.clear();
        respond(socket, request);
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_pending;
    QList<QByteArray> m_requests;
};

} // namespace

void TestColabVoiceCloneRunner::testRunsVoiceProfileAndGenerationDirectlyOnColab()
{
    VoiceCloneMock server;
    QVERIFY(server.start());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString referencePath = temporary.filePath(QStringLiteral("reference.wav"));
    QFile reference(referencePath);
    QVERIFY(reference.open(QIODevice::WriteOnly));
    QVERIFY(reference.write(referenceWav()) > 0);
    reference.close();

    qRegisterMetaType<ColabVoiceCloneRequest>("ColabVoiceCloneRequest");
    qRegisterMetaType<QVector<float>>("QVector<float>");
    QThread workerThread;
    auto *runner = new ColabVoiceCloneRunner;
    runner->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, runner, &QObject::deleteLater);
    workerThread.start();
    QSignalSpy profiles(runner, &ColabVoiceCloneRunner::profileReady);
    QSignalSpy deleted(runner, &ColabVoiceCloneRunner::profileDeleted);
    QSignalSpy finished(runner, &ColabVoiceCloneRunner::finished);
    QSignalSpy failures(runner, &ColabVoiceCloneRunner::failed);
    QSignalSpy progress(runner, &ColabVoiceCloneRunner::progress);

    ColabVoiceCloneRequest request;
    request.workerUrl = QUrl(server.baseUrl());
    request.bearerToken = QStringLiteral("colab-voice-token");
    request.model = QStringLiteral("qwen3-tts-0.6b-base");
    request.referencePath = referencePath;
    request.referenceName = QStringLiteral("Consent test voice");
    request.referenceText = QStringLiteral("This is the exact transcript.");
    request.text = QStringLiteral("Generate this sentence.");
    request.language = QStringLiteral("en");
    request.speed = 1.1F;
    request.steps = 28;
    request.consentConfirmed = true;
    request.allowInsecureLocalhost = true;
    QVERIFY(QMetaObject::invokeMethod(runner, "clone", Qt::QueuedConnection,
                                      Q_ARG(ColabVoiceCloneRequest, request)));

    QVERIFY2(finished.wait(8000), "Colab voice-cloning worker did not finish.");
    QCOMPARE(failures.count(), 0);
    QCOMPARE(profiles.count(), 1);
    QCOMPARE(profiles.takeFirst().at(0).toString(), QStringLiteral("profile-1"));
    const QList<QVariant> result = finished.takeFirst();
    QCOMPARE(result.at(0).toByteArray().size(), 4);
    QCOMPARE(result.at(1).value<QVector<float>>().size(), 2);
    QCOMPARE(result.at(2).toInt(), 24000);
    QVERIFY(progress.count() >= 4);
    for (const QList<QVariant> &event : progress) {
        const int workerPercent = event.at(0).toInt();
        QVERIFY(workerPercent == 0 || workerPercent == 100);
    }
    QVERIFY(!progress.contains(QList<QVariant>{1, QStringLiteral("upload_reference")}));
    QVERIFY(!progress.contains(QList<QVariant>{52, QStringLiteral("queue_generation")}));
    QVERIFY(!progress.contains(QList<QVariant>{96, QStringLiteral("download_audio")}));

    const QList<QByteArray> calls = server.requests();
    QCOMPARE(calls.size(), 5);
    QCOMPARE(calls.at(0).left(calls.at(0).indexOf("\r\n")), QByteArrayLiteral("POST /v2/jobs/profile HTTP/1.1"));
    QVERIFY(calls.at(0).toLower().contains("authorization: bearer colab-voice-token"));
    QVERIFY(calls.at(0).toLower().contains("content-type: audio/wav"));
    QVERIFY(calls.at(0).contains("consent_confirmed"));
    QVERIFY(calls.at(0).contains("qwen3-tts-0.6b-base"));
    QVERIFY(calls.at(0).contains("This is the exact transcript."));
    QCOMPARE(calls.at(2).left(calls.at(2).indexOf("\r\n")), QByteArrayLiteral("POST /v2/jobs/generation HTTP/1.1"));
    QVERIFY(calls.at(2).contains("\"profile_id\":\"profile-1\""));
    QVERIFY(calls.at(2).contains("\"model\":\"qwen3-tts-0.6b-base\""));
    QVERIFY(calls.at(2).contains("\"text\":\"Generate this sentence.\""));
    QVERIFY(calls.at(2).contains("\"language\":\"en\""));
    QVERIFY(calls.at(2).contains("\"num_step\":28"));
    QVERIFY(calls.at(4).startsWith("GET /v2/jobs/generation-job/audio HTTP/1.1\r\n"));

    ColabVoiceCloneRequest deleteRequest;
    deleteRequest.workerUrl = QUrl(server.baseUrl());
    deleteRequest.bearerToken = QStringLiteral("colab-voice-token");
    deleteRequest.existingProfileId = QStringLiteral("profile-1");
    deleteRequest.allowInsecureLocalhost = true;
    QVERIFY(QMetaObject::invokeMethod(runner, "deleteProfile", Qt::QueuedConnection,
                                      Q_ARG(ColabVoiceCloneRequest, deleteRequest)));
    QVERIFY2(deleted.wait(5000), "Colab voice profile was not deleted.");
    const QList<QByteArray> callsAfterDelete = server.requests();
    QCOMPARE(callsAfterDelete.size(), 6);
    QVERIFY(callsAfterDelete.at(5).startsWith("DELETE /v1/profiles/profile-1 HTTP/1.1\r\n"));
    QVERIFY(callsAfterDelete.at(5).toLower().contains("authorization: bearer colab-voice-token"));
    workerThread.quit();
    QVERIFY(workerThread.wait(5000));
}

void TestColabVoiceCloneRunner::controllerReusesProfileOnlyForMatchingDurableReference()
{
    VoiceCloneMock server;
    QVERIFY(server.start());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString referencePath = temporary.filePath(QStringLiteral("reference.wav"));
    QFile reference(referencePath);
    QVERIFY(reference.open(QIODevice::WriteOnly));
    QVERIFY(reference.write(referenceWav()) > 0);
    reference.close();
    const QString replacementReferencePath = temporary.filePath(QStringLiteral("replacement-reference.wav"));
    QFile replacementReference(replacementReferencePath);
    QVERIFY(replacementReference.open(QIODevice::WriteOnly));
    QVERIFY(replacementReference.write(referenceWav()) > 0);
    replacementReference.close();

    // setSession() is the explicit unit-test seam. Production UI must pass the
    // asynchronous exact worker verification before it can call useColab().
    ColabSession session;
    QString sessionError;
    QVERIFY(session.setSession(server.baseUrl(), QStringLiteral("loopback-token"),
                               &sessionError, true));
    ColabVoiceCloneController controller(&session, nullptr, nullptr, nullptr, nullptr);
    controller.useColab();
    QVERIFY(controller.colabActive());
    QSignalSpy finished(&controller, &ColabVoiceCloneController::synthesisFinished);
    QSignalSpy errors(&controller, &ColabVoiceCloneController::errorOccurred);

    const auto clone = [&controller, &finished](const QString &text, const QString &path,
                                                const QString &referenceText,
                                                const QString &language) {
        const int expected = finished.count() + 1;
        controller.cloneVoice(text, path, referenceText, language,
                              QStringLiteral("Saved reference"), true);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), expected, 8000);
    };

    clone(QStringLiteral("First segment"), referencePath, QStringLiteral("Exact source transcript"),
          QStringLiteral("vi"));
    QCOMPARE(errors.count(), 0);
    QCOMPARE(controller.profileId(), QStringLiteral("profile-1"));
    QCOMPARE(server.requestCount(QByteArrayLiteral("POST /v2/jobs/profile ")), 1);

    // Changing generated text must reuse the temporary profile created from
    // the same durable reference, transcript, language, and exact model.
    clone(QStringLiteral("Second segment"), referencePath, QStringLiteral("Exact source transcript"),
          QStringLiteral("vi"));
    QCOMPARE(errors.count(), 0);
    QCOMPARE(server.requestCount(QByteArrayLiteral("POST /v2/jobs/profile ")), 1);
    QCOMPARE(server.requestCount(QByteArrayLiteral("POST /v2/jobs/generation ")), 2);
    QVERIFY(server.requests().at(5).contains("\"profile_id\":\"profile-1\""));

    // Transcript/language are durable-reference inputs; either change must
    // rebuild the transient worker profile rather than silently reuse it.
    clone(QStringLiteral("Third segment"), referencePath, QStringLiteral("Changed exact transcript"),
          QStringLiteral("vi"));
    QCOMPARE(server.requestCount(QByteArrayLiteral("POST /v2/jobs/profile ")), 2);

    clone(QStringLiteral("Fourth segment"), referencePath,
          QStringLiteral("Changed exact transcript"), QStringLiteral("en"));
    QCOMPARE(server.requestCount(QByteArrayLiteral("POST /v2/jobs/profile ")), 3);

    clone(QStringLiteral("Fifth segment"), replacementReferencePath,
          QStringLiteral("Changed exact transcript"), QStringLiteral("en"));
    QCOMPARE(server.requestCount(QByteArrayLiteral("POST /v2/jobs/profile ")), 4);

    // A new worker session invalidates the old temporary profile. The next
    // request rebuilds it from the still-local durable reference.
    session.clear();
    QVERIFY(controller.profileId().isEmpty());
    QVERIFY(session.setSession(server.baseUrl(), QStringLiteral("loopback-token-2"),
                               &sessionError, true));
    controller.useColab();
    clone(QStringLiteral("Sixth segment"), replacementReferencePath,
          QStringLiteral("Changed exact transcript"),
          QStringLiteral("vi"));
    QCOMPARE(server.requestCount(QByteArrayLiteral("POST /v2/jobs/profile ")), 5);
    QCOMPARE(errors.count(), 0);

    // Changing model explicitly clears the session and the temporary profile;
    // it cannot reuse a profile across exact worker/model contracts.
    QVERIFY(controller.selectColabModel(QStringLiteral("qwen3-tts-0.6b-base")));
    QVERIFY(controller.profileId().isEmpty());
    QVERIFY(!controller.colabConnected());
}

void TestColabVoiceCloneRunner::controllerAllowsAutomaticReferenceTranscript()
{
    VoiceCloneMock server;
    QVERIFY(server.start());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString referencePath = temporary.filePath(QStringLiteral("reference.wav"));
    QFile reference(referencePath);
    QVERIFY(reference.open(QIODevice::WriteOnly));
    QVERIFY(reference.write(referenceWav()) > 0);
    reference.close();

    ColabSession session;
    QString sessionError;
    QVERIFY(session.setSession(server.baseUrl(), QStringLiteral("loopback-token"),
                               &sessionError, true));
    ColabVoiceCloneController controller(&session, nullptr, nullptr, nullptr, nullptr);
    controller.useColab();
    QSignalSpy finished(&controller, &ColabVoiceCloneController::synthesisFinished);
    QSignalSpy errors(&controller, &ColabVoiceCloneController::errorOccurred);

    controller.cloneVoice(QStringLiteral("Automatic transcript test"), referencePath, QString(),
                          QStringLiteral("vi"), QStringLiteral("Automatic reference"), true);
    QVERIFY2(finished.wait(8000), "The optional transcript Colab clone did not finish.");
    QCOMPARE(errors.count(), 0);
    const QList<QByteArray> calls = server.requests();
    QVERIFY(!calls.isEmpty());
    QVERIFY(calls.first().startsWith("POST /v2/jobs/profile HTTP/1.1\r\n"));
    QVERIFY(calls.first().contains("name=\"ref_text\""));
}

void TestColabVoiceCloneRunner::savedPresetSurvivesRestartAndInvalidatesTemporaryProfile()
{
    QTemporaryDir dataDirectory;
    QVERIFY(dataDirectory.isValid());
    const QByteArray previousDataDirectory = qgetenv("LASTUDIO_DATA_DIR");
    qputenv("LASTUDIO_DATA_DIR", dataDirectory.path().toUtf8());
    const auto restoreDataDirectory = qScopeGuard([previousDataDirectory] {
        if (previousDataDirectory.isEmpty()) qunsetenv("LASTUDIO_DATA_DIR");
        else qputenv("LASTUDIO_DATA_DIR", previousDataDirectory);
    });

    QTemporaryDir sourceDirectory;
    QVERIFY(sourceDirectory.isValid());
    const QString sourcePath = sourceDirectory.filePath(QStringLiteral("reference.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QVERIFY(source.write(referenceWav()) > 0);
    source.close();

    VoiceClonePresetService createdPresets;
    QVERIFY(createdPresets.addPreset(QStringLiteral("omnivoice"), QStringLiteral("Saved one"),
                                     sourcePath, QStringLiteral("Exact saved transcript")));
    QVERIFY(createdPresets.addPreset(QStringLiteral("omnivoice"), QStringLiteral("Saved two"),
                                     sourcePath, QStringLiteral("Exact saved transcript")));

    // A new service represents an application restart: the persisted IDs and
    // managed audio must be available without retaining the original source.
    VoiceClonePresetService reloadedPresets;
    const QVariantList saved = reloadedPresets.presetsForFamily(QStringLiteral("omnivoice"));
    QCOMPARE(saved.size(), 2);
    const QVariantMap savedOne = saved.at(0).toMap();
    const QVariantMap savedTwo = saved.at(1).toMap();
    QVERIFY(savedOne.value(QStringLiteral("valid")).toBool());
    QVERIFY(savedTwo.value(QStringLiteral("valid")).toBool());
    QVERIFY(!savedOne.value(QStringLiteral("id")).toString().isEmpty());
    QVERIFY(!savedTwo.value(QStringLiteral("id")).toString().isEmpty());
    QVERIFY(QFileInfo::exists(savedOne.value(QStringLiteral("audioPath")).toString()));

    VoiceCloneMock server;
    QVERIFY(server.start());
    ColabSession session;
    QString sessionError;
    QVERIFY(session.setSession(server.baseUrl(), QStringLiteral("loopback-token"),
                               &sessionError, true));
    ColabVoiceCloneController controller(&session, nullptr, nullptr, nullptr, nullptr);
    controller.useColab();
    QSignalSpy finished(&controller, &ColabVoiceCloneController::synthesisFinished);
    QSignalSpy errors(&controller, &ColabVoiceCloneController::errorOccurred);
    const auto run = [&controller, &finished, &savedOne](const QString &text, const QString &presetId) {
        const int expected = finished.count() + 1;
        controller.cloneVoice(text, savedOne.value(QStringLiteral("audioPath")).toString(),
                              savedOne.value(QStringLiteral("referenceText")).toString(),
                              QStringLiteral("vi"), QStringLiteral("Saved reference"), true, presetId);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), expected, 8000);
    };

    run(QStringLiteral("First saved sentence"), savedOne.value(QStringLiteral("id")).toString());
    run(QStringLiteral("Second saved sentence"), savedOne.value(QStringLiteral("id")).toString());
    QCOMPARE(server.requestCount(QByteArrayLiteral("POST /v2/jobs/profile ")), 1);
    QCOMPARE(server.requestCount(QByteArrayLiteral("POST /v2/jobs/generation ")), 2);

    // The UI normally changes audio too, but this isolates the durable-ID
    // invariant: a different saved selection cannot share an old profile.
    run(QStringLiteral("Third saved sentence"), savedTwo.value(QStringLiteral("id")).toString());
    QCOMPARE(server.requestCount(QByteArrayLiteral("POST /v2/jobs/profile ")), 2);
    QCOMPARE(server.requestCount(QByteArrayLiteral("POST /v2/jobs/generation ")), 3);
    QCOMPARE(errors.count(), 0);
}

void TestColabVoiceCloneRunner::testRejectsProfileWithoutConsent()
{
    VoiceCloneMock server;
    QVERIFY(server.start());
    qRegisterMetaType<ColabVoiceCloneRequest>("ColabVoiceCloneRequest");
    QThread workerThread;
    auto *runner = new ColabVoiceCloneRunner;
    runner->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, runner, &QObject::deleteLater);
    workerThread.start();
    QSignalSpy failures(runner, &ColabVoiceCloneRunner::failed);

    ColabVoiceCloneRequest request;
    request.workerUrl = QUrl(server.baseUrl());
    request.bearerToken = QStringLiteral("colab-voice-token");
    request.model = QStringLiteral("omnivoice");
    request.text = QStringLiteral("Blocked before upload");
    request.allowInsecureLocalhost = true;
    QVERIFY(QMetaObject::invokeMethod(runner, "clone", Qt::QueuedConnection,
                                      Q_ARG(ColabVoiceCloneRequest, request)));
    QVERIFY(failures.wait(5000));
    QCOMPARE(failures.takeFirst().at(0).toString(), QStringLiteral("Confirm permission before creating a voice profile"));
    QCOMPARE(server.requests().size(), 0);
    workerThread.quit();
    QVERIFY(workerThread.wait(5000));
}

void TestColabVoiceCloneRunner::exactModelMappingMatchesCatalogAndNotebooks()
{
    const QList<QPair<QString, QString>> mappings{
        {QStringLiteral("omnivoice"), QStringLiteral("LA_STUDIO_VOICE_CLONE_OMNIVOICE_GPU.ipynb")},
        {QStringLiteral("qwen3-tts-0.6b-base"), QStringLiteral("LA_STUDIO_VOICE_CLONE_QWEN3_BASE_0_6B_GPU.ipynb")},
        {QStringLiteral("qwen3-tts-1.7b-base"), QStringLiteral("LA_STUDIO_VOICE_CLONE_QWEN3_BASE_1_7B_GPU.ipynb")},
        {QStringLiteral("vieneu-tts-v2-turbo"), QStringLiteral("LA_STUDIO_VOICE_CLONE_VIENEU_V2_TURBO_GPU.ipynb")},
        {QStringLiteral("vieneu-tts-v3-turbo"), QStringLiteral("LA_STUDIO_VOICE_CLONE_VIENEU_V3_TURBO_GPU.ipynb")},
        {QStringLiteral("voxcpm2"), QStringLiteral("LA_STUDIO_VOICE_CLONE_VOXCPM2_GPU.ipynb")},
    };
    ColabVoiceCloneController controller(nullptr, nullptr, nullptr, nullptr, nullptr);
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
        QVERIFY(source.contains(QStringLiteral("@app.get(\"/v1/capabilities\")")));
        QVERIFY(source.contains(QStringLiteral("\"id\": \"voice-cloning\"")));
        QVERIFY(source.contains(QStringLiteral("@app.post(\"/v2/jobs/profile\"")));
        QVERIFY(source.contains(QStringLiteral("@app.post(\"/v2/jobs/generation\"")));
        QVERIFY(source.contains(QStringLiteral("require_exact_model")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_VOICE_CLONE_URL")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_VOICE_CLONE_TOKEN")));
        QVERIFY(source.contains(QStringLiteral("cloudflared")));
        QVERIFY2(source.contains(QStringLiteral("STARTUP_TIMEOUT_SECONDS = 20 * 60")),
                 "Exact-model notebook must allow a cold CUDA model download to finish.");
        QVERIFY2(source.contains(QStringLiteral("WORKER_LOG")),
                 "Exact-model notebook must retain startup logs when a worker fails.");
        QVERIFY2(source.contains(QStringLiteral("LA Studio worker log")),
                 "Exact-model notebook must return the root worker error instead of a generic timeout.");
        QVERIFY2(source.contains(QStringLiteral("reclaim_previous_la_studio_worker")),
                 "Re-running a Voice Clone notebook must reclaim its own prior Colab worker.");
        QVERIFY2(source.contains(QStringLiteral("WORKER_MODULE")),
                 "Port recovery must identify the exact LA Studio worker module before stopping it.");
        QVERIFY2(source.contains(QStringLiteral("not the previous LA Studio")),
                 "A foreign listener must be rejected instead of being terminated by the notebook.");
        QVERIFY2(!source.contains(QStringLiteral("Port {PORT} is already occupied by an earlier Colab worker.")),
                 "The obsolete instruction to destroy the full Colab runtime must not return.");
        if (model == QStringLiteral("omnivoice")) {
            QVERIFY2(source.contains(QStringLiteral("PORT = 3923")),
                     "The OmniVoice clone notebook must use its documented Voice Clone port.");
            QVERIFY2(source.contains(QStringLiteral("WORKER_MODULE = 'la_studio_voice_clone_worker'")),
                     "The OmniVoice clone notebook must reclaim only its own worker module.");
        }
        QVERIFY2(source.contains(QStringLiteral("str(health.get(\"model\", \"\")).strip().lower() == MODEL_ID")),
                 "Exact-model notebook must validate that the ready worker is the selected model.");
        QVERIFY2(source.contains(QStringLiteral("ref_text: str = Form(default=\"\")")),
                 "Direct Colab voice cloning must allow an automatic reference transcript.");
        if (model == QStringLiteral("omnivoice")) {
            QVERIFY2(source.contains(QStringLiteral("MODEL.create_voice_clone_prompt(**kwargs)")),
                     "OmniVoice must omit ref_text so its documented automatic ASR path can run.");
        } else if (model.startsWith(QStringLiteral("qwen3-tts-"))) {
            QVERIFY2(source.contains(QStringLiteral("x_vector_only_mode=True")),
                     "Qwen must use speaker-only cloning when no transcript is supplied.");
        } else if (model == QStringLiteral("voxcpm2")) {
            QVERIFY2(source.contains(QStringLiteral("VoxCPM can clone from reference audio alone")),
                     "VoxCPM must keep transcript-guided cloning optional.");
        }
        if (model.startsWith(QStringLiteral("vieneu-tts-"))) {
            QVERIFY2(source.contains(QStringLiteral("torchvision==0.23.0")),
                     "VieNeu must replace Colab's potentially incompatible torchvision build.");
            QVERIFY2(source.contains(QStringLiteral("Qwen3ForCausalLM")),
                     "VieNeu must validate the Qwen3 Transformers import before starting its worker.");
            QVERIFY2(source.contains(QStringLiteral("PreTrainedModel")),
                     "VieNeu must validate the v3 Transformers import before starting its worker.");
        }
        QVERIFY(!source.contains(QStringLiteral("API_GATEWAY")));
        QVERIFY(!source.contains(QStringLiteral("GATEWAY_BASE_URL")));
    }
    QVERIFY(controller.notebookForColabModel(QStringLiteral("not-a-model")).isEmpty());

    ColabSession session;
    QString error;
    QVERIFY(session.setSession(QStringLiteral("http://127.0.0.1:3923"),
                               QStringLiteral("temporary-token"), &error, true));
    ColabVoiceCloneController sessionController(&session, nullptr, nullptr, nullptr, nullptr);
    QVERIFY(sessionController.selectColabModel(QStringLiteral("voxcpm2")));
    QVERIFY2(!session.isActive(), "Changing the clone model must discard the previous model worker.");

    QFile output(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
                     .filePath(QStringLiteral("qml/components/voicecloning/VoiceCloningStudioView.qml")));
    QVERIFY(output.open(QIODevice::ReadOnly));
    QVERIFY(output.readAll().contains("progressEstimated: root.colabActive ? true"));
}

} // namespace LAStudio
