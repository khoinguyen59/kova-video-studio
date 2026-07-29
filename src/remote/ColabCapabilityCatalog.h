#pragma once

#include <QString>
#include <QUrl>
#include <QVariantList>

namespace LAStudio {

// Catalog client for a directly paired, temporary Colab worker only.  It
// intentionally knows nothing about Settings or API Gateway credentials.
class ColabCapabilityCatalog final
{
public:
    struct Result {
        QVariantList models;
        QString error;

        bool isSuccess() const { return error.isEmpty(); }
    };

    static Result fetch(const QUrl &workerUrl, const QString &bearerToken,
                        bool allowInsecureLocalhost = false,
                        int transferTimeoutMs = 20'000);
};

} // namespace LAStudio
