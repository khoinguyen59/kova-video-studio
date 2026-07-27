#include "test_MediaIngestService.h"

#include "dubbing/media/MediaIngestService.h"

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

} // namespace LAStudio
