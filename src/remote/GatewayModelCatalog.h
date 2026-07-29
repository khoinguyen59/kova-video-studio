#pragma once

#include <QString>
#include <QVariantList>

namespace LAStudio {

// Catalog client for API Gateway only.  It deliberately takes no Colab
// session or worker data, so refreshing Gateway models cannot alter Colab.
class GatewayModelCatalog final
{
public:
    struct Result {
        QVariantList models;
        QString error;

        bool isSuccess() const { return error.isEmpty(); }
    };

    static Result fetch(const QString &gatewayUrl, const QString &apiKey,
                        bool allowInsecureLocalhost = false,
                        int transferTimeoutMs = 20'000);
};

} // namespace LAStudio
