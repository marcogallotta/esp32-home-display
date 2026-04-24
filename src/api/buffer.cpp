#include "buffer.h"

#include <utility>

#include "../log.h"
#include "client.h"
#include "dropped_log.h"

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

std::string droppedReason(
    const BufferedRequest& request,
    const network::HttpResponse& response
) {
    if (response.transport == network::TransportResult::Timeout) {
        return request.timeoutRetryCount >= 2
            ? "transport_timeout_retries_exhausted"
            : "transport_timeout";
    }

    if (response.transport == network::TransportResult::TlsError) {
        return request.tlsRetryCount >= 2
            ? "transport_tls_retries_exhausted"
            : "transport_tls_error";
    }

    if (response.transport == network::TransportResult::InternalError) {
        return "transport_internal_error";
    }

    if (response.transport == network::TransportResult::NetworkError) {
        return "transport_network_error";
    }

    if (response.transport == network::TransportResult::Ok) {
        return "http_" + std::to_string(response.statusCode);
    }

    return "dropped";
}

void logDroppedBufferedRequest(
    const BufferedRequest& request,
    const network::HttpResponse& response
) {
    logLine(
        LogLevel::Warn,
        "Dropping buffered request to " + request.path +
        " for " + request.mac +
        ": " + transportResultName(response.transport) +
        ", HTTP " + std::to_string(response.statusCode) +
        ", timeout retries " + std::to_string(request.timeoutRetryCount) +
        ", TLS retries " + std::to_string(request.tlsRetryCount) +
        ", " + response.error
    );

    dropped_log::appendDroppedRequest(
        droppedReason(request, response),
        request.path,
        request.mac,
        request.body,
        response.statusCode,
        static_cast<int>(response.transport),
        response.error,
        request.timeoutRetryCount,
        request.tlsRetryCount
    );
}

void logDrainPaused(
    const BufferedRequest& request,
    const network::HttpResponse& response,
    const BufferDrainResult& result,
    const BufferState& buffer
) {
    logLine(
        LogLevel::Warn,
        "Buffer drain paused at " + request.path +
        " for " + request.mac +
        ": " + transportResultName(response.transport) +
        ", HTTP " + std::to_string(response.statusCode) +
        ", " + response.error +
        ". Sent " + std::to_string(result.sent) +
        ", dropped " + std::to_string(result.dropped) +
        ", remaining " + std::to_string(buffer.requests.size())
    );
}

void logDrainSummary(const BufferDrainResult& result, const BufferState& buffer) {
    if (result.attempted == 0) {
        return;
    }

    logLine(
        LogLevel::Info,
        "Buffer drain complete: sent " + std::to_string(result.sent) +
        ", dropped " + std::to_string(result.dropped) +
        ", remaining " + std::to_string(buffer.requests.size())
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
            " for " + request.mac +
            " (" + std::to_string(buffer.requests.size()) +
            "/" + std::to_string(config.inMemory) + ")"
        );
        return BufferInsertResult::DroppedNewRequestBufferFull;
    }

    const bool wasEmpty = buffer.requests.empty();
    buffer.requests.push_back(std::move(request));

    if (wasEmpty) {
        logLine(
            LogLevel::Warn,
            "Network issue; buffering API requests (" +
            std::to_string(buffer.requests.size()) +
            "/" + std::to_string(config.inMemory) + " queued)"
        );
    }

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

    if (buffer.requests.empty()) {
        return result;
    }

    logLine(
        LogLevel::Info,
        "Draining API buffer: " + std::to_string(buffer.requests.size()) +
        " queued"
    );

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
            buffer.requests.pop_front();
            result.sent += 1;
            continue;
        }

        if (decision == BufferDecision::KeepBuffered) {
            buffer.nextDrainAllowedAtEpochS =
                now + static_cast<std::time_t>(config.drainRateTickS);

            result.blockedByRetryableFailure = true;
            logDrainPaused(request, response, result, buffer);
            return result;
        }

        logDroppedBufferedRequest(request, response);
        buffer.requests.pop_front();
        result.dropped += 1;
    }

    logDrainSummary(result, buffer);
    return result;
}

} // namespace api
