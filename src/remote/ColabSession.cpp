#include "ColabSession.h"

#include "ExecutionProvider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace LAStudio {

namespace {

QString normalizedVariant(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    // Existing exact-model notebooks predate the optional variant field.  They
    // intentionally expose one immutable configuration, represented by
    // "fixed" instead of pretending that Local CPU file variants apply.
    return normalized.isEmpty() ? QStringLiteral("fixed") : normalized;
}

QString requiredResponseContract(const QString &capability)
{
    // v3 retries blank exact-model output, then returns a non-empty source
    // preserving needs-review patch so later dubbing segments can continue.
    // Older notebooks turn that same condition into an HTTP 503.
    if (capability == QStringLiteral("translation"))
        return QStringLiteral("translation-patches-v3");
    if (capability == QStringLiteral("subtitle-ocr"))
        return QStringLiteral("subtitle-ocr-crops-v1");
    return {};
}

QString requiredWorkerRevision(const QString &capability)
{
    // v3 continues after a remaining blank result instead of failing the
    // whole translation request. Prior workers reproduce the old HTTP 503.
    if (capability == QStringLiteral("translation"))
        return QStringLiteral("translation-2026-07-30.3");
    // STT v2 uploads media in chunks and reports an asynchronous job state.
    // Older notebooks may accept a connection but then use the legacy
    // endpoint, which makes the app wait on a response that never reaches
    // the selected exact-model worker.
    if (capability == QStringLiteral("stt"))
        return QStringLiteral("stt-2026-07-30.2");
    if (capability == QStringLiteral("subtitle-ocr"))
        return QStringLiteral("subtitle-ocr-2026-08-01.1");
    return {};
}

QString capabilityDisplayName(const QString &capability)
{
    if (capability == QStringLiteral("translation"))
        return QStringLiteral("Translation");
    if (capability == QStringLiteral("stt"))
        return QStringLiteral("Speech-to-Text");
    if (capability == QStringLiteral("subtitle-ocr"))
        return QStringLiteral("Subtitle OCR");
    return capability.isEmpty() ? QStringLiteral("selected") : capability;
}

QString outdatedNotebookMessage(const QString &capability)
{
    return QStringLiteral(
        "The selected %1 notebook is outdated. Open the current exact-model notebook, "
        "run all cells again, then use Check Colab.")
        .arg(capabilityDisplayName(capability));
}

} // namespace

ColabSession::ColabSession(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

QString ColabSession::workerUrl() const
{
    return m_endpoint.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
}

QUrl ColabSession::endpoint() const
{
    return m_endpoint;
}

bool ColabSession::isActive() const
{
    return m_verified && m_endpoint.isValid() && !m_bearerToken.isEmpty();
}

bool ColabSession::hasVerifiedRoute(const QString &capability, const QString &model,
                                    QString *errorMessage, const QString &variant) const
{
    if (errorMessage) errorMessage->clear();
    const QString requiredCapability = capability.trimmed().toLower();
    const QString requiredModel = model.trimmed().toLower();
    const QString requiredVariant = normalizedVariant(variant);
    if (requiredCapability.isEmpty() || requiredModel.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("An exact Colab capability and model are required before dispatching work.");
        }
        return false;
    }
    if (!isActive()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Connect a verified Colab GPU worker before running this feature.");
        }
        return false;
    }

#ifdef LASTUDIO_UNIT_TESTS
    // setSession() is deliberately available only as a low-level test seam.
    // Unit tests that exercise HTTP runners without a real notebook have no
    // capability document to bind.  Production builds never take this path.
    if (m_expectedCapability.isEmpty() && m_expectedModel.isEmpty()) return true;
#endif

    if (m_expectedCapability.isEmpty() || m_expectedModel.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "This Colab worker was not verified for an exact capability and model. Reconnect it from the selected model.");
        }
        return false;
    }
    if (m_expectedCapability != requiredCapability || m_expectedModel != requiredModel) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Wrong Colab worker: this request needs '%1 / %2', but the connected worker is verified for '%3 / %4'. Reconnect the notebook for the selected model.")
                                .arg(requiredCapability, requiredModel,
                                     m_expectedCapability, m_expectedModel);
        }
        return false;
    }
    if (m_expectedVariant != requiredVariant) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Wrong Colab configuration: this worker was checked for variant '%1', but '%2' is required.")
                                .arg(m_expectedVariant, requiredVariant);
        }
        return false;
    }
    return true;
}

