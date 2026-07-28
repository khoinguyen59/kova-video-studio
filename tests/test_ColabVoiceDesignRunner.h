#pragma once

#include <QObject>

namespace LAStudio {

class TestColabVoiceDesignRunner final : public QObject
{
    Q_OBJECT

private slots:
    void testPostsIndependentVoiceDesignContract();
    void exactModelMappingMatchesCatalogAndNotebooks();
};

} // namespace LAStudio
