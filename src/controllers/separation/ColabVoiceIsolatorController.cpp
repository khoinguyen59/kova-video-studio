#include "ColabVoiceIsolatorController.h"

#include "audio/AudioFileDecoder.h"
#include "core/PathUtils.h"
#include "core/Settings.h"
#include "remote/ColabSession.h"
#include "separation/ColabSeparationRunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMetaObject>
#include <QSaveFile>
#include <QUrl>
#include <QtConcurrent>

namespace LAStudio {

ColabVoiceIsolatorController::ColabVoiceIsolatorController(ColabSession *session, Settings *settings, QObject *parent)
    : QObject(parent), m_session(session), m_settings(settings)
{
    qRegisterMetaType<ColabSeparationRequest>("ColabSeparationRequest");
    qRegisterMetaType<ColabSeparationResult>("ColabSeparationResult");
    m_runner = new ColabSeparationRunner;
    m_runner->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_runner, &QObject::deleteLater);
    connect(m_runner, &ColabSeparationRunner::progress, this, &ColabVoiceIsolatorController::onRunnerProgress);
    connect(m_runner, &ColabSeparationRunner::finished, this, &ColabVoiceIsolatorController::onRunnerFinished);
    connect(m_runner, &ColabSeparationRunner::failed, this, &ColabVoiceIsolatorController::onRunnerFailed);
    if (m_session) {
        connect(m_session, &ColabSession::sessionChanged,
                this, &ColabVoiceIsolatorController::onSessionChanged);
        connect(m_session, &ColabSession::verificationFinished, this,
                [this](bool success, const QString &message) {
            if (!m_activateColabWhenVerified) return;
            m_activateColabWhenVerified = false;
            if (success) useColab();
            else setError(message);
        });
    }
    if (m_settings) connect(m_settings, &Settings::remoteFirstModeChanged,
                            this, &ColabVoiceIsolatorController::onRemoteFirstModeChanged);
    m_thread.start();
    onSessionChanged();
}

