#include "HistoryRepository.h"

#include "core/Logger.h"
#include "core/PathUtils.h"
#include "audio/WavIO.h"

#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QStringList>
#include <QSet>
#include <QUrl>
#include <algorithm>

namespace LAStudio {

namespace {

constexpr int kHistorySchemaVersion = 1;
constexpr int kMaximumHistoryEntries = 100;

QJsonArray readHistoryArray(const QString &path, bool *readable = nullptr)
{
    if (readable) {
        *readable = true;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (readable && QFileInfo::exists(path)) {
            *readable = false;
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        Logger::warning(QStringLiteral("HistoryRepository"),
                        QStringLiteral("Ignoring malformed history file %1: %2").arg(path, parseError.errorString()));
        if (readable) *readable = false;
        return {};
    }
    // Version 0 was the legacy bare-array format.  Preserve read compatibility
    // and migrate it into the envelope on the next successful write.
    if (document.isArray()) {
        return document.array();
    }
    if (!document.isObject()) {
        if (readable) *readable = false;
        return {};
    }
    const QJsonObject root = document.object();
    const int version = root.value(QStringLiteral("schemaVersion")).toInt();
    if (version > kHistorySchemaVersion) {
        Logger::warning(QStringLiteral("HistoryRepository"),
                        QStringLiteral("History file %1 uses unsupported schema version %2")
                            .arg(path).arg(version));
        if (readable) *readable = false;
        return {};
    }
    return root.value(QStringLiteral("entries")).toArray();
}

bool writeHistoryArrayAtomically(const QString &path, const QJsonArray &entries, QString &errorMsg)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorMsg = QStringLiteral("Failed to open history JSON for safe write: ") + path;
        return false;
    }
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), kHistorySchemaVersion);
    root.insert(QStringLiteral("entries"), entries);
    const QByteArray payload = QJsonDocument(root).toJson();
    if (file.write(payload) != payload.size() || !file.commit()) {
        errorMsg = QStringLiteral("Failed to safely commit history JSON: ") + path;
        return false;
    }
    return true;
}

QStringList trimHistoryEntries(QJsonArray &entries)
{
    QStringList removedFiles;
    while (entries.size() > kMaximumHistoryEntries) {
        const QJsonObject removed = entries.at(entries.size() - 1).toObject();
        entries.removeLast();
        const QString path = PathUtils::urlToLocalPath(removed.value(QStringLiteral("filePath")).toString());
        if (!path.isEmpty()) {
            removedFiles.append(path);
        }
    }
    return removedFiles;
}

void removeFiles(const QStringList &paths)
{
    for (const QString &path : paths) {
        QFile::remove(path);
    }
}

void pruneOrphanedAudio(const QString &audioDir, const QJsonArray &entries)
{
    QSet<QString> referenced;
    for (const QJsonValue &entryValue : entries) {
        const QString path = PathUtils::urlToLocalPath(entryValue.toObject().value(QStringLiteral("filePath")).toString());
        if (!path.isEmpty()) {
            referenced.insert(QFileInfo(path).absoluteFilePath());
        }
    }

    const QDir dir(audioDir);
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.wav")}, QDir::Files);
    for (const QFileInfo &file : files) {
        if (!referenced.contains(file.absoluteFilePath())) {
            QFile::remove(file.absoluteFilePath());
        }
    }
}

} // namespace

QVariantList HistoryRepository::loadTtsHistory(const QString &dataDir)
{
    QVariantList list;
    QString historyPath = dataDir + QStringLiteral("/history/tts_history.json");
    const QJsonArray arr = readHistoryArray(historyPath);
    list.reserve(arr.size());
    for (const QJsonValue &val : arr) {
        if (val.isObject()) {
            list.append(val.toObject().toVariantMap());
        }
    }
    return list;
}

