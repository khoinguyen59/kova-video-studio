#pragma once

#include <QObject>

namespace LAStudio {

class TestMediaIngestService final : public QObject {
    Q_OBJECT

private slots:
    void rejectsMissingInputExactlyOnce();
};

} // namespace LAStudio
