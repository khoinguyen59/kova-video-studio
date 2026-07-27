#include "test_LlmChatEngine.h"

#include "llm/LlmChatEngine.h"

#include <QByteArray>
#include <QSignalSpy>
#include <QtTest>

namespace LAStudio {
namespace {

class ScopedEnvironmentVariable final
{
public:
    ScopedEnvironmentVariable(const char *name, const QByteArray &value)
        : m_name(name)
        , m_wasSet(qEnvironmentVariableIsSet(name))
        , m_previous(qgetenv(name))
    {
        qputenv(m_name.constData(), value);
    }

    ~ScopedEnvironmentVariable()
    {
        if (m_wasSet) {
            qputenv(m_name.constData(), m_previous);
        } else {
            qunsetenv(m_name.constData());
        }
    }

private:
    QByteArray m_name;
    bool m_wasSet = false;
    QByteArray m_previous;
};

} // namespace

void TestLlmChatEngine::rejectsMissingLocalRuntime()
{
    ScopedEnvironmentVariable hostOverride("LASTUDIO_RUNTIME_HOST", "0");
    LlmChatEngine engine;
    QSignalSpy errors(&engine, &LlmChatEngine::errorOccurred);

    engine.load(QStringLiteral("missing-runtime.dll"),
                QStringLiteral("missing-model.gguf"), false);

    QVERIFY2(errors.wait(5000), "Loading a missing local runtime must fail promptly.");
    QCOMPARE(errors.count(), 1);
    QCOMPARE(engine.state(), LlmChatEngine::Error);
    QVERIFY(!engine.isModelLoaded());
    QVERIFY(!engine.isProcessing());
    QVERIFY(!errors.takeFirst().at(0).toString().isEmpty());
}

} // namespace LAStudio
