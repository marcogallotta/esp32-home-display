#include "buffered_client.h"

#include <cstddef>
#include <utility>

#include "payloads.h"

namespace api {
namespace {

enum class FreshRequestDecision {
    Sent,
    Buffer,
    DropPermanent,
};

bool isBufferFull(const BufferState& buffer, const ApiBufferConfig& config) {
    return buffer.requests.size() >= static_cast<std::size_t>(config.inMemory);
}

bool hasBacklog(const BufferState& buffer) {
    return !buffer.requests.empty();
}

FreshRequestDecision classifyFreshResponse(const network::HttpResponse& response) {
    if (response.transport == network::TransportResult::Ok &&
        response.statusCode >= 200 &&
        response.statusCode < 300) {
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
        return WriteResult{WriteStatus::DroppedPermanent, 0};
    }

    if (hasBacklog(buffer_) && isBufferFull(buffer_, config_.api.buffer)) {
        return WriteResult{WriteStatus::DroppedBufferFull, 0};
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
        return WriteResult{WriteStatus::DroppedPermanent, 0};
    }

    if (hasBacklog(buffer_) && isBufferFull(buffer_, config_.api.buffer)) {
        return WriteResult{WriteStatus::DroppedBufferFull, 0};
    }

    return postBufferedRequest(BufferedRequest{
        "/xiaomi/reading",
        toJson(*payload),
    });
}

WriteResult BufferedClient::postBufferedRequest(BufferedRequest request) {
    if (hasBacklog(buffer_)) {
        const BufferInsertResult insertResult =
            bufferRequest(buffer_, std::move(request), config_.api.buffer);

        return WriteResult{mapBufferInsertResult(insertResult), 0};
    }

    const network::HttpResponse response = client_.postJson(request.path, request.body);

    switch (classifyFreshResponse(response)) {
        case FreshRequestDecision::Sent:
            return WriteResult{WriteStatus::Sent, response.statusCode};

        case FreshRequestDecision::DropPermanent:
            return WriteResult{WriteStatus::DroppedPermanent, response.statusCode};

        case FreshRequestDecision::Buffer: {
            const BufferInsertResult insertResult =
                bufferRequest(buffer_, std::move(request), config_.api.buffer);

            return WriteResult{
                mapBufferInsertResult(insertResult),
                response.statusCode
            };
        }
    }

    return WriteResult{WriteStatus::DroppedPermanent, response.statusCode};
}

} // namespace api
