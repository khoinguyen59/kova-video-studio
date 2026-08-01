#include "PathUtils.h"
#include <QStandardPaths>
#include <QDir>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace LAStudio {

namespace {

QString dataDirectoryOverride()
{
#ifdef Q_OS_WIN
    // qgetenv()/qEnvironmentVariable() are byte-oriented.  A user profile or
    // explicit LASTUDIO_DATA_DIR can be Unicode on Windows, so query the wide
    // environment directly rather than decoding UTF-8 bytes through the
    // active console codepage.
    const DWORD required = GetEnvironmentVariableW(L"LASTUDIO_DATA_DIR", nullptr, 0);
    if (required == 0) return {};
    QString value(static_cast<qsizetype>(required), Qt::Uninitialized);
    const DWORD written = GetEnvironmentVariableW(
        L"LASTUDIO_DATA_DIR", reinterpret_cast<wchar_t *>(value.data()), required);
    if (written == 0 || written >= required) return {};
    value.truncate(static_cast<qsizetype>(written));
    return value.trimmed();
#else
    return qEnvironmentVariable("LASTUDIO_DATA_DIR").trimmed();
#endif
}

} // namespace

QString PathUtils::dataDir()
{
    const QString overrideDir = dataDirectoryOverride();
    if (!overrideDir.isEmpty()) {
        return QDir::cleanPath(overrideDir);
    }
    return QDir::homePath() + QStringLiteral("/.lastudio");
}

QString PathUtils::modelsDir()
{
    return dataDir() + QStringLiteral("/models");
}

QString PathUtils::hubModelsDir()
{
    return dataDir() + QStringLiteral("/hub/models");
}

QString PathUtils::cacheDir()
{
    // A data-directory override is used by the QML smoke test and by
    // disposable package acceptance profiles.  Keep every mutable artifact
    // inside that profile; otherwise media staging can unexpectedly write to
    // the real per-user cache while the rest of the application is isolated.
    const QString overrideDir = dataDirectoryOverride();
    if (!overrideDir.isEmpty()) {
        return QDir::cleanPath(QDir(overrideDir).filePath(QStringLiteral("cache")));
    }
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

QString PathUtils::logsDir()
{
    return dataDir() + QStringLiteral("/logs");
}

QString PathUtils::extensionsDir()
{
    return dataDir() + QStringLiteral("/extensions");
}

QString PathUtils::backendsDir()
{
    return extensionsDir() + QStringLiteral("/backends");
}

void PathUtils::ensureDirsExist()
{
    QDir().mkpath(dataDir());
    QDir().mkpath(hubModelsDir());
    QDir().mkpath(modelsDir());
    QDir().mkpath(cacheDir());
    QDir().mkpath(logsDir());
    QDir().mkpath(extensionsDir());
    QDir().mkpath(backendsDir());
}

#ifdef Q_OS_WIN
#include <windows.h>
#include <QVarLengthArray>
#endif

QString PathUtils::toNativeShortPath(const QString &longPath)
{
#ifdef Q_OS_WIN
    if (longPath.isEmpty())
        return longPath;

    QString nativePath = QDir::toNativeSeparators(longPath);
    DWORD length = GetShortPathNameW(reinterpret_cast<const wchar_t*>(nativePath.utf16()), nullptr, 0);
    if (length == 0) {
        return longPath;
    }

    QVarLengthArray<wchar_t, MAX_PATH> buffer(static_cast<int>(length));
    DWORD result = GetShortPathNameW(reinterpret_cast<const wchar_t*>(nativePath.utf16()), buffer.data(), length);
    if (result > 0 && result < length) {
        return QDir::fromNativeSeparators(QString::fromWCharArray(buffer.constData(), static_cast<int>(result)));
    }
#endif
    return longPath;
}

QString PathUtils::urlToLocalPath(const QString &urlStr)
{
    QUrl url(urlStr);
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    return urlStr;
}

} // namespace LAStudio

