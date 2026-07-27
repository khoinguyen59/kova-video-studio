#include "test_MediaToolService.h"

#include "dubbing/media/MediaToolService.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace LAStudio {

void TestMediaToolService::rejectsMissingMediaInputsExactlyOnce()
{
    MediaToolService service;
    QSignalSpy finished(&service, &MediaToolService::finished);
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    const QString outputPath = temporaryDirectory.filePath(QStringLiteral("dubbed.mp4"));
    service.muxVideoWithAudio(temporaryDirectory.filePath(QStringLiteral("missing-video.mp4")),
                              temporaryDirectory.filePath(QStringLiteral("missing-audio.wav")),
                              QString(), outputPath);

    QCOMPARE(finished.count(), 1);
    const QList<QVariant> result = finished.takeFirst();
    QCOMPARE(result.at(0).toBool(), false);
    QCOMPARE(result.at(1).toString(), outputPath);
    QVERIFY(!result.at(2).toString().isEmpty());
}

} // namespace LAStudio
