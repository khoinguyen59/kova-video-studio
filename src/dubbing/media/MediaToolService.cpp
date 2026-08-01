#include "dubbing/media/MediaToolService.h"

#include "core/MediaRuntimeLocator.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace LAStudio {

namespace {

QString escapedSubtitleFilterPath(const QString &path)
{
    QString escaped = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    escaped.replace(QLatin1Char(':'), QStringLiteral("\\:"));
    escaped.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    return escaped;
}

} // namespace

MediaToolService::MediaToolService(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::readyReadStandardError,
            this, &MediaToolService::onReadyReadStandardError);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &MediaToolService::onFinished);
    connect(&m_process, &QProcess::errorOccurred,
            this, &MediaToolService::onProcessError);
}

QString MediaToolService::executablePath() const
{
    return MediaRuntimeLocator::resolve().ffmpeg;
}

bool MediaToolService::available() const
{
    return !executablePath().isEmpty();
}

void MediaToolService::muxVideoWithAudio(const QString &videoPath,
                                         const QString &audioPath,
                                         const QString &subtitlePath,
                                         const QString &outputPath,
                                         bool burnInSubtitles,
                                         const QString &subtitleFontDirectory)
{
    if (m_process.state() != QProcess::NotRunning) {
        emit finished(false, outputPath, QStringLiteral("Another media operation is already running."));
        return;
    }
    const QString executable = executablePath();
    if (executable.isEmpty()) {
        emit finished(false, outputPath,
                      QStringLiteral("FFmpeg was not found. Install the managed media runtime or set LASTUDIO_FFMPEG."));
        return;
    }
    if (!QFileInfo(videoPath).isFile() || !QFileInfo(audioPath).isFile()) {
        emit finished(false, outputPath, QStringLiteral("Video or dubbing audio file does not exist."));
        return;
    }

    m_outputPath = outputPath;
    m_stderr.clear();
    m_process.setProgram(executable);
    m_process.setWorkingDirectory(QFileInfo(executable).absolutePath());
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    QStringList arguments{
        QStringLiteral("-hide_banner"), QStringLiteral("-y"),
        QStringLiteral("-i"), videoPath,
        QStringLiteral("-i"), audioPath,
    };
    const bool hasSubtitles = !subtitlePath.isEmpty() && QFileInfo(subtitlePath).isFile();
    if (hasSubtitles)
        arguments.append({QStringLiteral("-i"), subtitlePath});
    if (burnInSubtitles && hasSubtitles) {
        const QString filterPath = escapedSubtitleFilterPath(subtitlePath);
        QString filter = QStringLiteral("subtitles=filename='%1'").arg(filterPath);
        if (!subtitleFontDirectory.trimmed().isEmpty()
            && QFileInfo(subtitleFontDirectory).isDir()) {
            filter += QStringLiteral(":fontsdir='%1'")
                          .arg(escapedSubtitleFilterPath(subtitleFontDirectory));
        }
        arguments.append({QStringLiteral("-vf"),
                          filter});
    }
    arguments.append({
        QStringLiteral("-map"), QStringLiteral("0:v:0?"),
        QStringLiteral("-map"), QStringLiteral("1:a:0"),
        // The bundled internal FFmpeg is deliberately LGPL-only and does not
        // ship libx264. MPEG-4 Part 2 is available in that runtime, so use it
        // whenever subtitle burn-in requires video re-encoding instead of
        // advertising an MP4 export that fails at encoder selection.
        QStringLiteral("-c:v"), burnInSubtitles && hasSubtitles
            ? QStringLiteral("mpeg4") : QStringLiteral("copy")
    });
    if (burnInSubtitles && hasSubtitles)
        arguments.append({QStringLiteral("-q:v"), QStringLiteral("3")});
    arguments.append({
        QStringLiteral("-c:a"), QStringLiteral("aac"),
        QStringLiteral("-b:a"), QStringLiteral("192k")
    });
    if (hasSubtitles && !burnInSubtitles) {
        const QString suffix = QFileInfo(outputPath).suffix().toLower();
        const QString codec = suffix == QStringLiteral("webm")
            ? QStringLiteral("webvtt")
            : (suffix == QStringLiteral("mp4") || suffix == QStringLiteral("mov")
               || suffix == QStringLiteral("m4v"))
                ? QStringLiteral("mov_text") : QStringLiteral("srt");
        arguments.append({QStringLiteral("-map"), QStringLiteral("2:0"),
                          QStringLiteral("-c:s"), codec,
                          QStringLiteral("-metadata:s:s:0"), QStringLiteral("title=Dubbed subtitles"),
                          QStringLiteral("-disposition:s:0"), QStringLiteral("default")});
    }
    arguments.append(outputPath);
    m_process.setArguments(arguments);
    m_process.start();
}

void MediaToolService::cancel()
{
    if (m_process.state() != QProcess::NotRunning) m_process.kill();
}

void MediaToolService::onReadyReadStandardError()
{
    m_stderr += m_process.readAllStandardError();
    if (m_stderr.size() > 1024 * 1024) m_stderr = m_stderr.right(1024 * 1024);
}

void MediaToolService::onFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_outputPath.isEmpty()) return;
    const bool ok = status == QProcess::NormalExit && exitCode == 0 && QFileInfo(m_outputPath).isFile();
    const QString error = ok ? QString() : QStringLiteral("FFmpeg export failed: %1")
        .arg(QString::fromLocal8Bit(m_stderr).trimmed());
    emit progress(ok ? 100 : 0);
    emit finished(ok, m_outputPath, error);
    m_outputPath.clear();
    m_stderr.clear();
}

void MediaToolService::onProcessError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart || m_outputPath.isEmpty()) return;
    const QString outputPath = m_outputPath;
    m_outputPath.clear();
    m_stderr.clear();
    emit progress(0);
    emit finished(false, outputPath,
                  QStringLiteral("FFmpeg could not be started: %1").arg(m_process.errorString()));
}

} // namespace LAStudio
