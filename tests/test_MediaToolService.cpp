#include "test_MediaToolService.h"

#include "dubbing/DubbingSubtitleService.h"
#include "dubbing/media/MediaToolService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
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

void TestMediaToolService::passesConfiguredFontDirectoryToBurnInFilter()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString videoPath = temporaryDirectory.filePath(QStringLiteral("source.mp4"));
    const QString audioPath = temporaryDirectory.filePath(QStringLiteral("dubbed.wav"));
    const QString subtitlePath = temporaryDirectory.filePath(QStringLiteral("styled.ass"));
    const QString fontDirectory = temporaryDirectory.filePath(QStringLiteral("fonts"));
    const QString fontPath = fontDirectory + QStringLiteral("/Custom.ttf");
    const QString argumentsPath = temporaryDirectory.filePath(QStringLiteral("arguments.txt"));
    const QString outputPath = temporaryDirectory.filePath(QStringLiteral("dubbed.mp4"));
    const QString ffmpegPath = temporaryDirectory.filePath(QStringLiteral("fake-ffmpeg.cmd"));
    QVERIFY(QDir().mkpath(fontDirectory));
    for (const QString &path : {videoPath, audioPath, subtitlePath, fontPath}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("fixture") > 0);
    }
    QFile ffmpeg(ffmpegPath);
    QVERIFY(ffmpeg.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray script = QByteArray("@echo off\r\n")
        + "echo %* > \"" + argumentsPath.toLocal8Bit() + "\"\r\n"
          "set \"last=\"\r\n"
          ":next\r\n"
          "if \"%~1\"==\"\" goto done\r\n"
          "set \"last=%~1\"\r\n"
          "shift\r\n"
          "goto next\r\n"
          ":done\r\n"
          "> \"%last%\" echo rendered\r\n";
    QVERIFY(ffmpeg.write(script) == script.size());
    ffmpeg.close();

    const QByteArray previousFfmpeg = qgetenv("LASTUDIO_FFMPEG");
    const bool hadFfmpeg = qEnvironmentVariableIsSet("LASTUDIO_FFMPEG");
    qputenv("LASTUDIO_FFMPEG", ffmpegPath.toUtf8());
    MediaToolService service;
    QSignalSpy finished(&service, &MediaToolService::finished);
    service.muxVideoWithAudio(videoPath, audioPath, subtitlePath, outputPath, true, fontDirectory);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    if (hadFfmpeg) qputenv("LASTUDIO_FFMPEG", previousFfmpeg);
    else qunsetenv("LASTUDIO_FFMPEG");
    QVERIFY(finished.constFirst().at(0).toBool());

    QFile arguments(argumentsPath);
    QVERIFY(arguments.open(QIODevice::ReadOnly));
    QString expectedDirectory = QDir::fromNativeSeparators(QFileInfo(fontDirectory).absoluteFilePath());
    expectedDirectory.replace(QLatin1Char(':'), QStringLiteral("\\:"));
    expectedDirectory.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    const QByteArray capturedArguments = arguments.readAll();
    QVERIFY(capturedArguments.contains(
        QStringLiteral("fontsdir='%1'").arg(expectedDirectory).toLocal8Bit()));
    QVERIFY(capturedArguments.contains("-c:v mpeg4"));
}

void TestMediaToolService::rendersLineSpacedAssWithStagedFfmpeg()
{
    const QString ffmpegPath = MediaToolService().executablePath();
    if (ffmpegPath.isEmpty()) QSKIP("This integration check requires the staged FFmpeg runtime.");
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString videoPath = temporaryDirectory.filePath(QStringLiteral("source.mp4"));
    const QString audioPath = temporaryDirectory.filePath(QStringLiteral("dubbed.wav"));
    const QString subtitlePath = temporaryDirectory.filePath(QStringLiteral("line-spacing.ass"));
    const QString outputPath = temporaryDirectory.filePath(QStringLiteral("rendered.mp4"));
    const auto runFfmpeg = [&ffmpegPath](const QStringList &arguments, QString *error) {
        QProcess process;
        process.setProgram(ffmpegPath);
        process.setArguments(arguments);
        process.start();
        if (!process.waitForStarted(5000) || !process.waitForFinished(15000)
            || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            if (error) *error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            return false;
        }
        return true;
    };
    QString setupError;
    QVERIFY2(runFfmpeg({QStringLiteral("-hide_banner"), QStringLiteral("-y"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), QStringLiteral("color=c=black:s=320x180:r=25"),
                        QStringLiteral("-t"), QStringLiteral("1"), QStringLiteral("-pix_fmt"),
                        QStringLiteral("yuv420p"), videoPath}, &setupError), qPrintable(setupError));
    QVERIFY2(runFfmpeg({QStringLiteral("-hide_banner"), QStringLiteral("-y"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), QStringLiteral("anullsrc=r=44100:cl=mono"),
                        QStringLiteral("-t"), QStringLiteral("1"), QStringLiteral("-c:a"),
                        QStringLiteral("pcm_s16le"), audioPath}, &setupError), qPrintable(setupError));

    QVariantMap style = DubbingSubtitleService::defaultStyle();
    style.insert(QStringLiteral("fontSize"), 36);
    style.insert(QStringLiteral("lineSpacing"), 1.6);
    const QVariantList segments{QVariantMap{{QStringLiteral("startMs"), 0},
                                            {QStringLiteral("endMs"), 900},
                                            {QStringLiteral("sourceText"),
                                             QStringLiteral("First render line\nSecond render line")}}};
    QString subtitleError;
    QVERIFY2(DubbingSubtitleService::writeAss(segments, style, subtitlePath, false, &subtitleError),
             qPrintable(subtitleError));

    MediaToolService service;
    QSignalSpy finished(&service, &MediaToolService::finished);
    service.muxVideoWithAudio(videoPath, audioPath, subtitlePath, outputPath, true);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);
    QVERIFY2(finished.constFirst().at(0).toBool(),
             qPrintable(finished.constFirst().at(2).toString()));
    QVERIFY(QFileInfo(outputPath).isFile());
    QVERIFY(QFileInfo(outputPath).size() > 0);
}

} // namespace LAStudio
