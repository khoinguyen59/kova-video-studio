#include "subtitles/SubtitleOcrRuntimeLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

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
    return resolveForApplicationDirectory(QCoreApplication::applicationDirPath());
}

QString SubtitleOcrRuntimeLocator::resolveForApplicationDirectory(const QString &applicationDirectory)
{
    const QString bundled = existingFile(QDir(applicationDirectory).filePath(
        QStringLiteral("subtitle-ocr/") + executableName()));
    if (!bundled.isEmpty()) return bundled;

    const QString configured = existingFile(qEnvironmentVariable("LASTUDIO_TESSERACT"));
    if (!configured.isEmpty()) return configured;
    return QStandardPaths::findExecutable(QStringLiteral("tesseract"));
}

} // namespace LAStudio
