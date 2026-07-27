#pragma once

#include <QObject>

namespace LAStudio {

class TestLlmChatEngine final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsMissingLocalRuntime();
};

} // namespace LAStudio