bool ColabSession::connectTemporaryWorker(const QString &workerUrl,
                                          const QString &bearerToken)
{
    return connectTemporaryWorker(workerUrl, bearerToken, {}, {});
}

bool ColabSession::connectTemporaryWorker(const QString &workerUrl,
                                          const QString &bearerToken,
                                          const QString &expectedCapability,
                                          const QString &expectedModel,
                                          const QString &expectedVariant)
{
    QString error;
    if (!beginVerifiedSession(workerUrl, bearerToken, expectedCapability,
                              expectedModel, &error, false, expectedVariant)) {
        setLastError(error);
        return false;
    }
    return true;
}

bool ColabSession::checkConnection()
{
    if (m_checking) return false;
    if (!m_endpoint.isValid() || m_bearerToken.isEmpty()) {
        setLastError(QStringLiteral("Connect a Colab worker before checking it"));
        return false;
    }
    if (m_expectedCapability.isEmpty() || m_expectedModel.isEmpty()) {
        setLastError(QStringLiteral(
            "Reconnect this worker with its exact capability and model before checking it"));
        return false;
    }
    QString error;
    const bool started = beginVerifiedSession(m_endpoint.toString(), m_bearerToken,
                                              m_expectedCapability, m_expectedModel,
                                              &error, m_allowInsecureLocalhostForTests,
                                              m_expectedVariant);
    if (!started) setLastError(error);
    return started;
}

void ColabSession::disconnectTemporaryWorker()
{
    clear();
}

bool ColabSession::beginVerifiedSession(const QString &workerUrl,
                                        const QString &bearerToken,
                                        const QString &expectedCapability,
                                        const QString &expectedModel,
                                        QString *errorMessage,
                                        bool allowInsecureLocalhost,
                                        const QString &expectedVariant)
{
    const RemoteEndpointValidation validated = validateRemoteEndpoint(
        workerUrl, RemoteEndpointKind::ColabWorker, allowInsecureLocalhost);
    const QString normalizedToken = bearerToken.trimmed();
    const QString capability = expectedCapability.trimmed().toLower();
    const QString model = expectedModel.trimmed().toLower();
    const QString variant = normalizedVariant(expectedVariant);
    if (!validated.isValid()) {
        if (errorMessage) *errorMessage = validated.error;
        return false;
    }
    if (normalizedToken.isEmpty()) {
        const QString error = QStringLiteral("Colab worker bearer token is required");
        if (errorMessage) *errorMessage = error;
        return false;
    }
    if (capability.isEmpty() != model.isEmpty()) {
        const QString error = QStringLiteral(
            "Both the expected Colab capability and exact model are required");
        if (errorMessage) *errorMessage = error;
        return false;
    }

    const bool endpointChanged = m_endpoint != validated.normalizedUrl;
    const bool tokenChanged = m_bearerToken != normalizedToken;
    const bool routeChanged = m_expectedCapability != capability
        || m_expectedModel != model || m_expectedVariant != variant;
    cancelVerification();
    const quint64 generation = ++m_verificationGeneration;
    m_endpoint = validated.normalizedUrl;
    m_bearerToken = normalizedToken;
    m_expectedCapability = capability;
    m_expectedModel = model;
    m_expectedVariant = variant;
    m_allowInsecureLocalhostForTests = allowInsecureLocalhost;
    m_reportedGpu.clear();
    m_verifiedAt = {};
    m_verified = false;
    m_checking = true;
    m_verificationState = QStringLiteral("checking");
    m_verificationMessage = capability.isEmpty()
        ? QStringLiteral("Checking Colab CUDA worker health...")
        : QStringLiteral("Checking CUDA worker for %1 / %2...")
              .arg(capability, model);
    setLastError({});
    if (endpointChanged || tokenChanged || routeChanged) emit sessionChanged();
    emit verificationChanged();
    requestVerificationDocument(VerificationStage::Health, generation);
    return true;
}

