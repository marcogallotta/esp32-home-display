#include "disk_buffer.h"

#include "../log.h"
#include "request_file_store.h"

namespace api::disk_buffer {
namespace {

request_file_store::Index toIndex(const State& state) {
    request_file_store::Index index;
    index.head = state.head;
    index.tail = state.tail;
    index.count = state.count;
    return index;
}

void fromIndex(const request_file_store::Index& index, State& state) {
    state.head = index.head;
    state.tail = index.tail;
    state.count = index.count;
    state.loaded = true;
}

bool ensureLoaded(State& state) {
    if (state.loaded) {
        return true;
    }

    return load(state);
}

bool hasDiskSpace(const ApiBufferConfig& config) {
    const std::uint64_t freeBytes = request_file_store::freeBytes();

    if (freeBytes <= config.diskReserveBytes) {
        logLine(LogLevel::Warn, "API disk buffer full: reserve would be crossed");
        return false;
    }

    return true;
}

bool advanceHead(State& state, const char* actionName) {
    if (!ensureLoaded(state)) {
        return false;
    }

    if (state.count == 0) {
        return false;
    }

    const std::uint32_t oldHead = state.head;

    State next = state;
    next.head += 1;
    next.count -= 1;

    if (!request_file_store::writeIndex(toIndex(next))) {
        logLine(
            LogLevel::Warn,
            std::string("API disk buffer ") + actionName + " failed: index write failed"
        );
        return false;
    }

    if (!request_file_store::removeRequest(oldHead)) {
        logLine(
            LogLevel::Warn,
            std::string("API disk buffer ") + actionName + " warning: request delete failed"
        );
    }

    state = next;
    return true;
}

} // namespace

bool load(State& state) {
    request_file_store::Index index;
    if (!request_file_store::readIndex(index)) {
        logLine(LogLevel::Warn, "API disk buffer load failed");
        return false;
    }

    fromIndex(index, state);
    return true;
}

bool enqueue(
    State& state,
    const BufferedRequest& request,
    const ApiBufferConfig& config
) {
    if (!ensureLoaded(state)) {
        return false;
    }

    if (!hasDiskSpace(config)) {
        return false;
    }

    const std::uint32_t sequence = state.tail;

    if (!request_file_store::writeRequest(sequence, request)) {
        logLine(LogLevel::Warn, "API disk buffer enqueue failed: request write failed");
        return false;
    }

    State next = state;
    next.tail += 1;
    next.count += 1;

    if (!request_file_store::writeIndex(toIndex(next))) {
        logLine(LogLevel::Warn, "API disk buffer enqueue failed: index write failed");
        return false;
    }

    state = next;
    return true;
}

bool peek(State& state, BufferedRequest& out) {
    if (!ensureLoaded(state)) {
        return false;
    }

    if (state.count == 0) {
        return false;
    }

    if (!request_file_store::readRequest(state.head, out)) {
        logLine(LogLevel::Warn, "API disk buffer peek failed: request read failed");
        return false;
    }

    return true;
}

bool consume(State& state) {
    return advanceHead(state, "consume");
}

bool dropFront(State& state) {
    return advanceHead(state, "drop front");
}

bool rewriteFront(State& state, const BufferedRequest& request) {
    if (!ensureLoaded(state)) {
        return false;
    }

    if (state.count == 0) {
        return false;
    }

    if (!request_file_store::writeRequest(state.head, request)) {
        logLine(LogLevel::Warn, "API disk buffer rewrite failed");
        return false;
    }

    return true;
}

} // namespace api::disk_buffer
