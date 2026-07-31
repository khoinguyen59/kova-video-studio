#pragma once

#include <QVariantList>

namespace LAStudio {

// Deterministic STT/OCR reconciliation.  It never calls a model and never
// discards a conflicting observation: callers receive the evidence required
// for a reviewer to choose the final text.
class DubbingTranscriptFusionService final
{
public:
    static QVariantList normalizeOcrSegments(const QVariantList &ocrSegments);
    static QVariantList fuse(const QVariantList &sttSegments,
                             const QVariantList &ocrSegments);
};

} // namespace LAStudio