bool ColabSession::setSession(const QString &workerUrl, const QString &bearerToken,
                              QString *errorMessage, bool allowInsecureLocalhost)
{
    const RemoteEndpointValidation validated = validateRemoteEndpoint(
        workerUrl, RemoteEndpointKind::ColabWorker, allowInsecureLocalhost);
    const QString normalizedToken = bearerToken.trimmed();
    if (!validated.isValid()) {
        if (errorMessage) *errorMessage = validated.error;
        return false;
    }
    if (normalizedToken.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Colab worker bearer token is required");
        return false;
    }

    const bool changed = m_endpoint != validated.normalizedUrl
        || m_bearerToken != normalizedToken || !m_verified || m_checking;
    cancelVerification();
    ++m_verificationGeneration;
    m_endpoint = validated.normalizedUrl;
    m_bearerToken = normalizedToken;
    m_expectedCapability.clear();
    m_expectedModel.clear();
    m_expectedVariant.clear();
    m_allowInsecureLocalhostForTests = allowInsecureLocalhost;
    m_reportedGpu.clear();
    m_verifiedAt = {};
    m_checking = false;
    m_verified = true;
    m_verificationState = QStringLiteral("trusted");
    m_verificationMessage = QStringLiteral("Trusted internal session");
    setLastError({});
    if (changed) {
        emit verificationChanged();
        emit sessionChanged();
    }
    return true;
}

void ColabSession::clear()
{
    const bool changed = m_endpoint.isValid() || !m_bearerToken.isEmpty()
        || m_checking || m_verified || m_verificationState != QStringLiteral("disconnected");
    if (!changed) return;
    cancelVerification();
    ++m_verificationGeneration;
    m_endpoint = {};
    m_bearerToken.clear();
    m_expectedCapability.clear();
    m_expectedModel.clear();
    m_expectedVariant.clear();
    m_reportedGpu.clear();
    m_verifiedAt = {};
    m_allowInsecureLocalhostForTests = false;
    m_checking = false;
    m_verified = false;
    m_verificationState = QStringLiteral("disconnected");
    m_verificationMessage.clear();
    setLastError({});
    emit verificationChanged();
    emit sessionChanged();
}

QString ColabSession::bearerTokenForRequest() const
{
    return m_bearerToken;
}

void ColabSession::cancelVerification()
{
    if (!m_verificationReply) return;
    disconnect(m_verificationReply, nullptr, this, nullptr);
    m_verificationReply->abort();
    m_verificationReply->deleteLater();
    m_verificationReply.clear();
}

void ColabSession::requestVerificationDocument(VerificationStage stage,
                                               quint64 generation)
{
    if (generation != m_verificationGeneration || !m_network) return;
    const QString path = stage == VerificationStage::Health
        ? QStringLiteral("health")
        : QStringLiteral("v1/capabilities");
    QNetworkRequest request(appendRemotePath(m_endpoint, path));
    request.setRawHeader("Authorization",
                         QByteArrayLiteral("Bearer ") + m_bearerToken.toUtf8());
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15'000);
    QNetworkReply *reply = m_network->get(request);
    m_verificationReply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, stage, generation] {
        handleVerificationReply(reply, stage, generation);
    });
}

