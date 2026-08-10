#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUrl>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace LAStudio {

class DouyinBrowserSessionService;

// Downloads a user-supplied, direct media URL into app-owned staging. It never
// persists the URL (which can contain a short-lived signed query). Browser
// cookies are never read automatically. A caller may explicitly select a
// Netscape cookie file for one resolver run; the service copies it to a
// short-lived private temp file and removes that copy when the run ends.
class RemoteMediaImportService final : public QObject
{
    Q_OBJECT
public:
    explicit RemoteMediaImportService(const QString &storageRoot = QString(),
                                      QObject *parent = nullptr,
                                      int resolverTimeoutMs = 60000);
    ~RemoteMediaImportService() override;

    bool download(const QUrl &sourceUrl);
    void cancel();
    bool setCookieFilePath(const QString &path, QString *error = nullptr);
    void clearCookieFilePath();
    bool hasCookieFilePath() const { return !m_cookieSourcePath.isEmpty(); }
    void setDouyinBrowserEnabled(bool enabled) { m_douyinBrowserEnabled = enabled; }
    bool douyinBrowserEnabled() const { return m_douyinBrowserEnabled; }
    bool active() const { return m_active; }
    // Keep the untrusted page URL as one positional process argument.  This
    // is public so the regression can assert the process contract directly.
    static QStringList publicVideoResolverArguments(const QUrl &sourceUrl,
                                                    const QString &cookieFilePath = {});

signals:
    void transferProgress(qint64 receivedBytes, qint64 totalBytes);
    void finished(bool success, const QString &localPath, const QString &error);

private:
    bool isSupportedSource(const QUrl &sourceUrl) const;
    bool isPublicVideoPage(const QUrl &sourceUrl) const;
    bool resolvePublicVideoPage(const QUrl &sourceUrl);
    bool resolvePublicVideoInBrowser(const QUrl &sourceUrl);
    bool prepareCookieFile(QString *error);
    void validateAndStartDirectDownload(const QUrl &sourceUrl);
    void startDirectDownload(const QUrl &sourceUrl);
    bool ensureOutputFile();
    void consumeAvailableData();
    void fail(const QString &error);
    QString outputFileName() const;

    QString m_storageRoot;
    QString m_cookieSourcePath;
    std::unique_ptr<QTemporaryFile> m_cookieFile;
    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QProcess m_resolver;
    DouyinBrowserSessionService *m_browserSession = nullptr;
    QByteArray m_resolverOutput;
    QByteArray m_resolverError;
    int m_hostLookupId = -1;
    std::unique_ptr<QSaveFile> m_output;
    QString m_outputPath;
    qint64 m_bytesWritten = 0;
    bool m_active = false;
    bool m_browserDownloadActive = false;
    bool m_douyinBrowserEnabled = false;
    int m_resolverTimeoutMs = 60000;
    quint64 m_resolverRunId = 0;
    QString m_pendingResolverTerminationError;
};

} // namespace LAStudio
