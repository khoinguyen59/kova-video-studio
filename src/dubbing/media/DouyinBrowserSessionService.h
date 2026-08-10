#pragma once

#include <QObject>
#include <QProcess>
#include <QUrl>

namespace LAStudio {

// Runs an app-owned Playwright Chromium profile for Douyin.  The profile is
// never imported from Chrome/Edge/Firefox; it is created below the app data
// directory and is used only after the user explicitly starts the session.
class DouyinBrowserSessionService final : public QObject
{
    Q_OBJECT
public:
    enum class Operation { None, Login, Check, Download };

    struct Runtime {
        QString python;
        QString script;
        QString profile;
        bool isUsable() const { return !python.isEmpty() && !script.isEmpty(); }
    };

    explicit DouyinBrowserSessionService(QObject *parent = nullptr);
    ~DouyinBrowserSessionService() override;

    Runtime runtime() const;
    bool available(QString *error = nullptr) const;
    bool profileExists() const;
    QString profileDirectory() const;
    bool busy() const { return m_process.state() != QProcess::NotRunning; }

    bool openLogin(QString *error = nullptr);
    bool checkConnection(const QUrl &sourceUrl = QUrl(QStringLiteral("https://www.douyin.com/")),
                         QString *error = nullptr);
    bool download(const QUrl &sourceUrl, const QString &outputPath, QString *error = nullptr);
    void cancel();

    // Public for a deterministic process-contract regression.  It contains
    // only paths and operation data; credentials are never command arguments.
    static QStringList helperArguments(const QString &script, const QString &profile,
                                       const QString &mode, const QUrl &sourceUrl = {},
                                       const QString &outputPath = {}, int timeoutMs = 60000);

signals:
    void loginFinished(bool success, const QString &message);
    void connectionChecked(bool success, const QString &message);
    void downloadFinished(bool success, const QString &localPath, const QString &message);
    void statusChanged(const QString &status);

private:
    bool start(Operation operation, const QUrl &sourceUrl, const QString &outputPath,
               QString *error);
    void finishProcess(int exitCode, QProcess::ExitStatus status);
    QString diagnostic() const;
    static Runtime resolveRuntime();

    QProcess m_process;
    Operation m_operation = Operation::None;
    QString m_outputPath;
    QByteArray m_stdout;
    QByteArray m_stderr;
};

} // namespace LAStudio
