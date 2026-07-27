#pragma once

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QVector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace LAStudio {

// Temporarily makes explicit, trusted runtime directories available to a DLL
// load. This relies on SetDefaultDllDirectories configured at process startup,
// so PATH and the current working directory are never searched.
class ScopedTrustedDllDirectories {
public:
    explicit ScopedTrustedDllDirectories(const QStringList &directories)
    {
#ifdef Q_OS_WIN
        for (const QString &directory : directories) {
            const QString clean = QDir::toNativeSeparators(QDir::cleanPath(directory));
            if (clean.isEmpty() || !QDir(clean).exists() || m_directories.contains(clean, Qt::CaseInsensitive)) {
                continue;
            }
            m_directories.append(clean);
            if (const auto cookie = AddDllDirectory(reinterpret_cast<LPCWSTR>(clean.utf16()))) {
                m_cookies.append(cookie);
            }
        }
#else
        Q_UNUSED(directories);
#endif
    }

    ~ScopedTrustedDllDirectories()
    {
#ifdef Q_OS_WIN
        for (auto it = m_cookies.crbegin(); it != m_cookies.crend(); ++it) {
            RemoveDllDirectory(*it);
        }
#endif
    }

    ScopedTrustedDllDirectories(const ScopedTrustedDllDirectories &) = delete;
    ScopedTrustedDllDirectories &operator=(const ScopedTrustedDllDirectories &) = delete;

private:
    QStringList m_directories;
#ifdef Q_OS_WIN
    QVector<DLL_DIRECTORY_COOKIE> m_cookies;
#endif
};

} // namespace LAStudio