bool HistoryRepository::addTtsHistoryItem(const QString &dataDir,
                                          const QString &text,
                                          const QString &modelName,
                                          const QString &voiceName,
                                          const QVector<float> &samples,
                                          int sampleRate,
                                          QString &errorMsg)
{
    QString historyDir = dataDir + QStringLiteral("/history");
    QString audioDir = historyDir + QStringLiteral("/audio");
    QDir().mkpath(audioDir);

    QString id = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString fileName = id + QStringLiteral(".wav");
    QString filePath = audioDir + QStringLiteral("/") + fileName;

    // Save the WAV file
    bool ok = WavIO::saveFloat(filePath, samples.constData(),
                                samples.size(), sampleRate);
    if (!ok) {
        errorMsg = QStringLiteral("Failed to save history audio file");
        return false;
    }

    // Compute duration
    double seconds = static_cast<double>(samples.size()) / sampleRate;
    QString durationText = QString::number(seconds, 'f', 1) + QStringLiteral("s");

    // Timestamp
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));

    // Create JSON object
    QJsonObject item;
    item[QStringLiteral("id")] = id;
    item[QStringLiteral("text")] = text;
    item[QStringLiteral("timestamp")] = timestamp;
    item[QStringLiteral("filePath")] = QUrl::fromLocalFile(filePath).toString();
    item[QStringLiteral("modelName")] = modelName;
    item[QStringLiteral("voiceName")] = voiceName;
    item[QStringLiteral("durationText")] = durationText;
    item[QStringLiteral("sampleRate")] = sampleRate;

    // Read current JSON list, prepend item
    QString historyPath = historyDir + QStringLiteral("/tts_history.json");
    bool readable = false;
    QJsonArray arr = readHistoryArray(historyPath, &readable);
    if (!readable) {
        QFile::remove(filePath);
        errorMsg = QStringLiteral("History file cannot be safely migrated: ") + historyPath;
        return false;
    }

    arr.prepend(item);
    const QStringList evictedFiles = trimHistoryEntries(arr);

    if (!writeHistoryArrayAtomically(historyPath, arr, errorMsg)) {
        QFile::remove(filePath);
        return false;
    }
    removeFiles(evictedFiles);
    pruneOrphanedAudio(audioDir, arr);
    return true;
}

bool HistoryRepository::deleteTtsHistoryItem(const QString &dataDir, const QString &id, QString &errorMsg)
{
    QString historyDir = dataDir + QStringLiteral("/history");
    QString historyPath = historyDir + QStringLiteral("/tts_history.json");

    bool readable = false;
    QJsonArray arr = readHistoryArray(historyPath, &readable);
    if (!readable) {
        errorMsg = QStringLiteral("History file cannot be safely migrated: ") + historyPath;
        return false;
    }

    QJsonArray newArr;
    QStringList filesToRemove;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        if (obj[QStringLiteral("id")].toString() == id) {
            QString pathUrl = obj[QStringLiteral("filePath")].toString();
            filesToRemove.append(PathUtils::urlToLocalPath(pathUrl));
        } else {
            newArr.append(obj);
        }
    }

    if (!writeHistoryArrayAtomically(historyPath, newArr, errorMsg)) {
        return false;
    }
    removeFiles(filesToRemove);
    pruneOrphanedAudio(historyDir + QStringLiteral("/audio"), newArr);
    return true;
}

bool HistoryRepository::clearTtsHistory(const QString &dataDir, QString &errorMsg)
{
    QString historyDir = dataDir + QStringLiteral("/history");
    QString historyPath = historyDir + QStringLiteral("/tts_history.json");

    bool readable = false;
    QJsonArray arr = readHistoryArray(historyPath, &readable);
    if (!readable) {
        errorMsg = QStringLiteral("History file cannot be safely migrated: ") + historyPath;
        return false;
    }

    QStringList filesToRemove;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        QString pathUrl = obj[QStringLiteral("filePath")].toString();
        filesToRemove.append(PathUtils::urlToLocalPath(pathUrl));
    }

    if (!writeHistoryArrayAtomically(historyPath, QJsonArray(), errorMsg)) {
        return false;
    }
    removeFiles(filesToRemove);
    pruneOrphanedAudio(historyDir + QStringLiteral("/audio"), QJsonArray());
    return true;
}

QVariantList HistoryRepository::loadSttHistory(const QString &dataDir)
{
    QVariantList list;
    QString historyPath = dataDir + QStringLiteral("/history/stt_history.json");
    const QJsonArray arr = readHistoryArray(historyPath);
    list.reserve(arr.size());
    for (const QJsonValue &val : arr) {
        if (val.isObject()) {
            list.append(val.toObject().toVariantMap());
        }
    }
    return list;
}

