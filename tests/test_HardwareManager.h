#pragma once

#include <QObject>

namespace LAStudio {

class TestHardwareManager final : public QObject {
    Q_OBJECT

private slots:
    void rejectsRuntimeWithMissingRequiredCpuFeature();
};

} // namespace LAStudio
