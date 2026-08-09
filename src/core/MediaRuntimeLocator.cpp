#include "core/MediaRuntimeLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace LAStudio {
namespace {

QString executableName(const QString &baseName)
{
#ifdef Q_OS_WIN
    return baseName + QStringLiteral(".exe");
#else
    return baseName;
#endif
}

QString existingFile(const QString &path)
{
    const QFileInfo info(path);
    return info.isFile() ? info.absoluteFilePath() : QString();
}

QString bundledTool(const QString &applicationDirectory, const QString &baseName)
{
    if (applicationDirectory.isEmpty())
        return {};
    // FFmpeg and FFprobe are deliberately grouped in media-tools, while the
    // pinned standalone public-video resolver is staged at the portable app
    // root by package.ps1.  Looking for every tool under media-tools made a
    // correctly packaged yt-dlp invisible at runtime.
    if (baseName == QStringLiteral("yt-dlp")) {
        return existingFile(QDir(applicationDirectory).filePath(executableName(baseName)));
    }
    return existingFile(QDir(applicationDirectory).filePath(
        QStringLiteral("media-tools/") + executableName(baseName)));
}

QString configuredTool(const char *environmentVariable)
{
    return existingFile(qEnvironmentVariable(environmentVariable));
}

QString siblingTool(const QString &toolPath, const QString &baseName)
{
    if (toolPath.isEmpty())
        return {};
    return existingFile(QDir(QFileInfo(toolPath).absolutePath()).filePath(executableName(baseName)));
}

} // namespace

bool MediaRuntimePaths::hasFfmpeg() const
{
    return !ffmpeg.isEmpty();
}

bool MediaRuntimePaths::hasFfprobe() const
{
    return !ffprobe.isEmpty();
}

bool MediaRuntimePaths::hasYtDlp() const
{
    return !ytDlp.isEmpty();
}

bool MediaRuntimePaths::isComplete() const
{
    return hasFfmpeg() && hasFfprobe();
}

MediaRuntimePaths MediaRuntimeLocator::resolve()
{
    return resolveForApplicationDirectory(QCoreApplication::applicationDirPath());
}

MediaRuntimePaths MediaRuntimeLocator::resolveForApplicationDirectory(const QString &applicationDirectory)
{
    MediaRuntimePaths result;

    // A release package is self-contained. Do not let an unrelated, globally
    // installed FFmpeg change the behavior of a packaged application.
    result.ffmpeg = bundledTool(applicationDirectory, QStringLiteral("ffmpeg"));
    result.ffprobe = bundledTool(applicationDirectory, QStringLiteral("ffprobe"));
    result.ytDlp = bundledTool(applicationDirectory, QStringLiteral("yt-dlp"));
    if (result.isComplete())
        return result;

    // Keep partial bundled installations diagnosable, while using a complete
    // configured/runtime pair whenever one is available.
    const QString configuredFfmpeg = configuredTool("LASTUDIO_FFMPEG");
    const QString configuredFfprobe = configuredTool("LASTUDIO_FFPROBE");
    const QString configuredSiblingProbe = siblingTool(configuredFfmpeg, QStringLiteral("ffprobe"));
    const QString pathFfmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    const QString pathFfprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    const QString configuredYtDlp = configuredTool("LASTUDIO_YTDLP");
    const QString pathYtDlp = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));

    if (result.ffmpeg.isEmpty())
        result.ffmpeg = !configuredFfmpeg.isEmpty() ? configuredFfmpeg : pathFfmpeg;
    if (result.ffprobe.isEmpty()) {
        result.ffprobe = !configuredFfprobe.isEmpty() ? configuredFfprobe : configuredSiblingProbe;
        if (result.ffprobe.isEmpty())
            result.ffprobe = siblingTool(result.ffmpeg, QStringLiteral("ffprobe"));
        if (result.ffprobe.isEmpty())
            result.ffprobe = pathFfprobe;
    }
    if (result.ytDlp.isEmpty())
        result.ytDlp = !configuredYtDlp.isEmpty() ? configuredYtDlp : pathYtDlp;

    return result;
}

} // namespace LAStudio
