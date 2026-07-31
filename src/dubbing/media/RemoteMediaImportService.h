#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QUrl>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace LAStudio {

// Downloads a user-supplied, direct media URL into app-owned staging. It never
// persists the URL (which can contain a short-lived signed query) and has no
// browser cookie, credential, DRM, or paywall handling.
class RemoteMediaImportService final : public QObject
{
    Q_OBJECT
public:
    explicit RemoteMediaImportService(const QString &storageRoot = QString(),
                                      QObject *parent = nullptr,
                                      int resolverTimeoutMs = 60000);

    bool download(const QUrl &sourceUrl);
    void cancel();
    bool active() const { return m_active; }
    // Keep the untrusted page URL as one positional process argument.  This
    // is public so the regression can assert the process contract directly.
    static QStringList publicVideoResolverArguments(const QUrl &sourceUrl);

signals:
    void transferProgress(qint64 receivedBytes, qint64 totalBytes);
    void finished(bool success, const QString &localPath, const QString &error);

private:
    bool isSupportedSource(const QUrl &sourceUrl) const;
    bool isPublicVideoPage(const QUrl &sourceUrl) const;
    bool resolvePublicVideoPage(const QUrl &sourceUrl);
    void validateAndStartDirectDownload(const QUrl &sourceUrl);
    void startDirectDownload(const QUrl &sourceUrl);
    bool ensureOutputFile();
    void consumeAvailableData();
    void fail(const QString &error);
    QString outputFileName() const;

    QString m_storageRoot;
    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QProcess m_resolver;
    QByteArray m_resolverOutput;
    QByteArray m_resolverError;
    int m_hostLookupId = -1;
    std::unique_ptr<QSaveFile> m_output;
    QString m_outputPath;
    qint64 m_bytesWritten = 0;
    bool m_active = false;
    int m_resolverTimeoutMs = 60000;
    quint64 m_resolverRunId = 0;
    QString m_pendingResolverTerminationError;
};

} // namespace LAStudio
