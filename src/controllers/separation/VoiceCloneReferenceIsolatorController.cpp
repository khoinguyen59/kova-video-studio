#include "controllers/separation/VoiceCloneReferenceIsolatorController.h"

#include "controllers/separation/ColabVoiceIsolatorController.h"
#include "controllers/separation/VoiceIsolatorController.h"
#include "core/PathUtils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace LAStudio {

namespace {

QString cacheRoot()
{
    return PathUtils::cacheDir() + QStringLiteral("/voice-cloning/reference-isolator");
}

QString stableHash(const QByteArray &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}

bool copyAtomically(const QString &source, const QString &destination)
{
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) return false;
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) return false;
    while (!input.atEnd()) {
        const QByteArray bytes = input.read(1024 * 1024);
        if (bytes.isEmpty() && input.error() != QFileDevice::NoError) return false;
        if (!bytes.isEmpty() && output.write(bytes) != bytes.size()) return false;
    }
    return output.commit();
}

} // namespace

VoiceCloneReferenceIsolatorController::VoiceCloneReferenceIsolatorController(
    VoiceIsolatorController *local, ColabVoiceIsolatorController *colab, QObject *parent)
    : QObject(parent), m_local(local), m_colab(colab)
{
    if (m_local) {
        connect(m_local, &VoiceIsolatorController::stateChanged,
                this, [this]() {
            const bool owned = m_ownedRun;
            observeOwnedRun();
            if (!owned) {
                if (!m_resultKey.isEmpty()
                    && m_resultConfigurationKey != configurationFingerprint()) {
                    m_statusText = QStringLiteral("Isolator route or model changed; run again before cloning with Vocals.");
                }
                emit stateChanged();
            }
        });
    }
    if (m_colab) {
        connect(m_colab, &ColabVoiceIsolatorController::stateChanged,
                this, [this]() {
            const bool owned = m_ownedRun;
            observeOwnedRun();
            if (!owned) {
                if (!m_resultKey.isEmpty()
                    && m_resultConfigurationKey != configurationFingerprint()) {
                    m_statusText = QStringLiteral("Isolator route or model changed; run again before cloning with Vocals.");
                }
                emit stateChanged();
            }
        });
        connect(m_colab, &ColabVoiceIsolatorController::colabStateChanged,
                this, [this]() { emit stateChanged(); });
        connect(m_colab, &ColabVoiceIsolatorController::modelChanged,
                this, [this]() { emit stateChanged(); });
    }
}

void VoiceCloneReferenceIsolatorController::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    if (!enabled && m_processing) cancel();
    m_enabled = enabled;
    m_lastError.clear();
    m_statusText = enabled
        ? QStringLiteral("Run Isolator to create a Vocals-only clone reference.")
        : QStringLiteral("Original reference audio will be used for cloning.");
    emit stateChanged();
}

void VoiceCloneReferenceIsolatorController::setSourcePath(const QString &path)
{
    const QString normalized = PathUtils::urlToLocalPath(path).trimmed();
    if (m_sourcePath == normalized) return;
    if (m_processing) cancel();
    m_sourcePath = normalized;
    m_resultKey.clear();
    m_resultConfigurationKey.clear();
    m_vocalsPath.clear();
    m_backgroundPath.clear();
    m_lastError.clear();
    m_progress = 0;
    m_statusText = normalized.isEmpty() ? QString()
        : (m_enabled ? QStringLiteral("Reference changed; previous Vocals cache is not reusable.")
                     : QStringLiteral("Original reference audio selected."));
    emit stateChanged();
}

QString VoiceCloneReferenceIsolatorController::cloneReferencePath() const
{
    // A checked cleanup option is a safety boundary: callers must never
    // silently fall back to the original reference while the required Vocals
    // stem is missing, stale, or unreadable.
    if (!m_enabled) return m_sourcePath;
    return resultReady() ? m_vocalsPath : QString();
}

bool VoiceCloneReferenceIsolatorController::usingColab() const
{
    return m_colab && m_colab->colabActive();
}

QString VoiceCloneReferenceIsolatorController::selectedRoute() const
{
    return usingColab() ? QStringLiteral("Direct Colab GPU") : QStringLiteral("Local CPU");
}

QString VoiceCloneReferenceIsolatorController::selectedModel() const
{
    if (usingColab()) return m_colab ? m_colab->model() : QString();
    return m_local ? m_local->configurationInfo().value(QStringLiteral("model")).toString() : QString();
}

