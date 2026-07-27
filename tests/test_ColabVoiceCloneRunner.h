#pragma once

#include <QObject>

namespace LAStudio {

class TestColabVoiceCloneRunner final : public QObject
{
    Q_OBJECT

private slots:
    void testRunsVoiceProfileAndGenerationDirectlyOnColab();
    void testRejectsProfileWithoutConsent();
};

} // namespace LAStudio
