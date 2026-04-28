#include "buffer.h"

#include <utility>

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
    RequestStore& store
) {
    if (!disk_buffer::enqueue(buffer.disk, request, config, store)) {
        return BufferInsertResult::DroppedNewRequestBufferFull;
    }

    return BufferInsertResult::Buffered;
}

} // namespace

BufferInsertResult bufferRequest(
    BufferState& buffer,
    BufferedRequest request,
    const ApiBufferConfig& config,
    RequestStore& store
) {
    if (buffer.requests.size() >= static_cast<std::size_t>(config.inMemory)) {
        return bufferToDisk(buffer, request, config, store);
    }

    buffer.requests.push_back(std::move(request));

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
