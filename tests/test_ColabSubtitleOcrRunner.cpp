#include "test_ColabSubtitleOcrRunner.h"

#include "subtitles/ColabSubtitleOcrRunner.h"

#include <QPointer>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QtTest>

namespace LAStudio {
namespace {

class SubtitleOcrWorkerMock final : public QObject
{
public:
    explicit SubtitleOcrWorkerMock(QList<QByteArray> responses = {}, bool holdResponse = false)
        : m_responses(std::move(responses)), m_holdResponse(holdResponse)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { consume(socket); });
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QUrl endpoint() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort())); }
    QList<QByteArray> requests() const { return m_requests; }

private:
    void consume(QTcpSocket *socket)
    {
        if (!socket) return;
        m_pending += socket->readAll();
        const int headerEnd = m_pending.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const QRegularExpression lengthPattern(QStringLiteral("Content-Length: (\\d+)"),
                                               QRegularExpression::CaseInsensitiveOption);
        const auto match = lengthPattern.match(QString::fromLatin1(m_pending.left(headerEnd)));
        if (!match.hasMatch()) return;
        const int requestLength = headerEnd + 4 + match.captured(1).toInt();
        if (m_pending.size() < requestLength) return;
        m_requests.append(m_pending.left(requestLength));
        m_pending.remove(0, requestLength);
        if (m_holdResponse) return;
        const QByteArray response = m_responses.isEmpty()
            ? QByteArrayLiteral("{\"text\":\"Xin chao\",\"confidence\":0.93}")
            : m_responses.takeFirst();
        const bool isError = response.startsWith("HTTP/");
        if (isError) {
            socket->write(response);
        } else {
            socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
                          + QByteArray::number(response.size())
                          + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + response);
        }
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QByteArray m_pending;
    QList<QByteArray> m_requests;
    QList<QByteArray> m_responses;
    bool m_holdResponse = false;
};

class RunnerThread final
{
public:
    RunnerThread()
    {
        runner = new ColabSubtitleOcrRunner;
        runner->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, runner, &QObject::deleteLater);
        thread.start();
    }
    ~RunnerThread()
    {
        if (runner && thread.isRunning()) QMetaObject::invokeMethod(runner, "cancel", Qt::QueuedConnection);
        thread.quit();
        thread.wait(5000);
    }

    QThread thread;
    ColabSubtitleOcrRunner *runner = nullptr;
};

bool invokeRecognition(ColabSubtitleOcrRunner *runner, const QUrl &endpoint)
{
    const QByteArray png("\x89PNG\r\n\x1a\nminimal-crop", 20);
    return QMetaObject::invokeMethod(runner, "recognize", Qt::QueuedConnection,
                                     Q_ARG(QUrl, endpoint), Q_ARG(QString, QStringLiteral("ocr-token")),
                                     Q_ARG(QString, QStringLiteral("pp-ocrv5-multilingual-3.1")),
                                     Q_ARG(QString, QStringLiteral("vie")), Q_ARG(QByteArray, png),
                                     Q_ARG(bool, true));
}

} // namespace

void TestColabSubtitleOcrRunner::postsOnlyCroppedPngToExactDirectWorker()
{
    SubtitleOcrWorkerMock worker;
    QVERIFY(worker.start());
    RunnerThread runner;
    QSignalSpy finished(runner.runner, &ColabSubtitleOcrRunner::finished);
    QSignalSpy failures(runner.runner, &ColabSubtitleOcrRunner::failed);
    QVERIFY(invokeRecognition(runner.runner, worker.endpoint()));
    QVERIFY2(finished.wait(5000), "Subtitle OCR worker did not complete.");
    QCOMPARE(failures.count(), 0);
    const QList<QVariant> result = finished.takeFirst();
    QCOMPARE(result.at(0).toString(), QStringLiteral("Xin chao"));
    QCOMPARE(result.at(1).toDouble(), 0.93);
    QCOMPARE(worker.requests().size(), 1);
    const QByteArray request = worker.requests().first();
    QVERIFY(request.startsWith("POST /v1/ocr/subtitles HTTP/1.1\r\n"));
    QVERIFY(request.toLower().contains("authorization: bearer ocr-token"));
    QVERIFY(request.contains("pp-ocrv5-multilingual-3.1"));
    QVERIFY(request.contains("name=\"language\""));
    QVERIFY(request.contains("vie"));
    QVERIFY(request.contains("filename=\"subtitle-roi.png\""));
    QVERIFY(request.contains("\x89PNG\r\n\x1a\nminimal-crop"));
    QVERIFY(!request.contains("gateway"));
    QVERIFY(!request.contains(".mp4"));
}

