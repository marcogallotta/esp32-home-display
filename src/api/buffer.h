#pragma once

#include <cstdint>
#include <ctime>
#include <deque>
#include <string>

#include "../pqueue/types.h"
#include "request_store.h"

namespace api {

namespace disk_buffer {

struct State {
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
    bool loaded = false;
};

} // namespace disk_buffer

using ApiRequest = pqueue::Record;

struct BufferState {
    std::deque<pqueue::Record> ramQueue;
    disk_buffer::State disk;
};

enum class BufferInsertResult {
    Buffered,
    DroppedNewRequestBufferFull,
};

struct BufferDrainResult {
    int attempted = 0;
    int sent = 0;
    int dropped = 0;
    bool blockedByRetryableFailure = false;
    bool notDueYet = false;
};

BufferInsertResult enqueue(
    BufferState& buffer,
    pqueue::Record request,
    const pqueue::Config& config,
    RequestStore& store
);

bool hasBacklog(BufferState& buffer, RequestStore& store);

bool peek(
    BufferState& buffer,
    pqueue::Record& out,
    RequestStore& store
);

bool pop(BufferState& buffer, RequestStore& store);
bool dropFront(BufferState& buffer, RequestStore& store);

bool rewriteFront(
    BufferState& buffer,
    const pqueue::Record& request,
    RequestStore& store
);


} // namespace api
