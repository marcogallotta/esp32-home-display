#include "buffered_client.h"

#include <cstddef>
#include <utility>

#include "payloads.h"
#include "../log.h"

namespace api {
namespace {

enum class FreshRequestDecision {
    Sent,
    Buffer,
    DropPermanent,
};

bool hasBacklog(const BufferState& buffer) {
    return !buffer.requests.empty();
}

bool isBufferFull(const BufferState& buffer, const ApiBufferConfig& config) {
    return buffer.requests.size() >= static_cast<std::size_t>(config.inMemory);
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

} // namespace

BufferedClient::BufferedClient(
    const Config& config,
    BufferState& buffer,
    const Client& client
)
    : config_(config),
      buffer_(buffer),
      client_(client) {
}

WriteResult BufferedClient::postSwitchbotReading(
    const SensorIdentity& identity,
    const SwitchbotReading& reading
) {
    const auto payload = makeSwitchbotPayload(identity, reading);
    if (!payload.has_value()) {
        logLine(LogLevel::Warn, "api.switchbot.drop_invalid_payload mac=" + identity.mac);
        return makeWriteResult(WriteStatus::DroppedPermanent);
    }

    if (hasBacklog(buffer_) && isBufferFull(buffer_, config_.api.buffer)) {
        logLine(LogLevel::Warn, "api.switchbot.drop_buffer_full mac=" + identity.mac);
        return makeWriteResult(WriteStatus::DroppedBufferFull);
    }

    return postBufferedRequest(BufferedRequest{
        "/switchbot/reading",
        toJson(*payload),
    });
}

WriteResult BufferedClient::postXiaomiReading(
    const SensorIdentity& identity,
    const XiaomiReading& reading
) {
    const auto payload = makeXiaomiPayload(identity, reading);
    if (!payload.has_value()) {
        logLine(LogLevel::Warn, "api.xiaomi.drop_invalid_payload mac=" + identity.mac);
        return makeWriteResult(WriteStatus::DroppedPermanent);
    }

    if (hasBacklog(buffer_) && isBufferFull(buffer_, config_.api.buffer)) {
        logLine(LogLevel::Warn, "api.xiaomi.drop_buffer_full mac=" + identity.mac);
        return makeWriteResult(WriteStatus::DroppedBufferFull);
    }

    return postBufferedRequest(BufferedRequest{
        "/xiaomi/reading",
        toJson(*payload),
    });
}

WriteResult BufferedClient::postBufferedRequest(BufferedRequest request) {
    if (hasBacklog(buffer_)) {
        const std::string path = request.path;
        const BufferInsertResult insertResult =
            bufferRequest(buffer_, std::move(request), config_.api.buffer);

        logLine(
            LogLevel::Debug,
            "api.fresh.buffered_due_to_backlog path=" + path
        );

        return makeWriteResult(mapBufferInsertResult(insertResult));
    }

    const network::HttpResponse response = client_.postJson(request.path, request.body);

    switch (classifyFreshResponse(response)) {
        case FreshRequestDecision::Sent:
            logLine(
                LogLevel::Info,
                "api.fresh.sent path=" + request.path +
                " transport=" + transportResultName(response.transport) +
                " status=" + std::to_string(response.statusCode) +
                " body=\"" + response.body + "\""
            );

            return makeWriteResult(
                WriteStatus::Sent,
                parseBackendWriteResult(response),
                response.statusCode,
                response.body
            );

        case FreshRequestDecision::DropPermanent:
            logLine(
                LogLevel::Warn,
                "api.fresh.drop_permanent path=" + request.path +
                " transport=" + transportResultName(response.transport) +
                " status=" + std::to_string(response.statusCode) +
                " error=\"" + response.error + "\"" +
                " body=\"" + response.body + "\""
            );

            return makeWriteResult(
                WriteStatus::DroppedPermanent,
                BackendWriteResult::Failed,
                response.statusCode,
                response.body
            );

        case FreshRequestDecision::Buffer: {
            logLine(
                LogLevel::Warn,
                "api.fresh.buffer path=" + request.path +
                " transport=" + transportResultName(response.transport) +
                " status=" + std::to_string(response.statusCode) +
                " error=\"" + response.error + "\""
            );

            const BufferInsertResult insertResult =
                bufferRequest(buffer_, std::move(request), config_.api.buffer);

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
