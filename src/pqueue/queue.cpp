#include "queue.h"
#include "storage_common.h"

#include <utility>

#ifdef ARDUINO
#include <Arduino.h>
#include <sstream>
#else
#include <chrono>
#include <sstream>
#include <thread>
#include <unistd.h>
#endif

namespace pqueue {
namespace {

constexpr const char* kLockFileName = ".pqueue.lock";
constexpr int kLockAttempts = 10;
constexpr int kLockRetryDelayMs = 10;


std::string makeLockContents(const void* owner) {
    std::ostringstream out;
    out << "pqueue-lock-v1\n";
#ifdef ARDUINO
    out << "pid=0\n";
    out << "token=" << reinterpret_cast<std::uintptr_t>(owner) << "-" << millis() << "\n";
#else
    out << "pid=" << static_cast<long>(::getpid()) << "\n";
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    out << "token=" << reinterpret_cast<std::uintptr_t>(owner) << "-" << now << "\n";
#endif
    return out.str();
}

void waitBeforeLockRetry() {
#ifdef ARDUINO
    delay(kLockRetryDelayMs);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(kLockRetryDelayMs));
#endif
}


void addQueueValidationError(ValidationResult& result, const ValidationOptions& options, ValidationIssue issue) {
    result.ok = false;
    if (result.errors.size() < options.maxErrors) {
        result.errors.push_back(std::move(issue));
    } else {
        result.stoppedEarly = true;
    }
}

ValidationIssue makeQueueIssue(ValidationIssueCode code, std::string message) {
    ValidationIssue issue;
    issue.code = code;
    issue.message = std::move(message);
    return issue;
}

bool sameIndex(const FileStoreIndex& lhs, const FileStoreIndex& rhs) {
    return lhs.head == rhs.head && lhs.tail == rhs.tail && lhs.count == rhs.count;
}

} // namespace

Queue::Queue(Config config)
    : config_(config),
      store_([&config] {
          FileStoreConfig storeConfig;
          storeConfig.basePath = config.basePath;
          storeConfig.backend = config.storageBackend;
          storeConfig.events = config.events;
          storeConfig.fileSystem = config.fileSystem;
          storeConfig.reservedBytes = config.reservedBytes;
          storeConfig.recordSizeBytes = config.recordSizeBytes;
          storeConfig.journalBytes = config.journalBytes;
          storeConfig.checkpointEveryOps = config.checkpointEveryOps;
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

    if (lockContents_.empty()) {
        lockContents_ = makeLockContents(this);
    }

    Status last = Status::failure(StatusCode::LockTimeout, "queue lock timeout");
    for (int attempt = 0; attempt < kLockAttempts; ++attempt) {
        Status st = store_.tryAcquireLockFile(kLockFileName, lockContents_);
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
    store_.releaseLockFile(kLockFileName, lockContents_);
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
    if (record.size() > config_.recordSizeBytes) {
        return diagnostic(Severity::Warning, Status::failure(StatusCode::RecordTooLarge, "record exceeds configured queue maximum"), "enqueue");
    }
    const std::uint32_t slotSizeBytes = static_cast<std::uint32_t>(storage_detail::kRecordHeaderBytes + config_.recordSizeBytes);
    const std::uint32_t capacityRecords = slotSizeBytes == 0 ? 0 : config_.reservedBytes / slotSizeBytes;
    if (capacityRecords == 0) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidArgument, "invalid pqueue storage config"), "enqueue");
    }
    if (index_.count >= capacityRecords) {
        // TODO: Consider making full-buffer behavior configurable. Options: reject newest, drop oldest, overwrite oldest.
        return diagnostic(Severity::Warning, Status::failure(StatusCode::QueueFull, "queue is full"), "enqueue");
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
    if (record.size() > config_.recordSizeBytes) {
        return diagnostic(Severity::Warning, Status::failure(StatusCode::RecordTooLarge, "record exceeds configured queue maximum"), "rewriteFront");
    }
    return store_.rewriteRecord(index_.head, record);
}


Status Queue::visitRecords(RecordVisitor visitor, void* context) {
    Status st = ensureLoaded();
    if (!st.ok()) {
        return st;
    }
    if (visitor == nullptr) {
        return Status::failure(StatusCode::InvalidArgument, "record visitor is null");
    }

    for (std::uint32_t ordinal = 0; ordinal < index_.count; ++ordinal) {
        const std::uint32_t sequence = index_.head + ordinal;
        std::string record;
        st = store_.readRecord(sequence, record);
        if (!st.ok()) {
            return st;
        }
        if (!visitor(context, record, sequence, ordinal)) {
            break;
        }
    }

    return Status::success();
}

ValidationResult Queue::validate(const ValidationOptions& options) {
    ValidationResult result;

    Status st = ensureLoaded();
    if (!st.ok()) {
        addQueueValidationError(result, options, makeQueueIssue(ValidationIssueCode::QueueLoadFailed, st.message));
        return result;
    }

    result = store_.validateUnlocked(options);
    if (result.stoppedEarly || result.errors.size() >= options.maxErrors) {
        return result;
    }

    FileStoreIndex diskIndex;
    st = store_.readIndexFromDisk(diskIndex);
    if (!st.ok()) {
        addQueueValidationError(result, options, makeQueueIssue(ValidationIssueCode::QueueLoadFailed, st.message));
        return result;
    }

    if (!sameIndex(index_, diskIndex)) {
        addQueueValidationError(result, options, makeQueueIssue(ValidationIssueCode::QueueIndexMismatch, "queue cached index does not match storage metadata"));
    }

    return result;
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
