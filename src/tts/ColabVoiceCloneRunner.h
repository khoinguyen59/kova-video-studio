#pragma once

#include "inference/InferenceCancellation.h"

#include <QObject>
#include <QByteArray>
#include <QUrl>
#include <QVector>

#include <memory>

namespace LAStudio {

struct ColabVoiceCloneRequest {
    QUrl workerUrl;
    QString bearerToken;
    QString model;
    QString referencePath;
    QString referenceName;
    QString referenceText;
    QString text;
    QString language;
    float speed = 1.0F;
    int steps = 32;
    bool consentConfirmed = false;
    QString existingProfileId;
    InferenceCancellationToken cancellation;
    bool allowInsecureLocalhost = false;
};

class ColabVoiceCloneRunner final : public QObject
{
    Q_OBJECT
public:
    explicit ColabVoiceCloneRunner(QObject *parent = nullptr);
    ~ColabVoiceCloneRunner() override;

public slots:
    void clone(const ColabVoiceCloneRequest &request);
    void deleteProfile(const ColabVoiceCloneRequest &request);
    void cancel();

signals:
    void progress(int percent, const QString &stage);
    void profileReady(const QString &profileId);
    void profileDeleted();
    void finished(const QByteArray &pcm16, const QVector<float> &samples, int sampleRate);
    void failed(const QString &error);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace LAStudio

Q_DECLARE_METATYPE(LAStudio::ColabVoiceCloneRequest)
