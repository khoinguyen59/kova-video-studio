#pragma once

#include <QObject>

namespace LAStudio {

class TestStudioCapabilities : public QObject {
    Q_OBJECT

private slots:
    void testForcedAlignmentDescriptor();
    void testForcedAlignmentFamilyMatching();
    void testVoiceIsolationSessionRegistered();
    void testTranslationDescriptorAndSession();
    void testLocalApiRequiresBearerAuthentication();
    void testCredentialStoreMigratesPlaintext();
    void testUpdateVersionPrecedence();
    void testUnsupportedLocalizationFallsBackAndPersists();
};

} // namespace LAStudio
