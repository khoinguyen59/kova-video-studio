#pragma once

#include <QString>
#include <QDebug>
#include <QDateTime>
#include <QLoggingCategory>
#include <QtGlobal>

namespace LAStudio {

// Declare logging categories for modular log filtering
Q_DECLARE_LOGGING_CATEGORY(logCore)
Q_DECLARE_LOGGING_CATEGORY(logSTT)
Q_DECLARE_LOGGING_CATEGORY(logTTS)
Q_DECLARE_LOGGING_CATEGORY(logDownload)
Q_DECLARE_LOGGING_CATEGORY(logHardware)
Q_DECLARE_LOGGING_CATEGORY(logUI)

class Logger {
public:
    enum Level { Debug, Info, Warning, Error };
    using MessageObserver = void (*)(QtMsgType, const QMessageLogContext &, const QString &);

    static void init();
    // Keep the application's logger as Qt's sole message handler while allowing
    // constrained modes (for example the offscreen QML smoke executable) to
    // observe messages.  Replacing Qt's handler directly would silently drop
    // normal file logging and caused QML warnings to be reported as a passing
    // smoke run.
    static void setMessageObserver(MessageObserver observer);
    static void clear();
    static qint64 sessionStartOffset();
    static QString sanitizeDiagnostics(const QString &text);
    static void log(Level level, const QString &category, const QString &message);
    static void debug(const QString &cat, const QString &msg)   { log(Debug, cat, msg); }
    static void info(const QString &cat, const QString &msg)    { log(Info, cat, msg); }
    static void warning(const QString &cat, const QString &msg) { log(Warning, cat, msg); }
    static void error(const QString &cat, const QString &msg)   { log(Error, cat, msg); }

    // Declared as friend to access private log rotation helpers
    friend void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);

private:
    static void rotateLogs();
    
    static constexpr qint64 MaxLogFileSize = 5 * 1024 * 1024; // 5 MB limit per log file
    static constexpr int    MaxLogFiles    = 5;               // Keep up to 5 historical log files
};

} // namespace LAStudio


