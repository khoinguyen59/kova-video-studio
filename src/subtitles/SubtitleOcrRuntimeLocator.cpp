#include "subtitles/SubtitleOcrRuntimeLocator.h"

#include "core/PathUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace LAStudio {
namespace {

QString executableName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("tesseract.exe");
#else
    return QStringLiteral("tesseract");
#endif
}

QString existingFile(const QString &path)
{
    const QFileInfo info(path);
    return info.isFile() ? info.absoluteFilePath() : QString();
}

} // namespace

QString SubtitleOcrRuntimeLocator::resolveTesseract()
{
    return resolve().path;
}

QString SubtitleOcrRuntimeLocator::resolveForApplicationDirectory(const QString &applicationDirectory)
{
    return resolveForApplicationDirectoryWithSource(applicationDirectory).path;
}

QString SubtitleOcrRuntimeLocator::managedRuntimeRoot()
{
    return QDir(PathUtils::dataDir()).filePath(QStringLiteral("subtitle-ocr/runtime"));
}

QString SubtitleOcrRuntimeLocator::managedTesseractPath()
{
    return QDir(managedRuntimeRoot()).filePath(executableName());
}

SubtitleOcrRuntimeResolution SubtitleOcrRuntimeLocator::resolve()
{
    return resolveForApplicationDirectoryWithSource(QCoreApplication::applicationDirPath());
}

SubtitleOcrRuntimeResolution SubtitleOcrRuntimeLocator::resolveForApplicationDirectoryWithSource(
    const QString &applicationDirectory)
{
    // An explicit environment value is an advanced override.  It wins over
    // the managed copy so power users can deliberately test another runtime.
    const QString configured = existingFile(qEnvironmentVariable("LASTUDIO_TESSERACT"));
    if (!configured.isEmpty()) return {configured, QStringLiteral("environment")};

    const QString managed = existingFile(managedTesseractPath());
    if (!managed.isEmpty()) return {managed, QStringLiteral("managed")};

    const QString bundled = existingFile(QDir(applicationDirectory).filePath(
        QStringLiteral("subtitle-ocr/") + executableName()));
    if (!bundled.isEmpty()) return {bundled, QStringLiteral("bundled")};
    return {};
}

} // namespace LAStudio