bool HistoryRepository::addSttHistoryItem(const QString &dataDir,
                                          const QString &text,
                                          const QString &modelName,
                                          const QVector<float> &samples,
                                          QString &errorMsg)
{
    QString historyDir = dataDir + QStringLiteral("/history");
    QString audioDir = historyDir + QStringLiteral("/stt_audio");
    QDir().mkpath(audioDir);

    QString id = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString fileName = id + QStringLiteral(".wav");
    QString filePath = audioDir + QStringLiteral("/") + fileName;

    QVector<int16_t> pcm16(samples.size());
    for (int i = 0; i < samples.size(); ++i) {
        float s = std::clamp(samples[i], -1.0f, 1.0f);
        pcm16[i] = static_cast<int16_t>(s * 32767.0f);
    }

    bool ok = WavIO::savePcm16(filePath, pcm16.constData(), pcm16.size(), 16000, 1);
    if (!ok) {
        errorMsg = QStringLiteral("Failed to save history audio file");
        return false;
    }

    double seconds = static_cast<double>(samples.size()) / 16000.0;

    QString durationText = QString::number(seconds, 'f', 1) + QStringLiteral("s");
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));

    QJsonObject item;
    item[QStringLiteral("id")] = id;
    item[QStringLiteral("text")] = text;
    item[QStringLiteral("timestamp")] = timestamp;
    item[QStringLiteral("filePath")] = QUrl::fromLocalFile(filePath).toString();
    item[QStringLiteral("modelName")] = modelName;
    item[QStringLiteral("durationText")] = durationText;

    QString historyPath = historyDir + QStringLiteral("/stt_history.json");
    bool readable = false;
    QJsonArray arr = readHistoryArray(historyPath, &readable);
    if (!readable) {
        QFile::remove(filePath);
        errorMsg = QStringLiteral("History file cannot be safely migrated: ") + historyPath;
        return false;
    }

    arr.prepend(item);
    const QStringList evictedFiles = trimHistoryEntries(arr);

    if (!writeHistoryArrayAtomically(historyPath, arr, errorMsg)) {
        QFile::remove(filePath);
        return false;
    }
    removeFiles(evictedFiles);
    pruneOrphanedAudio(audioDir, arr);
    return true;
}

bool HistoryRepository::deleteSttHistoryItem(const QString &dataDir, const QString &id, QString &errorMsg)
{
    QString historyDir = dataDir + QStringLiteral("/history");
    QString historyPath = historyDir + QStringLiteral("/stt_history.json");

    bool readable = false;
    QJsonArray arr = readHistoryArray(historyPath, &readable);
    if (!readable) {
        errorMsg = QStringLiteral("History file cannot be safely migrated: ") + historyPath;
        return false;
    }

    QJsonArray newArr;
    QStringList filesToRemove;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        if (obj[QStringLiteral("id")].toString() == id) {
            QString pathUrl = obj[QStringLiteral("filePath")].toString();
            filesToRemove.append(PathUtils::urlToLocalPath(pathUrl));
        } else {
            newArr.append(obj);
        }
    }

    if (!writeHistoryArrayAtomically(historyPath, newArr, errorMsg)) {
        return false;
    }
    removeFiles(filesToRemove);
    pruneOrphanedAudio(historyDir + QStringLiteral("/stt_audio"), newArr);
    return true;
}

bool HistoryRepository::clearSttHistory(const QString &dataDir, QString &errorMsg)
{
    QString historyDir = dataDir + QStringLiteral("/history");
    QString historyPath = historyDir + QStringLiteral("/stt_history.json");

    bool readable = false;
    QJsonArray arr = readHistoryArray(historyPath, &readable);
    if (!readable) {
        errorMsg = QStringLiteral("History file cannot be safely migrated: ") + historyPath;
        return false;
    }

    QStringList filesToRemove;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        QString pathUrl = obj[QStringLiteral("filePath")].toString();
        filesToRemove.append(PathUtils::urlToLocalPath(pathUrl));
    }

    if (!writeHistoryArrayAtomically(historyPath, QJsonArray(), errorMsg)) {
        return false;
    }
    removeFiles(filesToRemove);
    pruneOrphanedAudio(historyDir + QStringLiteral("/stt_audio"), QJsonArray());
    return true;
}

QVariantList HistoryRepository::loadVoiceDesignHistory(const QString &dataDir)
{
    QVariantList list;
    QString historyPath = dataDir + QStringLiteral("/history/voice_design_history.json");
    const QJsonArray arr = readHistoryArray(historyPath);
    list.reserve(arr.size());
    for (const QJsonValue &val : arr) {
        if (val.isObject()) {
            list.append(val.toObject().toVariantMap());
        }
    }
    return list;
}

