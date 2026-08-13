#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QSaveFile;

namespace LAStudio {

class ColabSession;

// Downloads a public-media URL only through a separately verified Colab
// worker.  The desktop app never resolves the public URL, invokes yt-dlp, or
// reads browser cookies.  Once the worker has completed the download this
// runner retrieves the resulting media into the app's private cache.
class ColabMediaDownloadRunner final : public QObject
{
    Q_OBJECT
public:
    explicit ColabMediaDownloadRunner(ColabSession *session = nullptr,
                                      QObject *parent = nullptr);
    ~ColabMediaDownloadRunner() override;

    void setSession(ColabSession *session);
    bool download(const QUrl &sourceUrl);
    void cancel();
    bool active() const { return m_active; }

signals:
    void transferProgress(qint64 receivedBytes, qint64 totalBytes);
    void phaseChanged(const QString &phase);
    void finished(bool success, const QString &localPath, const QString &error);

private:
    bool validateSourceUrl(const QUrl &sourceUrl, QString *error) const;
    QNetworkRequest authenticatedRequest(const QString &relativePath) const;
    void requestStatus();
    void requestResultFile();
    void finish(bool success, const QString &localPath = {}, const QString &error = {});
    QString safeDetail(const QString &detail) const;

    ColabSession *m_session = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_requestReply;
    QPointer<QNetworkReply> m_fileReply;
    std::unique_ptr<QSaveFile> m_output;
    QTimer m_pollTimer;
    QString m_jobId;
    QString m_outputPath;
    QString m_suggestedFileName;
    bool m_active = false;
    bool m_cancelled = false;
    bool m_outputWriteFailed = false;
};

} // namespace LAStudio
