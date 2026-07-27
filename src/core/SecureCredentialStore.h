#pragma once

#include <QSettings>
#include <QString>

namespace LAStudio::SecureCredentialStore {

// Stores a per-user secret with Windows DPAPI. The settings key only contains
// a versioned ciphertext, never the plaintext credential.
QString read(QSettings &settings, const QString &secretId, QString *errorMessage = nullptr);
bool write(QSettings &settings, const QString &secretId, const QString &secret,
           QString *errorMessage = nullptr);

// One-time migration: protect a legacy plaintext setting, then remove it only
// after DPAPI has accepted the ciphertext. A failed migration leaves the legacy
// value in place so the caller does not silently lose a credential.
QString migrateLegacy(QSettings &settings, const QString &secretId,
                      const QString &legacySettingKey, QString *errorMessage = nullptr);

} // namespace LAStudio::SecureCredentialStore