bool HistoryRepository::addVoiceDesignHistoryItem(const QString &dataDir,
                                                  const QString &text,
                                                  const QString &voiceDescription,
                                                  const QString &presetName,
                                                  const QString &familyId,
                                                  const QString &modelName,
                                                  const QVector<float> &samples,
                                                  int sampleRate,
                                                  QString &errorMsg)
{
    QString historyDir = dataDir + QStringLiteral("/history");
    QString audioDir = historyDir + QStringLiteral("/voice_design_audio");
    QDir().mkpath(audioDir);

    QString id = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString fileName = id + QStringLiteral(".wav");
    QString filePath = audioDir + QStringLiteral("/") + fileName;

    // Save the WAV file
    bool ok = WavIO::saveFloat(filePath, samples.constData(),
                                samples.size(), sampleRate);
    if (!ok) {
        errorMsg = QStringLiteral("Failed to save history audio file");
        return false;
    }

    // Compute duration
    double seconds = static_cast<double>(samples.size()) / sampleRate;
    QString durationText = QString::number(seconds, 'f', 1) + QStringLiteral("s");

    // Timestamp
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));

    // Create JSON object
    QJsonObject item;
    item[QStringLiteral("id")] = id;
    item[QStringLiteral("text")] = text;
    item[QStringLiteral("voiceDescription")] = voiceDescription;
    item[QStringLiteral("presetName")] = presetName;
    item[QStringLiteral("familyId")] = familyId;
    item[QStringLiteral("modelName")] = modelName;
    item[QStringLiteral("filePath")] = QUrl::fromLocalFile(filePath).toString();
    item[QStringLiteral("durationText")] = durationText;
    item[QStringLiteral("sampleRate")] = sampleRate;
    item[QStringLiteral("timestamp")] = timestamp;

    // Read current JSON list, prepend item
    QString historyPath = historyDir + QStringLiteral("/voice_design_history.json");
    bool readable = false;
    QJsonArray arr = readHistoryArray(historyPath, &readable);
    if (!readable) {
        QFile::remove(filePath);
        errorMsg = QStringLiteral("History file cannot be safely migrated: ") + historyPath;
        return false;
    }

    arr.prepend(item);
    const QStringList evictedFiles = trimHistoryEntries(arr);

    if (!writeHistoryArrayAtomically(historyPath, arr, errorMsg)) {
        QFile::remove(filePath);
        return false;
    }
    removeFiles(evictedFiles);
    pruneOrphanedAudio(audioDir, arr);
    return true;
}

bool HistoryRepository::deleteVoiceDesignHistoryItem(const QString &dataDir, const QString &id, QString &errorMsg)
{
    QString historyDir = dataDir + QStringLiteral("/history");
    QString historyPath = historyDir + QStringLiteral("/voice_design_history.json");

    bool readable = false;
    QJsonArray arr = readHistoryArray(historyPath, &readable);
    if (!readable) {
        errorMsg = QStringLiteral("History file cannot be safely migrated: ") + historyPath;
        return false;
    }

    QJsonArray newArr;
    QStringList filesToRemove;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        if (obj[QStringLiteral("id")].toString() == id) {
            QString pathUrl = obj[QStringLiteral("filePath")].toString();
            filesToRemove.append(PathUtils::urlToLocalPath(pathUrl));
        } else {
            newArr.append(obj);
        }
    }

    if (!writeHistoryArrayAtomically(historyPath, newArr, errorMsg)) {
        return false;
    }
    removeFiles(filesToRemove);
    pruneOrphanedAudio(historyDir + QStringLiteral("/voice_design_audio"), newArr);
    return true;
}

bool HistoryRepository::clearVoiceDesignHistory(const QString &dataDir, QString &errorMsg)
{
    QString historyDir = dataDir + QStringLiteral("/history");
    QString historyPath = historyDir + QStringLiteral("/voice_design_history.json");

    bool readable = false;
    QJsonArray arr = readHistoryArray(historyPath, &readable);
    if (!readable) {
        errorMsg = QStringLiteral("History file cannot be safely migrated: ") + historyPath;
        return false;
    }

    QStringList filesToRemove;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        QString pathUrl = obj[QStringLiteral("filePath")].toString();
        filesToRemove.append(PathUtils::urlToLocalPath(pathUrl));
    }

    if (!writeHistoryArrayAtomically(historyPath, QJsonArray(), errorMsg)) {
        return false;
    }
    removeFiles(filesToRemove);
    pruneOrphanedAudio(historyDir + QStringLiteral("/voice_design_audio"), QJsonArray());
    return true;
}

} // namespace LAStudio
