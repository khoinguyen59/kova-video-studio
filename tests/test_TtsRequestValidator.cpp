#include "test_TtsRequestValidator.h"

#include "tts/TtsRequestValidator.h"
#include "tts/TtsSavedVoiceProfile.h"

#include <QtTest>

namespace LAStudio {

namespace {
QVariantList schema()
{
    return {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("speed")},
                    {QStringLiteral("type"), QStringLiteral("float")},
                    {QStringLiteral("min"), 0.5}, {QStringLiteral("max"), 2.0},
                    {QStringLiteral("default"), 1.0}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("speaker")},
                    {QStringLiteral("type"), QStringLiteral("choice")},
                    {QStringLiteral("choices"), QVariantList{QVariantMap{{QStringLiteral("value"), QStringLiteral("a")}},
                                                               QVariantMap{{QStringLiteral("value"), QStringLiteral("b")}}}}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("seed")},
                    {QStringLiteral("type"), QStringLiteral("int")},
                    {QStringLiteral("min"), 0}, {QStringLiteral("max"), 10}}
    };
}
}

void TestTtsRequestValidator::appliesDefaultsAndNormalizesTypes()
{
    QVariantMap normalized;
    QString error;
    const QVariantMap studio{{QStringLiteral("inputs"), QVariantList{QStringLiteral("language"), QStringLiteral("instruct")}},
                             {QStringLiteral("requiredInputs"), QVariantList{QStringLiteral("language"), QStringLiteral("instruct")}}};
    QVERIFY2(TtsRequestValidator::normalize(schema(), studio,
                                             {{QStringLiteral("lang"), QStringLiteral("vi")},
                                              {QStringLiteral("instruct"), QStringLiteral("Speak warmly")},
                                              {QStringLiteral("speaker"), QStringLiteral("b")},
                                              {QStringLiteral("seed"), 4.0}},
                                             normalized, error), qPrintable(error));
    QCOMPARE(normalized.value(QStringLiteral("speed")).toDouble(), 1.0);
    QCOMPARE(normalized.value(QStringLiteral("seed")).toLongLong(), qint64(4));
    QCOMPARE(normalized.value(QStringLiteral("speaker")).toString(), QStringLiteral("b"));
}

void TestTtsRequestValidator::rejectsMissingRequiredInputs()
{
    QVariantMap normalized;
    QString error;
    const QVariantMap studio{{QStringLiteral("inputs"), QVariantList{QStringLiteral("language"), QStringLiteral("instruct")}},
                             {QStringLiteral("requiredInputs"), QVariantList{QStringLiteral("language"), QStringLiteral("instruct")}}};
    QVERIFY(!TtsRequestValidator::normalize(schema(), studio,
                                            {{QStringLiteral("lang"), QStringLiteral("vi")}}, normalized, error));
    QVERIFY(error.contains(QStringLiteral("instruct")));
}

void TestTtsRequestValidator::rejectsUnsupportedAndInvalidSettings()
{
    QVariantMap normalized;
    QString error;
    const QVariantMap studio{{QStringLiteral("inputs"), QVariantList{QStringLiteral("language")}}};
    QVERIFY(!TtsRequestValidator::normalize(schema(), studio,
                                            {{QStringLiteral("lang"), QStringLiteral("vi")},
                                             {QStringLiteral("speed"), 3.0}}, normalized, error));
    QVERIFY(error.contains(QStringLiteral("outside")));
    QVERIFY(!TtsRequestValidator::normalize(schema(), studio,
                                            {{QStringLiteral("lang"), QStringLiteral("vi")},
                                             {QStringLiteral("unknown"), true}}, normalized, error));
    QVERIFY(error.contains(QStringLiteral("Unsupported")));
}

void TestTtsRequestValidator::preservesVerifiedInternalSavedVoiceProfile()
{
    QVariantMap normalized;
    QString error;
    const QVariantMap savedVoice{{QLatin1String(kTtsSavedVoiceId), QStringLiteral("preset-42")},
                                 {QLatin1String(kTtsSavedVoiceReferencePath), QStringLiteral("C:/managed/preset.wav")},
                                 {QLatin1String(kTtsSavedVoiceReferenceText), QStringLiteral("Approved reference")}};
    QVERIFY2(TtsRequestValidator::normalize(schema(), {}, savedVoice, normalized, error),
             qPrintable(error));
    for (auto it = savedVoice.cbegin(); it != savedVoice.cend(); ++it)
        QCOMPARE(normalized.value(it.key()), it.value());
}

void TestTtsRequestValidator::permitsSavedVoiceProfilesOnlyForQwen3Tts()
{
    QVERIFY(localTtsSupportsSavedVoiceProfile(
        {{QStringLiteral("id"), QStringLiteral("qwen3-tts-1.7b-customvoice")}}));
    QVERIFY(!localTtsSupportsSavedVoiceProfile(
        {{QStringLiteral("id"), QStringLiteral("omnivoice")}}));
}

} // namespace LAStudio