void ColabSession::handleVerificationReply(QNetworkReply *reply,
                                           VerificationStage stage,
                                           quint64 generation)
{
    if (!reply) return;
    const bool current = generation == m_verificationGeneration
        && reply == m_verificationReply;
    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    if (!current) return;
    m_verificationReply.clear();

    const QString stageName = stage == VerificationStage::Health
        ? QStringLiteral("health")
        : QStringLiteral("capability");
    if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
        QString detail = reply->errorString().trimmed();
        const QJsonDocument errorDocument = QJsonDocument::fromJson(body);
        if (errorDocument.isObject()) {
            const QJsonValue value = errorDocument.object().value(QStringLiteral("detail"));
            if (value.isString() && !value.toString().trimmed().isEmpty())
                detail = value.toString().trimmed();
        }
        failVerification(QStringLiteral("Colab worker %1 check failed%2: %3")
                             .arg(stageName,
                                  status > 0 ? QStringLiteral(" (HTTP %1)").arg(status) : QString(),
                                  detail.isEmpty() ? QStringLiteral("network error") : detail),
                         generation);
        return;
    }
    if (body.size() > 1024 * 1024) {
        failVerification(QStringLiteral("Colab worker %1 response is too large")
                             .arg(stageName),
                         generation);
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        failVerification(QStringLiteral("Colab worker %1 response is not valid JSON")
                             .arg(stageName),
                         generation);
        return;
    }
    const QJsonObject root = document.object();

    if (stage == VerificationStage::Health) {
        const QString device = root.value(QStringLiteral("device")).toString()
                                   .trimmed().toLower();
        if (!root.value(QStringLiteral("ready")).toBool(false)) {
            failVerification(QStringLiteral("Colab worker is not ready"), generation);
            return;
        }
        if (!device.startsWith(QStringLiteral("cuda"))
            || root.value(QStringLiteral("cpu_fallback")).toBool(false)) {
            failVerification(QStringLiteral(
                "Colab worker did not confirm CUDA GPU execution; CPU workers are rejected"),
                             generation);
            return;
        }
        const QString expectedRevision = requiredWorkerRevision(m_expectedCapability);
        const QString reportedRevision = root.value(QStringLiteral("worker_revision"))
                                             .toString().trimmed();
        if (!expectedRevision.isEmpty() && reportedRevision != expectedRevision) {
            failVerification(outdatedNotebookMessage(m_expectedCapability), generation);
            return;
        }
        const QString reportedModel = root.value(QStringLiteral("model")).toString()
                                          .trimmed().toLower();
        if (!m_expectedModel.isEmpty() && reportedModel != m_expectedModel) {
            failVerification(QStringLiteral(
                "Wrong Colab model: app selected '%1' but worker loaded '%2'")
                                 .arg(m_expectedModel,
                                      reportedModel.isEmpty()
                                          ? QStringLiteral("<not reported>")
                                          : reportedModel),
                             generation);
            return;
        }
        const QString reportedVariant = normalizedVariant(
            root.value(QStringLiteral("variant")).toString());
        if (!m_expectedVariant.isEmpty() && reportedVariant != m_expectedVariant) {
            failVerification(QStringLiteral(
                "Wrong Colab configuration: app selected variant '%1' but worker loaded '%2'")
                                 .arg(m_expectedVariant, reportedVariant),
                             generation);
            return;
        }
        m_reportedGpu = root.value(QStringLiteral("gpu")).toString().trimmed();
        m_verificationMessage = QStringLiteral("CUDA health passed; checking exact capability...");
        emit verificationChanged();
        requestVerificationDocument(VerificationStage::Capabilities, generation);
        return;
    }

    if (root.value(QStringLiteral("contract_version")).toInt(-1) != 1) {
        failVerification(QStringLiteral(
            "Unsupported Colab worker contract; contract_version must be 1"),
                         generation);
        return;
    }
    const QString rootDevice = root.value(QStringLiteral("device")).toString()
                                   .trimmed().toLower();
    if (!rootDevice.startsWith(QStringLiteral("cuda"))) {
        failVerification(QStringLiteral(
            "Colab capability catalog did not confirm a CUDA device"),
                         generation);
        return;
    }
    const QString expectedRevision = requiredWorkerRevision(m_expectedCapability);
    const QString reportedRevision = root.value(QStringLiteral("worker_revision"))
                                         .toString().trimmed();
    if (!expectedRevision.isEmpty() && reportedRevision != expectedRevision) {
        failVerification(outdatedNotebookMessage(m_expectedCapability), generation);
        return;
    }

    const QJsonArray capabilities = root.value(QStringLiteral("capabilities")).toArray();
    bool capabilityFound = m_expectedCapability.isEmpty();
    bool modelFound = m_expectedModel.isEmpty();
    bool exactRouteUsesCuda = false;
    bool exactModelLoaded = false;
    for (const QJsonValue &capabilityValue : capabilities) {
        const QJsonObject capability = capabilityValue.toObject();
        const QString capabilityId = capability.value(QStringLiteral("id")).toString()
                                         .trimmed().toLower();
        if (!m_expectedCapability.isEmpty()
            && capabilityId != m_expectedCapability) {
            continue;
        }
        capabilityFound = true;
        const QJsonArray models = capability.value(QStringLiteral("models")).toArray();
        for (const QJsonValue &modelValue : models) {
            const QJsonObject model = modelValue.toObject();
            const QString modelId = model.value(QStringLiteral("id")).toString()
                                        .trimmed().toLower();
            if (!m_expectedModel.isEmpty() && modelId != m_expectedModel) continue;
            modelFound = true;
            const QString reportedVariant = normalizedVariant(
                model.value(QStringLiteral("variant")).toString());
            if (!m_expectedVariant.isEmpty() && reportedVariant != m_expectedVariant) {
                failVerification(QStringLiteral(
                    "Wrong Colab configuration: exact model '%1' has variant '%2', but '%3' was selected")
                                     .arg(m_expectedModel, reportedVariant, m_expectedVariant),
                                 generation);
                return;
            }
            const QString device = model.value(QStringLiteral("device")).toString()
                                       .trimmed().toLower();
            // The feature is bound to this one model, not to a generic
            // capability.  Do not infer its readiness from a worker-wide or
            // capability-wide CUDA claim: the model entry itself must prove
            // that it is loaded on CUDA.
            exactRouteUsesCuda = device.startsWith(QStringLiteral("cuda"));
            exactModelLoaded = model.contains(QStringLiteral("loaded"))
                && model.value(QStringLiteral("loaded")).toBool(false);
            const QString expectedResponseContract = requiredResponseContract(m_expectedCapability);
            const QString reportedResponseContract = model.value(
                QStringLiteral("response_contract")).toString().trimmed();
            if (!expectedResponseContract.isEmpty()
                && reportedResponseContract != expectedResponseContract) {
                failVerification(QStringLiteral(
                    "The selected %1 notebook uses an outdated response contract. "
                    "Open the current exact-model notebook, run it again, then use Check Colab.")
                                     .arg(m_expectedCapability),
                                 generation);
                return;
            }
            break;
        }
        if (modelFound && exactRouteUsesCuda && exactModelLoaded) break;
    }
    if (!capabilityFound) {
        failVerification(QStringLiteral("Wrong Colab worker: capability '%1' is missing")
                             .arg(m_expectedCapability),
                         generation);
        return;
    }
    if (!modelFound) {
        failVerification(QStringLiteral(
            "Wrong Colab worker: exact model '%1' is not advertised for '%2'")
                             .arg(m_expectedModel, m_expectedCapability),
                         generation);
        return;
    }
    if (!exactRouteUsesCuda) {
        failVerification(QStringLiteral(
            "The selected Colab capability/model is not advertised on CUDA"),
                         generation);
        return;
    }
    if (!exactModelLoaded) {
        failVerification(QStringLiteral("The selected Colab model is not loaded"),
                         generation);
        return;
    }
    finishVerification(generation);
}

