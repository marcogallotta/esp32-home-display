#include "buffered_client.h"

#include <cstddef>
#include <utility>

#include "../log.h"
#include "disk_buffer.h"
#include "dropped_log.h"
#include "payloads.h"

namespace api {
namespace {

enum class FreshRequestDecision {
    Sent,
    Buffer,
    DropPermanent,
};

bool hasBacklog(BufferState& buffer, RequestStore& store) {
    if (!buffer.requests.empty()) {
        return true;
    }

    if (!buffer.disk.loaded && !disk_buffer::load(buffer.disk, store)) {
        return false;
    }

    return buffer.disk.count > 0;
}

bool isAcceptedHttp(const network::HttpResponse& response) {
    return response.transport == network::TransportResult::Ok &&
           response.statusCode >= 200 &&
           response.statusCode < 300;
}

FreshRequestDecision classifyFreshResponse(const network::HttpResponse& response) {
    if (isAcceptedHttp(response)) {
        return FreshRequestDecision::Sent;
    }

    if (response.transport == network::TransportResult::InternalError) {
        return FreshRequestDecision::DropPermanent;
    }

    if (response.transport == network::TransportResult::Ok &&
        response.statusCode >= 400 &&
        response.statusCode <= 499) {
        return FreshRequestDecision::DropPermanent;
    }

    if (response.transport == network::TransportResult::Ok &&
        (response.statusCode == 500 || response.statusCode >= 505)) {
        return FreshRequestDecision::DropPermanent;
    }

    return FreshRequestDecision::Buffer;
}

WriteStatus mapBufferInsertResult(BufferInsertResult result) {
    return result == BufferInsertResult::Buffered
        ? WriteStatus::Buffered
        : WriteStatus::DroppedBufferFull;
}

WriteResult makeWriteResult(
    WriteStatus status,
    BackendWriteResult backendResult = BackendWriteResult::Failed,
    int httpStatusCode = 0,
    std::string body = ""
) {
    WriteResult result;
    result.status = status;
    result.backendResult = backendResult;
    result.httpStatusCode = httpStatusCode;
    result.body = std::move(body);
    return result;
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

    std::string reason;
    if (response.transport == network::TransportResult::InternalError) {
        reason = "transport_internal_error";
    } else if (response.transport == network::TransportResult::Ok) {
        reason = "http_" + std::to_string(response.statusCode);
    } else {
        reason = "dropped";
    }

    dropped_log::appendDroppedRequest(
        reason,
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

} // namespace

BufferedClient::BufferedClient(
    const Config& config,
    BufferState& buffer,
    const Client& client,
    RequestStore& store
)
    : config_(config),
      buffer_(buffer),
      client_(client),
      store_(store) {
}

WriteResult BufferedClient::postSwitchbotReading(
    const SensorIdentity& identity,
    const SwitchbotReading& reading
) {
    const auto payload = makeSwitchbotPayload(identity, reading);
    if (!payload.has_value()) {
        logLine(LogLevel::Warn, "Dropping SwitchBot reading: invalid payload for " + identity.mac);
        return makeWriteResult(WriteStatus::DroppedPermanent);
    }

    return postBufferedRequest(BufferedRequest{
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
        logLine(LogLevel::Warn, "Dropping Xiaomi reading: invalid payload for " + identity.mac);
        return makeWriteResult(WriteStatus::DroppedPermanent);
    }

    return postBufferedRequest(BufferedRequest{
        "/xiaomi/reading",
        identity.mac,
        toJson(*payload),
    });
}

WriteResult BufferedClient::postBufferedRequest(BufferedRequest request) {
    if (hasBacklog(buffer_, store_)) {
        const BufferInsertResult insertResult =
            bufferRequest(buffer_, request, config_.api.buffer, store_);

        if (insertResult == BufferInsertResult::DroppedNewRequestBufferFull) {
            logDroppedBufferFullRequest(request);
        }

        return makeWriteResult(mapBufferInsertResult(insertResult));
    }

    const network::HttpResponse response = client_.postJson(request.path, request.body);

    switch (classifyFreshResponse(response)) {
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
                bufferRequest(buffer_, request, config_.api.buffer, store_);

            if (insertResult == BufferInsertResult::DroppedNewRequestBufferFull) {
                logDroppedBufferFullRequest(request);
            }

            return makeWriteResult(
                mapBufferInsertResult(insertResult),
                BackendWriteResult::Failed,
                response.statusCode,
                response.body
            );
        }
    }

    return makeWriteResult(WriteStatus::DroppedPermanent);
}

} // namespace api
