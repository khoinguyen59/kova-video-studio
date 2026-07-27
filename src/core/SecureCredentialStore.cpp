#include "SecureCredentialStore.h"

#include <QByteArray>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace LAStudio::SecureCredentialStore {

namespace {

QString encryptedSettingKey(const QString &secretId)
{
    return QStringLiteral("secrets/") + secretId;
}

QByteArray entropyFor(const QString &secretId)
{
    return QStringLiteral("LA Studio credential/%1/v1").arg(secretId).toUtf8();
}

#ifdef Q_OS_WIN
DATA_BLOB blobFor(QByteArray &data)
{
    DATA_BLOB blob{};
    blob.cbData = static_cast<DWORD>(data.size());
    blob.pbData = reinterpret_cast<BYTE *>(data.data());
    return blob;
}

QString windowsError(const QString &operation)
{
    return QStringLiteral("%1 failed (Windows error %2)").arg(operation).arg(GetLastError());
}
#endif

} // namespace

QString read(QSettings &settings, const QString &secretId, QString *errorMessage)
{
    const QString encoded = settings.value(encryptedSettingKey(secretId)).toString();
    if (encoded.isEmpty()) return {};

#ifdef Q_OS_WIN
    if (!encoded.startsWith(QStringLiteral("dpapi-v1:"))) {
        if (errorMessage) *errorMessage = QStringLiteral("Credential ciphertext uses an unsupported format");
        return {};
    }
    QByteArray encrypted = QByteArray::fromBase64(encoded.mid(9).toLatin1());
    if (encrypted.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Credential ciphertext is invalid");
        return {};
    }
    QByteArray entropy = entropyFor(secretId);
    DATA_BLOB input = blobFor(encrypted);
    DATA_BLOB optionalEntropy = blobFor(entropy);
    DATA_BLOB plaintext{};
    if (!CryptUnprotectData(&input, nullptr, &optionalEntropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &plaintext)) {
        if (errorMessage) *errorMessage = windowsError(QStringLiteral("DPAPI credential decryption"));
        return {};
    }
    const QString secret = QString::fromUtf8(reinterpret_cast<const char *>(plaintext.pbData), plaintext.cbData);
    LocalFree(plaintext.pbData);
    return secret;
#else
    Q_UNUSED(secretId);
    if (errorMessage) *errorMessage = QStringLiteral("Secure credential storage requires Windows DPAPI");
    return {};
#endif
}

bool write(QSettings &settings, const QString &secretId, const QString &secret,
           QString *errorMessage)
{
    const QString key = encryptedSettingKey(secretId);
    if (secret.isEmpty()) {
        settings.remove(key);
        settings.sync();
        return settings.status() == QSettings::NoError;
    }

#ifdef Q_OS_WIN
    QByteArray plaintext = secret.toUtf8();
    QByteArray entropy = entropyFor(secretId);
    DATA_BLOB input = blobFor(plaintext);
    DATA_BLOB optionalEntropy = blobFor(entropy);
    DATA_BLOB encrypted{};
    if (!CryptProtectData(&input, nullptr, &optionalEntropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &encrypted)) {
        if (errorMessage) *errorMessage = windowsError(QStringLiteral("DPAPI credential encryption"));
        return false;
    }
    const QByteArray ciphertext(reinterpret_cast<const char *>(encrypted.pbData), encrypted.cbData);
    LocalFree(encrypted.pbData);
    settings.setValue(key, QStringLiteral("dpapi-v1:") + QString::fromLatin1(ciphertext.toBase64()));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not persist DPAPI credential ciphertext");
        return false;
    }
    return true;
#else
    Q_UNUSED(secretId);
    Q_UNUSED(secret);
    if (errorMessage) *errorMessage = QStringLiteral("Secure credential storage requires Windows DPAPI");
    return false;
#endif
}

QString migrateLegacy(QSettings &settings, const QString &secretId,
                      const QString &legacySettingKey, QString *errorMessage)
{
    const QString encryptedKey = encryptedSettingKey(secretId);
    if (settings.contains(encryptedKey)) {
        return read(settings, secretId, errorMessage);
    }
    if (!settings.contains(legacySettingKey)) return {};

    const QString legacy = settings.value(legacySettingKey).toString().trimmed();
    if (legacy.isEmpty()) {
        settings.remove(legacySettingKey);
        settings.sync();
        return {};
    }
    if (!write(settings, secretId, legacy, errorMessage)) {
        return legacy;
    }
    settings.remove(legacySettingKey);
    settings.sync();
    return legacy;
}

} // namespace LAStudio::SecureCredentialStore
