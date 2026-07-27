// SPDX-License-Identifier: Apache-2.0 AND MIT
// SPDX-FileCopyrightText: 2024 Vietnamese Normalizer Contributors

#pragma once

#include <QString>

namespace vietnorm::detail {

class VietnameseTextProcessor final {
public:
    QString process(const QString &input) const;

private:
    QString numberToWords(const QString &digits) const;
    QString processGroup(int value, bool hundreds) const;
};

} // namespace vietnorm::detail
