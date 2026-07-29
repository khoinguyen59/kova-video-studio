#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QIcon>
#include <QFont>
#include <QTimer>

#include <atomic>
#include <cstdio>

#include "lastudio/AppVersion.h"
#include "core/Logger.h"
#include "core/CrashHandler.h"
#include "core/QmlLogger.h"
#include "controllers/app/AppController.h"
#include "controllers/app/StudioSessionViewModel.h"
#include "core/Settings.h"
#include "core/HFHubClient.h"
#include "core/DownloadManager.h"
#include "core/ModelManager.h"
#include "core/CatalogManager.h"
#include "core/VoiceCloningUtils.h"
#include "stt/SttEngine.h"
#include "tts/TtsEngine.h"
#include "audio/AudioRecorder.h"
#include "audio/AudioPlayer.h"
#include "audio/WaveformProvider.h"
#include "core/HardwareManager.h"


#include <QtQml/qqml.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
std::atomic_bool s_qmlSmokeWarning{false};

void qmlSmokeMessageHandler(QtMsgType type, const QMessageLogContext &context,
                            const QString &message)
{
    const QString category = QString::fromLatin1(context.category ? context.category : "");
    const QString file = QString::fromLatin1(context.file ? context.file : "");
    if (type == QtWarningMsg
        && (category.contains(QStringLiteral("qml"), Qt::CaseInsensitive)
            || file.endsWith(QStringLiteral(".qml"), Qt::CaseInsensitive))) {
        s_qmlSmokeWarning.store(true, std::memory_order_relaxed);
    }
    std::fprintf(stderr, "%s\n", qPrintable(message));
}

bool configureHardenedDllSearch()
{
#ifdef Q_OS_WIN
    // Remove the current working directory and PATH from implicit DLL search.
    // Explicit runtime loads must provide their own trusted directory instead.
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

    // Suppress noisy DirectWrite font warnings about legacy raster fonts
    qputenv("QT_LOGGING_RULES", "qt.gui.font.directwrite=false");

    QGuiApplication app(argc, argv);
    const bool qmlSmokeMode = qEnvironmentVariableIntValue("LASTUDIO_QML_SMOKE") == 1;
    if (qmlSmokeMode) {
        qInstallMessageHandler(qmlSmokeMessageHandler);
        QTimer::singleShot(60000, &app, [] { QCoreApplication::exit(3); });
    }
    LAStudio::CrashHandler::initialize();
    
    // Set a modern default font to avoid falling back to legacy fonts
    app.setFont(QFont(QStringLiteral("Segoe UI"), 10));

    app.setOrganizationName(QStringLiteral(""));
    app.setApplicationName(QStringLiteral("LA Studio"));
    app.setApplicationVersion(QString::fromLatin1(LASTUDIO_VERSION)
                              + QString::fromLatin1(LASTUDIO_RELEASE_SUFFIX));
    // Set application window icon from the embedded .ico resource.
    app.setWindowIcon(QIcon(QStringLiteral(":/LAStudio/icons/app_icon.ico")));

    LAStudio::Logger::init();
    LAStudio::Logger::info("App", "Application starting...");

    // Establish the hardware singleton on the GUI thread before QML or worker
    // services can access it. GPU discovery itself remains asynchronous.
    LAStudio::HardwareManager::instance();

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
 

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("LAStudio", "Main");
    LAStudio::Logger::info("App", "QML module loaded.");

    if (qmlSmokeMode) {
        if (engine.rootObjects().isEmpty()) {
            return 1;
        }
        QObject *root = engine.rootObjects().constFirst();
        QTimer::singleShot(0, root, [root] {
            QMetaObject::invokeMethod(root, "startQmlRouteSmoke");
        });
    }

    if (auto *controller = LAStudio::AppController::instance()) {
        controller->localization()->setEngine(&engine);
        engine.addImageProvider(QStringLiteral("waveform"), controller->waveformProvider());
    }

    const int exitCode = app.exec();
    return exitCode == 0 && s_qmlSmokeWarning.load(std::memory_order_relaxed) ? 2 : exitCode;
}
