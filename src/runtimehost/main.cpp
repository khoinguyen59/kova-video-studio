#include "RuntimeHostServer.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QLoggingCategory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace LAStudio;

namespace {
bool configureHardenedDllSearch()
{
#ifdef Q_OS_WIN
    return SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) != 0;
#else
    return true;
#endif
}
} // namespace

int main(int argc, char *argv[])
{
    if (!configureHardenedDllSearch()) {
        return 1;
    }

    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LAStudioRuntimeHost"));

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption socketOption(QStringLiteral("socket"), QStringLiteral("Local socket name."), QStringLiteral("name"));
    parser.addOption(socketOption);
    parser.process(app);

    RuntimeHostServer server(parser.value(socketOption),
                             qEnvironmentVariable("LASTUDIO_RUNTIME_HOST_TOKEN"));
    QString error;
    if (!server.start(&error)) {
        qCritical().noquote() << error;
        return 2;
    }
    return app.exec();
}
