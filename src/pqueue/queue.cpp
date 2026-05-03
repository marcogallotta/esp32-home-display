#include "queue.h"

namespace pqueue {

Queue::Queue(Config config)
    : config_(config),
      store_([&config] {
          FileStoreConfig storeConfig;
          storeConfig.basePath = config.basePath;
          storeConfig.backend = config.storageBackend;
          storeConfig.events = config.events;
          return storeConfig;
      }()) {}

Status Queue::emit(Event event) const {
    config_.events.emit(event);
    return event.status;
}

Status Queue::diagnostic(Severity severity, Status status, const char* operation) const {
    return emit(Event{
        EventKind::Diagnostic,
        severity,
        status,
        "Queue",
        operation,
    });
}

Status Queue::ensureLoaded() {
    if (loaded_) {
        return Status::success();
    }
    Status st = store_.readIndex(index_);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "ensureLoaded");
    }
    loaded_ = true;
    return Status::success();
}

Status Queue::enqueue(const std::string& record) {
    Status st = ensureLoaded();
    if (!st.ok()) {
        return st;
    }
    if (record.size() > config_.maxRecordBytes) {
        return diagnostic(Severity::Warning, Status::failure(StatusCode::RecordTooLarge, "record exceeds configured queue maximum"), "enqueue");
    }
    if (store_.freeBytes() <= config_.diskReserveBytes) {
        // TODO: make full-queue behavior configurable instead of always rejecting newest.
        return diagnostic(Severity::Warning, Status::failure(StatusCode::QueueFull, "queue disk reserve reached"), "enqueue");
    }

    const std::uint32_t sequence = index_.tail;
    st = store_.writeRecord(sequence, record);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "enqueue");
    }

    FileStoreIndex next = index_;
    next.tail += 1;
    next.count += 1;
    st = store_.writeIndex(next);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "enqueue");
    }

    index_ = next;
    return Status::success();
}

Status Queue::peek(std::string& out) {
    Status st = ensureLoaded();
    if (!st.ok()) {
        return st;
    }
    if (index_.count == 0) {
        return Status::failure(StatusCode::QueueEmpty, "queue is empty");
    }
    return store_.readRecord(index_.head, out);
}

Status Queue::pop() {
    Status st = ensureLoaded();
    if (!st.ok()) {
        return st;
    }
    if (index_.count == 0) {
        return Status::failure(StatusCode::QueueEmpty, "queue is empty");
    }

    const std::uint32_t oldHead = index_.head;
    FileStoreIndex next = index_;
    next.head += 1;
    next.count -= 1;

    st = store_.writeIndex(next);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "pop");
    }

    index_ = next;
    store_.removeRecord(oldHead);
    return Status::success();
}

Status Queue::rewriteFront(const std::string& record) {
    Status st = ensureLoaded();
    if (!st.ok()) {
        return st;
    }
    if (index_.count == 0) {
        return Status::failure(StatusCode::QueueEmpty, "queue is empty");
    }
    if (record.size() > config_.maxRecordBytes) {
        return diagnostic(Severity::Warning, Status::failure(StatusCode::RecordTooLarge, "record exceeds configured queue maximum"), "rewriteFront");
    }
    return store_.writeRecord(index_.head, record);
}

Stats Queue::stats() {
    Stats out;
    if (!ensureLoaded().ok()) {
        return out;
    }
    out.count = index_.count;
    out.freeBytes = store_.freeBytes();
    return out;
}

} // namespace pqueue