bool VoiceCloneReferenceIsolatorController::routeReady() const
{
    return usingColab() ? (m_colab && m_colab->ready()) : (m_local && m_local->ready());
}

QString VoiceCloneReferenceIsolatorController::sourceFingerprint(QString *error) const
{
    const QFileInfo info(m_sourcePath);
    if (!info.isFile()) {
        if (error) *error = QStringLiteral("Reference audio file was not found.");
        return {};
    }
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Reference audio cannot be read: %1").arg(file.errorString());
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFileDevice::NoError) {
            if (error) *error = QStringLiteral("Reference audio cannot be fingerprinted: %1").arg(file.errorString());
            return {};
        }
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString VoiceCloneReferenceIsolatorController::configurationFingerprint() const
{
    QVariantMap config;
    config.insert(QStringLiteral("route"), usingColab() ? QStringLiteral("colab-direct") : QStringLiteral("local"));
    if (usingColab()) {
        config.insert(QStringLiteral("model"), m_colab ? m_colab->model() : QString());
        config.insert(QStringLiteral("variant"), QStringLiteral("fixed"));
    } else if (m_local) {
        config = m_local->configurationInfo();
        config.insert(QStringLiteral("route"), QStringLiteral("local"));
    }
    return stableHash(QJsonDocument::fromVariant(config).toJson(QJsonDocument::Compact));
}

bool VoiceCloneReferenceIsolatorController::validStem(const QString &path) const
{
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) return false;
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) return false;
    // RIFF/WAV has a 44-byte fixed header while FLAC's STREAMINFO metadata
    // starts at byte 4 and needs only 42 bytes for the lightweight cache
    // validation below.  Read the larger prefix so legacy WAV workers remain
    // valid after adding FLAC support.
    const QByteArray header = file.read(44);
    const QString suffix = info.suffix().toLower();
    // Direct Colab isolation now transfers lossless FLAC by default.  Do not
    // synchronously decode an entire stem merely to decide whether it can be
    // cached: that is wasteful for large files and can freeze the UI on a bad
    // upload.  The exact consumer validates/decodes it when it needs PCM.
    if (suffix == QStringLiteral("flac"))
        return header.size() >= 42 && header.startsWith("fLaC");
    return header.size() >= 44
        && header.startsWith("RIFF")
        && header.mid(8, 4) == QByteArrayLiteral("WAVE");
}

