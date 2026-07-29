#include "test_HardwareManager.h"

#include "core/HardwareManager.h"

#include <QtTest>

namespace LAStudio {

void TestHardwareManager::sharesOneInstanceWithQmlFactory()
{
    HardwareManager *const nativeInstance = HardwareManager::instance();

    QCOMPARE(HardwareManager::create(nullptr, nullptr), nativeInstance);
    QCOMPARE(HardwareManager::instance(), nativeInstance);
}

void TestHardwareManager::rejectsRuntimeWithMissingRequiredCpuFeature()
{
    const QVariantMap runtime{
        {QStringLiteral("id"), QStringLiteral("test-cpu-runtime")},
        {QStringLiteral("requiredCpuFeatures"), QVariantList{QStringLiteral("LASTUDIO_TEST_UNSUPPORTED_SIMD")}}
    };

    const QVariantMap compatibility = HardwareManager::instance()->runtimeCompatibility(runtime);

    QCOMPARE(compatibility.value(QStringLiteral("compatible")).toBool(), false);
    QVERIFY(compatibility.value(QStringLiteral("detail")).toString()
                 .contains(QStringLiteral("LASTUDIO_TEST_UNSUPPORTED_SIMD")));
}

} // namespace LAStudio
