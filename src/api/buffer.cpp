#include "buffer.h"

#include <utility>

#include "client.h"
#include "../log.h"

namespace api {
namespace {

enum class BufferDecision {
    Sent,
    KeepBuffered,
    Drop,
};

bool isSuccessfulSend(const network::HttpResponse& response) {
    return response.transport == network::TransportResult::Ok &&
           response.statusCode >= 200 &&
           response.statusCode < 300;
}

BufferDecision decideBufferedResponse(
    BufferedRequest& request,
    const network::HttpResponse& response
) {
    if (isSuccessfulSend(response)) {
        return BufferDecision::Sent;
    }

    if (response.transport == network::TransportResult::NetworkError) {
        return BufferDecision::KeepBuffered;
    }

    if (response.transport == network::TransportResult::Timeout) {
        if (request.timeoutRetryCount < 2) {
            request.timeoutRetryCount += 1;
            return BufferDecision::KeepBuffered;
        }
        return BufferDecision::Drop;
    }

    if (response.transport == network::TransportResult::TlsError) {
        if (request.tlsRetryCount < 2) {
            request.tlsRetryCount += 1;
            return BufferDecision::KeepBuffered;
        }
        return BufferDecision::Drop;
    }

    if (response.transport == network::TransportResult::InternalError) {
        return BufferDecision::Drop;
    }

    if (response.statusCode >= 400 && response.statusCode <= 499) {
        return BufferDecision::Drop;
    }

    if (response.statusCode >= 502 && response.statusCode <= 504) {
        return BufferDecision::KeepBuffered;
    }

    return BufferDecision::Drop;
}

void logDroppedBufferedRequest(
    const BufferedRequest& request,
    const network::HttpResponse& response
) {
    logLine(
        LogLevel::Warn,
        "Dropping buffered request to " + request.path +
        ": " + transportResultName(response.transport) +
        ", HTTP " + std::to_string(response.statusCode) +
        ", timeout retries " + std::to_string(request.timeoutRetryCount) +
        ", TLS retries " + std::to_string(request.tlsRetryCount) +
        ", " + response.error
    );
}

} // namespace

BufferInsertResult bufferRequest(
    BufferState& buffer,
    BufferedRequest request,
    const ApiBufferConfig& config
) {
    if (buffer.requests.size() >= static_cast<std::size_t>(config.inMemory)) {
        logLine(
            LogLevel::Warn,
            "Buffer full; dropping new request to " + request.path +
            " (" + std::to_string(buffer.requests.size()) +
            "/" + std::to_string(config.inMemory) + ")"
        );
        return BufferInsertResult::DroppedNewRequestBufferFull;
    }

    const std::string path = request.path;
    buffer.requests.push_back(std::move(request));

    logLine(
        LogLevel::Info,
        "Buffered request to " + path +
        " (" + std::to_string(buffer.requests.size()) + " queued)"
    );

    return BufferInsertResult::Buffered;
}

BufferDrainResult maybeDrainBuffer(
    BufferState& buffer,
    std::time_t now,
    const ApiBufferConfig& config,
    const Client& client
) {
    BufferDrainResult result;

    if (now < buffer.nextDrainAllowedAtEpochS) {
        result.notDueYet = true;
        return result;
    }

    if (!buffer.requests.empty()) {
        logLine(
            LogLevel::Debug,
            "Draining API buffer: " + std::to_string(buffer.requests.size()) +
            " queued, cap " + std::to_string(config.drainRateCap)
        );
    }

    buffer.nextDrainAllowedAtEpochS =
        now + static_cast<std::time_t>(config.drainRateTickS);

    for (int i = 0; i < config.drainRateCap; ++i) {
        if (buffer.requests.empty()) {
            break;
        }

        BufferedRequest& request = buffer.requests.front();
        const network::HttpResponse response = client.postJson(request.path, request.body);
        result.attempted += 1;

        const BufferDecision decision = decideBufferedResponse(request, response);

        if (decision == BufferDecision::Sent) {
            const std::string path = request.path;
            buffer.requests.pop_front();
            result.sent += 1;

            logLine(
                LogLevel::Info,
                "Sent buffered request to " + path +
                " (" + std::to_string(buffer.requests.size()) + " remaining)"
            );

            continue;
        }

        if (decision == BufferDecision::KeepBuffered) {
            buffer.nextDrainAllowedAtEpochS =
                now + static_cast<std::time_t>(config.drainRateTickS);

            logLine(
                LogLevel::Warn,
                "Buffered request blocked: " + request.path +
                ", " + transportResultName(response.transport) +
                ", HTTP " + std::to_string(response.statusCode) +
                ", " + response.error
            );

            result.blockedByRetryableFailure = true;
            break;
        }

        logDroppedBufferedRequest(request, response);
        buffer.requests.pop_front();
        result.dropped += 1;
    }

    return result;
}

} // namespace api