ColabVoiceIsolatorController::~ColabVoiceIsolatorController()
{
    if (m_cancellation) m_cancellation->store(true, std::memory_order_relaxed);
    if (m_runner && m_thread.isRunning()) QMetaObject::invokeMethod(m_runner, "cancel", Qt::QueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

bool ColabVoiceIsolatorController::colabConnected() const
{
    return m_session && m_session->hasVerifiedRoute(QStringLiteral("voice-isolation"), m_model);
}

QString ColabVoiceIsolatorController::notebookForColabModel(const QString &model) const
{
    const QString normalized = model.trimmed().toLower();
    if (normalized == QStringLiteral("sherpa-onnx-spleeter-2stems-fp16"))
        return QStringLiteral("LA_STUDIO_SEPARATION_SPLEETER_2STEMS_GPU.ipynb");
    if (normalized == QStringLiteral("sherpa-onnx-uvr-vocals-ft"))
        return QStringLiteral("LA_STUDIO_SEPARATION_UVR_VOCALS_GPU.ipynb");
    return {};
}

QString ColabVoiceIsolatorController::colabNotebookFile() const
{
    return notebookForColabModel(m_model);
}

void ColabVoiceIsolatorController::setModel(const QString &model)
{
    const QString normalized = model.trimmed().toLower();
    if (notebookForColabModel(normalized).isEmpty()) {
        setError(QStringLiteral("No Colab notebook is mapped for voice-isolation model '%1'.").arg(model));
        return;
    }
    if (normalized == m_model) return;
    cancel();
    clearResult();
    if (m_session && (m_session->isActive() || m_session->isChecking())) {
        m_colabActive = false;
        m_session->clear();
        emit colabStateChanged();
    }
    m_model = normalized;
    emit modelChanged();
}

bool ColabVoiceIsolatorController::selectColabModel(const QString &model)
{
    const QString normalized = model.trimmed().toLower();
    if (notebookForColabModel(normalized).isEmpty()) {
        setError(QStringLiteral("No Colab notebook is mapped for voice-isolation model '%1'.").arg(model));
        return false;
    }
    setModel(normalized);
    return m_model == normalized;
}

void ColabVoiceIsolatorController::setSourcePath(const QString &path)
{
    const QString normalized = PathUtils::urlToLocalPath(path);
    if (m_sourcePath == normalized) return;
    m_sourcePath = normalized;
    clearResult();
    emit stateChanged();
}

bool ColabVoiceIsolatorController::connectColab(const QString &workerUrl, const QString &bearerToken)
{
    if (!m_session) { setError(QStringLiteral("Colab session is unavailable")); return false; }
    QString error;
    m_activateColabWhenVerified = true;
    if (!m_session->beginVerifiedSession(
            workerUrl, bearerToken, QStringLiteral("voice-isolation"), m_model, &error)) {
        m_activateColabWhenVerified = false;
        setError(error);
        return false;
    }
    return true;
}

void ColabVoiceIsolatorController::useColab()
{
    if (!colabConnected()) { setError(QStringLiteral("Connect a Colab GPU worker before using direct voice isolation")); return; }
    if (!m_colabActive) { m_colabActive = true; emit colabStateChanged(); emit stateChanged(); }
}

void ColabVoiceIsolatorController::useLocal()
{
    if (m_settings && m_settings->remoteFirstMode()) {
        setError(QStringLiteral("Remote-first mode requires a direct Colab voice-isolation worker. Disable Remote-first mode before selecting Local Dev isolation."));
        return;
    }
    if (!m_colabActive) return;
    cancel();
    m_colabActive = false;
    emit colabStateChanged();
    emit stateChanged();
}

void ColabVoiceIsolatorController::isolate(bool)
{
    if (!ready()) { setError(QStringLiteral("Connect and select a direct Colab separation worker first")); return; }
    if (m_processing) return;
    if (m_sourcePath.isEmpty() || !QFileInfo(m_sourcePath).isFile()) { setError(QStringLiteral("Choose an audio or video file first")); return; }
    QString routeError;
    if (!m_session->hasVerifiedRoute(QStringLiteral("voice-isolation"), m_model, &routeError)) {
        setError(routeError);
        return;
    }
    clearResult();
    m_tempDir = std::make_unique<QTemporaryDir>(QDir(QDir::tempPath()).filePath(QStringLiteral("LA-Studio-ColabSeparation-XXXXXX")));
    if (!m_tempDir->isValid()) { setError(QStringLiteral("Failed to create temporary output directory")); return; }
    m_cancellation = std::make_shared<std::atomic_bool>(false);
    m_processing = true;
    m_progress = 0;
    m_warning.clear();
    emit stateChanged();
    ColabSeparationRequest request;
    request.workerUrl = m_session->endpoint();
    request.bearerToken = m_session->bearerTokenForRequest();
    request.audioPath = m_sourcePath;
    request.outputRoot = m_tempDir->path();
    request.model = m_model;
    // This stays false in production.  Carry the ColabSession's explicit
    // loopback-test flag so controller integration tests exercise the same
    // direct-worker boundary without weakening HTTPS validation for users.
    request.allowInsecureLocalhost = m_session->allowsInsecureLocalhostForTests();
    request.cancellation = InferenceCancellationToken(m_cancellation);
    m_activeSessionRevision = m_sessionRevision;
    QMetaObject::invokeMethod(m_runner, "separate", Qt::QueuedConnection,
                              Q_ARG(ColabSeparationRequest, request));
}

void ColabVoiceIsolatorController::cancel()
{
    if (!m_processing) return;
    if (m_cancellation) m_cancellation->store(true, std::memory_order_relaxed);
    m_warning = QStringLiteral("Cancellation requested; direct Colab artifacts will be discarded.");
    emit stateChanged();
    QMetaObject::invokeMethod(m_runner, "cancel", Qt::QueuedConnection);
}

void ColabVoiceIsolatorController::clearResult()
{
    m_vocalsPath.clear(); m_backgroundPath.clear(); m_vocalsSamples.clear(); m_backgroundSamples.clear();
    m_lastError.clear(); m_warning.clear(); m_recentResults.clear();
    emit vocalsSamplesChanged(); emit backgroundSamplesChanged(); emit stateChanged();
}

bool ColabVoiceIsolatorController::exportStem(const QString &sourcePath, const QString &destinationPath)
{
    const QString source = PathUtils::urlToLocalPath(sourcePath);
    const QString target = PathUtils::urlToLocalPath(destinationPath);
    if (!QFileInfo(source).isFile() || target.isEmpty()) return false;
    QDir().mkpath(QFileInfo(target).absolutePath());
    QSaveFile file(target);
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly) || !file.open(QIODevice::WriteOnly)) return false;
    while (!input.atEnd()) { const QByteArray chunk = input.read(1024 * 1024); if (file.write(chunk) != chunk.size()) return false; }
    return file.commit();
}

void ColabVoiceIsolatorController::openRecent(const QString &vocalsPath, const QString &backgroundPath)
{
    m_vocalsPath = vocalsPath; m_backgroundPath = backgroundPath;
    loadSamples(m_vocalsPath, true);
    loadSamples(m_backgroundPath, false);
    emit stateChanged();
}

