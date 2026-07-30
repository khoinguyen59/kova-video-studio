#include "test_FileAccessService.h"
#include <QtTest>
#include <QFile>
#include <QTextStream>
#include "controllers/shared/FileAccessService.h"
#include "core/PathUtils.h"

namespace LAStudio {

void TestFileAccessService::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
}

void TestFileAccessService::testFileAccessService()
{
    qDebug() << "--- START: testFileAccessService ---";
    FileAccessService service;

    // Test urlToLocalPath
    QString urlStr = QStringLiteral("file:///test/path/file.txt");
    QString resolved = service.urlToLocalPath(urlStr);
    QVERIFY(!resolved.isEmpty());

    // Test readTextFile and localFileExists
    QString tempFilePath = m_tempDir.filePath(QStringLiteral("test_file.txt"));
    QFile file(tempFilePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    out << QStringLiteral("Hello World");
    file.close();

    QVERIFY(service.localFileExists(tempFilePath));
    QString content = service.readTextFile(tempFilePath);
    QCOMPARE(content, QStringLiteral("Hello World"));
}

void TestFileAccessService::dataDirectoryOverrideAlsoIsolatesCache()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());

    constexpr auto variableName = "LASTUDIO_DATA_DIR";
    const bool hadPreviousValue = qEnvironmentVariableIsSet(variableName);
    const QByteArray previousValue = qgetenv(variableName);

    qputenv(variableName, profile.path().toUtf8());
    const QString actualDataDir = PathUtils::dataDir();
    const QString actualCacheDir = PathUtils::cacheDir();

    if (hadPreviousValue) {
        qputenv(variableName, previousValue);
    } else {
        qunsetenv(variableName);
    }

    QCOMPARE(actualDataDir, QDir::cleanPath(profile.path()));
    QCOMPARE(actualCacheDir,
             QDir::cleanPath(QDir(profile.path()).filePath(QStringLiteral("cache"))));
}

} // namespace LAStudio
