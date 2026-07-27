// SPDX-License-Identifier: Apache-2.0 AND MIT
// SPDX-FileCopyrightText: 2024 Vietnamese Normalizer Contributors

#pragma once

#include <QString>
#include <QStringList>

namespace vietnorm {

struct NormalizationResult {
    QString text;
    QString profileId;
    QString dataVersion;
    QStringList warnings;
    bool changed = false;
};

} // namespace vietnorm