void ColabVoiceIsolatorController::onSessionChanged()
{
    ++m_sessionRevision;
    if (m_processing) cancel();
    if (m_settings && m_settings->remoteFirstMode() && colabConnected()) {
        m_colabActive = true;
    } else if (!colabConnected()) {
        m_colabActive = false;
    }
    emit colabStateChanged(); emit stateChanged();
}

void ColabVoiceIsolatorController::onRemoteFirstModeChanged()
{
    if (m_settings && m_settings->remoteFirstMode() && colabConnected()) {
        m_colabActive = true;
    }
    emit colabStateChanged(); emit stateChanged();
}

void ColabVoiceIsolatorController::onRunnerProgress(int percent)
{
    m_progress = qBound(0, percent, 100);
    emit stateChanged();
}

void ColabVoiceIsolatorController::onRunnerFinished(const ColabSeparationResult &result)
{
    if (!m_processing || (m_cancellation && m_cancellation->load(std::memory_order_relaxed))) return;
    if (m_activeSessionRevision != m_sessionRevision) {
        m_processing = false;
        m_progress = 0;
        m_warning = QStringLiteral("Colab worker changed; discarded the previous separation result.");
        emit stateChanged();
        return;
    }
    m_processing = false; m_progress = 100; m_lastError.clear();
    m_vocalsPath = result.vocalsPath; m_backgroundPath = result.backgroundPath;
    loadSamples(m_vocalsPath, true);
    loadSamples(m_backgroundPath, false);
    m_recentResults = {QVariantMap{{QStringLiteral("vocalsPath"), m_vocalsPath},
                                   {QStringLiteral("backgroundPath"), m_backgroundPath},
                                   {QStringLiteral("sourceHash"), QStringLiteral("colab:%1").arg(result.jobId)}}};
    emit stateChanged();
}

void ColabVoiceIsolatorController::onRunnerFailed(const QString &error)
{
    const bool wasProcessing = m_processing;
    m_processing = false; m_progress = 0;
    if (m_activeSessionRevision != m_sessionRevision) {
        m_warning = QStringLiteral("Colab worker changed; discarded the previous separation result.");
        if (wasProcessing) emit stateChanged();
        return;
    }
    if (!error.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive)) {
        m_lastError = error;
        emit errorOccurred(error);
    }
    if (wasProcessing) emit stateChanged();
}

void ColabVoiceIsolatorController::loadSamples(const QString &path, bool vocals)
{
    QVariantList *target = vocals ? &m_vocalsSamples : &m_backgroundSamples;
    target->clear();
    if (vocals) emit vocalsSamplesChanged(); else emit backgroundSamplesChanged();

    const QString samplePath = PathUtils::urlToLocalPath(path);
    if (samplePath.isEmpty() || !QFileInfo::exists(samplePath)) return;

    // A valid FLAC contains at least the 4-byte marker and a metadata block
    // header.  Reject a visibly truncated upload before handing it to a media
    // backend which may wait for more bytes.  This protects both manually
    // uploaded stems and the preview UI from malformed files.
    if (samplePath.endsWith(QStringLiteral(".flac"), Qt::CaseInsensitive)) {
        QFile file(samplePath);
        if (!file.open(QIODevice::ReadOnly)) return;
        const QByteArray header = file.read(42);
        if (header.size() < 42 || !header.startsWith("fLaC")) {
            qWarning().noquote() << "[colab-voice-isolator] ignored truncated FLAC waveform:" << samplePath;
            return;
        }
    }

    auto *watcher = new QFutureWatcher<WavIO::WavData>(this);
    connect(watcher, &QFutureWatcher<WavIO::WavData>::finished, this,
            [this, watcher, samplePath, vocals] {
        const WavIO::WavData data = watcher->result();
        watcher->deleteLater();
        if ((vocals ? m_vocalsPath : m_backgroundPath) != samplePath) return;

        QVariantList samples;
        const int step = qMax(1, data.samples.size() / 1000);
        for (int index = 0; index < data.samples.size(); index += step)
            samples.append(data.samples.at(index));
        if (vocals) {
            m_vocalsSamples = std::move(samples);
            emit vocalsSamplesChanged();
        } else {
            m_backgroundSamples = std::move(samples);
            emit backgroundSamplesChanged();
        }
    });
    watcher->setFuture(QtConcurrent::run([samplePath] {
        QString error;
        WavIO::WavData data = AudioFileDecoder::decode(samplePath, &error);
        if (data.samples.isEmpty() && !error.isEmpty())
            qWarning().noquote() << "[colab-voice-isolator] waveform decode failed:" << error;
        return data;
    }));
}

void ColabVoiceIsolatorController::setError(const QString &error)
{
    m_lastError = error;
    emit stateChanged();
    emit errorOccurred(error);
}

} // namespace LAStudio
