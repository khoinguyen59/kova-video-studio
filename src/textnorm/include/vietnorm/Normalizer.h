// SPDX-License-Identifier: Apache-2.0 AND MIT
// SPDX-FileCopyrightText: 2024 Vietnamese Normalizer Contributors

#pragma once

#include "NormalizationOptions.h"
#include "NormalizationResult.h"

#include <QString>
#include <QStringView>
#include <memory>

namespace vietnorm {

class Normalizer final {
public:
    static std::unique_ptr<Normalizer> create(QString *error = nullptr);
    static std::unique_ptr<Normalizer> fromDataDirectory(const QString &directory,
                                                          QString *error = nullptr);

    ~Normalizer();

    NormalizationResult normalize(QStringView input,
                                  const NormalizationOptions &options = {}) const;

private:
    class Impl;
    explicit Normalizer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace vietnorm