bool VoiceCloneReferenceIsolatorController::loadCachedResult(
    const QString &key, const QString &expectedSourceFingerprint,
    const QString &expectedConfigurationFingerprint)
{
    const QString directory = QDir(cacheRoot()).filePath(key);
    const QString manifestPath = QDir(directory).filePath(QStringLiteral("manifest.json"));
    QFile manifest(manifestPath);
    if (!manifest.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument parsed = QJsonDocument::fromJson(manifest.readAll());
    const QJsonObject object = parsed.object();
    const int version = object.value(QStringLiteral("version")).toInt();
    if ((version != 1 && version != 2)
        || object.value(QStringLiteral("sourceFingerprint")).toString() != expectedSourceFingerprint
        || object.value(QStringLiteral("configurationFingerprint")).toString()
               != expectedConfigurationFingerprint
        || object.value(QStringLiteral("route")).toString()
               != (usingColab() ? QStringLiteral("colab-direct") : QStringLiteral("local"))
        || object.value(QStringLiteral("model")).toString() != selectedModel()) {
        return false;
    }
    const QString vocals = QDir(directory).filePath(object.value(QStringLiteral("vocalsFile")).toString());
    const QString background = QDir(directory).filePath(object.value(QStringLiteral("backgroundFile")).toString());
    if (!validStem(vocals) || !validStem(background)) return false;
    m_resultKey = key;
    m_resultConfigurationKey = expectedConfigurationFingerprint;
    m_vocalsPath = vocals;
    m_backgroundPath = background;
    m_progress = 100;
    m_lastError.clear();
    m_statusText = QStringLiteral("Using cached Vocals and Background stems for the current reference and Isolator configuration.");
    return true;
}

bool VoiceCloneReferenceIsolatorController::persistResult(const QString &key, const QString &vocals,
                                                           const QString &background)
{
    if (!validStem(vocals) || !validStem(background)) return false;
    const QString directory = QDir(cacheRoot()).filePath(key);
    if (!QDir().mkpath(directory)) return false;
    const QString vocalsSuffix = QFileInfo(vocals).suffix().toLower();
    const QString backgroundSuffix = QFileInfo(background).suffix().toLower();
    if ((vocalsSuffix != QStringLiteral("wav") && vocalsSuffix != QStringLiteral("flac"))
        || (backgroundSuffix != QStringLiteral("wav") && backgroundSuffix != QStringLiteral("flac"))) {
        return false;
    }
    const QString vocalsFile = QStringLiteral("vocals.") + vocalsSuffix;
    const QString backgroundFile = QStringLiteral("background.") + backgroundSuffix;
    const QString vocalsDestination = QDir(directory).filePath(vocalsFile);
    const QString backgroundDestination = QDir(directory).filePath(backgroundFile);
    if (!copyAtomically(vocals, vocalsDestination) || !copyAtomically(background, backgroundDestination)) return false;
    const QJsonObject manifest{
        {QStringLiteral("version"), 2},
        {QStringLiteral("sourceFingerprint"), sourceFingerprint()},
        {QStringLiteral("configurationFingerprint"), configurationFingerprint()},
        {QStringLiteral("route"), usingColab() ? QStringLiteral("colab-direct") : QStringLiteral("local")},
        {QStringLiteral("model"), selectedModel()},
        {QStringLiteral("vocalsFile"), vocalsFile},
        {QStringLiteral("backgroundFile"), backgroundFile}
    };
    QSaveFile file(QDir(directory).filePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact)) < 0
        || !file.commit()) return false;
    m_resultKey = key;
    m_resultConfigurationKey = configurationFingerprint();
    m_vocalsPath = vocalsDestination;
    m_backgroundPath = backgroundDestination;
    return true;
}

bool VoiceCloneReferenceIsolatorController::resultReady() const
{
    if (!m_enabled) return !m_sourcePath.isEmpty();
    if (m_resultKey.isEmpty() || m_resultConfigurationKey != configurationFingerprint())
        return false;
    return validStem(m_vocalsPath) && validStem(m_backgroundPath);
}

bool VoiceCloneReferenceIsolatorController::start()
{
    if (!m_enabled) {
        m_lastError = QStringLiteral("Enable 'Clean reference audio with Isolator' before starting reference cleanup.");
        emit stateChanged();
        return false;
    }
    if (m_processing) return false;
    QString error;
    const QString source = sourceFingerprint(&error);
    if (source.isEmpty()) {
        setFailure(error);
        return false;
    }
    if (!routeReady()) {
        setFailure(usingColab()
            ? QStringLiteral("Direct Colab Isolator is not verified for the selected model. Open Isolator setup and run Check.")
            : QStringLiteral("Local Isolator runtime/model is not ready. Configure Isolator before cleaning the reference audio."));
        return false;
    }
    const QString configuration = configurationFingerprint();
    const QString key = stableHash((source + QLatin1Char('|') + configuration).toUtf8());
    if (loadCachedResult(key, source, configuration)) {
        emit stateChanged();
        return true;
    }
    if ((usingColab() && m_colab->processing()) || (!usingColab() && m_local->processing())) {
        setFailure(QStringLiteral("Isolator is already processing another request. Wait for it to finish or cancel it before cleaning this reference."));
        return false;
    }
    m_resultKey.clear();
    m_resultConfigurationKey.clear();
    m_vocalsPath.clear();
    m_backgroundPath.clear();
    m_lastError.clear();
    m_progress = 0;
    // Setting a new source clears the shared Isolator's prior result and
    // emits stateChanged. Do it before claiming this controller owns a run;
    // otherwise that synchronous notification can be mistaken for a finished
    // cleanup and incorrectly reject the still-to-be-started job.
    const bool colab = usingColab();
    if (colab) m_colab->setSourcePath(m_sourcePath);
    else m_local->setSourcePath(m_sourcePath);
    m_ownedColabRun = colab;
    m_statusText = QStringLiteral("Separating Vocals and Background using %1 / %2.").arg(selectedRoute(), selectedModel());
    if (m_ownedColabRun) {
        m_colab->isolate();
    } else {
        m_local->isolate();
    }
    // Both existing controllers synchronously clear their old result before
    // they mark themselves processing. Do not claim ownership until after
    // that reset has completed, otherwise its stateChanged signal can look
    // like a completed reference-cleanup job.
    const bool backendStarted = m_ownedColabRun ? m_colab->processing() : m_local->processing();
    if (!backendStarted) {
        const QString startError = m_ownedColabRun ? m_colab->lastError() : m_local->lastError();
        setFailure(startError.isEmpty()
            ? QStringLiteral("Isolator did not start the requested reference cleanup.")
            : startError);
        return false;
    }
    m_processing = true;
    m_ownedRun = true;
    // A runner can still reject immediately after starting, so observe once
    // after this controller owns the real operation.
    observeOwnedRun();
    emit stateChanged();
    return m_processing || resultReady();
}

