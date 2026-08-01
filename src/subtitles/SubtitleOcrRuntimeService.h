#pragma once

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QString>
#include <QVariantList>

#include <QtQml/qqml.h>

namespace LAStudio {

class DownloadManager;

// Owns the explicit, user-initiated installation of the CPU Tesseract runtime
// and its individual language packs.  It never starts a transfer merely from
// page activation or OCR execution.
class SubtitleOcrRuntimeService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SubtitleOcrRuntimeService is managed by AppController")

    Q_PROPERTY(int installState READ installState NOTIFY stateChanged)
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)
    Q_PROPERTY(bool runtimeAvailable READ runtimeAvailable NOTIFY runtimeChanged)
    Q_PROPERTY(QString runtimePath READ runtimePath NOTIFY runtimeChanged)
    Q_PROPERTY(QString runtimeSource READ runtimeSource NOTIFY runtimeChanged)
    Q_PROPERTY(QString runtimeVersion READ runtimeVersion NOTIFY runtimeChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString diagnostics READ diagnostics NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool canCleanFailedDownload READ canCleanFailedDownload NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString managedRuntimePath READ managedRuntimePath CONSTANT)
    Q_PROPERTY(QString managedTessdataPath READ managedTessdataPath CONSTANT)
    Q_PROPERTY(QVariantList languagePacks READ languagePacks NOTIFY languagePacksChanged)
    Q_PROPERTY(qint64 bytesReceived READ bytesReceived NOTIFY progressChanged)
    Q_PROPERTY(qint64 bytesTotal READ bytesTotal NOTIFY progressChanged)
    Q_PROPERTY(bool progressAvailable READ progressAvailable NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)

public:
    enum InstallState {
        Missing = 0,
        Downloading = 1,
        Installing = 2,
        Installed = 3,
        Invalid = 4,
        Failed = 5,
    };
    Q_ENUM(InstallState)

    explicit SubtitleOcrRuntimeService(DownloadManager *downloads, QObject *parent = nullptr);
    ~SubtitleOcrRuntimeService() override;

    int installState() const { return m_installState; }
    QString stateName() const;
    bool runtimeAvailable() const { return m_runtimeValid; }
    QString runtimePath() const { return m_runtimePath; }
    QString runtimeSource() const { return m_runtimeSource; }
    QString runtimeVersion() const { return m_runtimeVersion; }
    QString error() const { return m_error; }
    QString diagnostics() const { return m_diagnostics; }
    bool canCleanFailedDownload() const;
    QString managedRuntimePath() const;
    QString managedTessdataPath() const;
    QVariantList languagePacks() const { return m_languagePacks; }
    qint64 bytesReceived() const { return m_bytesReceived; }
    qint64 bytesTotal() const { return m_bytesTotal; }
    bool progressAvailable() const { return m_bytesTotal > 0; }
    bool busy() const { return m_pendingKind != PendingKind::None; }

    // Immutable release descriptors are intentionally surfaced for package and
    // regression verification.  They must stay pinned to an HTTPS URL and a
    // SHA-256 before a user can initiate installation.
    static QVariantMap runtimeDescriptor();
    static QVariantList languageDescriptors();

    Q_INVOKABLE bool installRuntime();
    Q_INVOKABLE bool installLanguage(const QString &languageCode);
    Q_INVOKABLE bool cancelInstallation();
    Q_INVOKABLE bool retryInstallation();
    Q_INVOKABLE bool cleanFailedDownload();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool isLanguageInstalled(const QString &languageCode) const;

    // Both the preflight and the OCR controller use these exact values for an
    // app-owned runtime.  Do not rely on an inherited TESSDATA_PREFIX: it can
    // point at a system install unrelated to the verified language packs.
    QStringList tesseractDataArguments() const;
    QProcessEnvironment tesseractProcessEnvironment() const;

signals:
    void stateChanged();
    void runtimeChanged();
    void languagePacksChanged();
    void errorChanged();
    void diagnosticsChanged();
    void progressChanged();

private slots:
    void onDownloadFinished(const QString &identifier, const QString &filename,
                            const QString &localPath, const QVariantMap &metadata);
    void onDownloadError(const QString &identifier, const QString &filename,
                         const QString &errorMessage);
    void updateTransferProgress();
    void onInstallerFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onInstallerError(QProcess::ProcessError error);
    void onLanguagePreflightFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onLanguagePreflightError(QProcess::ProcessError error);

private:
    enum class PendingKind { None, Runtime, Language };
    enum class RuntimeProcessPhase { None, Installer, HealthCheck };

    struct Asset {
        QString code;
        QString label;
        QString fileName;
        QString url;
        QString sha256;
        qint64 bytes = 0;
    };

    static QList<Asset> languageAssets();
    static QString sha256File(const QString &path);
    static bool hasValidBundledRuntime(const QString &applicationDirectory,
                                       QString *errorMessage = nullptr);
    static bool replaceFileAtomically(const QString &sourcePath, const QString &destinationPath,
                                      const QString &expectedSha256, QString *errorMessage);
    static bool replaceRuntimeAtomically(const QString &stagingPath, const QString &runtimeRoot,
                                         QString *errorMessage);
    static QSet<QString> parseTesseractLanguages(const QByteArray &output);

    QString runtimeRoot() const;
    QString downloadRoot() const;
    QString manifestPath() const;
    QString languagePath(const QString &languageCode) const;
    QString languageFingerprint() const;
    bool hasValidManagedRuntime(QString *errorMessage = nullptr) const;
    bool hasUsablePackagedRuntime(QString *errorMessage = nullptr) const;
    bool writeInstallationManifest(const QString &installationRoot, QString *errorMessage) const;
    bool beginDownload(PendingKind kind, const Asset &asset);
    void beginInstaller(const QString &installerPath);
    void beginRuntimeHealthCheck();
    void activateVerifiedRuntime();
    void completePending();
    void completeCancelled();
    void fail(const QString &message);
    void setInstallState(int state);
    void setError(const QString &message);
    void appendDiagnostics(const QString &phase, const QString &message);
    QString processDiagnostics(const QString &phase, QProcess::ProcessError error) const;
    bool isManagedDownloadPath(const QString &path) const;
    void rebuildLanguagePacks();
    void resetLanguagePreflight(const QString &fingerprint);
    void beginLanguagePreflight();
    void completeLanguagePreflight(const QSet<QString> &languages, const QString &errorMessage);
    Asset languageAsset(const QString &languageCode) const;

    friend class TestSubtitleOcrRuntimeService;
    friend class TestSubtitleOcrController;

    DownloadManager *m_downloads = nullptr;
    QProcess m_installer;
    QProcess m_languagePreflight;
    int m_installState = Missing;
    QString m_runtimePath;
    QString m_runtimeSource;
    QString m_runtimeVersion;
    bool m_runtimeValid = false;
    QString m_error;
    QVariantList m_languagePacks;
    qint64 m_bytesReceived = 0;
    qint64 m_bytesTotal = 0;
    PendingKind m_pendingKind = PendingKind::None;
    Asset m_pendingAsset;
    QString m_stagingPath;
    bool m_cancelRequested = false;
    QString m_lastAction;
    QString m_lastLanguage;
    RuntimeProcessPhase m_runtimeProcessPhase = RuntimeProcessPhase::None;
    QString m_failedDownloadPath;
    QString m_diagnostics;
    QString m_languagePreflightFingerprint;
    QSet<QString> m_workerLanguages;
    QString m_languagePreflightError;
    QStringList m_languagePreflightExpectedCodes;
    bool m_languagePreflightRunning = false;
    bool m_languagePreflightFinished = false;
};

} // namespace LAStudio
