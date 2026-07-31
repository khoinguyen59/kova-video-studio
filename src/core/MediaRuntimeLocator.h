#pragma once

#include <QString>

namespace LAStudio {

struct MediaRuntimePaths {
    QString ffmpeg;
    QString ffprobe;
    QString ytDlp;

    bool hasFfmpeg() const;
    bool hasFfprobe() const;
    bool hasYtDlp() const;
    bool isComplete() const;
};

// Resolves the media command-line runtime used by Dubbing and audio decoding.
// Release packages place the runtime beside the application in media-tools/;
// explicit environment variables and PATH are compatibility fallbacks.
class MediaRuntimeLocator final {
public:
    static MediaRuntimePaths resolve();
    static MediaRuntimePaths resolveForApplicationDirectory(const QString &applicationDirectory);
};

} // namespace LAStudio
