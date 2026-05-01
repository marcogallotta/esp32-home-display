#pragma once

#include "../config.h"
#include "buffer.h"
#include "request_store.h"

namespace api::disk_buffer {

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
