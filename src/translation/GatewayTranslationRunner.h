#pragma once

#include "translation/backends/TranslationBackend.h"

#include <QObject>

#include <memory>

namespace LAStudio {

// Runs direct 9Router translation requests. It deliberately bypasses local
// translation runtimes and has no dependency on Colab session state.
class GatewayTranslationRunner final : public QObject
{
    Q_OBJECT
public:
    explicit GatewayTranslationRunner(QObject *parent = nullptr);
    ~GatewayTranslationRunner() override;

public slots:
    void translate(const QString &gatewayUrl, const QString &apiKey, const QString &model,
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
