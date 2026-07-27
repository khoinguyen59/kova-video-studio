#pragma once

#include <QObject>

namespace LAStudio {

class TestMediaToolService : public QObject
{
    Q_OBJECT

private slots:
    void rejectsMissingMediaInputsExactlyOnce();
};

} // namespace LAStudio
