#pragma once

#include <QObject>

namespace LAStudio {

class TestTtsRequestValidator final : public QObject
{
    Q_OBJECT

private slots:
    void appliesDefaultsAndNormalizesTypes();
    void rejectsMissingRequiredInputs();
    void rejectsUnsupportedAndInvalidSettings();
};

} // namespace LAStudio
