#include "buffer.h"

#include <utility>

#include "../log.h"
#include "disk_buffer.h"
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

bool ensureDiskLoaded(BufferState& buffer, RequestStore& store) {
    if (buffer.disk.loaded) {
        return true;
    }

    return disk_buffer::load(buffer.disk, store);
}

bool hasDiskBacklog(BufferState& buffer, RequestStore& store) {
    return ensureDiskLoaded(buffer, store) && buffer.disk.count > 0;
}

BufferInsertResult bufferToDisk(
    BufferState& buffer,
    const BufferedRequest& request,
    const ApiBufferConfig& config,
    RequestStore& store
) {
    if (!disk_buffer::enqueue(buffer.disk, request, config, store)) {
        logLine(
            LogLevel::Warn,
            "Disk buffer full; dropping new request to " + request.path +
            " for " + request.mac
        );
        return BufferInsertResult::DroppedNewRequestBufferFull;
    }

    logLine(
        LogLevel::Warn,
        "Buffered API request on disk: " +
        std::to_string(buffer.disk.count) + " queued"
    );

    return BufferInsertResult::Buffered;
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
        ", remaining RAM " + std::to_string(buffer.requests.size()) +
        ", disk " + std::to_string(buffer.disk.count)
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
        ", remaining RAM " + std::to_string(buffer.requests.size()) +
        ", disk " + std::to_string(buffer.disk.count)
    );
}

} // namespace

BufferInsertResult bufferRequest(
    BufferState& buffer,
    BufferedRequest request,
    const ApiBufferConfig& config,
    RequestStore& store
) {
    if (hasDiskBacklog(buffer, store)) {
        return bufferToDisk(buffer, request, config, store);
    }

    if (buffer.requests.size() >= static_cast<std::size_t>(config.inMemory)) {
        return bufferToDisk(buffer, request, config, store);
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
    const ApiPoster& poster,
    RequestStore& store
) {
    BufferDrainResult result;

    if (now < buffer.nextDrainAllowedAtEpochS) {
        result.notDueYet = true;
        return result;
    }

    if (buffer.requests.empty() && !hasDiskBacklog(buffer, store)) {
        return result;
    }

    logLine(
        LogLevel::Info,
        "Draining API buffer: RAM " + std::to_string(buffer.requests.size()) +
        ", disk " + std::to_string(buffer.disk.count) +
        " queued"
    );

    buffer.nextDrainAllowedAtEpochS =
        now + static_cast<std::time_t>(config.drainRateTickS);

    for (int i = 0; i < config.drainRateCap; ++i) {
        if (!buffer.requests.empty()) {
            BufferedRequest& request = buffer.requests.front();
            const network::HttpResponse response = poster.postJson(request.path, request.body);
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
            continue;
        }

        if (!hasDiskBacklog(buffer, store)) {
            break;
        }

        BufferedRequest request;
        if (!disk_buffer::peek(buffer.disk, request, store)) {
            logLine(LogLevel::Warn, "Dropping corrupt disk-buffered API request");
            if (disk_buffer::dropFront(buffer.disk, store)) {
                result.dropped += 1;
                continue;
            }
            result.blockedByRetryableFailure = true;
            return result;
        }

        const int timeoutRetryCount = request.timeoutRetryCount;
        const int tlsRetryCount = request.tlsRetryCount;
        const network::HttpResponse response = poster.postJson(request.path, request.body);
        result.attempted += 1;

        const BufferDecision decision = decideBufferedResponse(request, response);

        if (decision == BufferDecision::Sent) {
            if (!disk_buffer::consume(buffer.disk, store)) {
                result.blockedByRetryableFailure = true;
                return result;
            }
            result.sent += 1;
            continue;
        }

        if (decision == BufferDecision::KeepBuffered) {
            if ((request.timeoutRetryCount != timeoutRetryCount ||
                 request.tlsRetryCount != tlsRetryCount) &&
                !disk_buffer::rewriteFront(buffer.disk, request, store)) {
                result.blockedByRetryableFailure = true;
                return result;
            }

            buffer.nextDrainAllowedAtEpochS =
                now + static_cast<std::time_t>(config.drainRateTickS);

            result.blockedByRetryableFailure = true;
            logDrainPaused(request, response, result, buffer);
            return result;
        }

        logDroppedBufferedRequest(request, response);
        if (!disk_buffer::dropFront(buffer.disk, store)) {
            result.blockedByRetryableFailure = true;
            return result;
        }
        result.dropped += 1;
    }

    logDrainSummary(result, buffer);
    return result;
}

} // namespace api
