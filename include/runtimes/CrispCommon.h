#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLibrary>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace LAStudio {

struct crispasr_session;

struct crispasr_open_params_v1 {
    int abi_version;
    int n_threads;
    int use_gpu;
    int verbosity;
    int flash_attn;
    int n_gpu_layers;
    int reserved[6];
};

inline bool crispContainsPath(const QStringList& entries, const QString& path)
{
    const QString cleanPath = QDir::toNativeSeparators(QDir::cleanPath(path));
    return entries.contains(cleanPath, Qt::CaseInsensitive);
}

inline QStringList crispRuntimeDependencyDirs(const QString& libPath)
{
    const QFileInfo libInfo(libPath);

    QStringList dirs;
    auto addDir = [&dirs](const QString& path) {
        const QString cleanPath = QDir::toNativeSeparators(QDir::cleanPath(path));
        if (!cleanPath.isEmpty() && QDir(cleanPath).exists() && !crispContainsPath(dirs, cleanPath)) {
            dirs.append(cleanPath);
        }
    };

    addDir(libInfo.absolutePath());

    // A runtime may use a sibling directory for an explicitly declared native
    // dependency. Never discover directories by recursively scanning an
    // untrusted archive; the installer copies this list from the catalog into
    // backend-manifest.json.
    QDir runtimeRoot(libInfo.absolutePath());
    runtimeRoot.cdUp();
    QFile manifest(runtimeRoot.absoluteFilePath(QStringLiteral("backend-manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly)) {
        return dirs;
    }
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll());
    if (!document.isObject()) {
        return dirs;
    }
    for (const QJsonValue &value : document.object().value(QStringLiteral("nativeDependencies")).toArray()) {
        const QString dependency = QDir::cleanPath(value.toString());
        if (dependency.isEmpty() || QDir::isAbsolutePath(dependency) ||
            dependency == QStringLiteral("..") || dependency.startsWith(QStringLiteral("../")) ||
            dependency.startsWith(QStringLiteral("..\\")) ||
            !dependency.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive)) {
            continue;
        }
        const QFileInfo dependencyInfo(runtimeRoot.absoluteFilePath(dependency));
        if (dependencyInfo.isFile()) {
            addDir(dependencyInfo.absolutePath());
        }
    }

    return dirs;
}

#ifdef Q_OS_WIN
inline int crispDllLoadPriority(const QString& fileName)
{
    const QString name = fileName.toLower();
    if (name == QStringLiteral("ggml.dll") || name == QStringLiteral("ggmk.dll")) return 0;
    if (name == QStringLiteral("ggml-base.dll") || name == QStringLiteral("ggmk-base.dll")) return 1;
    if (name.startsWith(QStringLiteral("ggml-")) || name.startsWith(QStringLiteral("ggmk-"))) return 2;
    if (name.startsWith(QStringLiteral("cudart")) || name.startsWith(QStringLiteral("cublas"))) return 3;
    if (name.contains(QStringLiteral("espeak"))) return 4;
    return 5;
}

inline QVector<HMODULE> crispPreloadRuntimeDlls(const QString& mainLibPath, const QStringList& dirs)
{
    Q_UNUSED(dirs);
    const QString mainPath = QDir::toNativeSeparators(QDir::cleanPath(mainLibPath));
    QVector<QFileInfo> dlls;

    const QFileInfo mainInfo(mainLibPath);
    QDir runtimeRoot(mainInfo.absolutePath());
    runtimeRoot.cdUp();
    QFile manifest(runtimeRoot.absoluteFilePath(QStringLiteral("backend-manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll());
    if (!document.isObject()) {
        return {};
    }
    for (const QJsonValue &value : document.object().value(QStringLiteral("nativeDependencies")).toArray()) {
        const QString dependency = QDir::cleanPath(value.toString());
        if (dependency.isEmpty() || QDir::isAbsolutePath(dependency) ||
            dependency == QStringLiteral("..") || dependency.startsWith(QStringLiteral("../")) ||
            dependency.startsWith(QStringLiteral("..\\")) ||
            !dependency.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive)) {
            continue;
        }
        const QFileInfo dependencyInfo(runtimeRoot.absoluteFilePath(dependency));
        const QString dependencyPath = QDir::toNativeSeparators(QDir::cleanPath(dependencyInfo.absoluteFilePath()));
        if (dependencyInfo.isFile() && dependencyPath.compare(mainPath, Qt::CaseInsensitive) != 0) {
            dlls.append(dependencyInfo);
        }
    }

    std::sort(dlls.begin(), dlls.end(), [](const QFileInfo& a, const QFileInfo& b) {
        const int ap = crispDllLoadPriority(a.fileName());
        const int bp = crispDllLoadPriority(b.fileName());
        if (ap != bp) return ap < bp;
        return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0;
    });

    QSet<QString> loadedPaths;
    QVector<HMODULE> handles;
    for (const QFileInfo& dll : dlls) {
        const QString filePath = QDir::toNativeSeparators(QDir::cleanPath(dll.absoluteFilePath()));
        if (loadedPaths.contains(filePath))
            continue;
        loadedPaths.insert(filePath);

        HMODULE module = LoadLibraryExW(reinterpret_cast<LPCWSTR>(filePath.utf16()), nullptr,
                                        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                        LOAD_LIBRARY_SEARCH_USER_DIRS);
        if (module) {
            handles.append(module);
        }
    }
    return handles;
}

inline void crispReleasePreloadedRuntimeDlls(QVector<HMODULE>& handles)
{
    for (auto it = handles.rbegin(); it != handles.rend(); ++it) {
        FreeLibrary(*it);
    }
    handles.clear();
}

inline void crispUnloadLibraryAndDependencies(QLibrary& library, QVector<HMODULE>& handles)
{
    if (library.isLoaded()) {
        library.unload();
    }
    crispReleasePreloadedRuntimeDlls(handles);
}
#endif

} // namespace LAStudio
