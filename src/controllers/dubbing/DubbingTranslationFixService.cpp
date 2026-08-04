#include "controllers/dubbing/DubbingTranslationFixService.h"

#include "core/Logger.h"
#include "core/PathUtils.h"
#include "core/SecureCredentialStore.h"
#include "dubbing/DubbingDuration.h"
#include "dubbing/EspeakNgPhonemizer.h"
#include "remote/ColabSession.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>

namespace LAStudio {
namespace {

QString settingsPath()
{
    return PathUtils::dataDir() + QStringLiteral("/settings.ini");
}

QString normalizedServerBase(QString value)
{
    value = value.trimmed();
    while (value.endsWith(QLatin1Char('/'))) value.chop(1);
    const QStringList suffixes = {
        QStringLiteral("/api/v1/chat"),
        QStringLiteral("/v1/chat/completions"),
        QStringLiteral("/api/v1"),
        QStringLiteral("/v1")
    };
    for (const QString &suffix : suffixes) {
        if (!value.endsWith(suffix, Qt::CaseInsensitive)) continue;
        value.chop(suffix.size());
        break;
    }
    return value;
}

int actualPhonemeCount(const QVariantMap &segment, const QString &language)
{
    return EspeakNgPhonemizer::count(
        segment.value(QStringLiteral("targetText")).toString(), language);
}

bool isOverBudget(const QVariantMap &segment, const QString &language)
{
    const QVariantMap budget = segment.value(QStringLiteral("durationBudget")).toMap();
    if (budget.isEmpty()
        || segment.value(QStringLiteral("targetText")).toString().trimmed().isEmpty())
        return false;
    const int maximum = budget.value(QStringLiteral("maxUnits")).toInt();
    const int phonemes = actualPhonemeCount(segment, language);
    if (phonemes < 0) return false;
    return phonemes > maximum;
}

int distanceToBudget(int phonemes, int minimum, int maximum)
{
    if (phonemes < minimum) return minimum - phonemes;
    if (phonemes > maximum) return phonemes - maximum;
    return 0;
}

QString responseError(const QByteArray &body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) return QString::fromUtf8(body).trimmed();
    const QJsonValue error = document.object().value(QStringLiteral("error"));
    if (error.isString()) return error.toString();
    if (error.isObject())
        return error.toObject().value(QStringLiteral("message")).toString();
    return {};
}

QString cliConnectionError(const QByteArray &stderrData)
{
    QString detail = QString::fromUtf8(stderrData).trimmed();
    if (detail.contains(QStringLiteral("not logged into"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("authentication required"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("not authenticated"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("login required"), Qt::CaseInsensitive)) {
        return QStringLiteral("CLI authentication is required. Open a terminal, run the selected CLI, and complete its sign-in flow.");
    }
    if (detail.contains(QStringLiteral("unauthorized"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("invalid api key"), Qt::CaseInsensitive)) {
        return QStringLiteral("CLI authentication was rejected. Sign in again and retry.");
    }
    if (detail.isEmpty())
        return QStringLiteral("The CLI completed without returning a model response.");

    const QStringList lines = detail.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    detail = lines.isEmpty() ? detail : lines.constLast().trimmed();
    if (detail.size() > 500) detail = detail.left(500) + QChar(0x2026);
    return detail;
}

QString translationRepairSystemPrompt()
{
    return QStringLiteral(
        "You repair translations for timed dubbing. This is a text-only "
        "transformation: do not call command, terminal, filesystem, web, search, "
        "or any other tool. Preserve the complete source meaning and the meaning "
        "of the current translation: facts, names, numbers, rank/order, time, "
        "comparison, causality, and negation. Rewrite naturally in the requested "
        "target language while meeting the supplied eSpeak NG phoneme maximum. "
        "Never invent or omit information. Return only the rewritten translation, "
        "without analysis, labels, quotes, or a phoneme count.");
}

QString createCliDiagnosticLogPath(const QString &cliAgent)
{
    if (cliAgent != QStringLiteral("antigravity")) return {};
    QTemporaryFile file(
        QDir(QDir::tempPath()).filePath(
            QStringLiteral("la-studio-agy-XXXXXX.log")));
    file.setAutoRemove(false);
    if (!file.open()) return {};
    const QString path = file.fileName();
    file.close();
    return path;
}

QByteArray takeCliDiagnosticLog(const QString &path)
{
    if (path.isEmpty()) return {};
    QFile file(path);
    QByteArray result;
    if (file.open(QIODevice::ReadOnly)) result = file.readAll();
    file.close();
    QFile::remove(path);
    return result;
}

bool containsCliAuthFailure(const QString &detail)
{
    return detail.contains(QStringLiteral("not logged into"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("authentication required"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("authentication timed out"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("not authenticated"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("login required"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("\"authenticated\":false"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("\"loggedIn\":false"), Qt::CaseInsensitive);
}

bool containsCliAuthSuccess(const QString &detail)
{
    // agy 1.1.5 may log transient "not logged into" errors while its backend
    // starts, then silently load a valid token from the OS keyring. Treat the
    // later successful authentication markers as the final state.
    return detail.contains(QStringLiteral("silent auth succeeded"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("authenticated via keyring"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("OAuth: authenticated successfully"), Qt::CaseInsensitive);
}

QString classifiedCliFailure(const QString &cliAgent,
                             const QByteArray &stdoutData,
                             const QByteArray &stderrData,
                             const QByteArray &diagnosticLog)
{
    const QString processDetail = QString::fromUtf8(
        stdoutData + QByteArrayLiteral("\n") + stderrData);
    const QString diagnosticDetail = QString::fromUtf8(diagnosticLog);
    const QString detail = processDetail + QLatin1Char('\n') + diagnosticDetail;
    if (detail.contains(QStringLiteral("RESOURCE_EXHAUSTED"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("individual quota reached"), Qt::CaseInsensitive)
        || (detail.contains(QStringLiteral("429"))
            && detail.contains(QStringLiteral("quota"), Qt::CaseInsensitive))) {
        return cliAgent == QStringLiteral("antigravity")
            ? QStringLiteral("Antigravity quota is exhausted for the selected model. Choose another model in LA Studio or wait for the quota to reset.")
            : QStringLiteral("The selected CLI model has reached its usage limit. Retry later or select another model.");
    }
    const bool processReportsAuthFailure = containsCliAuthFailure(processDetail);
    const bool diagnosticReportsFinalAuthFailure =
        containsCliAuthFailure(diagnosticDetail)
        && !containsCliAuthSuccess(diagnosticDetail);
    if (processReportsAuthFailure || diagnosticReportsFinalAuthFailure) {
        return cliAgent == QStringLiteral("antigravity")
            ? QStringLiteral("Antigravity authentication is required. Open a terminal, run agy once, complete Google sign-in, then retry.")
            : QStringLiteral("CLI authentication is required. Open a terminal, run the selected CLI, and complete its sign-in flow.");
    }
    if (detail.contains(QStringLiteral("unauthorized"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("invalid api key"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("oauth token expired"), Qt::CaseInsensitive)) {
        return QStringLiteral("CLI authentication was rejected. Sign in again and retry.");
    }
    if (detail.contains(QStringLiteral("headless mode cannot prompt"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("tool required the \"command\" permission"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("permission request was denied"), Qt::CaseInsensitive)) {
        return QStringLiteral("The CLI requested an interactive tool permission that cannot be approved in headless mode. Update the CLI and retry with the sandboxed non-interactive integration.");
    }
    if (detail.contains(QStringLiteral("unknown option"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("unknown flag"), Qt::CaseInsensitive)
        || detail.contains(QStringLiteral("unexpected argument"), Qt::CaseInsensitive)) {
        return QStringLiteral("The installed CLI version does not support the required non-interactive options. Update the CLI and retry.");
    }
    return {};
}

QString cliArgumentsForLog(
    const DubbingTranslationFixService::CliInvocation &invocation)
{
    QStringList safeArguments = invocation.arguments;
    if (!invocation.promptViaStdin
        && invocation.agentId == QStringLiteral("antigravity")
        && !safeArguments.isEmpty()) {
        safeArguments.last() = QStringLiteral("<prompt>");
    }
    return safeArguments.join(QLatin1Char(' '));
}

QJsonObject readJsonObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject();
}

QByteArray readLocalFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

void appendCliModel(QVariantList &models, QSet<QString> &seen,
                    const QString &value, const QString &text,
                    const QString &detail)
{
    const QString id = value.trimmed();
    if (id.isEmpty() || seen.contains(id)) return;
    seen.insert(id);
    models.append(QVariantMap{
        {QStringLiteral("value"), id},
        {QStringLiteral("text"), text.trimmed().isEmpty() ? id : text.trimmed()},
        {QStringLiteral("detail"), detail}
    });
}

QString configuredCliModel(const QString &agent, const QString &home)
{
    if (agent == QStringLiteral("claude")) {
        return readJsonObject(
                   QDir(home).filePath(QStringLiteral(".claude/settings.json")))
            .value(QStringLiteral("model")).toString().trimmed();
    }
    if (agent == QStringLiteral("codex")) {
        const QString config = QString::fromUtf8(readLocalFile(
            QDir(home).filePath(QStringLiteral(".codex/config.toml"))));
        const QRegularExpression modelPattern(
            QStringLiteral("^\\s*model\\s*=\\s*\"([^\"]+)\""),
            QRegularExpression::MultilineOption);
        const QRegularExpressionMatch match = modelPattern.match(config);
        return match.hasMatch() ? match.captured(1).trimmed() : QString();
    }
    return readJsonObject(
               QDir(home).filePath(
                   QStringLiteral(".gemini/antigravity-cli/settings.json")))
        .value(QStringLiteral("model")).toString().trimmed();
}

} // namespace

DubbingTranslationFixService::DubbingTranslationFixService(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this))
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    QString credentialError;
    m_configuration = normalizedConfiguration({
        {QStringLiteral("serverUrl"),
         settings.value(QStringLiteral("dubbing/translationFixServerUrl"),
                        QStringLiteral("http://127.0.0.1:1234")).toString()},
        {QStringLiteral("provider"),
         settings.value(QStringLiteral("dubbing/adaptiveProvider"),
                        settings.value(QStringLiteral("dubbing/cinematicProvider"),
                                       QStringLiteral("lmstudio"))).toString()},
        {QStringLiteral("cliAgent"),
         settings.value(QStringLiteral("dubbing/adaptiveCliAgent"),
                        QStringLiteral("claude")).toString()},
        {QStringLiteral("configured"),
         settings.value(QStringLiteral("dubbing/adaptiveConfigured"),
                        settings.value(QStringLiteral("dubbing/cinematicConfigured"), false)).toBool()},
        {QStringLiteral("model"),
         settings.value(QStringLiteral("dubbing/translationFixModel"),
                        QStringLiteral("qwen3.5-2b")).toString()},
        {QStringLiteral("runtimeId"),
         settings.value(QStringLiteral("dubbing/adaptiveRuntimeId")).toString()},
        {QStringLiteral("runtimeVersion"),
         settings.value(QStringLiteral("dubbing/adaptiveRuntimeVersion")).toString()},
        {QStringLiteral("selectedFiles"),
         settings.value(QStringLiteral("dubbing/adaptiveSelectedFiles")).toMap()},
        {QStringLiteral("apiKey"),
         SecureCredentialStore::migrateLegacy(settings, QStringLiteral("dubbing-translation-fix"),
                                               QStringLiteral("dubbing/translationFixApiKey"), &credentialError)},
        {QStringLiteral("maxAttempts"),
         settings.value(QStringLiteral("dubbing/translationFixMaxAttempts"), 4).toInt()},
        {QStringLiteral("temperature"),
         settings.value(QStringLiteral("dubbing/translationFixTemperature"), 0.35).toDouble()}
    });
    if (!credentialError.isEmpty()) {
        Logger::error(QStringLiteral("DubbingTranslationFixService"),
                      QStringLiteral("Translation API credential migration failed: %1").arg(credentialError));
    }
}

QVariantMap DubbingTranslationFixService::normalizedConfiguration(
    const QVariantMap &configuration)
{
    QVariantMap result;
    QString provider = configuration.value(QStringLiteral("provider"),
                                           QStringLiteral("lmstudio"))
                           .toString().trimmed().toLower();
    if (provider != QStringLiteral("api") && provider != QStringLiteral("local")
        && provider != QStringLiteral("cli") && provider != QStringLiteral("colab-direct"))
        provider = QStringLiteral("lmstudio");
    result.insert(QStringLiteral("provider"), provider);

    QString cliAgent = configuration.value(QStringLiteral("cliAgent"),
                                            QStringLiteral("claude"))
                           .toString().trimmed().toLower();
    if (cliAgent != QStringLiteral("codex") && cliAgent != QStringLiteral("antigravity"))
        cliAgent = QStringLiteral("claude");
    result.insert(QStringLiteral("cliAgent"), cliAgent);
    result.insert(QStringLiteral("configured"),
                  configuration.value(QStringLiteral("configured"), false).toBool());
    // Reconciliation is a structured LLM task, not ordinary translation.
    // Keep this explicit so an arbitrary OpenAI-compatible endpoint or a
    // machine-translation model is never assumed capable without user setup.
    result.insert(QStringLiteral("supportsStructuredReconciliation"),
                  configuration.value(QStringLiteral("supportsStructuredReconciliation"),
                                      false).toBool());
    result.insert(QStringLiteral("serverUrl"),
                  provider == QStringLiteral("colab-direct")
                      ? QString()
                      : configuration.value(QStringLiteral("serverUrl"),
                                            QStringLiteral("http://127.0.0.1:1234"))
                            .toString().trimmed());
    result.insert(QStringLiteral("model"),
                  configuration.value(QStringLiteral("model"),
                                      QStringLiteral("qwen3.5-2b"))
                      .toString().trimmed());
    // A remote route must never retain a local runtime/model selection.  It
    // otherwise makes a later Automatic run look local and can enqueue a
    // download after the user explicitly chose Colab or the API Gateway.
    const bool localProvider = provider == QStringLiteral("local");
    result.insert(QStringLiteral("runtimeId"), localProvider
                      ? configuration.value(QStringLiteral("runtimeId")).toString().trimmed()
                      : QString());
    result.insert(QStringLiteral("runtimeVersion"), localProvider
                      ? configuration.value(QStringLiteral("runtimeVersion")).toString().trimmed()
                      : QString());
    result.insert(QStringLiteral("selectedFiles"), localProvider
                      ? configuration.value(QStringLiteral("selectedFiles")).toMap()
                      : QVariantMap());
    result.insert(QStringLiteral("apiKey"), provider == QStringLiteral("colab-direct")
                      ? QString()
                      : configuration.value(QStringLiteral("apiKey")).toString().trimmed());
    result.insert(QStringLiteral("maxAttempts"),
                  qBound(1, configuration.value(QStringLiteral("maxAttempts"), 4).toInt(), 8));
    result.insert(QStringLiteral("temperature"),
                  qBound(0.0, configuration.value(QStringLiteral("temperature"), 0.35).toDouble(), 1.5));
    return result;
}

bool DubbingTranslationFixService::reconciliationAvailable(
    const QVariantMap &configuration, QString *reason)
{
    const QVariantMap normalized = normalizedConfiguration(configuration);
    const QString provider = normalized.value(QStringLiteral("provider")).toString();
    const auto reject = [reason](const QString &message) {
        if (reason) *reason = message;
        return false;
    };
    if (!normalized.value(QStringLiteral("configured")).toBool()) {
        return reject(QStringLiteral("AI suggestion requires the project Translation Fix LLM to be configured. A plain translation model is not used for reconciliation."));
    }
    if (!normalized.value(QStringLiteral("supportsStructuredReconciliation")).toBool()) {
        return reject(QStringLiteral("AI suggestion is disabled until this Translation Fix model is explicitly marked as supporting structured source-language reconciliation. Plain translation models are not used."));
    }
    if (provider == QStringLiteral("local")) {
        return reject(QStringLiteral("The selected local Translate model is a translation runtime, not a structured reconciliation LLM. Configure Translation Fix LLM or choose a manual policy."));
    }
    else if (provider == QStringLiteral("cli")) {
        const QString agent = normalized.value(QStringLiteral("cliAgent")).toString();
        if (cliExecutablePath(agent).isEmpty()) {
            return reject(QStringLiteral("The configured Translation Fix CLI is unavailable, so AI suggestion cannot run."));
        }
    } else if (provider != QStringLiteral("colab-direct")) {
        const QString base = normalizedServerBase(
            normalized.value(QStringLiteral("serverUrl")).toString());
        const QUrl endpoint(provider == QStringLiteral("api")
                                ? base + QStringLiteral("/v1/chat/completions")
                                : base + QStringLiteral("/api/v1/chat"));
        if (!endpoint.isValid() || endpoint.host().isEmpty()
            || normalized.value(QStringLiteral("model")).toString().trimmed().isEmpty()) {
            return reject(QStringLiteral("The configured Translation Fix LLM route or model is incomplete."));
        }
    }
    if (reason) reason->clear();
    return true;
}

QString DubbingTranslationFixService::cliExecutablePath(const QString &cliAgent)
{
    const QString normalized = cliAgent.trimmed().toLower();
    QString program = QStringLiteral("claude");
    if (normalized == QStringLiteral("codex"))
        program = QStringLiteral("codex");
    else if (normalized == QStringLiteral("antigravity"))
        program = QStringLiteral("agy");

    const QString fromPath = QStandardPaths::findExecutable(program);
    if (!fromPath.isEmpty()) return fromPath;

#ifdef Q_OS_WIN
    QStringList candidates;
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    const QString appData = qEnvironmentVariable("APPDATA");
    const QString userProfile = qEnvironmentVariable("USERPROFILE");
    if (normalized == QStringLiteral("antigravity") && !localAppData.isEmpty()) {
        candidates << localAppData + QStringLiteral("/agy/bin/agy.exe");
    } else if (normalized == QStringLiteral("codex")) {
        if (!appData.isEmpty())
            candidates << appData + QStringLiteral("/npm/codex.cmd");
        if (!localAppData.isEmpty())
            candidates << localAppData + QStringLiteral("/Programs/codex/codex.exe");
    } else if (normalized == QStringLiteral("claude")) {
        if (!userProfile.isEmpty())
            candidates << userProfile + QStringLiteral("/.local/bin/claude.exe");
        if (!localAppData.isEmpty())
            candidates << localAppData + QStringLiteral("/Programs/claude/claude.exe");
    }
    for (const QString &candidate : std::as_const(candidates)) {
        const QFileInfo info(candidate);
        if (info.isFile()) return info.absoluteFilePath();
    }
#endif
    return {};
}

QVariantList DubbingTranslationFixService::cliModelOptions(
    const QString &cliAgent, const QString &homePath)
{
    QString agent = cliAgent.trimmed().toLower();
    if (agent != QStringLiteral("codex")
        && agent != QStringLiteral("antigravity"))
        agent = QStringLiteral("claude");

    const QString home = homePath.trimmed().isEmpty()
        ? QDir::homePath() : QDir(homePath).absolutePath();
    QVariantList models;
    QSet<QString> seen;
    const QString configured = configuredCliModel(agent, home);
    appendCliModel(
        models, seen, QStringLiteral("default"),
        QStringLiteral("Default (CLI config)"),
        configured.isEmpty()
            ? QStringLiteral("Use the model selected by the CLI")
            : QStringLiteral("Configured model: %1").arg(configured));
    if (!configured.isEmpty()) {
        appendCliModel(models, seen, configured, configured,
                       QStringLiteral("Selected in the CLI configuration"));
    }

    if (agent == QStringLiteral("claude")) {
        const QJsonObject usage = readJsonObject(
            QDir(home).filePath(QStringLiteral(".claude/stats-cache.json")))
                                      .value(QStringLiteral("modelUsage"))
                                      .toObject();
        for (auto it = usage.constBegin(); it != usage.constEnd(); ++it) {
            appendCliModel(models, seen, it.key(), it.key(),
                           QStringLiteral("Found in Claude Code usage cache"));
        }
        const QList<QPair<QString, QString>> fallbacks = {
            {QStringLiteral("sonnet"), QStringLiteral("Sonnet (latest alias)")},
            {QStringLiteral("opus"), QStringLiteral("Opus (latest alias)")},
            {QStringLiteral("haiku"), QStringLiteral("Haiku (latest alias)")}
        };
        for (const auto &option : fallbacks) {
            appendCliModel(models, seen, option.first, option.second,
                           QStringLiteral("Claude Code model alias"));
        }
        return models;
    }

    if (agent == QStringLiteral("codex")) {
        const QJsonArray cachedModels = readJsonObject(
            QDir(home).filePath(QStringLiteral(".codex/models_cache.json")))
                                            .value(QStringLiteral("models"))
                                            .toArray();
        for (const QJsonValue &value : cachedModels) {
            const QJsonObject item = value.toObject();
            if (item.value(QStringLiteral("visibility")).toString()
                == QStringLiteral("hidden"))
                continue;
            const QString id =
                item.value(QStringLiteral("slug")).toString().trimmed();
            appendCliModel(
                models, seen, id,
                item.value(QStringLiteral("display_name")).toString(),
                QStringLiteral("Available in the Codex model cache"));
        }
        const QStringList fallbacks = {
            QStringLiteral("gpt-5.6-sol"),
            QStringLiteral("gpt-5.6-terra"),
            QStringLiteral("gpt-5.6-luna"),
            QStringLiteral("gpt-5.5"),
            QStringLiteral("gpt-5.4"),
            QStringLiteral("gpt-5.4-mini")
        };
        for (const QString &model : fallbacks) {
            appendCliModel(models, seen, model, model,
                           QStringLiteral("Codex CLI fallback model"));
        }
        return models;
    }

    // agy 1.1.5 exposes these exact ids through `agy models`. Unlike the
    // display labels used by agy 1.0.3 and the older Open Design adapter,
    // these slugs are accepted directly by the current --model option.
    const QList<QPair<QString, QString>> antigravityModels = {
        {QStringLiteral("gemini-3.6-flash-high"),
         QStringLiteral("Gemini 3.6 Flash (High)")},
        {QStringLiteral("gemini-3.6-flash-medium"),
         QStringLiteral("Gemini 3.6 Flash (Medium)")},
        {QStringLiteral("gemini-3.6-flash-low"),
         QStringLiteral("Gemini 3.6 Flash (Low)")},
        {QStringLiteral("gemini-3.5-flash-high"),
         QStringLiteral("Gemini 3.5 Flash (High)")},
        {QStringLiteral("gemini-3.5-flash-medium"),
         QStringLiteral("Gemini 3.5 Flash (Medium)")},
        {QStringLiteral("gemini-3.5-flash-low"),
         QStringLiteral("Gemini 3.5 Flash (Low)")},
        {QStringLiteral("gemini-3.1-pro-high"),
         QStringLiteral("Gemini 3.1 Pro (High)")},
        {QStringLiteral("gemini-3.1-pro-low"),
         QStringLiteral("Gemini 3.1 Pro (Low)")},
        {QStringLiteral("claude-sonnet-4-6"),
         QStringLiteral("Claude Sonnet 4.6")},
        {QStringLiteral("claude-opus-4-6-thinking"),
         QStringLiteral("Claude Opus 4.6 (Thinking)")},
        {QStringLiteral("gpt-oss-120b-medium"),
         QStringLiteral("GPT-OSS 120B (Medium)")}
    };
    for (const auto &option : antigravityModels) {
        appendCliModel(models, seen, option.first, option.second,
                       QStringLiteral("Available from Antigravity CLI"));
    }
    return models;
}

DubbingTranslationFixService::CliInvocation
DubbingTranslationFixService::cliInvocation(
    const QString &cliAgent, const QString &model,
    const QString &prompt, const QString &executablePath,
    const QString &diagnosticLogPath, int timeoutSeconds)
{
    CliInvocation invocation;
    invocation.agentId = cliAgent.trimmed().toLower();
    invocation.program = executablePath;
    invocation.workingDirectory = QDir::tempPath();

    if (invocation.agentId == QStringLiteral("codex")) {
        invocation.binaryName = QStringLiteral("codex");
        invocation.displayName = QStringLiteral("Codex CLI");
        invocation.arguments
            << QStringLiteral("exec") << QStringLiteral("--json")
            << QStringLiteral("--ephemeral")
            << QStringLiteral("--sandbox") << QStringLiteral("read-only")
            << QStringLiteral("--skip-git-repo-check");
    } else if (invocation.agentId == QStringLiteral("antigravity")) {
        invocation.binaryName = QStringLiteral("agy");
        invocation.displayName = QStringLiteral("Google Antigravity");
        invocation.diagnosticLogPath = diagnosticLogPath;
        if (!diagnosticLogPath.isEmpty()) {
            // agy requires --log-file before -p for the diagnostic file to be
            // populated reliably.
            invocation.arguments
                << QStringLiteral("--log-file") << diagnosticLogPath;
        }
        invocation.arguments
            // Keep terminal activity restricted to agy's sandbox while
            // auto-approving requests that a headless process cannot prompt for.
            << QStringLiteral("--sandbox")
            << QStringLiteral("--dangerously-skip-permissions")
            << QStringLiteral("--print-timeout")
            << QStringLiteral("%1s").arg(qMax(1, timeoutSeconds));
        // agy 1.1.5 treats `-p -` as the literal one-character prompt "-".
        // Supply the prompt as -p's value instead of writing it to stdin.
        invocation.promptViaStdin = false;
    } else {
        invocation.agentId = QStringLiteral("claude");
        invocation.binaryName = QStringLiteral("claude");
        invocation.displayName = QStringLiteral("Claude Code");
        invocation.arguments
            << QStringLiteral("-p")
            << QStringLiteral("--input-format") << QStringLiteral("text")
            << QStringLiteral("--output-format") << QStringLiteral("json")
            << QStringLiteral("--no-session-persistence")
            // Dubbing repair only needs a text response; disable Claude's tools.
            << QStringLiteral("--tools") << QString();
    }

    if (!model.isEmpty() && model != QStringLiteral("default"))
        invocation.arguments << QStringLiteral("--model") << model;
    if (invocation.agentId == QStringLiteral("antigravity"))
        invocation.arguments << QStringLiteral("-p") << prompt;

#ifdef Q_OS_WIN
    const QString lowerExe = executablePath.toLower();
    if (lowerExe.endsWith(QStringLiteral(".cmd"))
        || lowerExe.endsWith(QStringLiteral(".bat"))) {
        invocation.program = QStringLiteral("cmd.exe");
        invocation.arguments.prepend(executablePath);
        invocation.arguments.prepend(QStringLiteral("/c"));
    }
#endif
    return invocation;
}

QString DubbingTranslationFixService::cliFailureMessage(
    const QString &cliAgent, const QByteArray &stdoutData,
    const QByteArray &stderrData, const QByteArray &diagnosticLog)
{
    const QString classified = classifiedCliFailure(
        cliAgent, stdoutData, stderrData, diagnosticLog);
    if (!classified.isEmpty()) return classified;
    if (!stderrData.trimmed().isEmpty()) return cliConnectionError(stderrData);
    if (!stdoutData.trimmed().isEmpty()
        && containsCliAuthSuccess(QString::fromUtf8(diagnosticLog)))
        return {};
    if (!diagnosticLog.trimmed().isEmpty()
        && !containsCliAuthSuccess(QString::fromUtf8(diagnosticLog)))
        return cliConnectionError(diagnosticLog);
    return QStringLiteral("The CLI completed without returning a model response.");
}

void DubbingTranslationFixService::setConfiguration(const QVariantMap &configuration)
{
    if (m_busy || m_testing) return;
    m_configuration = normalizedConfiguration(configuration);
    saveConfiguration();
    emit stateChanged();
}

QUrl DubbingTranslationFixService::chatUrl(const QString &serverUrl)
{
    return QUrl(normalizedServerBase(serverUrl) + QStringLiteral("/api/v1/chat"));
}

QUrl DubbingTranslationFixService::modelsUrl(const QString &serverUrl)
{
    return QUrl(normalizedServerBase(serverUrl) + QStringLiteral("/api/v1/models"));
}

QString DubbingTranslationFixService::cleanAssistantText(const QString &content)
{
    QString result = content.trimmed();
    result.remove(QRegularExpression(QStringLiteral("<think>.*?</think>"),
                                     QRegularExpression::DotMatchesEverythingOption
                                         | QRegularExpression::CaseInsensitiveOption));
    result = result.trimmed();
    if (result.startsWith(QStringLiteral("```"))) {
        result.remove(QRegularExpression(QStringLiteral("^```(?:text|json)?\\s*"),
                                         QRegularExpression::CaseInsensitiveOption));
        result.remove(QRegularExpression(QStringLiteral("\\s*```$")));
    }
    result.remove(QRegularExpression(
        QStringLiteral("^(?:translation|revised translation|bản dịch|câu viết lại)\\s*:\\s*"),
        QRegularExpression::CaseInsensitiveOption));
    result = result.trimmed();
    if (result.size() >= 2
        && ((result.startsWith(QLatin1Char('"')) && result.endsWith(QLatin1Char('"')))
            || (result.startsWith(QChar(0x201c)) && result.endsWith(QChar(0x201d)))))
        result = result.mid(1, result.size() - 2).trimmed();
    return result;
}

int DubbingTranslationFixService::eligibleSegmentCount(
    const QVariantList &segments, const QString &targetLanguage)
{
    int count = 0;
    for (const QVariant &value : segments) {
        if (isOverBudget(value.toMap(), targetLanguage)) ++count;
    }
    return count;
}

bool DubbingTranslationFixService::isCloserToBudget(
    int currentPhonemes, int candidatePhonemes, int minimum, int maximum)
{
    return distanceToBudget(candidatePhonemes, minimum, maximum)
        < distanceToBudget(currentPhonemes, minimum, maximum);
}

void DubbingTranslationFixService::saveConfiguration()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    const bool directColab = m_configuration.value(QStringLiteral("provider"))
        .toString() == QStringLiteral("colab-direct");
    settings.setValue(QStringLiteral("dubbing/adaptiveProvider"),
                      m_configuration.value(QStringLiteral("provider")));
    settings.setValue(QStringLiteral("dubbing/adaptiveCliAgent"),
                      m_configuration.value(QStringLiteral("cliAgent")));
    settings.setValue(QStringLiteral("dubbing/adaptiveConfigured"),
                      m_configuration.value(QStringLiteral("configured")));
    // Colab URL/token are intentionally memory-only.  More importantly, a
    // Direct Colab choice must not erase a separately configured API Gateway
    // endpoint or credential that the user may choose again later.
    if (!directColab) {
        settings.setValue(QStringLiteral("dubbing/translationFixServerUrl"),
                          m_configuration.value(QStringLiteral("serverUrl")));
    }
    settings.setValue(QStringLiteral("dubbing/translationFixModel"),
                      m_configuration.value(QStringLiteral("model")));
    settings.setValue(QStringLiteral("dubbing/adaptiveRuntimeId"),
                      m_configuration.value(QStringLiteral("runtimeId")));
    settings.setValue(QStringLiteral("dubbing/adaptiveRuntimeVersion"),
                      m_configuration.value(QStringLiteral("runtimeVersion")));
    settings.setValue(QStringLiteral("dubbing/adaptiveSelectedFiles"),
                      m_configuration.value(QStringLiteral("selectedFiles")));
    if (!directColab) {
        QString credentialError;
        if (!SecureCredentialStore::write(settings, QStringLiteral("dubbing-translation-fix"),
                                          m_configuration.value(QStringLiteral("apiKey")).toString(),
                                          &credentialError)) {
            Logger::error(QStringLiteral("DubbingTranslationFixService"),
                          QStringLiteral("Translation API credential was not persisted: %1").arg(credentialError));
        }
    }
    settings.remove(QStringLiteral("dubbing/translationFixApiKey"));
    settings.setValue(QStringLiteral("dubbing/translationFixMaxAttempts"),
                      m_configuration.value(QStringLiteral("maxAttempts")));
    settings.setValue(QStringLiteral("dubbing/translationFixTemperature"),
                      m_configuration.value(QStringLiteral("temperature")));
    settings.sync();
}

bool DubbingTranslationFixService::start(
    const QString &sourceLanguage, const QString &targetLanguage,
    const QVariantList &segments, const QVariantMap &configuration,
    int segmentIndex)
{
    if (m_busy || m_testing) return false;
    m_reconciliation = false;
    m_configuration = normalizedConfiguration(configuration.isEmpty()
                                                  ? m_configuration : configuration);
    const QString provider = m_configuration.value(QStringLiteral("provider")).toString();
    if (provider == QStringLiteral("local")) {
        setError(QStringLiteral("Local translation models do not use the remote rewrite service."));
        return false;
    }
    if (provider == QStringLiteral("colab-direct")) {
        QString routeError;
        if (!m_directColabSession
            || !m_directColabSession->hasVerifiedRoute(
                QStringLiteral("llm-chat"),
                m_configuration.value(QStringLiteral("model")).toString(), &routeError)) {
            setError(routeError.isEmpty()
                         ? QStringLiteral("Connect and check the exact Direct Colab Adaptive LLM worker before rewriting translations.")
                         : routeError);
            return false;
        }
    } else if (provider == QStringLiteral("cli")) {
        const QString cliAgent = m_configuration.value(QStringLiteral("cliAgent"), QStringLiteral("claude")).toString();
        QString binName = QStringLiteral("claude");
        if (cliAgent == QStringLiteral("codex")) binName = QStringLiteral("codex");
        else if (cliAgent == QStringLiteral("antigravity")) binName = QStringLiteral("agy");
        if (cliExecutablePath(cliAgent).isEmpty()) {
            setError(QStringLiteral("Local CLI Agent binary '%1' is not found on system PATH.").arg(binName));
            return false;
        }
    } else {
        const QString base = normalizedServerBase(
            m_configuration.value(QStringLiteral("serverUrl")).toString());
        const QUrl endpoint(provider == QStringLiteral("api")
                                ? base + QStringLiteral("/v1/chat/completions")
                                : base + QStringLiteral("/api/v1/chat"));
        if (!endpoint.isValid() || endpoint.host().isEmpty()) {
            setError(provider == QStringLiteral("api")
                         ? QStringLiteral("LLM API URL is invalid.")
                         : QStringLiteral("LM Studio server URL is invalid."));
            return false;
        }
        if (m_configuration.value(QStringLiteral("model")).toString().isEmpty()) {
            setError(provider == QStringLiteral("api")
                         ? QStringLiteral("LLM API model identifier is required.")
                         : QStringLiteral("LM Studio model identifier is required."));
            return false;
        }
    }

    m_segments = segments;
    m_sourceLanguage = sourceLanguage;
    m_targetLanguage = targetLanguage;
    m_eligibleIndices.clear();
    for (int i = 0; i < m_segments.size(); ++i) {
        if (segmentIndex >= 0 && i != segmentIndex) continue;
        const QVariantMap segment = m_segments.at(i).toMap();
        if (actualPhonemeCount(segment, targetLanguage) < 0) {
            setError(QStringLiteral(
                "eSpeak NG is unavailable, so translated phonemes cannot be verified."));
            return false;
        }
        if (isOverBudget(segment, targetLanguage)) m_eligibleIndices.append(i);
    }
    if (m_eligibleIndices.isEmpty()) {
        setError(segmentIndex >= 0
                     ? QStringLiteral("This translation does not exceed its phoneme limit.")
                     : QStringLiteral("No translated segment exceeds its phoneme limit."));
        return false;
    }

    saveConfiguration();
    m_maxAttempts = m_configuration.value(QStringLiteral("maxAttempts")).toInt();
    m_segmentPosition = 0;
    m_fixedCount = 0;
    m_improvedCount = 0;
    m_unresolvedCount = 0;
    m_lastError.clear();
    setProgress(0);
    setBusy(true);
    Logger::info(
        QStringLiteral("DubbingTranslationFix"),
        QStringLiteral("Starting %1 rewrite model=%2 segments=%3 selectedIndex=%4 maxAttempts=%5 targetLanguage=%6")
            .arg(provider,
                 m_configuration.value(QStringLiteral("model")).toString())
            .arg(m_eligibleIndices.size()).arg(segmentIndex).arg(m_maxAttempts)
            .arg(targetLanguage));
    beginSegment();
    return true;
}

void DubbingTranslationFixService::setDirectColabSession(ColabSession *session)
{
    m_directColabSession = session;
}

bool DubbingTranslationFixService::startReconciliation(
    const QString &sourceLanguage, const QVariantList &segments,
    const QVariantMap &configuration, int segmentIndex)
{
    if (m_busy || m_testing) return false;
    m_configuration = normalizedConfiguration(configuration.isEmpty()
                                                  ? m_configuration : configuration);
    QString unavailableReason;
    if (!reconciliationAvailable(m_configuration, &unavailableReason)) {
        setError(unavailableReason);
        return false;
    }

    m_reconciliation = true;
    m_segments = segments;
    m_sourceLanguage = sourceLanguage.trimmed().isEmpty()
        ? QStringLiteral("auto") : sourceLanguage.trimmed();
    m_targetLanguage.clear();
    m_eligibleIndices.clear();
    for (int i = 0; i < m_segments.size(); ++i) {
        if (segmentIndex >= 0 && i != segmentIndex) continue;
        const QVariantMap segment = m_segments.at(i).toMap();
        if (segment.value(QStringLiteral("fusionStatus")).toString()
                == QStringLiteral("conflict")
            && segment.value(QStringLiteral("fusionNeedsReview")).toBool()) {
            m_eligibleIndices.append(i);
        }
    }
    if (m_eligibleIndices.isEmpty()) {
        m_reconciliation = false;
        setError(segmentIndex >= 0
                     ? QStringLiteral("This transcript conflict no longer needs review.")
                     : QStringLiteral("No unresolved STT/OCR conflict is available for AI suggestion."));
        return false;
    }

    saveConfiguration();
    // One response per conflict creates a review suggestion; it is never
    // retried into an automatically accepted answer.
    m_maxAttempts = 1;
    m_segmentPosition = 0;
    m_fixedCount = 0;
    m_improvedCount = 0;
    m_unresolvedCount = 0;
    m_suggestedCount = 0;
    m_lastError.clear();
    setProgress(0);
    setBusy(true);
    Logger::info(
        QStringLiteral("DubbingTranscriptReconciliation"),
        QStringLiteral("Starting provider=%1 model=%2 conflicts=%3 selectedIndex=%4 sourceLanguage=%5")
            .arg(m_configuration.value(QStringLiteral("provider")).toString(),
                 m_configuration.value(QStringLiteral("model")).toString())
            .arg(m_eligibleIndices.size()).arg(segmentIndex).arg(m_sourceLanguage));
    beginSegment();
    return true;
}

void DubbingTranslationFixService::testConnection(
    const QVariantMap &configuration)
{
    if (m_busy || m_testing) return;
    m_configuration = normalizedConfiguration(configuration.isEmpty()
                                                  ? m_configuration : configuration);
    const QString provider = m_configuration.value(QStringLiteral("provider")).toString();
    if (provider == QStringLiteral("colab-direct")) {
        QString routeError;
        const bool ready = m_directColabSession
            && m_directColabSession->hasVerifiedRoute(
                QStringLiteral("llm-chat"),
                m_configuration.value(QStringLiteral("model")).toString(), &routeError);
        saveConfiguration();
        emit stateChanged();
        emit connectionTested(
            ready,
            ready ? QStringLiteral("Verified Direct Colab Adaptive LLM worker is ready.")
                  : (routeError.isEmpty()
                         ? QStringLiteral("Connect and check the exact Direct Colab Adaptive LLM worker.")
                         : routeError));
        return;
    }
    if (provider == QStringLiteral("local")) {
        saveConfiguration();
        emit connectionTested(true, QStringLiteral("Local LA Studio model selected."));
        emit stateChanged();
        return;
    }
    if (provider == QStringLiteral("cli")) {
        const QString cliAgent = m_configuration
                                     .value(QStringLiteral("cliAgent"),
                                            QStringLiteral("claude"))
                                     .toString();
        saveConfiguration();
        const QString exePath = cliExecutablePath(cliAgent);
        if (exePath.isEmpty()) {
            const CliInvocation unresolved = cliInvocation(
                cliAgent, {}, {}, {}, {}, 30);
            emit connectionTested(false, QStringLiteral("CLI binary \"%1\" was not found on system PATH.")
                                             .arg(unresolved.binaryName));
            emit stateChanged();
            return;
        }

        const QString prompt = QStringLiteral(
            "Connection health check for LA Studio. This is a text-only request. "
            "Do not call any tools. Reply with exactly: OK");
        const QString logPath = createCliDiagnosticLogPath(cliAgent);
        const CliInvocation invocation = cliInvocation(
            cliAgent,
            m_configuration.value(QStringLiteral("model")).toString(),
            prompt, exePath, logPath, 30);

        QProcess *process = new QProcess(this);
        process->setWorkingDirectory(invocation.workingDirectory);
        connect(process, &QObject::destroyed,
                [logPath = invocation.diagnosticLogPath]() {
            if (!logPath.isEmpty()) QFile::remove(logPath);
        });
        m_cliProcess = process;
        m_testing = true;
        emit stateChanged();
        connect(process, &QProcess::finished, this,
                [this, process, invocation](int exitCode,
                                            QProcess::ExitStatus exitStatus) {
            if (m_cliProcess == process) m_cliProcess = nullptr;
            const QByteArray diagnosticLog =
                takeCliDiagnosticLog(invocation.diagnosticLogPath);
            if (!m_testing) {
                process->deleteLater();
                return;
            }
            m_testing = false;
            const QByteArray stdoutData = process->readAllStandardOutput();
            const QByteArray stderrData = process->readAllStandardError();
            const QString response = parseCliResponse(stdoutData);
            const QString classifiedFailure = classifiedCliFailure(
                invocation.agentId, stdoutData, stderrData, diagnosticLog);
            const bool expectedSmokeReply =
                response.compare(QStringLiteral("OK"),
                                 Qt::CaseInsensitive) == 0;
            const bool success = exitStatus == QProcess::NormalExit
                && exitCode == 0 && !response.isEmpty()
                && classifiedFailure.isEmpty() && expectedSmokeReply;
            const QString message = success
                ? QStringLiteral("%1 is authenticated and returned a valid model response.")
                      .arg(invocation.displayName)
                : QStringLiteral("%1 connection failed: %2")
                      .arg(invocation.displayName,
                           classifiedFailure.isEmpty()
                               ? (!response.isEmpty() && !expectedSmokeReply
                                      ? QStringLiteral("The CLI returned an unexpected smoke-test response instead of OK: \"%1\"")
                                            .arg(response.left(160))
                                      : cliFailureMessage(invocation.agentId,
                                                          stdoutData, stderrData,
                                                          diagnosticLog))
                               : classifiedFailure);
            Logger::info(QStringLiteral("DubbingTranslationFix"),
                         QStringLiteral("CLI connection test agent=%1 success=%2 exitCode=%3 responseChars=%4 message=%5")
                             .arg(invocation.displayName,
                                  success ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(exitCode).arg(response.size()).arg(message));
            process->deleteLater();
            emit stateChanged();
            emit connectionTested(success, message);
        });

        Logger::info(
            QStringLiteral("DubbingTranslationFix"),
            QStringLiteral("CLI connection launch agent=%1 executable=%2 args=%3 promptViaStdin=%4 diagnosticLog=%5")
                .arg(invocation.agentId, invocation.program,
                     cliArgumentsForLog(invocation),
                     invocation.promptViaStdin ? QStringLiteral("true")
                                               : QStringLiteral("false"),
                     invocation.diagnosticLogPath.isEmpty()
                         ? QStringLiteral("disabled") : QStringLiteral("enabled")));
        connect(process, &QProcess::errorOccurred, this,
                [this, process, invocation, exePath](QProcess::ProcessError processError) {
            if (processError != QProcess::FailedToStart || m_cliProcess != process) return;
            takeCliDiagnosticLog(invocation.diagnosticLogPath);
            m_cliProcess = nullptr;
            m_testing = false;
            process->deleteLater();
            emit stateChanged();
            emit connectionTested(false, QStringLiteral("Failed to launch %1 at %2.")
                                           .arg(invocation.displayName, exePath));
        });
        connect(process, &QProcess::started, this, [process, prompt, invocation]() {
            if (invocation.promptViaStdin) process->write(prompt.toUtf8());
            process->closeWriteChannel();
        });
        process->start(invocation.program, invocation.arguments);

        QTimer::singleShot(45000, process, [this, process, invocation]() {
            if (m_cliProcess != process || !m_testing) return;
            m_cliProcess = nullptr;
            m_testing = false;
            process->kill();
            emit stateChanged();
            emit connectionTested(
                false, QStringLiteral("%1 connection test timed out. Check sign-in and network access.")
                           .arg(invocation.displayName));
        });
        emit stateChanged();
        return;
    }
    const QString base = normalizedServerBase(
        m_configuration.value(QStringLiteral("serverUrl")).toString());
    const QUrl endpoint(provider == QStringLiteral("api")
                            ? base + QStringLiteral("/v1/models")
                            : base + QStringLiteral("/api/v1/models"));
    if (!endpoint.isValid() || endpoint.host().isEmpty()) {
        emit connectionTested(false, provider == QStringLiteral("api")
                                          ? QStringLiteral("LLM API URL is invalid.")
                                          : QStringLiteral("LM Studio server URL is invalid."));
        return;
    }
    saveConfiguration();
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LA-Studio"));
    request.setRawHeader("Accept", "application/json");
    const QString apiKey = m_configuration.value(QStringLiteral("apiKey")).toString();
    if (!apiKey.isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());
    request.setTransferTimeout(10000);
    m_testing = true;
    emit stateChanged();
    QNetworkReply *pending = m_network->get(request);
    m_reply = pending;
    connect(pending, &QNetworkReply::finished, this, [this, pending]() {
        if (m_reply == pending) m_reply = nullptr;
        if (!m_testing) {
            pending->deleteLater();
            return;
        }
        m_testing = false;
        QNetworkReply *reply = pending;
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString model = m_configuration.value(QStringLiteral("model")).toString();
        bool found = false;
        const QString provider = m_configuration.value(QStringLiteral("provider")).toString();
        const QJsonObject response = QJsonDocument::fromJson(body).object();
        const QJsonArray models = provider == QStringLiteral("api")
            ? response.value(QStringLiteral("data")).toArray()
            : response.value(QStringLiteral("models")).toArray();
        for (const QJsonValue &value : models) {
            const QJsonObject modelObject = value.toObject();
            if (modelObject.value(provider == QStringLiteral("api")
                                      ? QStringLiteral("id") : QStringLiteral("key")).toString() == model) {
                found = true;
                break;
            }
            const QJsonArray instances =
                modelObject.value(QStringLiteral("loaded_instances")).toArray();
            for (const QJsonValue &instance : instances) {
                if (instance.toObject().value(QStringLiteral("id")).toString() == model) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        const bool success = reply->error() == QNetworkReply::NoError
            && status >= 200 && status < 300 && found;
        QString message;
        if (success)
            message = QStringLiteral("Connected. Model \"%1\" is available.").arg(model);
        else if (reply->error() != QNetworkReply::NoError)
            message = QStringLiteral("Connection failed: %1").arg(reply->errorString());
        else if (!found)
            message = QStringLiteral("Connected, but model \"%1\" was not listed by %2.")
                          .arg(model, provider == QStringLiteral("api")
                                          ? QStringLiteral("the LLM API")
                                          : QStringLiteral("LM Studio"));
        else
            message = QStringLiteral("%1 returned HTTP %2: %3")
                          .arg(provider == QStringLiteral("api")
                                   ? QStringLiteral("LLM API") : QStringLiteral("LM Studio"))
                          .arg(status).arg(responseError(body));
        Logger::info(QStringLiteral("DubbingTranslationFix"),
                     QStringLiteral("Connection test success=%1 endpoint=%2 model=%3 message=%4")
                         .arg(success ? QStringLiteral("true") : QStringLiteral("false"),
                              reply->url().toString(), model, message));
        reply->deleteLater();
        emit stateChanged();
        emit connectionTested(success, message);
    });
}

void DubbingTranslationFixService::cancel()
{
    if (!m_busy && !m_testing) return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (m_cliProcess) {
        m_cliProcess->kill();
        m_cliProcess->deleteLater();
        m_cliProcess = nullptr;
    }
    m_testing = false;
    if (m_busy) {
        setStatus(QStringLiteral("Translation fix cancelled."));
        setBusy(false);
    } else {
        emit stateChanged();
    }
    if (reply) {
        reply->abort();
        reply->deleteLater();
    }
}

void DubbingTranslationFixService::clearError()
{
    if (m_lastError.isEmpty()) return;
    m_lastError.clear();
    emit stateChanged();
}

void DubbingTranslationFixService::beginSegment()
{
    if (!m_busy) return;
    if (m_segmentPosition >= m_eligibleIndices.size()) {
        finishRun();
        return;
    }
    const QVariantMap segment =
        m_segments.at(m_eligibleIndices.at(m_segmentPosition)).toMap();
    if (m_reconciliation) {
        m_originalTranslation.clear();
        m_promptTranslation.clear();
        m_lastCandidate.clear();
        m_bestCandidate.clear();
        m_seenCandidates.clear();
        m_lastCandidatePhonemes = 0;
        m_bestCandidatePhonemes = 0;
        m_promptPhonemes = 0;
        m_attempt = 0;
        setStatus(QStringLiteral("Preparing AI suggestion for conflict %1 of %2")
                      .arg(m_segmentPosition + 1).arg(m_eligibleIndices.size()));
        requestAttempt();
        return;
    }
    m_originalTranslation =
        segment.value(QStringLiteral("targetText")).toString().trimmed();
    m_promptTranslation = m_originalTranslation;
    m_lastCandidate.clear();
    m_bestCandidate.clear();
    m_seenCandidates.clear();
    m_lastCandidatePhonemes = actualPhonemeCount(segment, m_targetLanguage);
    m_bestCandidatePhonemes = m_lastCandidatePhonemes;
    m_promptPhonemes = m_lastCandidatePhonemes;
    m_attempt = 0;
    setStatus(QStringLiteral("Fixing segment %1 of %2")
                  .arg(m_segmentPosition + 1).arg(m_eligibleIndices.size()));
    requestAttempt();
}

void DubbingTranslationFixService::requestAttempt()
{
    if (!m_busy) return;
    const QString provider = m_configuration.value(QStringLiteral("provider")).toString();
    if (provider == QStringLiteral("cli")) {
        executeCliAttempt();
        return;
    }

    const QVariantMap segment =
        m_segments.at(m_eligibleIndices.at(m_segmentPosition)).toMap();
    const bool directColab = provider == QStringLiteral("colab-direct");
    const bool openAiCompatible = provider == QStringLiteral("api") || directColab;
    QJsonObject payload;
    payload.insert(QStringLiteral("model"),
                   m_configuration.value(QStringLiteral("model")).toString());
    payload.insert(QStringLiteral("temperature"),
                   m_configuration.value(QStringLiteral("temperature")).toDouble());
    payload.insert(openAiCompatible ? QStringLiteral("max_tokens")
                                    : QStringLiteral("max_output_tokens"), 384);
    payload.insert(QStringLiteral("stream"), false);
    if (!openAiCompatible) {
        payload.insert(QStringLiteral("store"), false);
        payload.insert(QStringLiteral("reasoning"), QStringLiteral("off"));
    }
    payload.insert(QStringLiteral("top_p"), 0.8);
    if (!openAiCompatible)
        payload.insert(QStringLiteral("top_k"), 20);
    const QString systemPrompt = m_reconciliation
        ? QStringLiteral("You reconcile two conflicting transcript observations for timed dubbing. This is a text-only task: do not call tools. Return only one concise proposed source-language transcript. Preserve names, numbers, negation, meaning, and the language of the supplied source observations. Do not translate it and do not add analysis, labels, or quotes.")
        : translationRepairSystemPrompt();
    if (openAiCompatible) {
        payload.insert(QStringLiteral("messages"), QJsonArray{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                        {QStringLiteral("content"), systemPrompt}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), buildPrompt(segment)}}});
    } else {
        payload.insert(QStringLiteral("system_prompt"), systemPrompt);
        payload.insert(QStringLiteral("input"), buildPrompt(segment));
    }

    const QString base = directColab && m_directColabSession
        ? normalizedServerBase(m_directColabSession->endpoint().toString())
        : normalizedServerBase(m_configuration.value(QStringLiteral("serverUrl")).toString());
    QNetworkRequest request(QUrl(openAiCompatible
                                     ? base + QStringLiteral("/v1/chat/completions")
                                     : base + QStringLiteral("/api/v1/chat")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LA-Studio"));
    request.setRawHeader("Accept", "application/json");
    const QString apiKey = directColab && m_directColabSession
        ? m_directColabSession->bearerTokenForRequest()
        : m_configuration.value(QStringLiteral("apiKey")).toString();
    if (!apiKey.isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());
    request.setTransferTimeout(120000);

    Logger::info(
        QStringLiteral("DubbingTranslationFix"),
            QStringLiteral("Request operation=%1 segment=%2 attempt=%3/%4 currentPhonemes=%5")
            .arg(m_reconciliation ? QStringLiteral("reconcile") : QStringLiteral("rewrite"))
            .arg(segment.value(QStringLiteral("id")).toString())
            .arg(m_attempt + 1).arg(m_maxAttempts).arg(m_lastCandidatePhonemes));
    QNetworkReply *pending = m_network->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_reply = pending;
    connect(pending, &QNetworkReply::finished, this, [this, pending]() {
        if (m_reply == pending) m_reply = nullptr;
        handleAttemptResponse(pending);
    });
}

void DubbingTranslationFixService::executeCliAttempt()
{
    if (!m_busy) return;
    const QVariantMap segment =
        m_segments.at(m_eligibleIndices.at(m_segmentPosition)).toMap();
    const QString cliAgent = m_configuration.value(QStringLiteral("cliAgent"), QStringLiteral("claude")).toString();
    const QString model = m_configuration.value(QStringLiteral("model")).toString();

    const QString exePath = cliExecutablePath(cliAgent);
    if (exePath.isEmpty()) {
        const CliInvocation unresolved =
            cliInvocation(cliAgent, model, {}, {}, {}, 180);
        setError(QStringLiteral("CLI Agent binary '%1' is not found on system PATH.")
                     .arg(unresolved.binaryName));
        return;
    }

    const QString systemPrompt = m_reconciliation
        ? QStringLiteral("You reconcile two conflicting transcript observations for timed dubbing. This is a text-only task: do not call tools. Return only one concise proposed source-language transcript. Preserve names, numbers, negation, meaning, and the language of the supplied source observations. Do not translate it and do not add analysis, labels, or quotes.")
        : translationRepairSystemPrompt();
    const QString fullPrompt = systemPrompt + QStringLiteral("\n\n") + buildPrompt(segment);
    const QString logPath = createCliDiagnosticLogPath(cliAgent);
    const CliInvocation invocation =
        cliInvocation(cliAgent, model, fullPrompt, exePath, logPath, 180);

    Logger::info(
        QStringLiteral("DubbingTranslationFix"),
            QStringLiteral("CLI Request operation=%1 agent=%2 program=%3 segment=%4 attempt=%5/%6 currentPhonemes=%7")
            .arg(m_reconciliation ? QStringLiteral("reconcile") : QStringLiteral("rewrite"))
            .arg(cliAgent, invocation.binaryName,
                 segment.value(QStringLiteral("id")).toString())
            .arg(m_attempt + 1).arg(m_maxAttempts).arg(m_lastCandidatePhonemes));

    QProcess *process = new QProcess(this);
    process->setWorkingDirectory(invocation.workingDirectory);
    connect(process, &QObject::destroyed,
            [logPath = invocation.diagnosticLogPath]() {
        if (!logPath.isEmpty()) QFile::remove(logPath);
    });
    m_cliProcess = process;

    connect(process, &QProcess::finished, this,
            [this, process, invocation](int exitCode,
                                        QProcess::ExitStatus status) {
        if (m_cliProcess == process) m_cliProcess = nullptr;
        const QByteArray diagnosticLog =
            takeCliDiagnosticLog(invocation.diagnosticLogPath);
        if (!m_busy) {
            process->deleteLater();
            return;
        }

        const QByteArray stdoutData = process->readAllStandardOutput();
        const QByteArray stderrData = process->readAllStandardError();
        process->deleteLater();
        const QString classifiedFailure = classifiedCliFailure(
            invocation.agentId, stdoutData, stderrData, diagnosticLog);
        if (exitCode != 0 || status != QProcess::NormalExit
            || !classifiedFailure.isEmpty()) {
            const QString detail = classifiedFailure.isEmpty()
                ? cliFailureMessage(invocation.agentId, stdoutData,
                                    stderrData, diagnosticLog)
                : classifiedFailure;
            setError(QStringLiteral("CLI Agent process failed (exit code %1): %2")
                         .arg(exitCode).arg(detail));
            return;
        }

        const QString candidate = parseCliResponse(stdoutData);
        if (candidate.isEmpty()) {
            setError(QStringLiteral("CLI Agent did not return a translation: %1")
                         .arg(cliFailureMessage(invocation.agentId, stdoutData,
                                                stderrData, diagnosticLog)));
            return;
        }

        processCandidate(candidate);
    });

    Logger::info(
        QStringLiteral("DubbingTranslationFix"),
        QStringLiteral("CLI rewrite launch agent=%1 executable=%2 args=%3 promptViaStdin=%4 diagnosticLog=%5")
            .arg(invocation.agentId, invocation.program,
                 cliArgumentsForLog(invocation),
                 invocation.promptViaStdin ? QStringLiteral("true")
                                           : QStringLiteral("false"),
                 invocation.diagnosticLogPath.isEmpty()
                     ? QStringLiteral("disabled") : QStringLiteral("enabled")));
    connect(process, &QProcess::errorOccurred, this,
            [this, process, invocation](QProcess::ProcessError processError) {
        if (processError != QProcess::FailedToStart || m_cliProcess != process) return;
        takeCliDiagnosticLog(invocation.diagnosticLogPath);
        process->deleteLater();
        m_cliProcess = nullptr;
        setError(QStringLiteral("Failed to launch CLI Agent binary '%1'.")
                     .arg(invocation.binaryName));
    });
    connect(process, &QProcess::started, this, [process, fullPrompt, invocation]() {
        if (invocation.promptViaStdin) process->write(fullPrompt.toUtf8());
        process->closeWriteChannel();
    });
    process->start(invocation.program, invocation.arguments);

    QTimer::singleShot(180000, process, [this, process]() {
        if (m_cliProcess != process || !m_busy || !process->state()) return;
        process->kill();
        setError(QStringLiteral("CLI Agent timed out while rewriting the translation."));
    });
}

QString DubbingTranslationFixService::parseCliResponse(const QByteArray &body)
{
    const QString raw = QString::fromUtf8(body).trimmed();
    if (raw.isEmpty()) return {};

    const QStringList lines = raw.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QString lastMessage;
    QString accumulatedText;

    for (const QString &line : lines) {
        const QJsonDocument doc = QJsonDocument::fromJson(line.trimmed().toUtf8());
        if (!doc.isObject()) continue;
        const QJsonObject obj = doc.object();

        if (obj.contains(QStringLiteral("result")) && obj.value(QStringLiteral("result")).isString()) {
            return cleanAssistantText(obj.value(QStringLiteral("result")).toString());
        }
        // Codex exec --json emits JSONL events such as:
        // {"type":"item.completed","item":{"type":"agent_message","text":"..."}}
        const QJsonObject item = obj.value(QStringLiteral("item")).toObject();
        if (item.value(QStringLiteral("type")).toString() == QStringLiteral("agent_message")) {
            const QString text = item.value(QStringLiteral("text")).toString();
            if (!text.isEmpty()) lastMessage = text;
            continue;
        }
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("assistant")) {
            const QJsonObject message = obj.value(QStringLiteral("message")).toObject();
            const QJsonArray contentArr = message.value(QStringLiteral("content")).toArray();
            for (const QJsonValue &val : contentArr) {
                if (val.isObject() && val.toObject().value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
                    accumulatedText += val.toObject().value(QStringLiteral("text")).toString();
                }
            }
        }
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("agent_message") ||
            obj.contains(QStringLiteral("content"))) {
            if (obj.value(QStringLiteral("content")).isString()) {
                accumulatedText += obj.value(QStringLiteral("content")).toString();
            }
        }
    }

    if (!lastMessage.isEmpty())
        return cleanAssistantText(lastMessage);
    if (!accumulatedText.isEmpty()) {
        return cleanAssistantText(accumulatedText);
    }

    return cleanAssistantText(raw);
}

void DubbingTranslationFixService::handleAttemptResponse(QNetworkReply *reply)
{
    const QByteArray body = reply->readAll();
    const int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!m_busy) {
        reply->deleteLater();
        return;
    }
    if (reply->error() != QNetworkReply::NoError
        || status < 200 || status >= 300) {
        const QString detail = reply->error() != QNetworkReply::NoError
            ? reply->errorString() : responseError(body);
        reply->deleteLater();
        const QString provider = m_configuration.value(QStringLiteral("provider")).toString();
        setError(QStringLiteral("%1 request failed (HTTP %2): %3")
                     .arg(provider == QStringLiteral("api") || provider == QStringLiteral("colab-direct")
                              ? (provider == QStringLiteral("colab-direct")
                                     ? QStringLiteral("Direct Colab LLM")
                                     : QStringLiteral("LLM API"))
                              : QStringLiteral("LM Studio"))
                     .arg(status).arg(detail));
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(body).object();
    QString content;
    const QString provider = m_configuration.value(QStringLiteral("provider")).toString();
    if (provider == QStringLiteral("api") || provider == QStringLiteral("colab-direct")) {
        const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty())
            content = choices.first().toObject().value(QStringLiteral("message"))
                          .toObject().value(QStringLiteral("content")).toString();
    }
    const QJsonArray output = root.value(QStringLiteral("output")).toArray();
    for (const QJsonValue &value : output) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("type")).toString()
            == QStringLiteral("message")) {
            content = item.value(QStringLiteral("content")).toString();
            if (!content.isEmpty()) break;
        }
    }
    const QString candidate = cleanAssistantText(content);
    reply->deleteLater();
    if (candidate.isEmpty()) {
        setError(provider == QStringLiteral("api")
                     ? QStringLiteral("LLM API returned an empty translation.")
                     : provider == QStringLiteral("colab-direct")
                         ? QStringLiteral("Direct Colab LLM returned an empty translation.")
                         : QStringLiteral("LM Studio returned an empty translation."));
        return;
    }

    processCandidate(candidate);
}

void DubbingTranslationFixService::processCandidate(const QString &candidate)
{
    if (m_reconciliation) {
        processReconciliationCandidate(candidate);
        return;
    }
    const QString candidateKey = candidate.simplified().toCaseFolded();
    if (m_seenCandidates.contains(candidateKey)) {
        ++m_attempt;
        Logger::warning(QStringLiteral("DubbingTranslationFix"),
                        QStringLiteral("Repeated rewrite rejected at attempt %1/%2")
                            .arg(m_attempt).arg(m_maxAttempts));
        if (m_attempt < m_maxAttempts) {
            requestAttempt();
            return;
        }
        if (!m_bestCandidate.isEmpty()) {
            const QVariantMap current =
                m_segments.at(m_eligibleIndices.at(m_segmentPosition)).toMap();
            QVariantMap improved = current;
            applyCandidate(improved, m_bestCandidate, m_bestCandidatePhonemes, false);
            m_segments[m_eligibleIndices.at(m_segmentPosition)] = improved;
            finishSegment(false, true);
        } else {
            finishSegment(false);
        }
        return;
    }
    m_seenCandidates.insert(candidateKey);

    const QVariantMap segment =
        m_segments.at(m_eligibleIndices.at(m_segmentPosition)).toMap();
    const QVariantMap budget =
        segment.value(QStringLiteral("durationBudget")).toMap();
    const int phonemes = EspeakNgPhonemizer::count(candidate, m_targetLanguage);
    if (phonemes < 0) {
        setError(QStringLiteral(
            "eSpeak NG became unavailable while validating the rewritten translation."));
        return;
    }
    const QStringList tokens = protectedTokens(
        segment.value(QStringLiteral("sourceText")).toString());
    const bool tokensPreserved = preservesProtectedTokens(candidate, tokens);
    const double semanticScore =
        DubbingDurationPlanner::semanticFidelityScore(
            m_originalTranslation, candidate);
    const bool semanticGuardPassed = semanticScore >= 0.25;
    const bool withinBudget =
        phonemes >= budget.value(QStringLiteral("minUnits")).toInt()
        && phonemes <= budget.value(QStringLiteral("maxUnits")).toInt();
    Logger::info(
        QStringLiteral("DubbingTranslationFix"),
        QStringLiteral("Response segment=%1 attempt=%2 chars=%3 phonemes=%4 range=%5-%6 withinBudget=%7 protectedTokens=%8 semanticScore=%9 semanticGuard=%10")
            .arg(segment.value(QStringLiteral("id")).toString())
            .arg(m_attempt + 1).arg(candidate.size()).arg(phonemes)
            .arg(budget.value(QStringLiteral("minUnits")).toInt())
            .arg(budget.value(QStringLiteral("maxUnits")).toInt())
            .arg(withinBudget ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(tokensPreserved ? QStringLiteral("preserved")
                                 : QStringLiteral("missing"))
            .arg(semanticScore, 0, 'f', 3)
            .arg(semanticGuardPassed ? QStringLiteral("passed")
                                     : QStringLiteral("rejected")));

    m_lastCandidate = candidate;
    m_lastCandidatePhonemes = phonemes;
    ++m_attempt;
    if (withinBudget && tokensPreserved && semanticGuardPassed) {
        QVariantMap accepted = segment;
        applyCandidate(accepted, candidate, phonemes, true);
        m_segments[m_eligibleIndices.at(m_segmentPosition)] = accepted;
        finishSegment(true);
        return;
    }

    const int minimum = budget.value(QStringLiteral("minUnits")).toInt();
    const int maximum = budget.value(QStringLiteral("maxUnits")).toInt();
    if (tokensPreserved && semanticGuardPassed
        && isCloserToBudget(m_bestCandidatePhonemes, phonemes, minimum, maximum)) {
        m_bestCandidate = candidate;
        m_bestCandidatePhonemes = phonemes;
        m_promptTranslation = candidate;
        m_promptPhonemes = phonemes;
    }
    if (m_attempt < m_maxAttempts) {
        requestAttempt();
        return;
    }
    if (!m_bestCandidate.isEmpty()) {
        QVariantMap improved = segment;
        applyCandidate(improved, m_bestCandidate, m_bestCandidatePhonemes, false);
        m_segments[m_eligibleIndices.at(m_segmentPosition)] = improved;
        Logger::info(
            QStringLiteral("DubbingTranslationFix"),
            QStringLiteral("Keeping closest safe rewrite segment=%1 phonemes=%2 range=%3-%4")
                .arg(segment.value(QStringLiteral("id")).toString())
                .arg(m_bestCandidatePhonemes).arg(minimum).arg(maximum));
        finishSegment(false, true);
        return;
    }
    finishSegment(false);
}

void DubbingTranslationFixService::processReconciliationCandidate(const QString &candidate)
{
    if (!m_reconciliation || m_segmentPosition >= m_eligibleIndices.size()) return;
    const QString suggestion = cleanAssistantText(candidate);
    const int index = m_eligibleIndices.at(m_segmentPosition);
    QVariantMap segment = m_segments.at(index).toMap();
    if (suggestion.isEmpty()) {
        finishReconciliationSegment(false);
        return;
    }

    // The proposal is intentionally stored beside, never instead of, STT/OCR
    // evidence. The controller must receive an explicit accept/reject action
    // before sourceText changes or Translate becomes available.
    segment.insert(QStringLiteral("fusionAiSuggestion"), suggestion);
    segment.insert(QStringLiteral("fusionAiSuggestionStatus"), QStringLiteral("pending"));
    segment.insert(QStringLiteral("fusionAiSuggestionLanguage"), m_sourceLanguage);
    segment.insert(QStringLiteral("fusionAiSuggestionProvider"),
                   m_configuration.value(QStringLiteral("provider")).toString());
    segment.insert(QStringLiteral("fusionAiSuggestionModel"),
                   m_configuration.value(QStringLiteral("model")).toString());
    segment.insert(QStringLiteral("fusionAiSuggestionEvidence"), QVariantMap{
        {QStringLiteral("sttText"), segment.value(QStringLiteral("fusionSttText"))},
        {QStringLiteral("ocrText"), segment.value(QStringLiteral("fusionOcrText"))},
        {QStringLiteral("sttConfidence"), segment.value(QStringLiteral("sttConfidence"))},
        {QStringLiteral("ocrConfidence"), segment.value(QStringLiteral("ocrConfidence"))},
        {QStringLiteral("startMs"), segment.value(QStringLiteral("startMs"))},
        {QStringLiteral("endMs"), segment.value(QStringLiteral("endMs"))}
    });
    segment.insert(QStringLiteral("fusionNeedsReview"), true);
    segment.insert(QStringLiteral("fusionStatus"), QStringLiteral("conflict"));
    segment.insert(QStringLiteral("state"), QStringLiteral("needs-review"));
    m_segments[index] = segment;
    Logger::info(
        QStringLiteral("DubbingTranscriptReconciliation"),
        QStringLiteral("Suggestion stored for segment=%1 chars=%2 sourceLanguage=%3")
            .arg(segment.value(QStringLiteral("id")).toString())
            .arg(suggestion.size()).arg(m_sourceLanguage));
    finishReconciliationSegment(true);
}

void DubbingTranslationFixService::finishSegment(bool fixed, bool improved)
{
    if (fixed) ++m_fixedCount;
    else {
        ++m_unresolvedCount;
        if (improved) ++m_improvedCount;
    }
    ++m_segmentPosition;
    setProgress(qRound(m_segmentPosition * 100.0
                       / qMax(1, m_eligibleIndices.size())));
    beginSegment();
}

void DubbingTranslationFixService::finishReconciliationSegment(bool suggested)
{
    if (suggested) ++m_suggestedCount;
    else ++m_unresolvedCount;
    ++m_segmentPosition;
    setProgress(qRound(m_segmentPosition * 100.0
                       / qMax(1, m_eligibleIndices.size())));
    beginSegment();
}

void DubbingTranslationFixService::finishRun()
{
    if (m_reconciliation) {
        setProgress(100);
        setStatus(QStringLiteral("Prepared %1 AI suggestion(s); %2 conflict(s) remain without a suggestion. Review is still required.")
                      .arg(m_suggestedCount).arg(m_unresolvedCount));
        Logger::info(
            QStringLiteral("DubbingTranscriptReconciliation"),
            QStringLiteral("Completed suggested=%1 unresolved=%2 total=%3")
                .arg(m_suggestedCount).arg(m_unresolvedCount)
                .arg(m_eligibleIndices.size()));
        m_reconciliation = false;
        setBusy(false);
        emit reconciliationCompleted(m_segments, m_suggestedCount, m_unresolvedCount);
        return;
    }
    setProgress(100);
    setStatus(QStringLiteral("Fixed %1 segment(s); improved %2; %3 still need review.")
                  .arg(m_fixedCount).arg(m_improvedCount).arg(m_unresolvedCount));
    Logger::info(
        QStringLiteral("DubbingTranslationFix"),
        QStringLiteral("Rewrite completed fixed=%1 improved=%2 unresolved=%3 total=%4")
            .arg(m_fixedCount).arg(m_improvedCount).arg(m_unresolvedCount)
            .arg(m_eligibleIndices.size()));
    setBusy(false);
    emit completed(m_segments, m_fixedCount, m_unresolvedCount);
}

QString DubbingTranslationFixService::buildPrompt(
    const QVariantMap &segment) const
{
    if (m_reconciliation) return buildReconciliationPrompt(segment);
    const QVariantMap budget =
        segment.value(QStringLiteral("durationBudget")).toMap();
    const int maximum = budget.value(QStringLiteral("maxUnits")).toInt();
    const QString direction = m_promptPhonemes > maximum
        ? QStringLiteral("Shorten the wording without dropping any source meaning.")
        : QStringLiteral("Keep the wording inside the required range.");
    QString feedback;
    if (m_attempt > 0) {
        feedback = QStringLiteral(
            "\nThe previous rewrite had %1 phonemes and did not pass validation. "
            "Use a different construction and correct the length.")
                       .arg(m_lastCandidatePhonemes);
    }
    const QString tokens = protectedTokens(
        segment.value(QStringLiteral("sourceText")).toString())
                               .join(QStringLiteral(", "));
    return QStringLiteral(
               "Source language: %1\nTarget language: %2\n"
               "Original source:\n%3\n\n"
               "Faithful current translation:\n%4\n\n"
               "Rewrite starting point:\n%5\n\n"
               "External eSpeak NG measurement: %6 phonemes.\n"
               "Required range: %7-%8 phonemes; ideal target: %9 phonemes.\n"
               "Protected tokens that must remain exactly unchanged: %10\n"
               "Keep the translation as semantically faithful as possible. %11%12")
        .arg(m_sourceLanguage, m_targetLanguage,
             segment.value(QStringLiteral("sourceText")).toString(),
             m_originalTranslation, m_promptTranslation)
        .arg(m_promptPhonemes)
        .arg(budget.value(QStringLiteral("minUnits")).toInt())
        .arg(budget.value(QStringLiteral("maxUnits")).toInt())
        .arg(budget.value(QStringLiteral("targetUnits")).toInt())
        .arg(tokens.isEmpty() ? QStringLiteral("(none)") : tokens,
             direction, feedback);
}

QString DubbingTranslationFixService::buildReconciliationPrompt(
    const QVariantMap &segment) const
{
    const int currentIndex = m_eligibleIndices.value(m_segmentPosition, -1);
    QString previous;
    QString next;
    if (currentIndex > 0)
        previous = m_segments.at(currentIndex - 1).toMap()
                       .value(QStringLiteral("sourceText")).toString().trimmed();
    if (currentIndex >= 0 && currentIndex + 1 < m_segments.size())
        next = m_segments.at(currentIndex + 1).toMap()
                   .value(QStringLiteral("sourceText")).toString().trimmed();
    return QStringLiteral(
               "Source language: %1\n"
               "Time: %2-%3 ms\n"
               "STT observation (confidence %4):\n%5\n\n"
               "OCR observation (confidence %6):\n%7\n\n"
               "Previous transcript context:\n%8\n\n"
               "Next transcript context:\n%9\n\n"
               "Propose one source-language transcript for a human reviewer. Do not translate it. Return only the proposed text.")
        .arg(m_sourceLanguage,
             segment.value(QStringLiteral("startMs")).toString(),
             segment.value(QStringLiteral("endMs")).toString(),
             QString::number(segment.value(QStringLiteral("sttConfidence")).toDouble(), 'f', 2),
             segment.value(QStringLiteral("fusionSttText")).toString(),
             QString::number(segment.value(QStringLiteral("ocrConfidence")).toDouble(), 'f', 2),
             segment.value(QStringLiteral("fusionOcrText")).toString(),
             previous.isEmpty() ? QStringLiteral("(none)") : previous,
             next.isEmpty() ? QStringLiteral("(none)") : next);
}

QStringList DubbingTranslationFixService::protectedTokens(
    const QString &text) const
{
    QStringList result;
    const QRegularExpression expression(
        QStringLiteral("(?:https?://\\S+|\\b\\d[\\d.,/%-]*|\\b[A-Z]{2,}\\b)"));
    auto matches = expression.globalMatch(text);
    while (matches.hasNext()) result.append(matches.next().captured(0));
    result.removeDuplicates();
    return result;
}

bool DubbingTranslationFixService::preservesProtectedTokens(
    const QString &candidate, const QStringList &tokens) const
{
    for (const QString &token : tokens) {
        if (!candidate.contains(token, Qt::CaseInsensitive)) return false;
    }
    return true;
}

void DubbingTranslationFixService::applyCandidate(
    QVariantMap &segment, const QString &candidate, int phonemes,
    bool withinBudget) const
{
    const QVariantMap budget =
        segment.value(QStringLiteral("durationBudget")).toMap();
    if (!segment.contains(QStringLiteral("referenceTranslation")))
        segment.insert(QStringLiteral("referenceTranslation"), m_originalTranslation);
    segment.insert(QStringLiteral("targetText"), candidate);
    segment.insert(QStringLiteral("durationUnits"), phonemes);
    segment.insert(QStringLiteral("phonemeDistance"),
                   qAbs(phonemes - budget.value(QStringLiteral("targetUnits")).toInt()));
    segment.insert(QStringLiteral("durationStatus"), withinBudget
                       ? QStringLiteral("within-budget")
                       : QStringLiteral("needs-review"));
    segment.insert(QStringLiteral("durationMetric"), QStringLiteral("phoneme-distance"));
    segment.insert(QStringLiteral("candidateSelectionMetric"), withinBudget
                       ? QStringLiteral("lm-studio-qwen-rewrite-v1")
                       : QStringLiteral("lm-studio-closest-safe-rewrite-v1"));
    segment.insert(QStringLiteral("rewriteProvider"),
                   m_configuration.value(QStringLiteral("provider")));
    segment.insert(QStringLiteral("rewriteModel"),
                   m_configuration.value(QStringLiteral("model")));
    segment.insert(QStringLiteral("rewriteAttempts"), m_attempt);
    segment.insert(QStringLiteral("targetChunks"),
                   DubbingDurationPlanner::pauseChunks(
                       candidate, budget.value(QStringLiteral("pauses")).toList()));
    segment.insert(QStringLiteral("pauseAligned"), true);
    segment.insert(QStringLiteral("pauseAlignmentMethod"),
                   QStringLiteral("deterministic-even-split-v1"));
    segment.insert(QStringLiteral("state"), QStringLiteral("translated"));
}

void DubbingTranslationFixService::setBusy(bool busy)
{
    if (m_busy == busy) return;
    m_busy = busy;
    emit stateChanged();
}

void DubbingTranslationFixService::setProgress(int progress)
{
    const int normalized = qBound(0, progress, 100);
    if (m_progress == normalized) return;
    m_progress = normalized;
    emit stateChanged();
}

void DubbingTranslationFixService::setStatus(const QString &status)
{
    if (m_statusText == status) return;
    m_statusText = status;
    emit stateChanged();
}

void DubbingTranslationFixService::setError(const QString &message)
{
    m_lastError = message;
    m_statusText = message;
    Logger::error(QStringLiteral("DubbingTranslationFix"), message);
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (m_cliProcess) {
        m_cliProcess->kill();
        m_cliProcess->deleteLater();
        m_cliProcess = nullptr;
    }
    setBusy(false);
    if (reply) {
        reply->abort();
        reply->deleteLater();
    }
    emit stateChanged();
    emit failed(message);
}

} // namespace LAStudio
