#include "ColabAlignmentController.h"

#include "alignment/ColabAlignmentRunner.h"
#include "core/Settings.h"
#include "remote/ColabSession.h"

#include <QMetaObject>

namespace LAStudio {

ColabAlignmentController::ColabAlignmentController(ColabSession *session, Settings *settings, QObject *parent)
    : QObject(parent), m_session(session), m_settings(settings)
{
    qRegisterMetaType<ColabAlignmentRequest>("ColabAlignmentRequest");
    qRegisterMetaType<ColabAlignmentResult>("ColabAlignmentResult");
    m_runner = new ColabAlignmentRunner;
    m_runner->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_runner, &QObject::deleteLater);
    connect(m_runner, &ColabAlignmentRunner::progress,
            this, &ColabAlignmentController::onRunnerProgress);
    connect(m_runner, &ColabAlignmentRunner::finished,
            this, &ColabAlignmentController::onRunnerFinished);
    connect(m_runner, &ColabAlignmentRunner::failed,
            this, &ColabAlignmentController::onRunnerFailed);
    if (m_session) {
        connect(m_session, &ColabSession::sessionChanged,
                this, &ColabAlignmentController::onSessionChanged);
        connect(m_session, &ColabSession::verificationFinished, this,
                [this](bool success, const QString &message) {
            if (!m_activateColabWhenVerified) return;
            m_activateColabWhenVerified = false;
            if (success) useColab();
            else setError(message);
        });
    }
    if (m_settings) {
        connect(m_settings, &Settings::remoteFirstModeChanged,
                this, &ColabAlignmentController::onRemoteFirstModeChanged);
    }
    m_thread.start();
    onSessionChanged();
}

ColabAlignmentController::~ColabAlignmentController()
{
    if (m_cancellation) m_cancellation->store(true, std::memory_order_relaxed);
    if (m_runner && m_thread.isRunning()) {
        QMetaObject::invokeMethod(m_runner, "cancel", Qt::QueuedConnection);
    }
    m_thread.quit();
    m_thread.wait();
}

bool ColabAlignmentController::colabConnected() const
{
    return m_session && m_session->hasVerifiedRoute(QStringLiteral("forced-alignment"), m_model);
}

QString ColabAlignmentController::notebookForColabModel(const QString &model) const
{
    const QString normalized = model.trimmed().toLower();
    if (normalized == QStringLiteral("wav2vec2-aligner-zh"))
        return QStringLiteral("LA_STUDIO_ALIGNMENT_WAV2VEC2_ZH_GPU.ipynb");
    if (normalized == QStringLiteral("canary-ctc-aligner"))
        return QStringLiteral("LA_STUDIO_ALIGNMENT_CANARY_CTC_GPU.ipynb");
    if (normalized == QStringLiteral("mms-forced-aligner-onnx"))
        return QStringLiteral("LA_STUDIO_ALIGNMENT_MMS_ONNX_GPU.ipynb");
    if (normalized == QStringLiteral("qwen3-forced-aligner-0.6b"))
        return QStringLiteral("LA_STUDIO_ALIGNMENT_QWEN3_0_6B_GPU.ipynb");
    return {};
}

QString ColabAlignmentController::colabNotebookFile() const
{
    return notebookForColabModel(m_model);
}

