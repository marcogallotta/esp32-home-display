#pragma once

#include <cstdint>
#include <string>

#include "file_store.h"
#include "status.h"
#include "types.h"

namespace pqueue {

// Queue v1 stores opaque records as std::string. This is acceptable for
// ESP32-class Arduino targets and desktop tests, but the public Queue API
// should eventually grow raw-buffer/header-peek operations for broader
// Arduino support.
//
// rewriteFront() is intentionally exposed for Outbox v1 so retry attempts can
// be persisted without changing FIFO order. Future backends may replace this
// with a cheaper metadata/sidecar strategy.
class Queue {
public:
    explicit Queue(Config config = Config{});
    ~Queue();

    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    Status enqueue(const std::string& record);
    Status peek(std::string& out);
    Status pop();
    Status rewriteFront(const std::string& record);
    Stats stats();

private:
    Status ensureLoaded();
    Status acquireLock();
    void releaseLock();
    Status emit(Event event) const;
    Status diagnostic(Severity severity, Status status, const char* operation) const;

    Config config_;
    FileStore store_;
    FileStoreIndex index_;
    bool loaded_ = false;
    bool lockHeld_ = false;
};

} // namespace pqueue
