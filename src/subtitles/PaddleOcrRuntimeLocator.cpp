#include "subtitles/PaddleOcrRuntimeLocator.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace LAStudio {
namespace {

QString existingFile(const QString &path)
{
    const QFileInfo info(path);
    return info.isFile() ? info.absoluteFilePath() : QString();
}

QString existingDirectory(const QString &path)
{
    const QFileInfo info(path);
    return info.isDir() ? info.absoluteFilePath() : QString();
}

bool containsRequiredModels(const QString &cacheRoot)
{
    const QDir models(QDir(cacheRoot).filePath(QStringLiteral("official_models")));
    return models.exists(QStringLiteral("PP-OCRv6_tiny_det/inference.yml"))
        && models.exists(QStringLiteral("PP-OCRv6_tiny_rec/inference.yml"));
}

bool isSha256(const QString &value)
{
    return QRegularExpression(QStringLiteral("^[a-f0-9]{64}$"))
        .match(value.trimmed().toLower()).hasMatch();
}

bool isSafeRelativePath(const QString &path)
{
    const QString normalized = QDir::cleanPath(path.trimmed()).replace(QLatin1Char('\\'), QLatin1Char('/'));
    return !normalized.isEmpty() && !QDir::isAbsolutePath(normalized)
        && normalized != QStringLiteral("..") && !normalized.startsWith(QStringLiteral("../"));
}

} // namespace

bool PaddleOcrRuntimeResolution::isUsable(QString *errorMessage) const
{
    if (pythonPath.isEmpty() || workerPath.isEmpty() || modelCachePath.isEmpty() || manifestPath.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("PaddleOCR runtime files are incomplete.");
        return false;
    }
    if (!containsRequiredModels(modelCachePath)) {
        if (errorMessage) *errorMessage = QStringLiteral("PaddleOCR required model files are missing.");
        return false;
    }
    return PaddleOcrRuntimeLocator::hasValidManifest(*this, errorMessage);
}

QString PaddleOcrRuntimeLocator::sha256File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray data = file.read(1024 * 1024);
        if (data.isEmpty() && file.error() != QFile::NoError) return {};
        hash.addData(data);
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString PaddleOcrRuntimeLocator::modelTreeSha256(const QString &cacheRoot)
{
    const QDir root(cacheRoot);
    if (!root.exists()) return {};
    QStringList files;
    QDirIterator iterator(root.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QString relative = root.relativeFilePath(iterator.filePath())
            .replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (relative.isEmpty() || relative.startsWith(QStringLiteral("../"))) return {};
        files.append(relative);
    }
    std::sort(files.begin(), files.end());
    QCryptographicHash tree(QCryptographicHash::Sha256);
    const QByteArray separator("\0", 1);
    const QByteArray newline("\n", 1);
    for (const QString &relative : files) {
        const QString fileHash = sha256File(root.filePath(relative));
        if (!isSha256(fileHash)) return {};
        const QByteArray relativeUtf8 = relative.toUtf8();
        const QByteArray hashLatin1 = fileHash.toLatin1();
        tree.addData(QByteArrayView(relativeUtf8));
        tree.addData(QByteArrayView(separator));
        tree.addData(QByteArrayView(hashLatin1));
        tree.addData(QByteArrayView(newline));
    }
    return files.isEmpty() ? QString() : QString::fromLatin1(tree.result().toHex());
}

