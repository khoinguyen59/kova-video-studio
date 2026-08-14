#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QIcon>
#include <QFont>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUrl>

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

void qmlSmokeMessageObserver(QtMsgType type, const QMessageLogContext &context,
                             const QString &message)
{
    const QString category = QString::fromLatin1(context.category ? context.category : "");
    const QString file = QString::fromLatin1(context.file ? context.file : "");
    if (type == QtWarningMsg
        && (category.contains(QStringLiteral("qml"), Qt::CaseInsensitive)
            || file.endsWith(QStringLiteral(".qml"), Qt::CaseInsensitive))) {
        s_qmlSmokeWarning.store(true, std::memory_order_relaxed);
    }
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
    QObject *qmlSmokeRoot = nullptr;
    if (qmlSmokeMode) {
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
    if (qmlSmokeMode) {
        // Logger::init installs the production Qt handler.  Observe through it
        // instead of replacing it so QML warnings make the smoke process fail.
        LAStudio::Logger::setMessageObserver(qmlSmokeMessageObserver);
    }
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
        qmlSmokeRoot = root;
        // The interaction smoke clicks the production Browse button, then
        // injects this small fixture through the accepted file-picker boundary.
        // It is deliberately created under test data rather than using a user
        // file or running a media/model workload during an offscreen UI test.
        QString smokeDataDir = qEnvironmentVariable("LASTUDIO_DATA_DIR");
        if (smokeDataDir.isEmpty())
            smokeDataDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                + QStringLiteral("/lastudio-qml-smoke");
        QDir().mkpath(smokeDataDir);
        const QString smokeMediaPath = QDir(smokeDataDir).filePath(
            QStringLiteral("dubbing-source-picker-fixture.wav"));
        QFile smokeMedia(smokeMediaPath);
        if (!smokeMedia.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || smokeMedia.write("RIFF\\x24\\x00\\x00\\x00WAVEfmt ", 16) != 16) {
            return 1;
        }
        smokeMedia.close();
        root->setProperty("qmlSmokeMediaPath", smokeMediaPath);
        const QString smokeProjectPath = QDir(smokeDataDir).filePath(
            QStringLiteral("qml-route-smoke.ladub.json"));
        root->setProperty("qmlSmokeProjectUrl",
                          QUrl::fromLocalFile(smokeProjectPath).toString());
        QTimer::singleShot(0, root, [root] {
            QMetaObject::invokeMethod(root, "startQmlRouteSmoke");
        });
    }

    if (auto *controller = LAStudio::AppController::instance()) {
        controller->localization()->setEngine(&engine);
        engine.addImageProvider(QStringLiteral("waveform"), controller->waveformProvider());
    }

    const int exitCode = app.exec();
    int qmlSmokeTraceCount = 0;
    if (qmlSmokeMode && qmlSmokeRoot) {
        QString tracePath = qEnvironmentVariable("LASTUDIO_QML_SMOKE_TRACE");
        if (tracePath.isEmpty())
            tracePath = QDir::current().filePath(QStringLiteral("dubbing-qml-interaction-trace.json"));
        QSaveFile traceFile(tracePath);
        const QVariantList trace = qmlSmokeRoot->property("qmlSmokeDubbingTrace").toList();
        qmlSmokeTraceCount = trace.size();
        if (!traceFile.open(QIODevice::WriteOnly)
            || traceFile.write(QJsonDocument::fromVariant(
                   trace).toJson(
                   QJsonDocument::Indented)) < 0
            || !traceFile.commit()) {
            return 4;
        }
    }
    if (exitCode == 0 && qmlSmokeMode && qmlSmokeRoot
        && qmlSmokeRoot->property("qmlSmokeFailed").toBool()) {
        return 3;
    }
    // A successful QML load is not enough: the production Dubbing route must
    // have exercised every recorded gate/source/stage/Colab/Fix control.
    if (exitCode == 0 && qmlSmokeMode && qmlSmokeTraceCount < 15)
        return 3;
    return exitCode == 0 && s_qmlSmokeWarning.load(std::memory_order_relaxed) ? 2 : exitCode;
}