void ColabAlignmentController::setModel(const QString &model)
{
    const QString normalized = model.trimmed().toLower();
    if (notebookForColabModel(normalized).isEmpty()) {
        setError(QStringLiteral("No Colab notebook is mapped for forced-alignment model '%1'.").arg(model));
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

bool ColabAlignmentController::selectColabModel(const QString &model)
{
    const QString normalized = model.trimmed().toLower();
    if (notebookForColabModel(normalized).isEmpty()) {
        setError(QStringLiteral("No Colab notebook is mapped for forced-alignment model '%1'.").arg(model));
        return false;
    }
    setModel(normalized);
    return m_model == normalized;
}

bool ColabAlignmentController::connectColab(const QString &workerUrl, const QString &bearerToken)
{
    if (!m_session) {
        setError(QStringLiteral("Colab session is unavailable"));
        return false;
    }
    QString error;
    m_activateColabWhenVerified = true;
    if (!m_session->beginVerifiedSession(
            workerUrl, bearerToken, QStringLiteral("forced-alignment"), m_model, &error)) {
        m_activateColabWhenVerified = false;
        setError(error);
        return false;
    }
    return true;
}

void ColabAlignmentController::useColab()
{
    if (!colabConnected()) {
        setError(QStringLiteral("Connect a Colab GPU worker before using direct alignment"));
        return;
    }
    if (!m_colabActive) {
        m_colabActive = true;
        m_statusText = QStringLiteral("Colab alignment ready");
        emit colabStateChanged();
        emit stateChanged();
    }
}

void ColabAlignmentController::useLocal()
{
    if (m_settings && m_settings->remoteFirstMode()) {
        setError(QStringLiteral("Remote-first mode requires a direct Colab alignment worker. Disable Remote-first mode before selecting Local Dev alignment."));
        return;
    }
    if (!m_colabActive) return;
    cancel();
    m_colabActive = false;
    m_statusText = QStringLiteral("Local alignment selected");
    emit colabStateChanged();
    emit stateChanged();
}

bool ColabAlignmentController::runAlignment(const QString &audioPath, const QString &transcript,
                                            const QString &language, const QString &outputFormat)
{
    if (!m_colabActive || !colabConnected()) {
        setError(QStringLiteral("Connect and select a Colab worker before starting alignment"));
        return false;
    }
    if (m_processing) return false;
    if (audioPath.trimmed().isEmpty() || transcript.trimmed().isEmpty()) {
        setError(QStringLiteral("Audio and transcript are required for Colab forced alignment"));
        return false;
    }
    QString routeError;
    if (!m_session->hasVerifiedRoute(QStringLiteral("forced-alignment"), m_model, &routeError)) {
        setError(routeError);
        return false;
    }
    clearResult();
    m_cancellation = std::make_shared<std::atomic_bool>(false);
    m_processing = true;
    m_progress = 0;
    m_statusText = QStringLiteral("Uploading audio to the direct Colab worker");
    emit stateChanged();

    ColabAlignmentRequest request;
    request.workerUrl = m_session->endpoint();
    request.bearerToken = m_session->bearerTokenForRequest();
    request.audioPath = audioPath.trimmed();
    request.transcript = transcript.trimmed();
    request.language = language.trimmed().isEmpty() ? QStringLiteral("en") : language.trimmed();
    request.outputFormat = outputFormat.trimmed().isEmpty() ? QStringLiteral("json") : outputFormat.trimmed();
    request.model = m_model;
    request.cancellation = InferenceCancellationToken(m_cancellation);
    m_activeSessionRevision = m_sessionRevision;
    QMetaObject::invokeMethod(m_runner, "align", Qt::QueuedConnection,
                              Q_ARG(ColabAlignmentRequest, request));
    return true;
}

void ColabAlignmentController::cancel()
{
    if (!m_processing) return;
    if (m_cancellation) m_cancellation->store(true, std::memory_order_relaxed);
    m_statusText = QStringLiteral("Cancelling Colab alignment");
    emit stateChanged();
    QMetaObject::invokeMethod(m_runner, "cancel", Qt::QueuedConnection);
}

void ColabAlignmentController::clearResult()
{
    m_errorMessage.clear();
    m_output.clear();
    m_segments.clear();
    m_diagnostics.clear();
    m_duration = 0.0;
    emit resultChanged();
    emit stateChanged();
}

double ColabAlignmentController::averageConfidence() const
{
    if (m_segments.isEmpty()) return 0.0;
    double sum = 0.0;
    for (const QVariant &entry : m_segments)
        sum += entry.toMap().value(QStringLiteral("confidence")).toDouble();
    return sum / static_cast<double>(m_segments.size());
}

QVariantList ColabAlignmentController::karaokeLines() const
{
    QVariantList lines;
    for (const QVariant &entry : m_segments) {
        const QVariantMap word = entry.toMap();
        QVariantMap line;
        line.insert(QStringLiteral("start"), word.value(QStringLiteral("start")));
        line.insert(QStringLiteral("end"), word.value(QStringLiteral("end")));
        line.insert(QStringLiteral("text"), word.value(QStringLiteral("text")));
        line.insert(QStringLiteral("words"), QVariantList{word});
        lines.append(line);
    }
    return lines;
}

int ColabAlignmentController::segmentIndexAt(double seconds) const
{
    for (qsizetype index = 0; index < m_segments.size(); ++index) {
        const QVariantMap segment = m_segments.at(index).toMap();
        if (seconds >= segment.value(QStringLiteral("start")).toDouble()
            && seconds < segment.value(QStringLiteral("end")).toDouble()) return static_cast<int>(index);
    }
    return -1;
}

int ColabAlignmentController::karaokeLineIndexAt(double seconds) const
{
    return segmentIndexAt(seconds);
}

void ColabAlignmentController::onSessionChanged()
{
    ++m_sessionRevision;
    if (m_processing) cancel();
    if (m_settings && m_settings->remoteFirstMode() && colabConnected()) {
        m_colabActive = true;
        m_statusText = QStringLiteral("Colab alignment ready");
    } else if (!colabConnected()) {
        m_colabActive = false;
    }
    if (!m_colabActive) m_statusText = QStringLiteral("Colab worker not connected");
    emit colabStateChanged();
    emit stateChanged();
}

void ColabAlignmentController::onRemoteFirstModeChanged()
{
    if (m_settings && m_settings->remoteFirstMode() && colabConnected()) {
        m_colabActive = true;
        m_statusText = QStringLiteral("Colab alignment ready");
    }
    emit colabStateChanged();
    emit stateChanged();
}

void ColabAlignmentController::onRunnerProgress(int percent)
{
    const int bounded = qBound(0, percent, 100);
    if (m_progress == bounded) return;
    m_progress = bounded;
    m_statusText = bounded >= 90 ? QStringLiteral("Validating direct Colab alignment")
                                 : QStringLiteral("Aligning on the Colab GPU");
    emit stateChanged();
}

void ColabAlignmentController::onRunnerFinished(const ColabAlignmentResult &result)
{
    if (!m_processing || (m_cancellation && m_cancellation->load(std::memory_order_relaxed))) return;
    if (m_activeSessionRevision != m_sessionRevision) {
        m_processing = false;
        m_progress = 0;
        m_statusText = QStringLiteral("Colab worker changed; discarded the previous alignment");
        emit stateChanged();
        return;
    }
    m_processing = false;
    m_progress = 100;
    m_statusText = QStringLiteral("Colab alignment completed");
    m_segments = result.segments;
    m_duration = result.duration;
    m_output = result.output;
    m_diagnostics = result.unalignedTokens;
    emit stateChanged();
    emit resultChanged();
    emit completed();
}

void ColabAlignmentController::onRunnerFailed(const QString &error)
{
    const bool wasProcessing = m_processing;
    m_processing = false;
    m_progress = 0;
    if (m_activeSessionRevision != m_sessionRevision) {
        m_statusText = QStringLiteral("Colab worker changed; discarded the previous alignment");
        if (wasProcessing) emit stateChanged();
        return;
    }
    m_statusText = QStringLiteral("Colab alignment failed");
    if (!error.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive)) {
        m_errorMessage = error;
        emit failed(error);
    }
    if (wasProcessing) emit stateChanged();
}

void ColabAlignmentController::setError(const QString &message)
{
    m_errorMessage = message;
    m_statusText = QStringLiteral("Colab alignment unavailable");
    emit stateChanged();
    emit failed(message);
}

} // namespace LAStudio