void ColabSession::failVerification(const QString &message, quint64 generation)
{
    if (generation != m_verificationGeneration) return;
    cancelVerification();
    m_bearerToken.clear();
    m_checking = false;
    m_verified = false;
    m_verifiedAt = {};
    m_verificationState = QStringLiteral("failed");
    m_verificationMessage = message;
    setLastError(message);
    emit verificationChanged();
    emit sessionChanged();
    emit verificationFinished(false, message);
}

void ColabSession::finishVerification(quint64 generation)
{
    if (generation != m_verificationGeneration) return;
    m_checking = false;
    m_verified = true;
    m_verifiedAt = QDateTime::currentDateTimeUtc();
    m_verificationState = QStringLiteral("ready");
    m_verificationMessage = m_expectedCapability.isEmpty()
        ? QStringLiteral("Verified direct Colab CUDA worker")
        : QStringLiteral("Verified CUDA worker for %1 / %2 / %3%4")
              .arg(m_expectedCapability, m_expectedModel, m_expectedVariant,
                   m_reportedGpu.isEmpty()
                       ? QString()
                       : QStringLiteral(" on %1").arg(m_reportedGpu));
    setLastError({});
    emit verificationChanged();
    emit sessionChanged();
    emit verificationFinished(true, m_verificationMessage);
}

void ColabSession::setLastError(const QString &message)
{
    if (m_lastError == message) return;
    m_lastError = message;
    emit sessionErrorChanged();
}

} // namespace LAStudio
