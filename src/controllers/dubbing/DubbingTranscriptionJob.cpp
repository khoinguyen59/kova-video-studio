#include "controllers/dubbing/DubbingTranscriptionJob.h"

#include "SttSessionController.h"
#include "alignment/ColabAlignmentRunner.h"
#include "controllers/dubbing/DubbingColabModelRoutes.h"
#include "core/Logger.h"
#include "dubbing/AlignmentRefinementService.h"
#include "dubbing/DubbingSegmentNormalizer.h"
#include "remote/ColabSession.h"
#include "remote/ExecutionProvider.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QUuid>
#include <QtConcurrent>
#include <QtMath>

namespace LAStudio {

DubbingTranscriptionJob::DubbingTranscriptionJob(SttSessionController *stt,
                                                 ModelManager *models,
                                                 RuntimeManager *runtimes,
                                                 QObject *parent)
    : QObject(parent), m_stt(stt), m_models(models), m_runtimes(runtimes)
{
    qRegisterMetaType<ColabAlignmentRequest>("ColabAlignmentRequest");
    qRegisterMetaType<ColabAlignmentResult>("ColabAlignmentResult");
    m_alignmentWatcher = new QFutureWatcher<QVariantMap>(this);
    connect(m_alignmentWatcher, &QFutureWatcher<QVariantMap>::finished,
            this, &DubbingTranscriptionJob::onAlignmentFinished);
    m_colabAlignmentRunner = new ColabAlignmentRunner;
    m_colabAlignmentRunner->moveToThread(&m_colabAlignmentThread);
    connect(&m_colabAlignmentThread, &QThread::finished,
            m_colabAlignmentRunner, &QObject::deleteLater);
    connect(m_colabAlignmentRunner, &ColabAlignmentRunner::progress,
            this, [this](int progress) {
        if (m_running)
            emit progressChanged(qBound(80, 80 + progress / 5, 99));
    });
    connect(m_colabAlignmentRunner, &ColabAlignmentRunner::finished,
            this, &DubbingTranscriptionJob::onColabAlignmentFinished);
    connect(m_colabAlignmentRunner, &ColabAlignmentRunner::failed,
            this, &DubbingTranscriptionJob::onColabAlignmentFailed);
    m_colabAlignmentThread.start();
    if (!m_stt) return;
    connect(m_stt, &SttSessionController::transcriptionFinished,
            this, &DubbingTranscriptionJob::onTranscriptionFinished);
    connect(m_stt, &SttSessionController::transcriptionFailed, this,
            [this](const QString &message) {
        if (m_running) fail(message);
    });
    connect(m_stt, &SttSessionController::progressChanged, this, [this]() {
        if (!m_running || m_waitingForInput) return;
        emit progressChanged(qBound(1, m_stt->progress(), 99));
    });
    connect(m_stt, &SttSessionController::inputErrorChanged, this, [this]() {
        if (m_running && !m_stt->inputError().isEmpty()) {
            fail(QStringLiteral("STT input audio could not be decoded: %1")
                     .arg(m_stt->inputError()));
        }
    });
    connect(m_stt, &SttSessionController::inputLoadingChanged, this, [this]() {
        beginTranscriptionAfterInputReady();
    });
}

DubbingTranscriptionJob::~DubbingTranscriptionJob()
{
    cancel();
    if (m_alignmentWatcher) {
        m_alignmentWatcher->cancel();
        m_alignmentWatcher->waitForFinished();
    }
    if (m_colabAlignmentRunner && m_colabAlignmentThread.isRunning())
        QMetaObject::invokeMethod(m_colabAlignmentRunner, "cancel", Qt::QueuedConnection);
    m_colabAlignmentThread.quit();
    m_colabAlignmentThread.wait();
}

void DubbingTranscriptionJob::setAlignmentSession(ColabSession *session)
{
    if (m_alignmentSession == session) return;
    if (m_alignmentSession) disconnect(m_alignmentSession, nullptr, this, nullptr);
    m_alignmentSession = session;
    if (m_alignmentSession) {
        connect(m_alignmentSession, &ColabSession::sessionChanged, this, [this]() {
            if (!m_running || !m_refineAlignmentWithColab
                || m_pendingAlignmentSegments.isEmpty()) return;
            if (m_colabAlignmentCancel)
                m_colabAlignmentCancel->store(true, std::memory_order_relaxed);
            if (m_colabAlignmentRunner)
                QMetaObject::invokeMethod(m_colabAlignmentRunner, "cancel",
                                          Qt::QueuedConnection);
            fail(QStringLiteral("The Colab alignment worker changed during dubbing alignment. Pair it again and rerun the Transcribe node."));
        });
    }
}

bool DubbingTranscriptionJob::start(const QString &language, const QString &audioPath,
                                    const QString &fallbackAudioPath,
                                    const QVariantMap &configuration)
{
    if (m_running || (m_stt && m_stt->processing())) {
        fail(QStringLiteral("Speech transcription is already running."));
        return false;
    }
    if (!m_stt || audioPath.isEmpty()) {
        fail(QStringLiteral("Import media before starting transcription."));
        return false;
    }
    const QVariantMap parameters = configuration.value(QStringLiteral("parameters")).toMap();
    m_executionProviderId = configuration.value(
        QStringLiteral("executionProvider"), parameters.value(
        QStringLiteral("executionProvider"), QStringLiteral("local-dev"))).toString().trimmed().toLower();
    ExecutionProvider provider = ExecutionProvider::LocalDev;
    if (!executionProviderFromId(m_executionProviderId, &provider)) {
        fail(QStringLiteral("Unknown dubbing STT provider: %1").arg(m_executionProviderId));
        return false;
    }
    m_modelId = configuration.value(QStringLiteral("modelId"),
                                    parameters.value(QStringLiteral("modelId"))).toString().trimmed();
    m_refineAlignmentWithColab = parameters.value(
        QStringLiteral("refineAlignmentWithColab"),
        configuration.value(QStringLiteral("refineAlignmentWithColab"), false)).toBool();
    m_alignmentModelId = parameters.value(
        QStringLiteral("alignmentModelId"),
        configuration.value(QStringLiteral("alignmentModelId"),
                            DubbingColabModelRoutes::defaultModelForNode(
                                QStringLiteral("alignment")))).toString().trimmed().toLower();
    if (provider == ExecutionProvider::ApiGateway && m_modelId.isEmpty())
        m_modelId = m_stt->gatewayModel();
    QString availabilityError;
    if (!m_stt->canTranscribeForProvider(provider, m_modelId, &availabilityError)) {
        fail(availabilityError);
        return false;
    }
    ++m_generation;
    m_running = true;
    m_audioPath = audioPath;
    m_fallbackAudioPath = fallbackAudioPath;
    m_retriedWithFallback = false;
    m_inputLoadStarted = false;
    m_language = language;
    emit progressChanged(1);
    startAudioInput(audioPath);
    return true;
}

void DubbingTranscriptionJob::startAudioInput(const QString &audioPath)
{
    m_audioPath = audioPath;
    m_waitingForInput = true;
    m_inputLoadStarted = false;
    emit progressChanged(2);
    m_stt->selectFileInput(audioPath);
    // selectFileInput() first clears the previous input and emits a synchronous
    // "not loading" signal.  Ignore that stale signal until the new selection
    // has been established, otherwise transcription starts with zero samples.
    m_inputLoadStarted = true;
    beginTranscriptionAfterInputReady();
}

void DubbingTranscriptionJob::beginTranscriptionAfterInputReady()
{
    if (!m_running || !m_waitingForInput || !m_inputLoadStarted || !m_stt
        || m_stt->inputLoading() || !m_stt->inputError().isEmpty()) {
        return;
    }

    m_waitingForInput = false;
    m_inputLoadStarted = false;
    emit progressChanged(3);
    ExecutionProvider provider = ExecutionProvider::LocalDev;
    if (!executionProviderFromId(m_executionProviderId, &provider)) {
        fail(QStringLiteral("Unknown dubbing STT provider: %1").arg(m_executionProviderId));
        return;
    }
    m_stt->transcribeInputForProvider(provider, m_modelId, m_language, false);
}

void DubbingTranscriptionJob::cancel()
{
    if (!m_running) return;
    ++m_generation;
    if (m_stt && m_stt->processing()) m_stt->cancelProcessing();
    if (m_alignmentCancel) m_alignmentCancel->storeRelease(true);
    if (m_colabAlignmentCancel)
        m_colabAlignmentCancel->store(true, std::memory_order_relaxed);
    if (m_colabAlignmentRunner && m_colabAlignmentThread.isRunning())
        QMetaObject::invokeMethod(m_colabAlignmentRunner, "cancel", Qt::QueuedConnection);
    m_pendingAlignmentSegments.clear();
    m_waitingForInput = false;
    m_inputLoadStarted = false;
    m_running = false;
}

void DubbingTranscriptionJob::onTranscriptionFinished(const QString &text,
                                                      const QVariantList &segments)
{
    if (!m_running) return;
    QVariantList normalizedInput;
    for (const QVariant &entry : segments) {
        const QVariantMap source = entry.toMap();
        QVariantMap normalized{
            {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("startMs"), qRound64(source.value(QStringLiteral("start")).toDouble() * 1000.0)},
            {QStringLiteral("endMs"), qRound64(source.value(QStringLiteral("end")).toDouble() * 1000.0)},
            {QStringLiteral("sourceText"), source.value(QStringLiteral("text")).toString().trimmed()},
            {QStringLiteral("targetText"), QString()},
            {QStringLiteral("speakerId"), QStringLiteral("speaker-1")},
            {QStringLiteral("timingSource"), QStringLiteral("asr")},
            {QStringLiteral("alignmentStatus"), QStringLiteral("pending")},
            {QStringLiteral("state"), QStringLiteral("transcribed")}};
        QVariantList words;
        for (const QVariant &wordEntry : source.value(QStringLiteral("words")).toList()) {
            const QVariantMap word = wordEntry.toMap();
            const QString wordText = word.value(QStringLiteral("text"),
                                                word.value(QStringLiteral("word"))).toString().trimmed();
            const qint64 startMs = qRound64(word.value(QStringLiteral("start")).toDouble() * 1000.0);
            const qint64 endMs = qRound64(word.value(QStringLiteral("end")).toDouble() * 1000.0);
            if (wordText.isEmpty() || endMs <= startMs) continue;
            words.append(QVariantMap{{QStringLiteral("text"), wordText},
                                     {QStringLiteral("startMs"), startMs},
                                     {QStringLiteral("endMs"), endMs},
                                     {QStringLiteral("confidence"),
                                      word.value(QStringLiteral("confidence"))}});
        }
        if (!words.isEmpty()) normalized.insert(QStringLiteral("words"), words);
        normalizedInput.append(normalized);
    }
    if (normalizedInput.isEmpty()) {
        const bool canRetry = !m_retriedWithFallback
            && !m_fallbackAudioPath.trimmed().isEmpty()
            && QFileInfo(m_fallbackAudioPath).exists()
            && QFileInfo(m_fallbackAudioPath).absoluteFilePath()
                != QFileInfo(m_audioPath).absoluteFilePath();
        if (canRetry) {
            m_retriedWithFallback = true;
            Logger::warning(QStringLiteral("DubbingPipeline"),
                            QStringLiteral("[transcription] no speech found in separated vocals; retrying normalized audio=%1")
                                .arg(m_fallbackAudioPath));
            startAudioInput(m_fallbackAudioPath);
            return;
        }
        fail(text.trimmed().isEmpty()
                 ? QStringLiteral("Whisper found no speech in either the vocals stem or normalized audio.")
                 : QStringLiteral("Whisper returned text without usable timestamp segments."));
        return;
    }
    beginAlignment(normalizedInput);
}

void DubbingTranscriptionJob::beginAlignment(const QVariantList &segments)
{
    ExecutionProvider provider = ExecutionProvider::LocalDev;
    if (!executionProviderFromId(m_executionProviderId, &provider)) {
        fail(QStringLiteral("Unknown dubbing STT provider: %1").arg(m_executionProviderId));
        return;
    }
    if (provider != ExecutionProvider::LocalDev) {
        if (m_refineAlignmentWithColab) {
            startColabAlignment(segments);
            return;
        }
        completeWithoutAlignment(
            segments,
            QStringLiteral("Remote STT timestamps retained; optional Colab forced alignment was not enabled."));
        return;
    }
    if (!m_alignmentWatcher) {
        completeWithoutAlignment(segments,
                                 QStringLiteral("Local alignment service is unavailable."));
        return;
    }
    const quint64 generation = m_generation;
    const QString effectiveLanguage = qEnvironmentVariable(
        "LASTUDIO_DUBBING_LANGUAGE", m_language.isEmpty() ? QStringLiteral("en") : m_language);
    const QString preset = qEnvironmentVariable("LASTUDIO_DUBBING_ALIGNMENT_PRESET",
                                                QStringLiteral("balanced"));
    const AlignmentRefinementConfiguration configuration =
        AlignmentRefinementService::resolveConfiguration(m_models, m_runtimes);
    m_alignmentCancel = std::make_shared<QAtomicInteger<bool>>(false);
    const auto cancel = m_alignmentCancel;
    const QString path = m_audioPath;
    m_alignmentWatcher->setFuture(QtConcurrent::run(
        [path, effectiveLanguage, segments, preset, configuration, cancel, generation]() {
        const AlignmentRefinementResult refined = AlignmentRefinementService::refine(
            path, effectiveLanguage, segments, configuration, preset, cancel.get());
        return QVariantMap{{QStringLiteral("generation"), QVariant::fromValue(generation)},
                           {QStringLiteral("segments"), refined.segments},
                           {QStringLiteral("status"), refined.status},
                           {QStringLiteral("diagnostic"), refined.diagnostic}};
    }));
}

void DubbingTranscriptionJob::startColabAlignment(const QVariantList &segments)
{
    if (!DubbingColabModelRoutes::supports(QStringLiteral("alignment"),
                                           m_alignmentModelId)) {
        fail(QStringLiteral("Select an exact Colab forced-alignment model for the Transcribe node."));
        return;
    }
    if (!m_colabAlignmentRunner) {
        fail(QStringLiteral("The Colab alignment runner is unavailable."));
        return;
    }
    if (!m_alignmentSession) {
        fail(QStringLiteral("Connect the optional Colab alignment worker before running refined dubbing transcription."));
        return;
    }
    QString routeError;
    if (!m_alignmentSession->hasVerifiedRoute(
            QStringLiteral("forced-alignment"), m_alignmentModelId, &routeError)) {
        fail(routeError);
        return;
    }
    QStringList transcriptParts;
    for (const QVariant &entry : segments) {
        const QString text = entry.toMap().value(QStringLiteral("sourceText"))
                                 .toString().trimmed();
        if (!text.isEmpty()) transcriptParts.append(text);
    }
    if (transcriptParts.isEmpty()) {
        fail(QStringLiteral("Dubbing transcription returned no text to align."));
        return;
    }

    m_pendingAlignmentSegments = segments;
    m_colabAlignmentCancel = std::make_shared<std::atomic_bool>(false);
    ColabAlignmentRequest request;
    request.workerUrl = m_alignmentSession->endpoint();
    request.bearerToken = m_alignmentSession->bearerTokenForRequest();
    request.audioPath = m_audioPath;
    request.transcript = transcriptParts.join(QLatin1Char(' '));
    if (m_language.trimmed().isEmpty()
        || m_language.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0) {
        fail(QStringLiteral("Choose the source language before enabling Colab forced alignment in Dubbing."));
        return;
    }
    request.language = m_language.trimmed().toLower();
    request.outputFormat = QStringLiteral("json");
    request.model = m_alignmentModelId;
    request.cancellation = InferenceCancellationToken(m_colabAlignmentCancel);
    emit progressChanged(80);
    QMetaObject::invokeMethod(m_colabAlignmentRunner, "align", Qt::QueuedConnection,
                              Q_ARG(ColabAlignmentRequest, request));
}

QVariantList DubbingTranscriptionJob::applyColabAlignment(
    const QVariantList &segments, const QVariantList &alignedWords) const
{
    if (segments.isEmpty() || alignedWords.isEmpty()) return {};
    QVariantList result;
    qsizetype wordIndex = 0;
    for (qsizetype segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex) {
        QVariantMap segment = segments.at(segmentIndex).toMap();
        const QString sourceText = segment.value(QStringLiteral("sourceText")).toString();
        const int requestedWords = qMax(
            1, sourceText.split(QRegularExpression(QStringLiteral("\\s+")),
                                Qt::SkipEmptyParts).size());
        const qsizetype remainingSegments = segments.size() - segmentIndex;
        const qsizetype remainingWords = alignedWords.size() - wordIndex;
        const qsizetype take = segmentIndex + 1 == segments.size()
            ? remainingWords
            : qMin<qsizetype>(requestedWords,
                              qMax<qsizetype>(1, remainingWords - (remainingSegments - 1)));
        if (take <= 0 || wordIndex >= alignedWords.size()) return {};

        QVariantList words;
        for (qsizetype offset = 0;
             offset < take && wordIndex < alignedWords.size();
             ++offset, ++wordIndex) {
            const QVariantMap aligned = alignedWords.at(wordIndex).toMap();
            const qint64 startMs = qRound64(
                aligned.value(QStringLiteral("start")).toDouble() * 1000.0);
            const qint64 endMs = qRound64(
                aligned.value(QStringLiteral("end")).toDouble() * 1000.0);
            if (endMs <= startMs) return {};
            words.append(QVariantMap{
                {QStringLiteral("text"), aligned.value(QStringLiteral("text"))},
                {QStringLiteral("startMs"), startMs},
                {QStringLiteral("endMs"), endMs},
                {QStringLiteral("confidence"),
                 aligned.value(QStringLiteral("confidence"),
                               aligned.value(QStringLiteral("score")))}
            });
        }
        if (words.isEmpty()) return {};
        segment.insert(QStringLiteral("startMs"),
                       words.constFirst().toMap().value(QStringLiteral("startMs")));
        segment.insert(QStringLiteral("endMs"),
                       words.constLast().toMap().value(QStringLiteral("endMs")));
        segment.insert(QStringLiteral("words"), words);
        segment.insert(QStringLiteral("timingSource"),
                       QStringLiteral("forced-alignment-colab"));
        segment.insert(QStringLiteral("alignmentStatus"), QStringLiteral("aligned"));
        segment.insert(QStringLiteral("alignmentModel"), m_alignmentModelId);
        result.append(segment);
    }
    return DubbingSegmentNormalizer::normalize(result);
}

void DubbingTranscriptionJob::onColabAlignmentFinished(
    const ColabAlignmentResult &result)
{
    if (!m_running || m_pendingAlignmentSegments.isEmpty()) return;
    const QVariantList aligned = applyColabAlignment(m_pendingAlignmentSegments,
                                                     result.segments);
    m_pendingAlignmentSegments.clear();
    m_colabAlignmentCancel.reset();
    if (aligned.isEmpty()) {
        fail(QStringLiteral("Colab alignment could not map timestamps back to the dubbing transcript."));
        return;
    }
    m_running = false;
    emit progressChanged(100);
    emit completed(aligned);
}

void DubbingTranscriptionJob::onColabAlignmentFailed(const QString &message)
{
    if (!m_running || m_pendingAlignmentSegments.isEmpty()) return;
    m_pendingAlignmentSegments.clear();
    m_colabAlignmentCancel.reset();
    if (message.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive)
        && !m_running) return;
    fail(message);
}

