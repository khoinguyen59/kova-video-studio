#pragma once

#include <QVariantList>

namespace LAStudio {

// Deterministic STT/OCR reconciliation.  It never calls a model and never
// discards a conflicting observation: callers receive the evidence required
// for a reviewer to choose the final text.
class DubbingTranscriptFusionService final
{
public:
    // `ask` is intentionally the default.  The deterministic fusion layer
    // must not make a hidden confidence-based choice when observations differ.
    static QString normalizePolicy(const QString &policy);
    static QVariantList normalizeOcrSegments(const QVariantList &ocrSegments);
    static QVariantList fuse(const QVariantList &sttSegments,
                             const QVariantList &ocrSegments,
                             const QString &policy = QStringLiteral("ask"));
};

} // namespace LAStudio
