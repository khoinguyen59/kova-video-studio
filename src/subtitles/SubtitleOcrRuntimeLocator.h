#pragma once

#include <QString>

namespace LAStudio {

struct SubtitleOcrRuntimeResolution {
    QString path;
    // One of managed, environment, bundled, or empty when unavailable.
    QString source;
};

// Tesseract is an optional offline runtime.  The managed copy always lives in
// app-owned data storage; PATH is deliberately not a discovery source because
// an arbitrary system executable would make the selected runtime ambiguous.
class SubtitleOcrRuntimeLocator final
{
public:
    static QString resolveTesseract();
    static QString resolveForApplicationDirectory(const QString &applicationDirectory);
    static SubtitleOcrRuntimeResolution resolve();
    static SubtitleOcrRuntimeResolution resolveForApplicationDirectoryWithSource(
        const QString &applicationDirectory);
    static QString managedRuntimeRoot();
    static QString managedTesseractPath();
};

} // namespace LAStudio