void DubbingTranscriptionJob::completeWithoutAlignment(
    const QVariantList &segments, const QString &diagnostic)
{
    QVariantList normalized = DubbingSegmentNormalizer::normalize(segments);
    for (QVariant &entry : normalized) {
        QVariantMap segment = entry.toMap();
        segment.insert(QStringLiteral("alignmentStatus"), QStringLiteral("skipped"));
        segment.insert(QStringLiteral("alignmentDiagnostic"), diagnostic);
        entry = segment;
    }
    m_running = false;
    emit progressChanged(100);
    emit completed(normalized);
}

void DubbingTranscriptionJob::onAlignmentFinished()
{
    const QVariantMap result = m_alignmentWatcher->result();
    if (!m_running || result.value(QStringLiteral("generation")).toULongLong() != m_generation) return;
    const QVariantList segments = DubbingSegmentNormalizer::normalize(
        result.value(QStringLiteral("segments")).toList());
    if (segments.isEmpty()) {
        fail(QStringLiteral("Forced alignment returned no transcript segments."));
        return;
    }
    m_running = false;
    m_alignmentCancel.reset();
    m_pendingAlignmentSegments.clear();
    emit progressChanged(100);
    emit completed(segments);
}

void DubbingTranscriptionJob::fail(const QString &message)
{
    if (!m_running && message.isEmpty()) return;
    m_running = false;
    m_waitingForInput = false;
    m_inputLoadStarted = false;
    m_pendingAlignmentSegments.clear();
    if (m_colabAlignmentCancel)
        m_colabAlignmentCancel->store(true, std::memory_order_relaxed);
    m_colabAlignmentCancel.reset();
    ++m_generation;
    emit failed(message);
}

} // namespace LAStudio
