#include "AudioPlayer.h"
#include "AudioFileDecoder.h"
#include "core/Logger.h"
#include "core/PathUtils.h"
#include <QBuffer>
#include <QAudioSink>
#include <QAudioFormat>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMediaDevices>
#include <QtConcurrent>
#include <algorithm>

namespace LAStudio {

class AudioPlaybackSession : public QObject {
    Q_OBJECT
public:
    AudioPlaybackSession(const QByteArray &pcmData, const QAudioFormat &fmt, QObject *parent = nullptr)
        : QObject(parent), m_pcmData(pcmData)
    {
        m_buffer.setData(m_pcmData);
        m_buffer.open(QIODevice::ReadOnly);

        QAudioDevice dev = QMediaDevices::defaultAudioOutput();
        if (!dev.isFormatSupported(fmt)) {
            Logger::warning("AudioPlaybackSession", "Format not supported by default output device");
        }

        m_sink = new QAudioSink(dev, fmt, this);
        connect(m_sink, &QAudioSink::stateChanged, this, &AudioPlaybackSession::onStateChanged);
    }

    void start() {
        m_sink->start(&m_buffer);
    }

    void stop() {
        m_sink->stop();
    }

    void pause() {
        m_sink->suspend();
    }

    void resume() {
        m_sink->resume();
    }

    void seek(qint64 positionMs) {
        const qint64 durationMs = playbackDurationMs();
        const int bytesPerFrame = m_sink ? m_sink->format().bytesPerFrame() : 0;
        const int sampleRate = m_sink ? m_sink->format().sampleRate() : 0;
        if (durationMs <= 0 || bytesPerFrame <= 0 || sampleRate <= 0)
            return;

        const qint64 boundedPosition = std::clamp(positionMs, qint64(0), durationMs);
        const qint64 bytesPerSecond = static_cast<qint64>(bytesPerFrame) * sampleRate;
        const qint64 byteOffset = (boundedPosition * bytesPerSecond / 1000 / bytesPerFrame) * bytesPerFrame;
        const bool remainPaused = m_sink->state() == QAudio::SuspendedState;

        m_sink->reset();
        m_buffer.seek(byteOffset);
        m_playbackOffsetMs = boundedPosition;
        m_sink->start(&m_buffer);
        if (remainPaused)
            m_sink->suspend();
    }

    qint64 playbackPositionMs() const {
        return m_sink ? m_playbackOffsetMs + m_sink->processedUSecs() / 1000 : m_playbackOffsetMs;
    }

    qint64 playbackDurationMs() const {
        const int bytesPerFrame = m_sink ? m_sink->format().bytesPerFrame() : 0;
        const int sampleRate = m_sink ? m_sink->format().sampleRate() : 0;
        if (bytesPerFrame <= 0 || sampleRate <= 0)
            return 0;
        return (static_cast<qint64>(m_pcmData.size()) * 1000) / (bytesPerFrame * sampleRate);
    }

signals:
    void finished();

private slots:
    void onStateChanged(QAudio::State state) {
        if (state == QAudio::IdleState) {
            emit finished();
        }
    }

private:
    QByteArray m_pcmData;
    QBuffer m_buffer;
    QAudioSink* m_sink = nullptr;
    qint64 m_playbackOffsetMs = 0;
};

namespace {

struct AudioDecodeResult {
    WavIO::WavData data;
    QString error;
};

} // namespace

AudioPlayer::AudioPlayer(QObject *parent)
    : QObject(parent)
{
    m_positionTimer.setInterval(80);
    connect(&m_positionTimer, &QTimer::timeout, this, &AudioPlayer::updatePlaybackPosition);
}

qint64 AudioPlayer::playbackPositionMs() const
{
    if (!m_session)
        return 0;
    return std::min(m_session->playbackPositionMs(), m_playbackDurationMs);
}

bool AudioPlayer::playFile(const QString &path)
{
    stop();

    const QString cleanPath = PathUtils::urlToLocalPath(path);
    if (cleanPath.isEmpty() || !QFileInfo::exists(cleanPath)) {
        const QString message = QStringLiteral("Failed to load audio file for playback: %1")
                                    .arg(cleanPath);
        Logger::error("AudioPlayer", message);
        emit errorOccurred(message);
        return false;
    }

    const quint64 requestId = ++m_decodeRequestId;
    m_pendingSeekPositionMs = -1;
    m_pauseWhenReady = false;
    m_playing = true;
    emit playingChanged();
    setLoading(true);
    auto *watcher = new QFutureWatcher<AudioDecodeResult>(this);
    connect(watcher, &QFutureWatcher<AudioDecodeResult>::finished, this,
            [this, watcher, requestId, cleanPath]() {
        const AudioDecodeResult result = watcher->result();
        watcher->deleteLater();
        if (requestId != m_decodeRequestId) return;

        setLoading(false);
        if (result.data.samples.isEmpty()) {
            const QString message = QStringLiteral("Failed to load audio file for playback: %1")
                                        .arg(result.error.isEmpty() ? cleanPath : result.error);
            Logger::error("AudioPlayer", message);
            emit errorOccurred(message);
            resetPlaybackState();
            return;
        }
        startDecodedPlayback(result.data);
    });
    watcher->setFuture(QtConcurrent::run([cleanPath]() {
        AudioDecodeResult result;
        result.data = AudioFileDecoder::decode(cleanPath, &result.error);
        return result;
    }));
    return true;
}

void AudioPlayer::startDecodedPlayback(const WavIO::WavData &data)
{
    QByteArray pcmData;
    pcmData.resize(data.samples.size() * 2);
    auto *out = reinterpret_cast<int16_t *>(pcmData.data());
    for (int i = 0; i < data.samples.size(); ++i) {
        float s = std::clamp(data.samples[i], -1.0f, 1.0f);
        out[i] = static_cast<int16_t>(s * 32767.0f);
    }

    QAudioFormat fmt;
    fmt.setSampleRate(data.sampleRate);
    fmt.setChannelCount(data.channels);
    fmt.setSampleFormat(QAudioFormat::Int16);

    auto *session = new AudioPlaybackSession(pcmData, fmt, this);
    m_session = session;
    m_playbackDurationMs = session->playbackDurationMs();
    emit playbackDurationChanged();

    connect(session, &AudioPlaybackSession::finished, this, [this, session]() {
        if (m_session == session) {
            m_session = nullptr;
            resetPlaybackState();
            emit playbackFinished();
        }
        session->deleteLater();
    });

    m_positionTimer.start();
    session->start();
    if (m_pendingSeekPositionMs >= 0) {
        session->seek(m_pendingSeekPositionMs);
        m_pendingSeekPositionMs = -1;
    }
    if (m_pauseWhenReady) {
        session->pause();
        m_pauseWhenReady = false;
    }
}

void AudioPlayer::playPcm(const QByteArray &pcm16Data, int sampleRate)
{
    stop();

    QAudioFormat fmt;
    fmt.setSampleRate(sampleRate);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);

