#include "test_ColabTranslationRunner.h"

#include "controllers/translation/TranslationController.h"
#include "remote/ColabSession.h"
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

#include <utility>

namespace LAStudio {
namespace {

class TranslationWorkerMock final : public QObject
{
public:
    explicit TranslationWorkerMock(bool holdResponse = false, QByteArray responseBody = {},
                                   bool mirrorRequestSegments = false, bool reviewFirstSegment = false)
        : m_holdResponse(holdResponse), m_responseBody(std::move(responseBody)),
          m_mirrorRequestSegments(mirrorRequestSegments), m_reviewFirstSegment(reviewFirstSegment)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                m_socket = socket;
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { consume(socket); });
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    QByteArray request() const { return m_request; }
    QList<QByteArray> requests() const { return m_requests; }

private:
    void consume(QTcpSocket *socket)
    {
        if (!socket) return;
        m_pending += socket->readAll();
        const int headerEnd = m_pending.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const auto match = QRegularExpression(QStringLiteral("Content-Length: (\\d+)"),
                                              QRegularExpression::CaseInsensitiveOption)
                               .match(QString::fromLatin1(m_pending.left(headerEnd)));
        if (!match.hasMatch()) return;
        const int requestLength = headerEnd + 4 + match.captured(1).toInt();
        if (m_pending.size() < requestLength) return;
        m_request = m_pending.left(requestLength);
        m_requests.append(m_request);
        m_pending.remove(0, requestLength);
        if (m_holdResponse) {
            socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: keep-alive\r\n\r\n"));
            return;
        }
        QByteArray body = m_responseBody.isEmpty()
            ? QByteArrayLiteral("{\"patches\":[{\"id\":\"segment-1\",\"targetText\":\"Xin chao\",\"state\":\"translated\"}]}")
            : m_responseBody;
        if (m_mirrorRequestSegments) {
            const QByteArray payload = m_request.mid(headerEnd + 4);
            const QJsonArray segments = QJsonDocument::fromJson(payload).object()
                                           .value(QStringLiteral("segments")).toArray();
            QJsonArray patches;
            for (const QJsonValue &value : segments) {
                const QJsonObject segment = value.toObject();
                const QString id = segment.value(QStringLiteral("id")).toString();
                if (m_reviewFirstSegment && m_requests.size() == 1) {
                    patches.append(QJsonObject{{QStringLiteral("id"), id},
                                               {QStringLiteral("targetText"), segment.value(QStringLiteral("sourceText")).toString()},
                                               {QStringLiteral("state"), QStringLiteral("needs-review")},
                                               {QStringLiteral("translationDiagnostic"), QStringLiteral("model output was blank after retry")}});
                } else {
                    patches.append(QJsonObject{{QStringLiteral("id"), id},
                                               {QStringLiteral("targetText"), QStringLiteral("Translated %1").arg(id)},
                                               {QStringLiteral("state"), QStringLiteral("translated")}});
                }
            }
            body = QJsonDocument(QJsonObject{{QStringLiteral("patches"), patches}})
                       .toJson(QJsonDocument::Compact);
        }
        socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
                        + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QPointer<QTcpSocket> m_socket;
    QByteArray m_pending;
    QByteArray m_request;
    QList<QByteArray> m_requests;
    bool m_holdResponse = false;
    QByteArray m_responseBody;
    bool m_mirrorRequestSegments = false;
    bool m_reviewFirstSegment = false;
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

void TestColabTranslationRunner::progressReflectsCompletedSegments()
{
    TranslationWorkerMock worker(false, {}, true);
    QVERIFY(worker.start());
    qRegisterMetaType<TranslationInferenceRequest>("TranslationInferenceRequest");
    QThread thread;
    auto *runner = new ColabTranslationRunner;
    runner->moveToThread(&thread);
    connect(&thread, &QThread::finished, runner, &QObject::deleteLater);
    thread.start();
    QSignalSpy finished(runner, &ColabTranslationRunner::finished);
    QSignalSpy progress(runner, &ColabTranslationRunner::progress);
    QSignalSpy failures(runner, &ColabTranslationRunner::failed);

    TranslationInferenceRequest request;
    request.segments = {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-1")}, {QStringLiteral("sourceText"), QStringLiteral("One")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-2")}, {QStringLiteral("sourceText"), QStringLiteral("Two")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-3")}, {QStringLiteral("sourceText"), QStringLiteral("Three")}},
    };
    request.sourceLanguage = QStringLiteral("en");
    request.targetLanguage = QStringLiteral("vi");
    QVERIFY(QMetaObject::invokeMethod(runner, "translate", Qt::QueuedConnection,
                                      Q_ARG(QUrl, QUrl(worker.baseUrl())),
                                      Q_ARG(QString, QStringLiteral("progress-token")),
                                      Q_ARG(QString, QStringLiteral("m2m100-418m")),
                                      Q_ARG(TranslationInferenceRequest, request), Q_ARG(bool, true)));

    QVERIFY2(finished.wait(5000), "Segmented Colab translation did not finish.");
    QCOMPARE(failures.count(), 0);
    QCOMPARE(finished.takeFirst().at(0).toList().size(), 3);
    const QList<int> expectedProgress{0, 33, 66, 100};
    QList<int> reportedProgress;
    for (const QList<QVariant> &signal : progress)
        reportedProgress.append(signal.at(0).toInt());
    QCOMPARE(reportedProgress, expectedProgress);
    const QList<QByteArray> sent = worker.requests();
    QCOMPARE(sent.size(), 3);
    for (int index = 0; index < sent.size(); ++index) {
        const QByteArray expectedId = QByteArrayLiteral("\"id\":\"segment-")
            + QByteArray::number(index + 1) + QByteArrayLiteral("\"");
        QVERIFY(sent.at(index).contains(expectedId));
        QVERIFY(!sent.at(index).contains(QByteArrayLiteral("\"id\":\"segment-")
                                         + QByteArray::number((index + 1) % 3 + 1) + QByteArrayLiteral("\"")));
    }
    thread.quit();
    QVERIFY(thread.wait(5000));
}

void TestColabTranslationRunner::invalidPatchReportsItsBrokenField()
{
    TranslationWorkerMock worker(
        false, QByteArrayLiteral("{\"patches\":[{\"id\":\"segment-1\",\"targetText\":\"\"}]}"));
    QVERIFY(worker.start());
    qRegisterMetaType<TranslationInferenceRequest>("TranslationInferenceRequest");
    QThread thread;
    auto *runner = new ColabTranslationRunner;
    runner->moveToThread(&thread);
    connect(&thread, &QThread::finished, runner, &QObject::deleteLater);
    thread.start();
    QSignalSpy failures(runner, &ColabTranslationRunner::failed);

    TranslationInferenceRequest request;
    request.segments = {QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-1")},
                                    {QStringLiteral("sourceText"), QStringLiteral("Hello")}}};
    request.sourceLanguage = QStringLiteral("en");
    request.targetLanguage = QStringLiteral("vi");
    QVERIFY(QMetaObject::invokeMethod(runner, "translate", Qt::QueuedConnection,
                                      Q_ARG(QUrl, QUrl(worker.baseUrl())),
                                      Q_ARG(QString, QStringLiteral("translation-patch-token")),
                                      Q_ARG(QString, QStringLiteral("m2m100-418m")),
                                      Q_ARG(TranslationInferenceRequest, request), Q_ARG(bool, true)));

