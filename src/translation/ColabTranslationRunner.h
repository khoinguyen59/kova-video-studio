#pragma once

#include "translation/backends/TranslationBackend.h"

#include <QObject>
#include <QUrl>

#include <memory>

namespace LAStudio {

class ColabTranslationRunner final : public QObject
{
    Q_OBJECT
public:
    explicit ColabTranslationRunner(QObject *parent = nullptr);
    ~ColabTranslationRunner() override;

public slots:
    void translate(const QUrl &workerUrl, const QString &bearerToken, const QString &model,
                   const TranslationInferenceRequest &request, bool allowInsecureLocalhost = false);
    void cancel();

signals:
    void progress(int percent);
    void finished(const QVariantList &patches);
    void failed(const QString &error);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace LAStudio
