#pragma once
#include <QObject>
#include <QString>
#include <QVector>

namespace LAStudio {

class SttAudioDecoder : public QObject {
    Q_OBJECT
public:
    explicit SttAudioDecoder(QObject *parent = nullptr);
    ~SttAudioDecoder() override = default;

    void startDecode(const QString &filePath);

signals:
    void finished(const QVector<float> &samples);
    void errorOccurred(const QString &error);

};

} // namespace LAStudio
