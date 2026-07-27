#include "test_History.h"
#include <QtTest>
#include <QSignalSpy>
#include <QThreadPool>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>

#include "controllers/shared/HistoryRepository.h"
#include "controllers/shared/HistoryService.h"
#include "tts/TtsEngine.h"
#include "audio/AudioRecorder.h"

namespace LAStudio {

void TestHistory::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
}

void TestHistory::cleanupTestCase()
{
    QThreadPool::globalInstance()->waitForDone();
}

void TestHistory::testHistoryRepository()
{
    qDebug() << "--- START: testHistoryRepository ---";
    QString dataDir = m_tempDir.path();

    // TTS Save
    QVector<float> samples = {0.5f, -0.5f, 0.2f, -0.2f};
    int sampleRate = 16000;
    QString errorMsg;
    bool ok = HistoryRepository::addTtsHistoryItem(dataDir, QStringLiteral("hello"), QStringLiteral("model"), QStringLiteral("voice"), samples, sampleRate, errorMsg);
    QVERIFY(ok);

    QVariantList list = HistoryRepository::loadTtsHistory(dataDir);
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().toMap().value(QStringLiteral("text")).toString(), QStringLiteral("hello"));

    // Cleanup
    ok = HistoryRepository::clearTtsHistory(dataDir, errorMsg);
    QVERIFY(ok);
    list = HistoryRepository::loadTtsHistory(dataDir);
    QCOMPARE(list.size(), 0);
}

void TestHistory::testHistoryEnvelopeAndLegacyCompatibility()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString historyDir = directory.filePath(QStringLiteral("history"));
    QVERIFY(QDir().mkpath(historyDir));
    const QString historyPath = historyDir + QStringLiteral("/tts_history.json");

    QJsonArray legacyEntries;
    for (int index = 0; index < 100; ++index) {
        legacyEntries.append(QJsonObject{
            {QStringLiteral("id"), QString::number(index)},
            {QStringLiteral("text"), QStringLiteral("legacy")}
        });
    }
    QFile legacyFile(historyPath);
    QVERIFY(legacyFile.open(QIODevice::WriteOnly));
    legacyFile.write(QJsonDocument(legacyEntries).toJson());
    legacyFile.close();

    QCOMPARE(HistoryRepository::loadTtsHistory(directory.path()).size(), 100);

    QString error;
    QVERIFY(HistoryRepository::addTtsHistoryItem(directory.path(), QStringLiteral("new"),
                                                  QStringLiteral("model"), QStringLiteral("voice"),
                                                  {0.1f, -0.1f}, 16000, error));

    QFile versionedFile(historyPath);
    QVERIFY(versionedFile.open(QIODevice::ReadOnly));
    const QJsonDocument versioned = QJsonDocument::fromJson(versionedFile.readAll());
    QVERIFY(versioned.isObject());
    QCOMPARE(versioned.object().value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(versioned.object().value(QStringLiteral("entries")).toArray().size(), 100);
    QCOMPARE(HistoryRepository::loadTtsHistory(directory.path()).first().toMap()
                 .value(QStringLiteral("text")).toString(), QStringLiteral("new"));
}

void TestHistory::testHistoryService()
{
    qDebug() << "--- START: testHistoryService ---";
    TtsEngine tts;
    AudioRecorder recorder;

    HistoryService service(&tts, &recorder);

    // Test asynchronous initial load
    QSignalSpy spyTts(&service, &HistoryService::ttsHistoryChanged);
    QSignalSpy spyStt(&service, &HistoryService::sttHistoryChanged);

    // Wait for initial load to finish to avoid race condition
    if (service.ttsHistory().isEmpty() && spyTts.isEmpty()) {
        spyTts.wait(1000);
    }
    spyTts.clear();

    // Add item (async execution trigger)
    service.addTtsHistoryItem(QStringLiteral("async test"), QStringLiteral("model"), QStringLiteral("voice"));
    
    // Wait for the async task queue to complete
    QVERIFY(spyTts.wait(2000));
    QVERIFY(service.ttsHistory().size() > 0);
}

void TestHistory::testSttHistory()
{
    qDebug() << "--- START: testSttHistory ---";
    QString dataDir = m_tempDir.path();
    QVector<float> samples = {0.1f, -0.2f, 0.3f, -0.4f};
    QString errorMsg;
    bool ok = HistoryRepository::addSttHistoryItem(dataDir, QStringLiteral("test transcription"), QStringLiteral("model"), samples, errorMsg);
    QVERIFY(ok);

    QVariantList list = HistoryRepository::loadSttHistory(dataDir);
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().toMap().value(QStringLiteral("text")).toString(), QStringLiteral("test transcription"));

    ok = HistoryRepository::clearSttHistory(dataDir, errorMsg);
    QVERIFY(ok);
}

} // namespace LAStudio
