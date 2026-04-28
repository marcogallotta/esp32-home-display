#include "buffered_client.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "../log.h"
#include "../platform.h"
#include "dropped_log.h"
#include "payloads.h"
#include "response_policy.h"

namespace api {
namespace {

enum class FreshRequestDecision {
    Sent,
    Buffer,
    DropPermanent,
};

enum class BufferedRequestDecision {
    Sent,
    KeepBuffered,
    Drop,
};

FreshRequestDecision decideFreshResponse(const network::HttpResponse& response) {
    switch (classifyApiResponse(response)) {
        case ApiResponseKind::Accepted:
            return FreshRequestDecision::Sent;

        case ApiResponseKind::TransportNetworkError:
        case ApiResponseKind::TransportTimeout:
        case ApiResponseKind::TransportTlsError:
        case ApiResponseKind::HttpRetryableServerError:
            return FreshRequestDecision::Buffer;

        case ApiResponseKind::TransportInternalError:
        case ApiResponseKind::HttpClientError:
        case ApiResponseKind::HttpPermanentServerError:
        case ApiResponseKind::HttpUnexpectedStatus:
            return FreshRequestDecision::DropPermanent;
    }

    return FreshRequestDecision::DropPermanent;
}

BufferedRequestDecision decideBufferedResponse(
    BufferedRequest& request,
    const network::HttpResponse& response
) {
    switch (classifyApiResponse(response)) {
        case ApiResponseKind::Accepted:
            return BufferedRequestDecision::Sent;

        case ApiResponseKind::TransportNetworkError:
        case ApiResponseKind::HttpRetryableServerError:
            return BufferedRequestDecision::KeepBuffered;

        case ApiResponseKind::TransportTimeout:
            if (request.timeoutRetryCount < 2) {
                request.timeoutRetryCount += 1;
                return BufferedRequestDecision::KeepBuffered;
            }
            return BufferedRequestDecision::Drop;

        case ApiResponseKind::TransportTlsError:
            if (request.tlsRetryCount < 2) {
                request.tlsRetryCount += 1;
                return BufferedRequestDecision::KeepBuffered;
            }
            return BufferedRequestDecision::Drop;

        case ApiResponseKind::TransportInternalError:
        case ApiResponseKind::HttpClientError:
        case ApiResponseKind::HttpPermanentServerError:
        case ApiResponseKind::HttpUnexpectedStatus:
            return BufferedRequestDecision::Drop;
    }

    return BufferedRequestDecision::Drop;
}

WriteStatus mapBufferInsertResult(BufferInsertResult result) {
    return result == BufferInsertResult::Buffered
        ? WriteStatus::Buffered
        : WriteStatus::DroppedBufferFull;
}

std::string invalidTimestampReason(const std::optional<std::int64_t>& epochS) {
    if (!epochS.has_value()) {
        return "missing timestamp";
    }

    if (*epochS <= 0) {
        return "invalid timestamp " + std::to_string(*epochS);
    }

    return "timestamp invalid";
}

std::string invalidSwitchbotPayloadReason(const SwitchbotReading& reading) {
    if (!reading.temperatureC.has_value()) {
        return "missing temperature";
    }

    if (!reading.humidityPct.has_value()) {
        return "missing humidity";
    }

    if (!reading.lastSeenEpochS.has_value() || *reading.lastSeenEpochS <= 0) {
        return invalidTimestampReason(reading.lastSeenEpochS);
    }

    return "unknown invalid payload";
}

std::string invalidXiaomiPayloadReason(const XiaomiReading& reading) {
    if (!reading.lastSeenEpochS.has_value() || *reading.lastSeenEpochS <= 0) {
        return invalidTimestampReason(reading.lastSeenEpochS);
    }

    if (!reading.temperatureC.has_value() &&
        !reading.moisturePct.has_value() &&
        !reading.lux.has_value() &&
        !reading.conductivityUsCm.has_value()) {
        return "missing all sensor values";
    }

    return "unknown invalid payload";
}

WriteResult makeWriteResult(
    WriteStatus status,
    BackendWriteResult backendResult = BackendWriteResult::Failed,
    int httpStatusCode = 0,
    std::string body = "",
    WriteBufferReason bufferReason = WriteBufferReason::None
) {
    WriteResult result;
    result.status = status;
    result.backendResult = backendResult;
    result.httpStatusCode = httpStatusCode;
    result.body = std::move(body);
    result.bufferReason = bufferReason;
    return result;
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

void logDroppedFreshRequest(
    const BufferedRequest& request,
    const network::HttpResponse& response
) {
    logLine(
        LogLevel::Warn,
        "Dropping API request permanently: " + request.path +
        " for " + request.mac +
        ", " + transportResultName(response.transport) +
        ", HTTP " + std::to_string(response.statusCode) +
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

void logDroppedBufferedRequest(
    const BufferedRequest& request,
    const network::HttpResponse& response
) {
    std::string message =
        "Dropping buffered request to " + request.path +
        " for " + request.mac +
        ": " + transportResultName(response.transport) +
        ", HTTP " + std::to_string(response.statusCode);

    if (response.transport == network::TransportResult::Timeout) {
        message += ", timeout retries " + std::to_string(request.timeoutRetryCount);
    }

    if (response.transport == network::TransportResult::TlsError) {
        message += ", TLS retries " + std::to_string(request.tlsRetryCount);
    }

    if (!response.error.empty()) {
        message += ", " + response.error;
    }

    logLine(LogLevel::Warn, message);

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

void logDroppedBufferFullRequest(const BufferedRequest& request) {
    logLine(
        LogLevel::Warn,
        "Dropping request because buffer is full: " + request.path +
        " for " + request.mac
    );

    dropped_log::appendDroppedRequest(
        "buffer_full",
        request.path,
        request.mac,
        request.body,
        0,
        -1,
        "",
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

BufferedClient::BufferedClient(
    const Config& config,
    BufferState& buffer,
    const ApiPoster& poster,
    RequestStore& store
)
    : config_(config),
      buffer_(buffer),
      poster_(poster),
      store_(store) {
}

WriteResult BufferedClient::postSwitchbotReading(
    const SensorIdentity& identity,
    const SwitchbotReading& reading
) {
    const auto payload = makeSwitchbotPayload(identity, reading);
    if (!payload.has_value()) {
        logLine(
            LogLevel::Warn,
            "Dropping SwitchBot reading: invalid payload for " + identity.mac +
            ": " + invalidSwitchbotPayloadReason(reading)
        );
        return makeWriteResult(WriteStatus::DroppedPermanent);
    }

    return send(BufferedRequest{
        "/switchbot/reading",
        identity.mac,
        toJson(*payload),
    });
}

WriteResult BufferedClient::postXiaomiReading(
    const SensorIdentity& identity,
    const XiaomiReading& reading
) {
    const auto payload = makeXiaomiPayload(identity, reading);
    if (!payload.has_value()) {
        logLine(
            LogLevel::Warn,
            "Dropping Xiaomi reading: invalid payload for " + identity.mac +
            ": " + invalidXiaomiPayloadReason(reading)
        );
        return makeWriteResult(WriteStatus::DroppedPermanent);
    }

    return send(BufferedRequest{
        "/xiaomi/reading",
        identity.mac,
        toJson(*payload),
    });
}

WriteResult BufferedClient::send(BufferedRequest request) {
    if (bufferHasBacklog(buffer_, store_)) {
        const BufferInsertResult insertResult =
            bufferRequest(buffer_, request, config_.api.buffer, store_, platform::millis());

        if (insertResult == BufferInsertResult::DroppedNewRequestBufferFull) {
            logDroppedBufferFullRequest(request);
        }

        return makeWriteResult(
            mapBufferInsertResult(insertResult),
            BackendWriteResult::Failed,
            0,
            "",
            insertResult == BufferInsertResult::Buffered
                ? WriteBufferReason::BacklogPresent
                : WriteBufferReason::None
        );
    }

    const network::HttpResponse response = poster_.postJson(request.path, request.body);

    switch (decideFreshResponse(response)) {
        case FreshRequestDecision::Sent:
            return makeWriteResult(
                WriteStatus::Sent,
                parseBackendWriteResult(response),
                response.statusCode,
                response.body
            );

        case FreshRequestDecision::DropPermanent:
            logDroppedFreshRequest(request, response);
            return makeWriteResult(
                WriteStatus::DroppedPermanent,
                BackendWriteResult::Failed,
                response.statusCode,
                response.body
            );

        case FreshRequestDecision::Buffer: {
            const BufferInsertResult insertResult =
                bufferRequest(buffer_, request, config_.api.buffer, store_, platform::millis());

            if (insertResult == BufferInsertResult::DroppedNewRequestBufferFull) {
                logDroppedBufferFullRequest(request);
            }

            return makeWriteResult(
                mapBufferInsertResult(insertResult),
                BackendWriteResult::Failed,
                response.statusCode,
                response.body,
                insertResult == BufferInsertResult::Buffered
                    ? WriteBufferReason::RetryableFailure
                    : WriteBufferReason::None
            );
        }
    }

    return makeWriteResult(WriteStatus::DroppedPermanent);
}

BufferDrainResult BufferedClient::drainPending(std::uint64_t nowMs) {
    BufferDrainResult result;

    if (nowMs < buffer_.nextDrainAllowedAtMs) {
        result.notDueYet = true;
        return result;
    }

    if (!bufferHasBacklog(buffer_, store_)) {
        return result;
    }

    logLine(
        LogLevel::Info,
        "Draining API buffer: RAM " + std::to_string(buffer_.requests.size()) +
        ", disk " + std::to_string(buffer_.disk.count) +
        " queued"
    );

    buffer_.nextDrainAllowedAtMs = nowMs + bufferDrainDelayMs(config_.api.buffer);

    for (int i = 0; i < config_.api.buffer.drainRateCap; ++i) {
        if (!bufferHasBacklog(buffer_, store_)) {
            break;
        }

        BufferedRequest request;
        if (!peekBufferedRequest(buffer_, request, store_)) {
            logLine(LogLevel::Warn, "Dropping corrupt disk-buffered API request");
            if (dropBufferedRequest(buffer_, store_)) {
                result.dropped += 1;
                continue;
            }
            result.blockedByRetryableFailure = true;
            return result;
        }

        const int timeoutRetryCount = request.timeoutRetryCount;
        const int tlsRetryCount = request.tlsRetryCount;
        const network::HttpResponse response = poster_.postJson(request.path, request.body);
        result.attempted += 1;

        const BufferedRequestDecision decision = decideBufferedResponse(request, response);

        if (decision == BufferedRequestDecision::Sent) {
            if (!consumeBufferedRequest(buffer_, store_)) {
                result.blockedByRetryableFailure = true;
                return result;
            }
            result.sent += 1;
            continue;
        }

        if (decision == BufferedRequestDecision::KeepBuffered) {
            if ((request.timeoutRetryCount != timeoutRetryCount ||
                 request.tlsRetryCount != tlsRetryCount) &&
                !rewriteBufferedRequest(buffer_, request, store_)) {
                result.blockedByRetryableFailure = true;
                return result;
            }

            buffer_.nextDrainAllowedAtMs = nowMs + bufferDrainDelayMs(config_.api.buffer);

            result.blockedByRetryableFailure = true;
            logDrainPaused(request, response, result, buffer_);
            return result;
        }

        logDroppedBufferedRequest(request, response);
        if (!dropBufferedRequest(buffer_, store_)) {
            result.blockedByRetryableFailure = true;
            return result;
        }
        result.dropped += 1;

        if (!bufferHasBacklog(buffer_, store_)) {
            break;
        }
    }

    logDrainSummary(result, buffer_);
    return result;
}

} // namespace api
