#pragma once

#include <cstdint>

#include "../pqueue/types.h"
#include "request_store.h"

namespace api::disk_buffer {

struct State {
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
    bool loaded = false;
};

bool load(State& state, RequestStore& store);

bool enqueue(
    State& state,
    const pqueue::Record& request,
    const pqueue::Config& config,
    RequestStore& store
);

bool peek(State& state, pqueue::Record& out, RequestStore& store);
bool consume(State& state, RequestStore& store);
bool dropFront(State& state, RequestStore& store);
bool rewriteFront(State& state, const pqueue::Record& request, RequestStore& store);

} // namespace api::disk_buffer
