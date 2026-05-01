#include "disk_buffer.h"

#include "../log.h"

namespace api::disk_buffer {
namespace {

RecordStoreIndex toIndex(const State& state) {
    RecordStoreIndex index;
    index.head = state.head;
    index.tail = state.tail;
    index.count = state.count;
    return index;
}

void fromIndex(const RecordStoreIndex& index, State& state) {
    state.head = index.head;
    state.tail = index.tail;
    state.count = index.count;
    state.loaded = true;
}

bool ensureLoaded(State& state, RecordStore& store) {
    if (state.loaded) {
        return true;
    }

    return load(state, store);
}

bool hasDiskSpace(const api::Config& config, RecordStore& store) {
    const std::uint64_t freeBytes = store.freeBytes();

    if (freeBytes <= config.diskReserveBytes) {
        logLine(LogLevel::Warn, "Disk buffer full: reserve would be crossed");
        return false;
    }

    return true;
}

bool advanceHead(State& state, const char* actionName, RecordStore& store) {
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
            std::string("Disk buffer ") + actionName + " failed: index write failed"
        );
        return false;
    }

    state = next;

    if (!store.removeRecord(oldHead)) {
        logLine(
            LogLevel::Warn,
            std::string("Disk buffer ") + actionName + " cleanup failed: request delete failed"
        );
    }

    return true;
}

} // namespace

bool load(State& state, RecordStore& store) {
    RecordStoreIndex index;
    if (!store.readIndex(index)) {
        logLine(LogLevel::Warn, "Disk buffer load failed");
        return false;
    }

    fromIndex(index, state);
    return true;
}

bool enqueue(
    State& state,
    const api::Record& request,
    const api::Config& config,
    RecordStore& store
) {
    if (!ensureLoaded(state, store)) {
        return false;
    }

    if (!hasDiskSpace(config, store)) {
        return false;
    }

    const std::uint32_t sequence = state.tail;

    if (!store.writeRecord(sequence, request)) {
        logLine(LogLevel::Warn, "Disk buffer enqueue failed: request write failed");
        return false;
    }

    State next = state;
    next.tail += 1;
    next.count += 1;

    if (!store.writeIndex(toIndex(next))) {
        logLine(LogLevel::Warn, "Disk buffer enqueue failed: index write failed");
        return false;
    }

    state = next;
    return true;
}

bool peek(State& state, api::Record& out, RecordStore& store) {
    if (!ensureLoaded(state, store)) {
        return false;
    }

    if (state.count == 0) {
        return false;
    }

    if (!store.readRecord(state.head, out)) {
        logLine(LogLevel::Warn, "Disk buffer peek failed: request read failed");
        return false;
    }

    return true;
}

bool consume(State& state, RecordStore& store) {
    return advanceHead(state, "consume", store);
}

bool dropFront(State& state, RecordStore& store) {
    return advanceHead(state, "drop front", store);
}

bool rewriteFront(State& state, const api::Record& request, RecordStore& store) {
    if (!ensureLoaded(state, store)) {
        return false;
    }

    if (state.count == 0) {
        return false;
    }

    if (!store.writeRecord(state.head, request)) {
        logLine(LogLevel::Warn, "Disk buffer rewrite failed");
        return false;
    }

    return true;
}

} // namespace api::disk_buffer
