#include "buffer.h"

#include <utility>

#include "../log.h"
#include "disk_buffer.h"

namespace api {
namespace {

bool ensureDiskLoaded(BufferState& buffer, RequestStore& store) {
    if (buffer.disk.loaded) {
        return true;
    }

    return disk_buffer::load(buffer.disk, store);
}

BufferInsertResult bufferToDisk(
    BufferState& buffer,
    const BufferedRequest& request,
    const ApiBufferConfig& config,
    RequestStore& store,
    std::uint64_t nowMs
) {
    if (!disk_buffer::enqueue(buffer.disk, request, config, store)) {
        logLine(
            LogLevel::Warn,
            "Disk buffer full; dropping new request to " + request.path +
            " for " + request.mac
        );
        return BufferInsertResult::DroppedNewRequestBufferFull;
    }

    delayNextBufferDrain(buffer, config, nowMs);

    logLine(
        LogLevel::Warn,
        "Buffered API request on disk: " +
        std::to_string(buffer.disk.count) + " queued"
    );

    return BufferInsertResult::Buffered;
}

} // namespace

std::uint64_t bufferDrainDelayMs(const ApiBufferConfig& config) {
    return static_cast<std::uint64_t>(config.drainRateTickS) * 1000;
}

void delayNextBufferDrain(BufferState& buffer, const ApiBufferConfig& config, std::uint64_t nowMs) {
    const std::uint64_t nextAllowed = nowMs + bufferDrainDelayMs(config);
    if (buffer.nextDrainAllowedAtMs <= nowMs) {
        buffer.nextDrainAllowedAtMs = nextAllowed;
    }
}

BufferInsertResult bufferRequest(
    BufferState& buffer,
    BufferedRequest request,
    const ApiBufferConfig& config,
    RequestStore& store,
    std::uint64_t nowMs
) {
    if (buffer.requests.size() >= static_cast<std::size_t>(config.inMemory)) {
        return bufferToDisk(buffer, request, config, store, nowMs);
    }

    buffer.requests.push_back(std::move(request));
    delayNextBufferDrain(buffer, config, nowMs);

    logLine(
        LogLevel::Warn,
        "Buffered API request in memory: " +
        std::to_string(buffer.requests.size()) +
        "/" + std::to_string(config.inMemory) + " queued"
    );

    return BufferInsertResult::Buffered;
}

bool bufferHasBacklog(BufferState& buffer, RequestStore& store) {
    if (!buffer.requests.empty()) {
        return true;
    }

    return ensureDiskLoaded(buffer, store) && buffer.disk.count > 0;
}

bool peekBufferedRequest(
    BufferState& buffer,
    BufferedRequest& out,
    RequestStore& store
) {
    if (!buffer.requests.empty()) {
        out = buffer.requests.front();
        return true;
    }

    if (!ensureDiskLoaded(buffer, store) || buffer.disk.count == 0) {
        return false;
    }

    return disk_buffer::peek(buffer.disk, out, store);
}

bool consumeBufferedRequest(BufferState& buffer, RequestStore& store) {
    if (!buffer.requests.empty()) {
        buffer.requests.pop_front();
        return true;
    }

    return disk_buffer::consume(buffer.disk, store);
}

bool dropBufferedRequest(BufferState& buffer, RequestStore& store) {
    if (!buffer.requests.empty()) {
        buffer.requests.pop_front();
        return true;
    }

    return disk_buffer::dropFront(buffer.disk, store);
}

bool rewriteBufferedRequest(
    BufferState& buffer,
    const BufferedRequest& request,
    RequestStore& store
) {
    if (!buffer.requests.empty()) {
        buffer.requests.front() = request;
        return true;
    }

    return disk_buffer::rewriteFront(buffer.disk, request, store);
}

} // namespace api
