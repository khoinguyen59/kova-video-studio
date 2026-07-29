#include "test_MediaIngestService.h"

#include "core/MediaRuntimeLocator.h"
#include "dubbing/media/MediaIngestService.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace LAStudio {

void TestMediaIngestService::rejectsMissingInputExactlyOnce()
{
    MediaIngestService service;
    QSignalSpy finished(&service, &MediaIngestService::finished);
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    service.ingest(temporaryDirectory.filePath(QStringLiteral("missing-input.wav")));

    QCOMPARE(finished.count(), 1);
    const QList<QVariant> result = finished.takeFirst();
    QCOMPARE(result.at(0).toBool(), false);
    QVERIFY(result.at(2).toString().contains(QStringLiteral("does not exist"), Qt::CaseInsensitive));
}

void TestMediaIngestService::prefersBundledMediaToolsOverExternalConfiguration()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString mediaToolsDirectory = temporaryDirectory.filePath(QStringLiteral("media-tools"));
    QVERIFY(QDir().mkpath(mediaToolsDirectory));
    const QString ffmpeg = mediaToolsDirectory + QStringLiteral("/ffmpeg.exe");
    const QString ffprobe = mediaToolsDirectory + QStringLiteral("/ffprobe.exe");
    QVERIFY(QFile(ffmpeg).open(QIODevice::WriteOnly));
    QVERIFY(QFile(ffprobe).open(QIODevice::WriteOnly));

    const QByteArray originalFfmpeg = qgetenv("LASTUDIO_FFMPEG");
    const QByteArray originalFfprobe = qgetenv("LASTUDIO_FFPROBE");
    qputenv("LASTUDIO_FFMPEG", QByteArray("C:/not-used/ffmpeg.exe"));
    qputenv("LASTUDIO_FFPROBE", QByteArray("C:/not-used/ffprobe.exe"));
    const MediaRuntimePaths runtime = MediaRuntimeLocator::resolveForApplicationDirectory(temporaryDirectory.path());
    if (originalFfmpeg.isEmpty()) qunsetenv("LASTUDIO_FFMPEG");
    else qputenv("LASTUDIO_FFMPEG", originalFfmpeg);
    if (originalFfprobe.isEmpty()) qunsetenv("LASTUDIO_FFPROBE");
    else qputenv("LASTUDIO_FFPROBE", originalFfprobe);

    QCOMPARE(QDir::cleanPath(runtime.ffmpeg), QDir::cleanPath(ffmpeg));
    QCOMPARE(QDir::cleanPath(runtime.ffprobe), QDir::cleanPath(ffprobe));
    QVERIFY(runtime.isComplete());
}

} // namespace LAStudio
