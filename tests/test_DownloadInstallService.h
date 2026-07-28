#pragma once
#include <QObject>
#include <QTemporaryDir>

namespace LAStudio {

class TestDownloadInstallService : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testDownloadInstallService();
    void remoteFirstModeBlocksLocalDownloads();
    void testQuickInstallSelectsLatestCatalogRuntime();
    void testArchiveMemberPathsCannotEscapeExtractionDir();

private:
    QTemporaryDir m_tempDir;
};

} // namespace LAStudio
