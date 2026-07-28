#include "test_ColabTranslationRunner.h"

#include "translation/ColabTranslationRunner.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QtTest>

namespace LAStudio {
namespace {

class TranslationWorkerMock final : public QObject
{
public:
    explicit TranslationWorkerMock(bool holdResponse = false)
        : m_holdResponse(holdResponse)
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
        if (m_holdResponse) {
            m_socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: keep-alive\r\n\r\n"));
            return;
        }
        const QByteArray body = QByteArrayLiteral("{\"patches\":[{\"id\":\"segment-1\",\"targetText\":\"Xin chao\",\"state\":\"translated\"}]}");
        m_socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
                        + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
        m_socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QPointer<QTcpSocket> m_socket;
    QByteArray m_pending;
    QByteArray m_request;
    bool m_holdResponse = false;
};

} // namespace

void TestColabTranslationRunner::postsBatchToDirectWorkerOnly()
{
    TranslationWorkerMock worker;
    QVERIFY(worker.start());
    qRegisterMetaType<TranslationInferenceRequest>("TranslationInferenceRequest");
    QThread thread;
    auto *runner = new ColabTranslationRunner;
    runner->moveToThread(&thread);
    connect(&thread, &QThread::finished, runner, &QObject::deleteLater);
    thread.start();
    QSignalSpy finished(runner, &ColabTranslationRunner::finished);
    QSignalSpy failures(runner, &ColabTranslationRunner::failed);

    TranslationInferenceRequest request;
    request.segments = {QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-1")},
                                    {QStringLiteral("sourceText"), QStringLiteral("Hello")}}};
    request.sourceLanguage = QStringLiteral("en");
    request.targetLanguage = QStringLiteral("vi");
    QVERIFY(QMetaObject::invokeMethod(runner, "translate", Qt::QueuedConnection,
                                      Q_ARG(QUrl, QUrl(worker.baseUrl())),
                                      Q_ARG(QString, QStringLiteral("colab-translation-token")),
                                      Q_ARG(QString, QStringLiteral("m2m100-418m")),
                                      Q_ARG(TranslationInferenceRequest, request), Q_ARG(bool, true)));

    QVERIFY2(finished.wait(5000), "Colab translation worker did not finish.");
    QCOMPARE(failures.count(), 0);
    const QVariantList patches = finished.takeFirst().at(0).toList();
    QCOMPARE(patches.size(), 1);
    QCOMPARE(patches.first().toMap().value(QStringLiteral("targetText")).toString(), QStringLiteral("Xin chao"));
    const QByteArray sent = worker.request();
    QVERIFY(sent.startsWith("POST /v1/translations HTTP/1.1\r\n"));
    QVERIFY(sent.toLower().contains("authorization: bearer colab-translation-token"));
    QVERIFY(sent.contains("\"model\":\"m2m100-418m\""));
    QVERIFY(sent.contains("\"source_language\":\"en\""));
    QVERIFY(sent.contains("\"target_language\":\"vi\""));
    QVERIFY(sent.contains("\"sourceText\":\"Hello\""));
    QVERIFY(!sent.contains("gateway"));
    thread.quit();
    QVERIFY(thread.wait(5000));
}

void TestColabTranslationRunner::cancellationAbortsDirectWorkerRequest()
{
    TranslationWorkerMock worker(true);
    QVERIFY(worker.start());
    qRegisterMetaType<TranslationInferenceRequest>("TranslationInferenceRequest");
    QThread thread;
    auto *runner = new ColabTranslationRunner;
    runner->moveToThread(&thread);
    connect(&thread, &QThread::finished, runner, &QObject::deleteLater);
    thread.start();
    QSignalSpy failures(runner, &ColabTranslationRunner::failed);

    auto cancellation = std::make_shared<std::atomic_bool>(false);
    TranslationInferenceRequest request;
    request.segments = {QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-cancel")},
                                    {QStringLiteral("sourceText"), QStringLiteral("Wait")}}};
    request.sourceLanguage = QStringLiteral("en");
    request.targetLanguage = QStringLiteral("vi");
    request.cancellation = InferenceCancellationToken(cancellation);
    QVERIFY(QMetaObject::invokeMethod(runner, "translate", Qt::QueuedConnection,
                                      Q_ARG(QUrl, QUrl(worker.baseUrl())),
                                      Q_ARG(QString, QStringLiteral("translation-cancel-token")),
                                      Q_ARG(QString, QStringLiteral("m2m100-418m")),
                                      Q_ARG(TranslationInferenceRequest, request), Q_ARG(bool, true)));
    QTRY_VERIFY_WITH_TIMEOUT(!worker.request().isEmpty(), 3000);
    cancellation->store(true, std::memory_order_relaxed);
    QVERIFY(QMetaObject::invokeMethod(runner, "cancel", Qt::QueuedConnection));
    QVERIFY2(failures.wait(5000), "Colab translation cancellation did not complete.");
    QCOMPARE(failures.takeFirst().at(0).toString(), QStringLiteral("Colab translation cancelled"));
    thread.quit();
    QVERIFY(thread.wait(5000));
}

void TestColabTranslationRunner::languageNotebookMatchesDirectTranslationContract()
{
    const QString path = QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
        .filePath(QStringLiteral("notebooks/LA_STUDIO_LANGUAGE_GPU.ipynb"));
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();
    QCOMPARE(root.value(QStringLiteral("nbformat")).toInt(), 4);

    QString source;
    const QJsonArray cells = root.value(QStringLiteral("cells")).toArray();
    QVERIFY(cells.size() >= 4);
    for (const QJsonValue &cellValue : cells) {
        const QJsonArray lines = cellValue.toObject().value(QStringLiteral("source")).toArray();
        for (const QJsonValue &line : lines) source += line.toString();
    }
    QVERIFY(source.contains(QStringLiteral("M2M100ForConditionalGeneration")));
    QVERIFY(source.contains(QStringLiteral("CPU fallback is disabled")));
    QVERIFY(source.contains(QStringLiteral("@app.post('/v1/translations')")));
    QVERIFY(source.contains(QStringLiteral("'translation'")));
    QVERIFY(source.contains(QStringLiteral("'device': 'cuda'")));
    QVERIFY(source.contains(QStringLiteral("LA_STUDIO_LANGUAGE_URL")));
    QVERIFY(source.contains(QStringLiteral("LA_STUDIO_LANGUAGE_TOKEN")));
    QVERIFY(source.contains(QStringLiteral("cloudflared")));
    QVERIFY(!source.contains(QStringLiteral("API_GATEWAY")));
}

} // namespace LAStudio
