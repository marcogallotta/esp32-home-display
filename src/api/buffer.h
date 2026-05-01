#pragma once

#include <cstdint>
#include <ctime>
#include <deque>
#include <string>

#include "../pqueue/types.h"
#include "disk_buffer.h"
#include "record_store.h"

namespace api {

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
    RecordStore& store
);

bool hasBacklog(BufferState& buffer, RecordStore& store);

bool peek(
    BufferState& buffer,
    pqueue::Record& out,
    RecordStore& store
);

bool pop(BufferState& buffer, RecordStore& store);
bool dropFront(BufferState& buffer, RecordStore& store);

bool rewriteFront(
    BufferState& buffer,
    const pqueue::Record& request,
    RecordStore& store
);


} // namespace api
