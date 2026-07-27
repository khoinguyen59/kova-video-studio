#pragma once

#include <QString>

namespace LAStudio {

class ModelsPathMigrator {
public:
    static bool copyDirectoryMerge(const QString &srcDir, const QString &dstDir, QString &errorMsg);
    static bool removeDirectory(const QString &dirPath);
    static bool filesAreIdentical(const QString &srcPath, const QString &dstPath, QString &errorMsg);
    static qint64 directorySize(const QString &dirPath, QString &errorMsg);
    static bool hasAvailableSpace(const QString &targetPath, qint64 requiredBytes, QString &errorMsg);
};

} // namespace LAStudio
