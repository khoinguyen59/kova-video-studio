#pragma once

#include <QString>

namespace LAStudio {

// Tesseract is an optional, managed offline runtime.  It is deliberately not
// downloaded on demand: a package may ship it under subtitle-ocr/, or the
// user/admin can explicitly configure a compatible installation.
class SubtitleOcrRuntimeLocator final
{
public:
    static QString resolveTesseract();
    static QString resolveForApplicationDirectory(const QString &applicationDirectory);
};

} // namespace LAStudio
