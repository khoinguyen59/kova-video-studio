#include "DownloadManager.h"
#include "HFHubClient.h"
#include "Logger.h"
#include "PathUtils.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStorageInfo>

#include <cmath>
#include <limits>

namespace LAStudio {

DownloadManager::DownloadManager(HFHubClient *hub, QObject *parent)
    : QObject(parent)
    , m_hub(hub)
{
    connect(m_hub, &HFHubClient::downloadProgress,
            this, &DownloadManager::onProgress);
    connect(m_hub, &HFHubClient::downloadFinished,
            this, &DownloadManager::onFinished);
    connect(m_hub, &HFHubClient::downloadError,
            this, &DownloadManager::onError);

    loadHistory();
}

QString DownloadManager::makeKey(const QString &modelId, const QString &filename) const
{
    return modelId + QStringLiteral("::") + filename;
}

QVariantList DownloadManager::activeDownloads() const
{
    QVariantList list;
    for (auto it = m_downloads.cbegin(); it != m_downloads.cend(); ++it) {
        if (!it->done) {
            QVariantMap m;
            m[QStringLiteral("identifier")]      = it->identifier;
            m[QStringLiteral("filename")]       = it->filename;
            m[QStringLiteral("bytesReceived")]  = it->bytesReceived;
            m[QStringLiteral("bytesTotal")]     = it->bytesTotal;
            m[QStringLiteral("metadata")]       = it->metadata;
            list.append(m);
        }
    }
    return list;
}

QVariantList DownloadManager::allDownloads() const
{
    QVariantList list;
    for (const QString &key : m_order) {
        if (!m_downloads.contains(key)) continue;
        const auto &it = m_downloads[key];
        QVariantMap m;
        m[QStringLiteral("identifier")]      = it.identifier;
        m[QStringLiteral("filename")]       = it.filename;
        m[QStringLiteral("bytesReceived")]  = it.bytesReceived;
        m[QStringLiteral("bytesTotal")]     = it.bytesTotal;
        m[QStringLiteral("done")]           = it.done;
        m[QStringLiteral("status")]         = it.status;
        m[QStringLiteral("errorMsg")]       = it.errorMsg;
        m[QStringLiteral("localPath")]      = it.localPath;
        m[QStringLiteral("metadata")]       = it.metadata;
        list.append(m);
    }
    return list;
}

bool DownloadManager::isDownloading(const QString &identifier, const QString &filename) const
{
    QString key = makeKey(identifier, filename);
    return m_downloads.contains(key) && !m_downloads[key].done;
}

bool DownloadManager::enqueue(const QString &modelId, const QString &filename,
                              const QString &destDir, const QVariantMap &metadata)
{
    QString key = makeKey(modelId, filename);
    if (m_downloads.contains(key) && !m_downloads[key].done)
        return true; // already downloading

    DownloadEntry e;
    e.identifier = modelId;
    e.filename = filename;
    e.status = QStringLiteral("downloading");
    e.metadata = metadata;
    QString spaceError;
    const qint64 expectedBytes = expectedDownloadBytes(metadata);
    if (expectedBytes > 0 && !hasSpaceForDownload(destDir, filename, expectedBytes, &spaceError)) {
        return rejectForDiskSpace(key, e, spaceError);
    }
    m_downloads[key] = e;

    m_order.removeAll(key);
    m_order.prepend(key);

    saveHistory();

    Logger::info(QStringLiteral("Download"), QStringLiteral("Enqueued HuggingFace file download: %1/%2 to %3").arg(modelId, filename, destDir));

    emit activeDownloadsChanged();
    emit allDownloadsChanged();
    m_hub->downloadFile(modelId, filename, destDir);
    return true;
}

bool DownloadManager::enqueueUrl(const QString &url, const QString &filename,
                                 const QString &destDir, const QVariantMap &metadata)
{
    QString key = makeKey(url, filename);
    if (m_downloads.contains(key) && !m_downloads[key].done)
        return true; // already downloading

    DownloadEntry e;
    e.identifier = url;
    e.filename = filename;
    e.status = QStringLiteral("downloading");
    e.metadata = metadata;
    QString spaceError;
    const qint64 expectedBytes = expectedDownloadBytes(metadata);
    if (expectedBytes > 0 && !hasSpaceForDownload(destDir, filename, expectedBytes, &spaceError)) {
        return rejectForDiskSpace(key, e, spaceError);
    }
    m_downloads[key] = e;

    m_order.removeAll(key);
    m_order.prepend(key);

    saveHistory();

    Logger::info(QStringLiteral("Download"), QStringLiteral("Enqueued URL download: %1 to %2").arg(url, destDir));

    emit activeDownloadsChanged();
    emit allDownloadsChanged();
    m_hub->downloadUrl(url, filename, destDir);
    return true;
}

bool DownloadManager::cancel(const QString &identifier, const QString &filename)
{
    const QString key = makeKey(identifier, filename);
    if (!m_downloads.contains(key) || m_downloads.value(key).done || !m_hub) return false;
    return m_hub->cancelDownload(identifier, filename);
}

qint64 DownloadManager::expectedDownloadBytes(const QVariantMap &metadata)
{
    for (const QString &key : {QStringLiteral("expectedBytes"), QStringLiteral("sizeBytes")}) {
        const QVariant value = metadata.value(key);
        bool ok = false;
        const qint64 bytes = value.toLongLong(&ok);
        if (ok && bytes > 0) return bytes;
    }

    const QString sizeText = metadata.value(QStringLiteral("expectedSize")).toString().trimmed();
    if (sizeText.isEmpty()) return 0;
    bool integerOk = false;
    const qint64 integerBytes = sizeText.toLongLong(&integerOk);
    if (integerOk && integerBytes > 0) return integerBytes;

    const QRegularExpression pattern(
        QStringLiteral(R"(^\s*([0-9]+(?:\.[0-9]+)?)\s*(B|KB|KiB|MB|MiB|GB|GiB)\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(sizeText);
    if (!match.hasMatch()) return 0;

    bool numberOk = false;
    const double amount = match.captured(1).toDouble(&numberOk);
    const QString unit = match.captured(2).toUpper();
    if (!numberOk || amount <= 0.0) return 0;
    double multiplier = 1.0;
    if (unit == QStringLiteral("KB")) multiplier = 1000.0;
    else if (unit == QStringLiteral("KIB")) multiplier = 1024.0;
    else if (unit == QStringLiteral("MB")) multiplier = 1000.0 * 1000.0;
    else if (unit == QStringLiteral("MIB")) multiplier = 1024.0 * 1024.0;
    else if (unit == QStringLiteral("GB")) multiplier = 1000.0 * 1000.0 * 1000.0;
    else if (unit == QStringLiteral("GIB")) multiplier = 1024.0 * 1024.0 * 1024.0;
    const double bytes = amount * multiplier;
    if (bytes > static_cast<double>(std::numeric_limits<qint64>::max())) return 0;
    return static_cast<qint64>(std::ceil(bytes));
}

bool DownloadManager::hasSpaceForDownload(const QString &destDir, const QString &filename,
                                          qint64 expectedBytes, QString *errorMessage)
{
    constexpr qint64 kSafetyMarginBytes = 64LL * 1024 * 1024;
    const qint64 resumeBytes = QFileInfo(QDir(destDir).absoluteFilePath(filename + QStringLiteral(".download"))).size();
    const qint64 remainingBytes = expectedBytes > resumeBytes ? expectedBytes - resumeBytes : expectedBytes;
    if (remainingBytes > std::numeric_limits<qint64>::max() - kSafetyMarginBytes) {
        if (errorMessage) *errorMessage = QStringLiteral("The requested download size is too large to validate safely.");
        return false;
    }

    const QStorageInfo storage(QDir(destDir).absolutePath());
    const qint64 requiredBytes = remainingBytes + kSafetyMarginBytes;
    if (!storage.isReady() || storage.bytesAvailable() < 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not determine free disk space for: %1").arg(destDir);
        return false;
    }
    if (storage.bytesAvailable() < requiredBytes) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Not enough free disk space for this download. Need %1 MiB, but only %2 MiB is available.")
                .arg((requiredBytes + 1024 * 1024 - 1) / (1024 * 1024))
                .arg(storage.bytesAvailable() / (1024 * 1024));
        }
        return false;
    }
    return true;
}

bool DownloadManager::rejectForDiskSpace(const QString &key, const DownloadEntry &entry,
                                         const QString &errorMessage)
{
    DownloadEntry failed = entry;
    failed.done = true;
    failed.status = QStringLiteral("failed");
    failed.errorMsg = errorMessage;
    m_downloads[key] = failed;
    m_order.removeAll(key);
    m_order.prepend(key);
    saveHistory();
    Logger::error(QStringLiteral("Download"), QStringLiteral("Download preflight failed for %1: %2")
                      .arg(entry.filename, errorMessage));
    emit activeDownloadsChanged();
    emit allDownloadsChanged();
    emit error(entry.identifier, entry.filename, errorMessage);
    return false;
}

void DownloadManager::cancelAll()
{
    if (m_hub) {
        for (auto it = m_downloads.cbegin(); it != m_downloads.cend(); ++it) {
            if (!it->done) m_hub->cancelDownload(it->identifier, it->filename);
        }
    }
    m_downloads.clear();
    m_order.clear();
    saveHistory();
    emit activeDownloadsChanged();
    emit allDownloadsChanged();
}

void DownloadManager::clearCompleted()
{
    for (auto it = m_downloads.begin(); it != m_downloads.end(); ) {
        if (it->done) {
            m_order.removeAll(it.key());
            it = m_downloads.erase(it);
        } else {
            ++it;
        }
    }
    saveHistory();
    emit activeDownloadsChanged();
    emit allDownloadsChanged();
}

void DownloadManager::removeDownload(const QString &identifier, const QString &filename)
{
    QString key = makeKey(identifier, filename);
    if (m_downloads.contains(key)) {
        if (!m_downloads.value(key).done && m_hub)
            m_hub->cancelDownload(identifier, filename);
        m_downloads.remove(key);
        m_order.removeAll(key);
        saveHistory();
        emit activeDownloadsChanged();
        emit allDownloadsChanged();
    }
}

void DownloadManager::onProgress(const QString &identifier, const QString &filename,
                                  qint64 bytesReceived, qint64 bytesTotal)
{
    QString key = makeKey(identifier, filename);
    if (!m_downloads.contains(key)) return;

    m_downloads[key].bytesReceived = bytesReceived;
    m_downloads[key].bytesTotal = bytesTotal;
    emit activeDownloadsChanged();
    emit allDownloadsChanged();
}

void DownloadManager::onFinished(const QString &identifier, const QString &filename,
                                  const QString &localPath)
{
    QString key = makeKey(identifier, filename);
    QVariantMap metadata;
    if (m_downloads.contains(key)) {
        m_downloads[key].done = true;
        m_downloads[key].status = QStringLiteral("completed");
        m_downloads[key].localPath = localPath;
        metadata = m_downloads[key].metadata;
    }

    Logger::info(QStringLiteral("Download"), QStringLiteral("Download completed successfully: %1 (Saved to: %2)").arg(filename, localPath));

    saveHistory();
    emit activeDownloadsChanged();
    emit allDownloadsChanged();
    emit finished(identifier, filename, localPath, metadata);
}

void DownloadManager::onError(const QString &identifier, const QString &filename,
                               const QString &errorMsg)
{
    QString key = makeKey(identifier, filename);
    if (m_downloads.contains(key)) {
        m_downloads[key].done = true;
        m_downloads[key].status = QStringLiteral("failed");
        m_downloads[key].errorMsg = errorMsg;
    }

    Logger::error(QStringLiteral("Download"), QStringLiteral("Download failed for %1: %2").arg(filename, errorMsg));

    saveHistory();
    emit activeDownloadsChanged();
    emit allDownloadsChanged();
    emit error(identifier, filename, errorMsg);
}

QString DownloadManager::historyFilePath() const
{
    return PathUtils::dataDir() + QStringLiteral("/downloads_history.json");
}

void DownloadManager::loadHistory()
{
    QFile file(historyFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return;
    }
    QJsonObject root = doc.object();
    QJsonArray orderArr = root.value(QStringLiteral("order")).toArray();
    QJsonObject downloadsObj = root.value(QStringLiteral("downloads")).toObject();

    m_downloads.clear();
    m_order.clear();

    for (const QJsonValue &val : orderArr) {
        m_order.append(val.toString());
    }

    for (auto it = downloadsObj.begin(); it != downloadsObj.end(); ++it) {
        QString key = it.key();
        QJsonObject obj = it.value().toObject();
        DownloadEntry e;
        e.identifier = obj.value(QStringLiteral("identifier")).toString();
        e.filename = obj.value(QStringLiteral("filename")).toString();
        e.bytesReceived = obj.value(QStringLiteral("bytesReceived")).toVariant().toLongLong();
        e.bytesTotal = obj.value(QStringLiteral("bytesTotal")).toVariant().toLongLong();
        e.done = obj.value(QStringLiteral("done")).toBool();
        e.status = obj.value(QStringLiteral("status")).toString();
        e.errorMsg = obj.value(QStringLiteral("errorMsg")).toString();
        e.localPath = obj.value(QStringLiteral("localPath")).toString();
        e.metadata = obj.value(QStringLiteral("metadata")).toObject().toVariantMap();

        // Mark any interrupted downloads as failed
        if (!e.done && e.status == QStringLiteral("downloading")) {
            e.done = true;
            e.status = QStringLiteral("failed");
            e.errorMsg = QStringLiteral("Interrupted");
        }

        m_downloads.insert(key, e);
    }
}

void DownloadManager::saveHistory() const
{
    QString path = historyFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Logger::error(QStringLiteral("Download"), "Failed to write downloads history file: " + path);
        return;
    }

    QJsonObject root;
    QJsonArray orderArr;
    for (const QString &key : m_order) {
        orderArr.append(key);
    }
    root.insert(QStringLiteral("order"), orderArr);

    QJsonObject downloadsObj;
    for (auto it = m_downloads.cbegin(); it != m_downloads.cend(); ++it) {
        QJsonObject obj;
        obj.insert(QStringLiteral("identifier"), it->identifier);
        obj.insert(QStringLiteral("filename"), it->filename);
        obj.insert(QStringLiteral("bytesReceived"), it->bytesReceived);
        obj.insert(QStringLiteral("bytesTotal"), it->bytesTotal);
        obj.insert(QStringLiteral("done"), it->done);
        obj.insert(QStringLiteral("status"), it->status);
        obj.insert(QStringLiteral("errorMsg"), it->errorMsg);
        obj.insert(QStringLiteral("localPath"), it->localPath);
        obj.insert(QStringLiteral("metadata"), QJsonObject::fromVariantMap(it->metadata));
        downloadsObj.insert(it.key(), obj);
    }
    root.insert(QStringLiteral("downloads"), downloadsObj);

    QJsonDocument doc(root);
    file.write(doc.toJson());
    file.close();
}

} // namespace LAStudio

