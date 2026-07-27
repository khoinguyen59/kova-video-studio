#include "ModelsPathMigrator.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QStorageInfo>

#include <limits>

namespace LAStudio {

bool ModelsPathMigrator::filesAreIdentical(const QString &srcPath, const QString &dstPath, QString &errorMsg)
{
    QFile srcFile(srcPath);
    QFile dstFile(dstPath);
    if (!srcFile.open(QIODevice::ReadOnly)) {
        errorMsg = QStringLiteral("Failed to open source file for compare: ") + srcPath;
        return false;
    }
    if (!dstFile.open(QIODevice::ReadOnly)) {
        errorMsg = QStringLiteral("Failed to open destination file for compare: ") + dstPath;
        return false;
    }

    if (srcFile.size() != dstFile.size()) {
        return false;
    }

    constexpr qint64 kChunkSize = 1024 * 1024;
    while (!srcFile.atEnd()) {
        const QByteArray srcChunk = srcFile.read(kChunkSize);
        const QByteArray dstChunk = dstFile.read(kChunkSize);
        if (srcChunk != dstChunk) {
            return false;
        }
    }
    return true;
}

qint64 ModelsPathMigrator::directorySize(const QString &dirPath, QString &errorMsg)
{
    const QDir root(dirPath);
    if (!root.exists()) {
        errorMsg = QStringLiteral("Models directory does not exist: ") + dirPath;
        return -1;
    }

    qint64 total = 0;
    QDirIterator iterator(dirPath,
                          QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const qint64 size = iterator.fileInfo().size();
        if (size < 0 || total > std::numeric_limits<qint64>::max() - size) {
            errorMsg = QStringLiteral("Could not calculate the size of the models directory safely.");
            return -1;
        }
        total += size;
    }
    return total;
}

bool ModelsPathMigrator::hasAvailableSpace(const QString &targetPath,
                                            qint64 requiredBytes,
                                            QString &errorMsg)
{
    if (requiredBytes < 0) {
        errorMsg = QStringLiteral("The required disk space is invalid.");
        return false;
    }

    const QStorageInfo storage(QDir(targetPath).absolutePath());
    const qint64 availableBytes = storage.bytesAvailable();
    if (!storage.isReady() || availableBytes < 0) {
        errorMsg = QStringLiteral("Could not determine free disk space for: ") + targetPath;
        return false;
    }
    if (availableBytes < requiredBytes) {
        errorMsg = QStringLiteral("Not enough free disk space at the destination. Need %1 MiB, but only %2 MiB is available.")
            .arg((requiredBytes + (1024 * 1024 - 1)) / (1024 * 1024))
            .arg(availableBytes / (1024 * 1024));
        return false;
    }
    return true;
}

bool ModelsPathMigrator::copyDirectoryMerge(const QString &srcDir, const QString &dstDir, QString &errorMsg)
{
    QDir src(srcDir);
    QDir dst(dstDir);
    if (!dst.exists()) {
        if (!dst.mkpath(dstDir)) {
            errorMsg = QStringLiteral("Failed to create destination directory: ") + dstDir;
            return false;
        }
    }

    QStringList dirs = src.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &d : dirs) {
        QString srcPath = src.absoluteFilePath(d);
        QString dstPath = dst.absoluteFilePath(d);
        if (!copyDirectoryMerge(srcPath, dstPath, errorMsg)) {
            return false;
        }
    }

    QStringList files = src.entryList(QDir::Files);
    for (const QString &f : files) {
        QString srcPath = src.absoluteFilePath(f);
        QString dstPath = dst.absoluteFilePath(f);

        QFileInfo srcFi(srcPath);
        QFileInfo dstFi(dstPath);

        if (dstFi.exists()) {
            const bool sameContent = filesAreIdentical(srcPath, dstPath, errorMsg);
            if (!errorMsg.isEmpty()) {
                return false;
            }
            if (!sameContent) {
                errorMsg = QStringLiteral("File conflict at destination: ") + dstPath;
                return false;
            }
            continue;
        } else {
            if (!QFile::copy(srcPath, dstPath)) {
                errorMsg = QStringLiteral("Failed to copy file: ") + srcPath + QStringLiteral(" to ") + dstPath;
                return false;
            }
        }
    }
    return true;
}

bool ModelsPathMigrator::removeDirectory(const QString &dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists()) {
        return true;
    }
    return dir.removeRecursively();
}

} // namespace LAStudio
