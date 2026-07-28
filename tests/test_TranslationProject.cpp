#include "test_TranslationProject.h"

#include "translation/TranslationProject.h"
#include "translation/GatewayTranslationRunner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

namespace LAStudio {
namespace {

class TranslationGatewayMock final : public QObject
{
public:
    TranslationGatewayMock()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                m_socket = socket;
                connect(socket, &QTcpSocket::readyRead, this, [this] { consumeRequest(); });
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(m_server.serverPort()); }
    QByteArray request() const { return m_request; }
    void setResponse(QByteArray response) { m_response = response; }

private:
    void consumeRequest()
    {
        if (!m_socket) return;
        m_pending += m_socket->readAll();
        const int headerEnd = m_pending.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const QRegularExpression expression(QStringLiteral("Content-Length: (\\d+)"),
                                             QRegularExpression::CaseInsensitiveOption);
        const auto match = expression.match(QString::fromLatin1(m_pending.left(headerEnd)));
        if (!match.hasMatch()) return;
        const int requestLength = headerEnd + 4 + match.captured(1).toInt();
        if (m_pending.size() < requestLength) return;
        m_request = m_pending.left(requestLength);
        m_socket->write(QByteArrayLiteral(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Connection: close\r\n\r\n") + m_response);
        m_socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QPointer<QTcpSocket> m_socket;
    QByteArray m_pending;
    QByteArray m_request;
    QByteArray m_response = QByteArrayLiteral(
        "data: {\"choices\":[{\"delta\":{\"content\":\"{\\\"patches\\\":[{\\\"id\\\":\\\"segment-1\\\",\\\"targetText\\\":\\\"Xin chao\\\"},{\\\"id\\\":\\\"segment-2\\\",\\\"targetText\\\":\\\"Tam biet\\\"}]}\"}}]}\n\n"
        "data: [DONE]\n\n");
};

} // namespace

void TestTranslationProject::textImportSplitsParagraphsAndRoundTrips()
{
    TranslationProject project;
    QString error;
    QVERIFY2(TranslationProject::importText(QStringLiteral("First paragraph.\n\nSecond paragraph."), project, &error), qPrintable(error));
    QCOMPARE(project.segments.size(), 2);
    QCOMPARE(project.segments.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("segment-1"));
    QTemporaryDir dir;
    project.projectPath = dir.filePath(QStringLiteral("sample.lastudio-translation.json"));
    project.segments[0] = QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-1")}, {QStringLiteral("sourceText"), QStringLiteral("First paragraph.")}, {QStringLiteral("targetText"), QStringLiteral("Doan mot.")}};
    QVERIFY2(project.save(&error), qPrintable(error));
    TranslationProject loaded;
    QVERIFY2(TranslationProject::load(project.projectPath, loaded, &error), qPrintable(error));
    QCOMPARE(loaded.segments.size(), 2);
    QCOMPARE(loaded.segments.first().toMap().value(QStringLiteral("targetText")).toString(), QStringLiteral("Doan mot."));
}

void TestTranslationProject::subtitleImportPreservesTimingAndExportsTargetText()
{
    TranslationProject project;
    QString error;
    const QString srt = QStringLiteral("1\n00:00:01,000 --> 00:00:02,500\nHello\nworld\n\n2\n00:00:03,000 --> 00:00:04,000\nGoodbye");
    QVERIFY2(TranslationProject::importSubtitle(srt, QStringLiteral("srt"), project, &error), qPrintable(error));
    QCOMPARE(project.segments.size(), 2);
    QCOMPARE(project.segments.first().toMap().value(QStringLiteral("startMs")).toLongLong(), qint64(1000));
    QVariantMap first = project.segments.first().toMap(); first.insert(QStringLiteral("targetText"), QStringLiteral("Xin chao")); project.segments[0] = first;
    QVariantMap second = project.segments.last().toMap(); second.insert(QStringLiteral("targetText"), QStringLiteral("Tam biet")); project.segments[1] = second;
    const QString output = project.exportSubtitle(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(output.contains(QStringLiteral("00:00:01,000 --> 00:00:02,500")));
    QVERIFY(output.contains(QStringLiteral("Xin chao")));
}

void TestTranslationProject::rejectsInvalidSubtitleCue()
{
    TranslationProject project;
    QString error;
    QVERIFY(!TranslationProject::importSubtitle(QStringLiteral("1\ninvalid --> timestamp\nText"), QStringLiteral("srt"), project, &error));
    QVERIFY(!error.isEmpty());
}

void TestTranslationProject::gatewayRunnerTranslatesSegmentsThroughGateway()
{
    TranslationGatewayMock gateway;
    QVERIFY(gateway.start());
    qRegisterMetaType<TranslationInferenceRequest>("TranslationInferenceRequest");
    QThread workerThread;
    auto *runner = new GatewayTranslationRunner;
    runner->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, runner, &QObject::deleteLater);
    workerThread.start();
    QSignalSpy finished(runner, &GatewayTranslationRunner::finished);
    QSignalSpy failures(runner, &GatewayTranslationRunner::failed);

    TranslationInferenceRequest request;
    request.segments = {QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-1")},
                                    {QStringLiteral("sourceText"), QStringLiteral("Hello")}},
                        QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-2")},
                                    {QStringLiteral("sourceText"), QStringLiteral("Goodbye")}}};
    request.sourceLanguage = QStringLiteral("en");
    request.targetLanguage = QStringLiteral("vi");
    request.maxTokens = 256;
    QVERIFY(QMetaObject::invokeMethod(runner, "translate", Qt::QueuedConnection,
                                      Q_ARG(QString, gateway.baseUrl()),
                                      Q_ARG(QString, QStringLiteral("translation-test-key")),
                                      Q_ARG(QString, QStringLiteral("translation-model")),
                                      Q_ARG(TranslationInferenceRequest, request), Q_ARG(bool, true)));

