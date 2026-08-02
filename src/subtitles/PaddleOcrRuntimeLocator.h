#pragma once

#include <QString>
#include <QStringList>

namespace LAStudio {

// Resolves the isolated, package-provisioned PaddleOCR runtime.  It never
// probes PATH or a machine-wide Python installation: an environment override
// is intentionally explicit for development/diagnostics only.
struct PaddleOcrRuntimeResolution {
    QString pythonPath;
    QString workerPath;
    QString modelCachePath;
    QString manifestPath;
    QString source;

    // This is intentionally a cheap structural/manifest validation.  The
    // worker performs the potentially expensive file and model-tree hashing
    // off the UI thread during its mandatory health check.
    bool isUsable(QString *errorMessage = nullptr) const;
};

class PaddleOcrRuntimeLocator final
{
public:
    static constexpr const char *engineId() { return "paddleocr-ppocrv6-tiny"; }
    static constexpr const char *engineVersion() { return "3.7.0"; }

    static PaddleOcrRuntimeResolution resolve();
    static PaddleOcrRuntimeResolution resolveForApplicationDirectory(const QString &applicationDirectory);
    // The internal candidate bundles only the Chinese profile which has been
    // verified end-to-end.  Other languages remain available through the
    // explicit Tesseract baseline or Direct Colab worker; they must never be
    // presented as ready for the local Paddle bundle.
    static QStringList bundledLanguageCodes();
    static bool supportsBundledLanguage(const QString &language);
    static QString sha256File(const QString &path);
    static QString modelTreeSha256(const QString &cacheRoot);
    static bool hasValidManifest(const PaddleOcrRuntimeResolution &resolution,
                                 QString *errorMessage = nullptr);
};

} // namespace LAStudio
