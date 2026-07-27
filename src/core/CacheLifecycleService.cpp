#include "CacheLifecycleService.h"

#include "PathUtils.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QtConcurrent>

#include <limits>

namespace LAStudio {
namespace {

QString cacheRoot()
{
    return QDir(PathUtils::cacheDir()).absoluteFilePath(QStringLiteral("dubbing"));
}

QStringList managedCacheDirectories()
{
    return {QStringLiteral("imports"), QStringLiteral("source-separation"), QStringLiteral("alignment")};
}

qint64 directoryBytes(const QString &path)
{
    qint64 total = 0;
    QDirIterator files(path, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        files.next();
        const qint64 size = files.fileInfo().size();
        if (size > 0 && total <= std::numeric_limits<qint64>::max() - size) total += size;
    }
    return total;
}

qint64 managedCacheBytes()
{
    const QDir root(cacheRoot());
    qint64 total = 0;
    for (const QString &name : managedCacheDirectories()) {
        const qint64 bytes = directoryBytes(root.absoluteFilePath(name));
        if (bytes > 0 && total <= std::numeric_limits<qint64>::max() - bytes) total += bytes;
    }
    return total;
}

QString clearManagedCache()
{
    const QDir root(cacheRoot());
    const QString rootPath = QDir::cleanPath(root.absolutePath());
    for (const QString &name : managedCacheDirectories()) {
        const QString target = QDir::cleanPath(root.absoluteFilePath(name));
        if (!target.startsWith(rootPath + QLatin1Char('/'), Qt::CaseInsensitive)) {
            return QStringLiteral("Refusing to clear a cache path outside the managed root.");
        }
        QDir directory(target);
        if (directory.exists() && !directory.removeRecursively()) {
            return QStringLiteral("Could not clear workflow cache directory: %1").arg(target);
        }
    }
    return {};
}

} // namespace

CacheLifecycleService::CacheLifecycleService(QObject *parent)
    : QObject(parent)
{
    connect(&m_refreshWatcher, &QFutureWatcher<qint64>::finished, this, [this]() {
        const qint64 bytes = m_refreshWatcher.result();
        m_refreshing = false;
        emit refreshingChanged();
        if (m_cacheBytes != bytes) {
            m_cacheBytes = bytes;
            emit cacheBytesChanged();
        }
    });
    connect(&m_clearWatcher, &QFutureWatcher<QString>::finished, this, [this]() {
        const QString error = m_clearWatcher.result();
        m_clearing = false;
        emit clearingChanged();
        if (!error.isEmpty()) {
            emit errorOccurred(error);
            return;
        }
        emit cleared();
        refresh();
    });
    refresh();
}

void CacheLifecycleService::refresh()
{
    if (m_refreshing || m_clearing) return;
    m_refreshing = true;
    emit refreshingChanged();
    m_refreshWatcher.setFuture(QtConcurrent::run([] { return managedCacheBytes(); }));
}

void CacheLifecycleService::clear()
{
    if (m_clearing || m_refreshing) return;
    m_clearing = true;
    emit clearingChanged();
    m_clearWatcher.setFuture(QtConcurrent::run([] { return clearManagedCache(); }));
}

} // namespace LAStudio
