#include "queue.h"

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <chrono>
#include <thread>
#endif

namespace pqueue {
namespace {

constexpr const char* kLockFileName = ".pqueue.lock";
constexpr int kLockAttempts = 10;
constexpr int kLockRetryDelayMs = 10;

void waitBeforeLockRetry() {
#ifdef ARDUINO
    delay(kLockRetryDelayMs);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(kLockRetryDelayMs));
#endif
}

} // namespace

Queue::Queue(Config config)
    : config_(config),
      store_([&config] {
          FileStoreConfig storeConfig;
          storeConfig.basePath = config.basePath;
          storeConfig.backend = config.storageBackend;
          storeConfig.events = config.events;
          return storeConfig;
      }()) {}

Queue::~Queue() {
    releaseLock();
}

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

Status Queue::acquireLock() {
    if (lockHeld_) {
        return Status::success();
    }

    // TODO: add owner metadata plus stale-lock detection/recovery for crashed writers.
    Status last = Status::failure(StatusCode::LockTimeout, "queue lock timeout");
    for (int attempt = 0; attempt < kLockAttempts; ++attempt) {
        Status st = store_.tryAcquireLockFile(kLockFileName);
        if (st.ok()) {
            lockHeld_ = true;
            return Status::success();
        }
        if (st.code != StatusCode::LockTimeout) {
            return diagnostic(Severity::Error, st, "acquireLock");
        }
        last = st;
        waitBeforeLockRetry();
    }

    return diagnostic(Severity::Error, Status::failure(StatusCode::LockTimeout, "queue lock timeout", last.backendCode), "acquireLock");
}

void Queue::releaseLock() {
    if (!lockHeld_) {
        return;
    }
    store_.releaseLockFile(kLockFileName);
    lockHeld_ = false;
}

Status Queue::ensureLoaded() {
    if (loaded_) {
        return Status::success();
    }
    Status st = acquireLock();
    if (!st.ok()) {
        return st;
    }
    st = store_.readIndex(index_);
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
