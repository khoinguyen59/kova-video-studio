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
    // An explicit environment value is an advanced override. It wins so power
    // users can deliberately test another runtime.
    const QString configured = existingFile(qEnvironmentVariable("LASTUDIO_TESSERACT"));
    if (!configured.isEmpty()) return {configured, QStringLiteral("environment")};

    // The package-provisioned executable has a package manifest and a pinned
    // hash. Prefer it over a runtime left in app data by an older release,
    // whose provenance cannot be established by this locator alone.
    const QString bundled = existingFile(QDir(applicationDirectory).filePath(
        QStringLiteral("subtitle-ocr/") + executableName()));
    if (!bundled.isEmpty()) return {bundled, QStringLiteral("bundled")};

    // Legacy app-data runtimes remain a fallback for recovery only. New
    // releases never install one and validate it before use in the service.
    const QString managed = existingFile(managedTesseractPath());
    if (!managed.isEmpty()) return {managed, QStringLiteral("managed")};
    return {};
}

} // namespace LAStudio