void TestColabSubtitleOcrRunner::rejectsMalformedOrFailedWorkerResponses()
{
    const QByteArray errorResponse = QByteArrayLiteral(
        "HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\nContent-Length: 25\r\nConnection: close\r\n\r\n{\"detail\":\"model failed\"}");
    SubtitleOcrWorkerMock worker({errorResponse, QByteArrayLiteral("{\"text\":\"missing confidence\"}")});
    QVERIFY(worker.start());
    RunnerThread runner;
    QSignalSpy failures(runner.runner, &ColabSubtitleOcrRunner::failed);
    QVERIFY(invokeRecognition(runner.runner, worker.endpoint()));
    QVERIFY2(failures.wait(5000), "HTTP failure was not reported.");
    QVERIFY(failures.takeFirst().at(0).toString().contains(QStringLiteral("HTTP 503")));
    QVERIFY(invokeRecognition(runner.runner, worker.endpoint()));
    QVERIFY2(failures.wait(5000), "Malformed response was not reported.");
    QVERIFY(failures.takeFirst().at(0).toString().contains(QStringLiteral("text and confidence")));
}

void TestColabSubtitleOcrRunner::cancellationAbortsTheInFlightCropRequest()
{
    SubtitleOcrWorkerMock worker({}, true);
    QVERIFY(worker.start());
    RunnerThread runner;
    QSignalSpy failures(runner.runner, &ColabSubtitleOcrRunner::failed);
    QVERIFY(invokeRecognition(runner.runner, worker.endpoint()));
    QTRY_VERIFY_WITH_TIMEOUT(!worker.requests().isEmpty(), 3000);
    QVERIFY(QMetaObject::invokeMethod(runner.runner, "cancel", Qt::QueuedConnection));
    QVERIFY2(failures.wait(5000), "Cancelling a subtitle crop request did not complete.");
    QVERIFY(!failures.takeFirst().at(0).toString().isEmpty());
}

void TestColabSubtitleOcrRunner::retryUsesTheSameExactWorkerContract()
{
    SubtitleOcrWorkerMock worker({
        QByteArrayLiteral("{\"text\":\"\",\"confidence\":0.0}"),
        QByteArrayLiteral("{\"text\":\"Second frame\",\"confidence\":0.8}"),
    });
    QVERIFY(worker.start());
    RunnerThread runner;
    QSignalSpy finished(runner.runner, &ColabSubtitleOcrRunner::finished);
    QSignalSpy failures(runner.runner, &ColabSubtitleOcrRunner::failed);
    QVERIFY(invokeRecognition(runner.runner, worker.endpoint()));
    QVERIFY2(finished.wait(5000), "A no-subtitle crop is a valid OCR result.");
    QCOMPARE(finished.takeFirst().at(0).toString(), QString());
    QVERIFY(invokeRecognition(runner.runner, worker.endpoint()));
    QVERIFY2(finished.wait(5000), "Retry did not reuse the exact worker contract.");
    QCOMPARE(finished.takeFirst().at(0).toString(), QStringLiteral("Second frame"));
    QCOMPARE(failures.count(), 0);
    QCOMPARE(worker.requests().size(), 2);
    for (const QByteArray &request : worker.requests())
        QVERIFY(request.contains("pp-ocrv5-multilingual-3.1"));
}

} // namespace LAStudio
