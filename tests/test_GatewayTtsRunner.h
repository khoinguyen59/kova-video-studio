#pragma once

#include <QObject>

namespace LAStudio {

class TestGatewayTtsRunner final : public QObject
{
    Q_OBJECT

private slots:
    void testPostsOpenAiCompatibleSpeechRequest();
};

} // namespace LAStudio