    const bool didFinish = finished.wait(5000);
    workerThread.quit();
    const bool didStop = workerThread.wait(5000);
    QVERIFY2(didFinish, "Gateway translation worker did not finish.");
    QVERIFY2(didStop, "Gateway translation worker did not stop.");
    QCOMPARE(failures.count(), 0);
    QCOMPARE(finished.count(), 1);
    const QVariantList patches = finished.takeFirst().at(0).toList();
    QCOMPARE(patches.size(), 2);
    QCOMPARE(patches.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("segment-1"));
    QCOMPARE(patches.first().toMap().value(QStringLiteral("targetText")).toString(), QStringLiteral("Xin chao"));
    QCOMPARE(patches.last().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("segment-2"));
    QCOMPARE(patches.last().toMap().value(QStringLiteral("targetText")).toString(), QStringLiteral("Tam biet"));
    const QByteArray requestBytes = gateway.request();
    QVERIFY(requestBytes.startsWith("POST /v1/chat/completions HTTP/1.1\r\n"));
    QVERIFY(requestBytes.toLower().contains("authorization: bearer translation-test-key"));
    QVERIFY(requestBytes.contains("Hello"));
    QVERIFY(requestBytes.contains("Goodbye"));
    const int headerEnd = requestBytes.indexOf("\r\n\r\n");
    QVERIFY(headerEnd >= 0);
    const QJsonDocument wireRequest = QJsonDocument::fromJson(requestBytes.mid(headerEnd + 4));
    QVERIFY(wireRequest.isObject());
    const QJsonArray messages = wireRequest.object().value(QStringLiteral("messages")).toArray();
    QCOMPARE(messages.size(), 2);
    const QString systemPrompt = messages.first().toObject().value(QStringLiteral("content")).toString();
    QVERIFY(systemPrompt.contains(QStringLiteral("\"patches\"")));
    QVERIFY(systemPrompt.contains(QStringLiteral("strict JSON")));
}

void TestTranslationProject::gatewayRunnerRejectsInvalidPatchSchema()
{
    TranslationGatewayMock gateway;
    gateway.setResponse(QByteArrayLiteral(
        "data: {\"choices\":[{\"delta\":{\"content\":\"{\\\"patches\\\":[{\\\"id\\\":\\\"segment-1\\\",\\\"targetText\\\":\\\"One\\\"},{\\\"id\\\":\\\"segment-1\\\",\\\"targetText\\\":\\\"Duplicate\\\"}]}\"}}]}\n\n"
        "data: [DONE]\n\n"));
    QVERIFY(gateway.start());
    qRegisterMetaType<TranslationInferenceRequest>("TranslationInferenceRequest");
    QThread workerThread;
    auto *runner = new GatewayTranslationRunner;
    runner->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, runner, &QObject::deleteLater);
    workerThread.start();
    QSignalSpy finished(runner, &GatewayTranslationRunner::finished);
    QSignalSpy failures(runner, &GatewayTranslationRunner::failed);

    TranslationInferenceRequest request;
    request.segments = {QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-1")},
                                    {QStringLiteral("sourceText"), QStringLiteral("One")}},
                        QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-2")},
                                    {QStringLiteral("sourceText"), QStringLiteral("Two")}}};
    request.sourceLanguage = QStringLiteral("en");
    request.targetLanguage = QStringLiteral("vi");
    QVERIFY(QMetaObject::invokeMethod(runner, "translate", Qt::QueuedConnection,
                                      Q_ARG(QString, gateway.baseUrl()),
                                      Q_ARG(QString, QStringLiteral("translation-test-key")),
                                      Q_ARG(QString, QStringLiteral("translation-model")),
                                      Q_ARG(TranslationInferenceRequest, request), Q_ARG(bool, true)));

    const bool didFail = failures.wait(5000);
    workerThread.quit();
    const bool didStop = workerThread.wait(5000);
    QVERIFY2(didFail, "Gateway translation worker did not reject malformed patches.");
    QVERIFY2(didStop, "Gateway translation worker did not stop.");
    QCOMPARE(finished.count(), 0);
    QCOMPARE(failures.count(), 1);
    QVERIFY(failures.takeFirst().at(0).toString().contains(QStringLiteral("duplicate"), Qt::CaseInsensitive));
}

} // namespace LAStudio