    auto *session = new AudioPlaybackSession(pcm16Data, fmt, this);
    m_session = session;
    m_playbackDurationMs = session->playbackDurationMs();
    emit playbackDurationChanged();

    connect(session, &AudioPlaybackSession::finished, this, [this, session]() {
        if (m_session == session) {
            m_session = nullptr;
            resetPlaybackState();
            emit playbackFinished();
        }
        session->deleteLater();
    });

    m_playing = true;
    emit playingChanged();
    m_positionTimer.start();
    session->start();
}

void AudioPlayer::pause()
{
    if (m_paused || (!m_session && !m_loading))
        return;

    if (m_loading) {
        m_pauseWhenReady = true;
    } else {
        m_session->pause();
    }
    m_paused = true;
    emit pausedChanged();
    updatePlaybackPosition();
}

void AudioPlayer::resume()
{
    if (!m_paused || (!m_session && !m_loading))
        return;

    if (m_loading) {
        m_pauseWhenReady = false;
    } else {
        m_session->resume();
    }
    m_paused = false;
    emit pausedChanged();
}

void AudioPlayer::seek(qint64 positionMs)
{
    if (m_loading) {
        m_pendingSeekPositionMs = qMax<qint64>(0, positionMs);
        return;
    }
    if (!m_session)
        return;

    m_session->seek(positionMs);
    updatePlaybackPosition();
}

void AudioPlayer::stop()
{
    ++m_decodeRequestId;
    setLoading(false);
    m_pendingSeekPositionMs = -1;
    m_pauseWhenReady = false;
    if (m_session) {
        AudioPlaybackSession *session = m_session.data();
        m_session = nullptr;
        if (session) {
            session->stop();
            session->deleteLater();
        }
    }
    resetPlaybackState();
}

void AudioPlayer::setLoading(bool loading)
{
    if (m_loading == loading) return;
    m_loading = loading;
    emit loadingChanged();
}

void AudioPlayer::updatePlaybackPosition()
{
    if (m_session)
        emit playbackPositionChanged();
}

void AudioPlayer::resetPlaybackState()
{
    m_positionTimer.stop();

    const bool wasPlaying = m_playing;
    const bool wasPaused = m_paused;
    const bool hadDuration = m_playbackDurationMs > 0;
    m_playing = false;
    m_paused = false;
    m_pendingSeekPositionMs = -1;
    m_pauseWhenReady = false;
    m_playbackDurationMs = 0;

    if (wasPlaying)
        emit playingChanged();
    if (wasPaused)
        emit pausedChanged();
    emit playbackPositionChanged();
    if (hadDuration)
        emit playbackDurationChanged();
}

} // namespace LAStudio

#include "AudioPlayer.moc"
