#include "buffer.h"

#include <utility>

#include "disk_buffer.h"

namespace api {
namespace {

bool ensureDiskLoaded(BufferState& buffer, RecordStore& store) {
    if (buffer.disk.loaded) {
        return true;
    }

    return disk_buffer::load(buffer.disk, store);
}

BufferInsertResult bufferToDisk(
    BufferState& buffer,
    const api::Record& request,
    const api::Config& config,
    RecordStore& store
) {
    if (!disk_buffer::enqueue(buffer.disk, request, config, store)) {
        return BufferInsertResult::DroppedNewRequestBufferFull;
    }

    return BufferInsertResult::Buffered;
}

} // namespace

BufferInsertResult enqueue(
    BufferState& buffer,
    api::Record request,
    const api::Config& config,
    RecordStore& store
) {
    if (buffer.ramQueue.size() >= static_cast<std::size_t>(config.inMemory)) {
        return bufferToDisk(buffer, request, config, store);
    }

    buffer.ramQueue.push_back(std::move(request));

    return BufferInsertResult::Buffered;
}

bool hasBacklog(BufferState& buffer, RecordStore& store) {
    if (!buffer.ramQueue.empty()) {
        return true;
    }

    return ensureDiskLoaded(buffer, store) && buffer.disk.count > 0;
}

bool peek(
    BufferState& buffer,
    api::Record& out,
    RecordStore& store
) {
    if (!buffer.ramQueue.empty()) {
        out = buffer.ramQueue.front();
        return true;
    }

    if (!ensureDiskLoaded(buffer, store) || buffer.disk.count == 0) {
        return false;
    }

    return disk_buffer::peek(buffer.disk, out, store);
}

bool pop(BufferState& buffer, RecordStore& store) {
    if (!buffer.ramQueue.empty()) {
        buffer.ramQueue.pop_front();
        return true;
    }

    return disk_buffer::consume(buffer.disk, store);
}

bool dropFront(BufferState& buffer, RecordStore& store) {
    if (!buffer.ramQueue.empty()) {
        buffer.ramQueue.pop_front();
        return true;
    }

    return disk_buffer::dropFront(buffer.disk, store);
}

bool rewriteFront(
    BufferState& buffer,
    const api::Record& request,
    RecordStore& store
) {
    if (!buffer.ramQueue.empty()) {
        buffer.ramQueue.front() = request;
        return true;
    }

    return disk_buffer::rewriteFront(buffer.disk, request, store);
}

} // namespace api
