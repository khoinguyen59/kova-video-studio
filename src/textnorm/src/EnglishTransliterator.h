// SPDX-License-Identifier: Apache-2.0 AND MIT
// SPDX-FileCopyrightText: 2024 Vietnamese Normalizer Contributors

#pragma once

#include <QString>

namespace vietnorm::detail {

bool isVietnameseWord(const QString &word);
QString transliterateWord(const QString &word);

} // namespace vietnorm::detail
