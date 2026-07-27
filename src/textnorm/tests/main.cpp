// SPDX-License-Identifier: Apache-2.0 AND MIT
// SPDX-FileCopyrightText: 2024 Vietnamese Normalizer Contributors

#include <QCoreApplication>
#include <QtTest>

#include "test_normalizer.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    vietnorm::tests::TestNormalizer suite;
    return QTest::qExec(&suite, argc, argv);
}
