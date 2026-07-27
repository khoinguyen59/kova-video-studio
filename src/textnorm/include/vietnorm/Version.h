// SPDX-License-Identifier: Apache-2.0 AND MIT
// SPDX-FileCopyrightText: 2024 Vietnamese Normalizer Contributors

#pragma once

#include <QString>

namespace vietnorm {

inline constexpr const char *kVersion = "0.1.0-internal";
inline constexpr const char *kDataVersion = "vietnormalizer-0.2.3";

inline QString version() { return QString::fromLatin1(kVersion); }
inline QString dataVersion() { return QString::fromLatin1(kDataVersion); }

} // namespace vietnorm
