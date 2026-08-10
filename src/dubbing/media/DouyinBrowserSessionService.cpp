#include "dubbing/media/DouyinBrowserSessionService.h"

#include "core/PathUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace LAStudio {
namespace {

QString existingFile(const QString &path)
{
    const QFileInfo info(QDir::cleanPath(path));
    return info.isFile() && info.isReadable() ? info.absoluteFilePath() : QString();
}

QString applicationScript(const QString &applicationDirectory)
{
    const QString configured = existingFile(qEnvironmentVariable("LASTUDIO_DOUYIN_BROWSER_SCRIPT"));
    if (!configured.isEmpty()) return configured;
    const QString bundled = existingFile(QDir(applicationDirectory)
                                             .filePath(QStringLiteral("douyin-browser/douyin_browser_session.py")));
    if (!bundled.isEmpty()) return bundled;
#ifdef LASTUDIO_SOURCE_DIR
    return existingFile(QDir(QStringLiteral(LASTUDIO_SOURCE_DIR))
                            .filePath(QStringLiteral("scripts/douyin_browser_session.py")));
#else
    return {};
#endif
}

bool interpreterHasPlaywright(const QString &python)
{
    if (python.isEmpty()) return false;
    static QHash<QString, bool> cache;
    const auto cached = cache.constFind(python);
    if (cached != cache.cend()) return cached.value();

    QProcess probe;
    probe.start(python, {QStringLiteral("-c"), QStringLiteral("import playwright")});
    const bool available = probe.waitForFinished(3000)
        && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
    cache.insert(python, available);
    return available;
}

void appendPythonCandidate(QStringList &candidates, const QString &path)
{
    const QString value = existingFile(path);
    if (!value.isEmpty() && !candidates.contains(value)) candidates.append(value);
}

QString findPython(const QString &applicationDirectory)
{
    const QString configured = existingFile(qEnvironmentVariable("LASTUDIO_DOUYIN_PYTHON"));
    if (!configured.isEmpty()) return configured;
#ifdef Q_OS_WIN
    const QString bundled = existingFile(QDir(applicationDirectory)
                                             .filePath(QStringLiteral("douyin-browser/python.exe")));
#else
    const QString bundled = existingFile(QDir(applicationDirectory)
                                             .filePath(QStringLiteral("douyin-browser/bin/python3")));
#endif
    if (!bundled.isEmpty()) return bundled;

    // QStandardPaths::findExecutable() returns only the first Python on PATH.
    // Windows machines commonly have a Python launcher, a system Python, and
    // Anaconda side by side; choose the first interpreter that can import the
    // dependency instead of silently selecting an unusable one.
    QStringList candidates;
#ifdef Q_OS_WIN
    const QStringList names{QStringLiteral("python.exe"), QStringLiteral("python3.exe")};
#else
    const QStringList names{QStringLiteral("python3"), QStringLiteral("python")};
#endif
    const QStringList pathEntries = qEnvironmentVariable("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
    for (const QString &entry : pathEntries) {
        const QDir directory(entry);
        for (const QString &name : names) appendPythonCandidate(candidates, directory.filePath(name));
    }
    appendPythonCandidate(candidates, QStandardPaths::findExecutable(QStringLiteral("python")));
    appendPythonCandidate(candidates, QStandardPaths::findExecutable(QStringLiteral("python3")));

    for (const QString &candidate : candidates) {
        if (interpreterHasPlaywright(candidate)) return candidate;
    }
    return candidates.isEmpty() ? QString() : candidates.constFirst();
}

QString defaultProfile()
{
    const QString configured = qEnvironmentVariable("LASTUDIO_DOUYIN_BROWSER_PROFILE").trimmed();
    if (!configured.isEmpty()) return QDir::cleanPath(configured);
    return QDir(PathUtils::dataDir()).filePath(QStringLiteral("douyin-browser-profile"));
}

QString operationName(DouyinBrowserSessionService::Operation operation)
{
    switch (operation) {
    case DouyinBrowserSessionService::Operation::Login: return QStringLiteral("login");
    case DouyinBrowserSessionService::Operation::Check: return QStringLiteral("check");
    case DouyinBrowserSessionService::Operation::Download: return QStringLiteral("download");
    case DouyinBrowserSessionService::Operation::None: break;
    }
    return {};
}

} // namespace

DouyinBrowserSessionService::DouyinBrowserSessionService(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_stdout += m_process.readAllStandardOutput();
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += m_process.readAllStandardError();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        const QString message = QStringLiteral("The managed Chromium session could not be started.");
        m_operation = Operation::None;
        emit statusChanged(message);
        emit loginFinished(false, message);
        emit connectionChecked(false, message);
        emit downloadFinished(false, {}, message);
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            &DouyinBrowserSessionService::finishProcess);
}

DouyinBrowserSessionService::~DouyinBrowserSessionService()
{
    cancel();
}

DouyinBrowserSessionService::Runtime DouyinBrowserSessionService::resolveRuntime()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return {findPython(appDir), applicationScript(appDir), defaultProfile()};
}

DouyinBrowserSessionService::Runtime DouyinBrowserSessionService::runtime() const
{
    return resolveRuntime();
}

