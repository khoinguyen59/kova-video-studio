#include "SttAudioDecoder.h"
#include "audio/AudioFileDecoder.h"
#include "audio/WavIO.h"
#include "core/PathUtils.h"
#include "core/Logger.h"
#include <QFileInfo>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QPair>

namespace LAStudio {

SttAudioDecoder::SttAudioDecoder(QObject *parent)
    : QObject(parent)
{
}

void SttAudioDecoder::startDecode(const QString &filePath)
{
    const QString localPath = PathUtils::urlToLocalPath(filePath);
    const QFileInfo fileInfo(localPath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        emit errorOccurred(QStringLiteral("STT input audio was not found: %1").arg(localPath));
        return;
    }
    if (fileInfo.size() <= 0) {
        emit errorOccurred(QStringLiteral("STT input audio is empty: %1").arg(localPath));
        return;
    }

    // QAudioDecoder alone is not reliable for FLAC artifacts created by the
    // Direct-Colab isolator (and sometimes reports finished with zero samples
    // for otherwise valid WAV). AudioFileDecoder preserves the WAV fast-path,
    // tries Qt Multimedia, then uses the staged FFmpeg decoder. Keep all of
    // this off the UI thread so a malformed artifact cannot freeze Dubbing.
    Logger::info("SttAudioDecoder", "Decoding STT input with resilient decoder in worker thread: " + localPath);
    auto *watcher = new QFutureWatcher<QPair<WavIO::WavData, QString>>(this);
    connect(watcher, &QFutureWatcher<QPair<WavIO::WavData, QString>>::finished,
            this, [this, watcher, localPath]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (!result.first.samples.isEmpty()) {
            Logger::info("SttAudioDecoder", "Finished resilient STT decode: " + localPath);
            emit finished(result.first.samples);
            return;
        }
        emit errorOccurred(result.second.isEmpty()
                               ? QStringLiteral("STT input audio could not be decoded: %1").arg(localPath)
                               : result.second);
    });
    watcher->setFuture(QtConcurrent::run([localPath]() {
        QString error;
        const WavIO::WavData data = AudioFileDecoder::decodeMono(localPath, 16000, &error);
        return qMakePair(data, error);
    }));
}

} // namespace LAStudio
