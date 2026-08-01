#pragma once

#include <QObject>

namespace LAStudio {

class TestColabSubtitleOcrRunner final : public QObject
{
    Q_OBJECT

private slots:
    void postsOnlyCroppedPngToExactDirectWorker();
    void rejectsMalformedOrFailedWorkerResponses();
    void cancellationAbortsTheInFlightCropRequest();
    void retryUsesTheSameExactWorkerContract();
};

} // namespace LAStudio
