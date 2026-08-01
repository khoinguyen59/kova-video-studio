#pragma once

#include <QObject>

namespace LAStudio {

class TestMediaToolService : public QObject
{
    Q_OBJECT

private slots:
    void rejectsMissingMediaInputsExactlyOnce();
    void passesConfiguredFontDirectoryToBurnInFilter();
    void rendersLineSpacedAssWithStagedFfmpeg();
};

} // namespace LAStudio
