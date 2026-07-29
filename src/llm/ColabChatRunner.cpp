#include "ColabChatRunner.h"

#include "remote/ColabWorkerClient.h"

#include <atomic>
#include <memory>

namespace LAStudio {

class ColabChatRunner::Private final
{
public:
    ColabWorkerClient client;
    std::shared_ptr<std::atomic_bool> cancellation;
};

ColabChatRunner::ColabChatRunner(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
}

ColabChatRunner::~ColabChatRunner() = default;

void ColabChatRunner::generate(const ColabChatRequest &request)
{
    QString error;
    if (!d->client.configure(request.workerUrl, request.bearerToken,
                             request.allowInsecureLocalhost, &error)) {
        emit failed(request.requestId, error);
        return;
    }
    d->cancellation = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancellation = d->cancellation;
    QString text;
    const bool ok = d->client.streamChat(
        request.messages, request.model, request.maxTokens, request.contextTokens,
        request.temperature, request.topP, request.topK, request.repeatPenalty,
        cancellation, [this, requestId = request.requestId](const QString &token) {
            emit tokenGenerated(requestId, token);
        }, &text, &error);
    d->cancellation.reset();
    if (cancellation->load(std::memory_order_relaxed)) {
        emit cancelled(request.requestId, text);
    } else if (ok) {
        emit finished(request.requestId, text);
    } else {
        emit failed(request.requestId, error.isEmpty() ? QStringLiteral("Colab chat request failed") : error);
    }
}

void ColabChatRunner::cancel()
{
    if (d->cancellation) d->cancellation->store(true, std::memory_order_relaxed);
    d->client.cancel();
}

} // namespace LAStudio