void VoiceCloneReferenceIsolatorController::cancel()
{
    if (!m_processing || !m_ownedRun) return;
    if (m_ownedColabRun && m_colab) m_colab->cancel();
    else if (m_local) m_local->cancel();
    m_statusText = QStringLiteral("Cancellation requested; this reference will not be used until valid Vocals are available.");
    emit stateChanged();
}

bool VoiceCloneReferenceIsolatorController::retry()
{
    if (m_processing) return false;
    m_lastError.clear();
    return start();
}

void VoiceCloneReferenceIsolatorController::clearResult()
{
    if (m_processing) cancel();
    m_resultKey.clear();
    m_resultConfigurationKey.clear();
    m_vocalsPath.clear();
    m_backgroundPath.clear();
    m_progress = 0;
    m_lastError.clear();
    m_statusText = m_enabled ? QStringLiteral("No Vocals stem is ready for this reference.") : QString();
    emit stateChanged();
}

void VoiceCloneReferenceIsolatorController::observeOwnedRun()
{
    if (!m_ownedRun) return;
    const bool colab = m_ownedColabRun;
    const bool backendProcessing = colab ? (m_colab && m_colab->processing()) : (m_local && m_local->processing());
    if (backendProcessing) {
        m_processing = true;
        m_progress = colab ? m_colab->progress() : m_local->progress();
        emit stateChanged();
        return;
    }
    finishOwnedRun(colab);
}

void VoiceCloneReferenceIsolatorController::finishOwnedRun(bool colab)
{
    if (!m_ownedRun) return;
    m_ownedRun = false;
    m_processing = false;
    QString error = colab ? (m_colab ? m_colab->lastError() : QString())
                          : (m_local ? m_local->lastError() : QString());
    // Colab cancellation deliberately does not set lastError on its shared
    // controller. Preserve its specific warning here so the reference-cleanup
    // UI can distinguish a user cancellation from unreadable output.
    if (error.isEmpty() && colab && m_colab && !m_colab->warning().isEmpty())
        error = m_colab->warning();
    const QString vocals = colab ? (m_colab ? m_colab->vocalsPath() : QString())
                                 : (m_local ? m_local->vocalsPath() : QString());
    const QString background = colab ? (m_colab ? m_colab->backgroundPath() : QString())
                                     : (m_local ? m_local->backgroundPath() : QString());
    if (!error.isEmpty()) {
        setFailure(error);
        return;
    }
    QString fingerprintError;
    const QString source = sourceFingerprint(&fingerprintError);
    const QString key = source.isEmpty() ? QString()
        : stableHash((source + QLatin1Char('|') + configurationFingerprint()).toUtf8());
    if (key.isEmpty() || !persistResult(key, vocals, background)) {
        setFailure(fingerprintError.isEmpty()
            ? QStringLiteral("Isolator did not return readable Vocals and Background stems; cloning remains blocked.")
            : fingerprintError);
        return;
    }
    m_progress = 100;
    m_lastError.clear();
    m_statusText = QStringLiteral("Vocals stem is ready. Voice Clone will use Vocals only; Original and Background remain preview-only.");
    emit stateChanged();
}

void VoiceCloneReferenceIsolatorController::setFailure(const QString &error)
{
    m_processing = false;
    m_ownedRun = false;
    m_progress = 0;
    m_lastError = error.isEmpty() ? QStringLiteral("Reference Isolator failed without an error message.") : error;
    m_statusText = QStringLiteral("Reference cleanup failed. Retry after fixing the Isolator route or model.");
    emit stateChanged();
}

} // namespace LAStudio
