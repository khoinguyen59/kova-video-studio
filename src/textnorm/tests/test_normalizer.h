// SPDX-License-Identifier: Apache-2.0 AND MIT
// SPDX-FileCopyrightText: 2024 Vietnamese Normalizer Contributors

#pragma once

#include <QObject>

namespace vietnorm::tests {

class TestNormalizer final : public QObject {
    Q_OBJECT

private slots:
    void normalizesDate();
    void normalizesTime();
    void normalizesCurrency();
    void normalizesPercentage();
    void normalizesStandaloneNumber();
    void normalizesPhoneNumber();
    void normalizesMeasurement();
    void removesUrlsAndSpecialCharacters();
    void preservesVietnameseText();
    void supportsCustomDictionaries();
};

} // namespace vietnorm::tests