    QVERIFY2(failures.wait(5000), "Invalid Colab translation patch was not rejected.");
    const QString message = failures.takeFirst().at(0).toString();
    QVERIFY(message.contains(QStringLiteral("patch 1/1")));
    QVERIFY(message.contains(QStringLiteral("targetText missing")));
    thread.quit();
    QVERIFY(thread.wait(5000));
}

void TestColabTranslationRunner::needsReviewPatchContinuesTranslation()
{
    TranslationWorkerMock worker(false, {}, true, true);
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
    request.segments = {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-1")},
                    {QStringLiteral("sourceText"), QStringLiteral("Original source")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("segment-2")},
                    {QStringLiteral("sourceText"), QStringLiteral("Later source")}},
    };
    request.sourceLanguage = QStringLiteral("en");
    request.targetLanguage = QStringLiteral("vi");
    QVERIFY(QMetaObject::invokeMethod(runner, "translate", Qt::QueuedConnection,
                                      Q_ARG(QUrl, QUrl(worker.baseUrl())),
                                      Q_ARG(QString, QStringLiteral("translation-patch-token")),
                                      Q_ARG(QString, QStringLiteral("m2m100-418m")),
                                      Q_ARG(TranslationInferenceRequest, request), Q_ARG(bool, true)));

    QVERIFY2(finished.wait(5000), "A review-required patch should not stop the translation job.");
    QCOMPARE(failures.count(), 0);
    const QVariantList patches = finished.takeFirst().at(0).toList();
    QCOMPARE(patches.size(), 2);
    const QVariantMap reviewPatch = patches.first().toMap();
    QCOMPARE(reviewPatch.value(QStringLiteral("state")).toString(), QStringLiteral("needs-review"));
    QCOMPARE(reviewPatch.value(QStringLiteral("targetText")).toString(), QStringLiteral("Original source"));
    QVERIFY(!reviewPatch.value(QStringLiteral("translationDiagnostic")).toString().isEmpty());
    QCOMPARE(patches.at(1).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("translated"));
    QCOMPARE(worker.requests().size(), 2);
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
    const QList<QPair<QString, QString>> expected{
        {QStringLiteral("m2m100-418m"), QStringLiteral("LA_STUDIO_TRANSLATION_M2M100_418M_GPU.ipynb")},
        {QStringLiteral("madlad400-3b-mt"), QStringLiteral("LA_STUDIO_TRANSLATION_MADLAD400_3B_GPU.ipynb")},
        {QStringLiteral("hy-mt2-1.8b"), QStringLiteral("LA_STUDIO_TRANSLATION_HY_MT2_1_8B_GPU.ipynb")},
    };
    ColabSession session;
    TranslationController controller(nullptr, nullptr, nullptr, &session);
    for (const auto &[model, notebook] : expected) {
        QCOMPARE(controller.notebookForColabModel(model), notebook);
        QVERIFY(controller.selectColabModel(model));
        QCOMPARE(controller.colabModel(), model);
        QCOMPARE(controller.colabNotebookFile(), notebook);

        const QString path = QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
            .filePath(QStringLiteral("notebooks/") + notebook);
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        QVERIFY(document.isObject());
        QCOMPARE(document.object().value(QStringLiteral("nbformat")).toInt(), 4);
        QString source;
        for (const QJsonValue &cellValue : document.object().value(QStringLiteral("cells")).toArray()) {
            for (const QJsonValue &line : cellValue.toObject().value(QStringLiteral("source")).toArray())
                source += line.toString();
        }
        QVERIFY(source.contains(model));
        QVERIFY(source.contains(QStringLiteral("CPU fallback is disabled")));
        QVERIFY(source.contains(QStringLiteral("require_exact_model(request.model)")));
        QVERIFY(source.contains(QStringLiteral("@app.post(\"/v1/translations\")")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_TRANSLATION_URL")));
        QVERIFY(source.contains(QStringLiteral("LA_STUDIO_COLAB_TRANSLATION_TOKEN")));
        QVERIFY(source.contains(QStringLiteral("cloudflared")));
        QVERIFY(source.contains(QStringLiteral("translation-patches-v3")));
        QVERIFY(source.contains(QStringLiteral("translation-2026-07-30.3")));
        QVERIFY(source.contains(QStringLiteral("make_translation_patches")));
        QVERIFY(source.contains(QStringLiteral("retry_empty_translations")));
        QVERIFY(source.contains(QStringLiteral("later segments continued")));
        QVERIFY(source.contains(QStringLiteral("NONLEXICAL_UTTERANCES")));
        if (model == QStringLiteral("m2m100-418m")) {
            QVERIFY(source.contains(QStringLiteral("num_beams=4")));
            QVERIFY(source.contains(QStringLiteral("min_new_tokens=1")));
        }
        QVERIFY(!source.contains(QStringLiteral("API_GATEWAY")));
    }
    QVERIFY(controller.notebookForColabModel(QStringLiteral("unknown-translation")).isEmpty());
    QVERIFY(!controller.selectColabModel(QStringLiteral("unknown-translation")));

    QString error;
    QVERIFY(session.setSession(QStringLiteral("https://translation-worker.example.test"),
                               QStringLiteral("translation-token"), &error));
    controller.useColab();
    QVERIFY(controller.colabActive());
    QVERIFY(controller.selectColabModel(QStringLiteral("m2m100-418m")));
    QVERIFY(!session.isActive());

    QFile page(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR)).filePath(
        QStringLiteral("qml/pages/TranslationPage.qml")));
    QVERIFY(page.open(QIODevice::ReadOnly));
    const QByteArray pageSource = page.readAll();
    QVERIFY(pageSource.contains("colabModelSelectionEnabled: true"));
    QVERIFY(pageSource.contains("AppController.translation.selectColabModel(familyId)"));
}

} // namespace LAStudio
