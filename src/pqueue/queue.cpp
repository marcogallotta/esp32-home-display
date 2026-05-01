#include "queue.h"

namespace pqueue {

Queue::Queue(FileStore& store, Config config) : store_(store), config_(config) {}

bool Queue::ensureLoaded() {
    if (loaded_) {
        return true;
    }
    if (!store_.readIndex(index_)) {
        return false;
    }
    loaded_ = true;
    return true;
}

bool Queue::enqueue(const std::string& record) {
    if (!ensureLoaded() || record.size() > config_.maxRecordBytes) {
        return false;
    }
    if (store_.freeBytes() <= config_.diskReserveBytes) {
        // TODO: make full-queue behavior configurable instead of always rejecting newest.
        return false;
    }

    const std::uint32_t sequence = index_.tail;
    if (!store_.writeRecord(sequence, record)) {
        return false;
    }

    FileStoreIndex next = index_;
    next.tail += 1;
    next.count += 1;
    if (!store_.writeIndex(next)) {
        return false;
    }

    index_ = next;
    return true;
}

bool Queue::peek(std::string& out) {
    if (!ensureLoaded() || index_.count == 0) {
        return false;
    }
    return store_.readRecord(index_.head, out);
}

bool Queue::pop() {
    if (!ensureLoaded() || index_.count == 0) {
        return false;
    }

    const std::uint32_t oldHead = index_.head;
    FileStoreIndex next = index_;
    next.head += 1;
    next.count -= 1;

    if (!store_.writeIndex(next)) {
        return false;
    }

    index_ = next;
    store_.removeRecord(oldHead);
    return true;
}

bool Queue::rewriteFront(const std::string& record) {
    if (!ensureLoaded() || index_.count == 0 || record.size() > config_.maxRecordBytes) {
        return false;
    }
    return store_.writeRecord(index_.head, record);
}

Stats Queue::stats() {
    Stats out;
    if (!ensureLoaded()) {
        return out;
    }
    out.count = index_.count;
    out.freeBytes = store_.freeBytes();
    return out;
}

} // namespace pqueue
