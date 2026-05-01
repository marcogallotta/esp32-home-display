#pragma once

#include <cstdint>

#include "../pqueue/types.h"
#include "record_store.h"

namespace api::disk_buffer {

struct State {
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
    bool loaded = false;
};

bool load(State& state, RecordStore& store);

bool enqueue(
    State& state,
    const pqueue::Record& request,
    const pqueue::Config& config,
    RecordStore& store
);

bool peek(State& state, pqueue::Record& out, RecordStore& store);
bool consume(State& state, RecordStore& store);
bool dropFront(State& state, RecordStore& store);
bool rewriteFront(State& state, const pqueue::Record& request, RecordStore& store);

} // namespace api::disk_buffer