bool PaddleOcrRuntimeLocator::hasValidManifest(const PaddleOcrRuntimeResolution &resolution,
                                                QString *errorMessage)
{
    QFile file(resolution.manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("PaddleOCR runtime manifest is unreadable.");
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = document.object();
    const QJsonObject engine = root.value(QStringLiteral("engine")).toObject();
    const QJsonObject models = root.value(QStringLiteral("models")).toObject();
    const QJsonObject runtime = root.value(QStringLiteral("runtime")).toObject();
    const QJsonObject worker = root.value(QStringLiteral("worker")).toObject();
    const bool valid = document.isObject() && root.value(QStringLiteral("schemaVersion")).toInt() == 1
        && engine.value(QStringLiteral("id")).toString() == QString::fromLatin1(engineId())
        && engine.value(QStringLiteral("version")).toString() == QString::fromLatin1(engineVersion())
        && engine.value(QStringLiteral("upstreamRepository")).toString()
               == QStringLiteral("https://github.com/PaddlePaddle/PaddleOCR")
        && engine.value(QStringLiteral("upstreamCommit")).toString().size() == 40
        && engine.value(QStringLiteral("license")).toString() == QStringLiteral("Apache-2.0")
        && models.value(QStringLiteral("detection")).toString() == QStringLiteral("PP-OCRv6_tiny_det")
        && models.value(QStringLiteral("recognition")).toString() == QStringLiteral("PP-OCRv6_tiny_rec")
        && isSafeRelativePath(models.value(QStringLiteral("cacheLayout")).toString())
        && isSha256(models.value(QStringLiteral("treeSha256")).toString())
        && runtime.value(QStringLiteral("delivery")).toString() == QStringLiteral("bundled-isolated-python")
        && !runtime.value(QStringLiteral("automaticDownload")).toBool()
        && isSafeRelativePath(runtime.value(QStringLiteral("pythonRelativePath")).toString())
        && isSha256(runtime.value(QStringLiteral("pythonSha256")).toString())
        && worker.value(QStringLiteral("relativePath")).toString() == QStringLiteral("paddle_ocr_worker.py")
        && isSha256(worker.value(QStringLiteral("sha256")).toString());
    if (!valid && errorMessage) {
        *errorMessage = QStringLiteral("PaddleOCR runtime manifest is invalid or incomplete.");
    }
    return valid;
}

PaddleOcrRuntimeResolution PaddleOcrRuntimeLocator::resolve()
{
    return resolveForApplicationDirectory(QCoreApplication::applicationDirPath());
}

PaddleOcrRuntimeResolution PaddleOcrRuntimeLocator::resolveForApplicationDirectory(
    const QString &applicationDirectory)
{
    PaddleOcrRuntimeResolution resolution;
    const QString configuredPython = existingFile(qEnvironmentVariable("LASTUDIO_PADDLE_PYTHON"));
    const QString configuredWorker = existingFile(qEnvironmentVariable("LASTUDIO_PADDLE_WORKER"));
    const QString configuredCache = existingDirectory(qEnvironmentVariable("LASTUDIO_PADDLE_CACHE"));
    const QString configuredManifest = existingFile(qEnvironmentVariable("LASTUDIO_PADDLE_MANIFEST"));
    if (!configuredPython.isEmpty() || !configuredWorker.isEmpty() || !configuredCache.isEmpty()
        || !configuredManifest.isEmpty()) {
        resolution.pythonPath = configuredPython;
        resolution.workerPath = configuredWorker;
        resolution.modelCachePath = configuredCache;
        resolution.manifestPath = configuredManifest;
        resolution.source = QStringLiteral("environment");
        return resolution;
    }

    const QDir root(QDir(applicationDirectory).filePath(QStringLiteral("subtitle-ocr/paddle")));
#ifdef Q_OS_WIN
    resolution.pythonPath = existingFile(root.filePath(QStringLiteral("runtime/python.exe")));
#else
    resolution.pythonPath = existingFile(root.filePath(QStringLiteral("runtime/bin/python3")));
#endif
    resolution.workerPath = existingFile(root.filePath(QStringLiteral("paddle_ocr_worker.py")));
    resolution.modelCachePath = existingDirectory(root.filePath(QStringLiteral("model-cache")));
    resolution.manifestPath = existingFile(root.filePath(QStringLiteral("runtime-manifest.json")));
    resolution.source = QStringLiteral("bundled");
    return resolution;
}

QStringList PaddleOcrRuntimeLocator::bundledLanguageCodes()
{
    return {QStringLiteral("chi_sim")};
}

bool PaddleOcrRuntimeLocator::supportsBundledLanguage(const QString &language)
{
    return bundledLanguageCodes().contains(language.trimmed().toLower());
}

} // namespace LAStudio
