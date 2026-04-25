#include "disk_buffer.h"

#include "../log.h"

namespace api::disk_buffer {
namespace {

RequestStoreIndex toIndex(const State& state) {
    RequestStoreIndex index;
    index.head = state.head;
    index.tail = state.tail;
    index.count = state.count;
    return index;
}

void fromIndex(const RequestStoreIndex& index, State& state) {
    state.head = index.head;
    state.tail = index.tail;
    state.count = index.count;
    state.loaded = true;
}

bool ensureLoaded(State& state, RequestStore& store) {
    if (state.loaded) {
        return true;
    }

    return load(state, store);
}

bool hasDiskSpace(const ApiBufferConfig& config, RequestStore& store) {
    const std::uint64_t freeBytes = store.freeBytes();

    if (freeBytes <= config.diskReserveBytes) {
        logLine(LogLevel::Warn, "API disk buffer full: reserve would be crossed");
        return false;
    }

    return true;
}

bool advanceHead(State& state, const char* actionName, RequestStore& store) {
    if (!ensureLoaded(state, store)) {
        return false;
    }

    if (state.count == 0) {
        return false;
    }

    const std::uint32_t oldHead = state.head;

    State next = state;
    next.head += 1;
    next.count -= 1;

    if (!store.writeIndex(toIndex(next))) {
        logLine(
            LogLevel::Warn,
            std::string("API disk buffer ") + actionName + " failed: index write failed"
        );
        return false;
    }

    if (!store.removeRequest(oldHead)) {
        logLine(
            LogLevel::Warn,
            std::string("API disk buffer ") + actionName + " warning: request delete failed"
        );
    }

    state = next;
    return true;
}

} // namespace

bool load(State& state, RequestStore& store) {
    RequestStoreIndex index;
    if (!store.readIndex(index)) {
        logLine(LogLevel::Warn, "API disk buffer load failed");
        return false;
    }

    fromIndex(index, state);
    return true;
}

bool enqueue(
    State& state,
    const BufferedRequest& request,
    const ApiBufferConfig& config,
    RequestStore& store
) {
    if (!ensureLoaded(state, store)) {
        return false;
    }

    if (!hasDiskSpace(config, store)) {
        return false;
    }

    const std::uint32_t sequence = state.tail;

    if (!store.writeRequest(sequence, request)) {
        logLine(LogLevel::Warn, "API disk buffer enqueue failed: request write failed");
        return false;
    }

    State next = state;
    next.tail += 1;
    next.count += 1;

    if (!store.writeIndex(toIndex(next))) {
        logLine(LogLevel::Warn, "API disk buffer enqueue failed: index write failed");
        return false;
    }

    state = next;
    return true;
}

bool peek(State& state, BufferedRequest& out, RequestStore& store) {
    if (!ensureLoaded(state, store)) {
        return false;
    }

    if (state.count == 0) {
        return false;
    }

    if (!store.readRequest(state.head, out)) {
        logLine(LogLevel::Warn, "API disk buffer peek failed: request read failed");
        return false;
    }

    return true;
}

bool consume(State& state, RequestStore& store) {
    return advanceHead(state, "consume", store);
}

bool dropFront(State& state, RequestStore& store) {
    return advanceHead(state, "drop front", store);
}

bool rewriteFront(State& state, const BufferedRequest& request, RequestStore& store) {
    if (!ensureLoaded(state, store)) {
        return false;
    }

    if (state.count == 0) {
        return false;
    }

    if (!store.writeRequest(state.head, request)) {
        logLine(LogLevel::Warn, "API disk buffer rewrite failed");
        return false;
    }

    return true;
}

} // namespace api::disk_buffer