bool DouyinBrowserSessionService::available(QString *error) const
{
    const Runtime value = runtime();
    if (value.python.isEmpty()) {
        if (error) *error = QStringLiteral("Python was not found for the managed Chromium session.");
        return false;
    }
    if (value.script.isEmpty()) {
        if (error) *error = QStringLiteral("The managed Chromium session helper is not installed.");
        return false;
    }
    if (!interpreterHasPlaywright(value.python)) {
        if (error) {
            *error = QStringLiteral("Playwright is not installed in the Python interpreter selected for LA Studio (%1). "
                                   "Install it there, or set LASTUDIO_DOUYIN_PYTHON to a Python environment "
                                   "that contains Playwright.").arg(value.python);
        }
        return false;
    }
    return true;
}

bool DouyinBrowserSessionService::profileExists() const
{
    return QDir(profileDirectory()).exists();
}

QString DouyinBrowserSessionService::profileDirectory() const
{
    return runtime().profile;
}

QStringList DouyinBrowserSessionService::helperArguments(const QString &script,
                                                          const QString &profile,
                                                          const QString &mode,
                                                          const QUrl &sourceUrl,
                                                          const QString &outputPath,
                                                          int timeoutMs)
{
    QStringList arguments{script, QStringLiteral("--mode"), mode,
                          QStringLiteral("--profile"), profile,
                          QStringLiteral("--timeout-ms"), QString::number(qMax(10000, timeoutMs))};
    if (sourceUrl.isValid()) arguments.append({QStringLiteral("--url"), sourceUrl.toString(QUrl::FullyEncoded)});
    if (!outputPath.trimmed().isEmpty()) arguments.append({QStringLiteral("--output"), outputPath});
    return arguments;
}

bool DouyinBrowserSessionService::start(Operation operation, const QUrl &sourceUrl,
                                        const QString &outputPath, QString *error)
{
    if (busy()) {
        if (error) *error = QStringLiteral("A managed Chromium operation is already running.");
        return false;
    }
    QString availabilityError;
    if (!available(&availabilityError)) {
        if (error) *error = availabilityError;
        return false;
    }
    const Runtime value = runtime();
    if (!QDir().mkpath(value.profile)) {
        if (error) *error = QStringLiteral("Could not create the app-owned Chromium profile directory.");
        return false;
    }
    if (operation != Operation::Login && !sourceUrl.isValid()) {
        if (error) *error = QStringLiteral("A valid Douyin URL is required.");
        return false;
    }
    if (operation == Operation::Download && outputPath.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("A staging output path is required for browser download.");
        return false;
    }
    m_operation = operation;
    m_outputPath = outputPath;
    m_stdout.clear();
    m_stderr.clear();
    m_process.setProgram(value.python);
    m_process.setArguments(helperArguments(value.script, value.profile, operationName(operation),
                                            sourceUrl, outputPath));
    m_process.start();
    if (!m_process.waitForStarted(2000)) {
        m_operation = Operation::None;
        if (error) *error = QStringLiteral("The managed Chromium session could not be started.");
        return false;
    }
    emit statusChanged(operation == Operation::Login
                           ? QStringLiteral("Waiting for Douyin login in the managed Chromium profile")
                           : operation == Operation::Check
                                 ? QStringLiteral("Checking the managed Douyin Chromium session")
                                 : QStringLiteral("Downloading Douyin media in the managed Chromium session"));
    return true;
}

bool DouyinBrowserSessionService::openLogin(QString *error)
{
    return start(Operation::Login, {}, {}, error);
}

bool DouyinBrowserSessionService::checkConnection(const QUrl &sourceUrl, QString *error)
{
    return start(Operation::Check, sourceUrl, {}, error);
}

bool DouyinBrowserSessionService::download(const QUrl &sourceUrl, const QString &outputPath,
                                           QString *error)
{
    return start(Operation::Download, sourceUrl, outputPath, error);
}

QString DouyinBrowserSessionService::diagnostic() const
{
    const QList<QByteArray> lines = m_stdout.trimmed().split('\n');
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QJsonDocument document = QJsonDocument::fromJson(it->trimmed());
        if (!document.isObject()) continue;
        const QString message = document.object().value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty()) return message;
        if (document.object().value(QStringLiteral("event")).toString() == QStringLiteral("ready"))
            return QStringLiteral("Managed Chromium session verified.");
    }
    return QStringLiteral("The managed Chromium session did not return a usable result.");
}

void DouyinBrowserSessionService::finishProcess(int exitCode, QProcess::ExitStatus status)
{
    m_stdout += m_process.readAllStandardOutput();
    m_stderr += m_process.readAllStandardError();
    const Operation operation = m_operation;
    const bool success = status == QProcess::NormalExit && exitCode == 0;
    const QString message = success ? diagnostic()
                                   : (diagnostic() == QStringLiteral("The managed Chromium session did not return a usable result.")
                                          ? QStringLiteral("The managed Chromium session failed. Install Playwright and Chromium, then retry.")
                                          : diagnostic());
    const QString path = success && operation == Operation::Download && QFileInfo(m_outputPath).isFile()
        ? QFileInfo(m_outputPath).absoluteFilePath() : QString();
    m_operation = Operation::None;
    m_outputPath.clear();
    emit statusChanged(message);
    switch (operation) {
    case Operation::Login: emit loginFinished(success, message); break;
    case Operation::Check: emit connectionChecked(success, message); break;
    case Operation::Download: emit downloadFinished(success && !path.isEmpty(), path, message); break;
    case Operation::None: break;
    }
}

void DouyinBrowserSessionService::cancel()
{
    if (!busy()) return;
    m_process.terminate();
    if (!m_process.waitForFinished(1200)) m_process.kill();
    m_operation = Operation::None;
}

} // namespace LAStudio
