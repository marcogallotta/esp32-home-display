#pragma once

#include <cstdint>

#include "../config.h"
#include "buffer.h"
#include "request_store.h"

namespace api::disk_buffer {

struct State {
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
    bool loaded = false;
};

bool load(State& state, RequestStore& store);
bool load(State& state);

bool enqueue(
    State& state,
    const BufferedRequest& request,
    const ApiBufferConfig& config,
    RequestStore& store
);

bool enqueue(
    State& state,
    const BufferedRequest& request,
    const ApiBufferConfig& config
);

bool peek(State& state, BufferedRequest& out, RequestStore& store);
bool peek(State& state, BufferedRequest& out);

bool consume(State& state, RequestStore& store);
bool consume(State& state);

bool dropFront(State& state, RequestStore& store);
bool dropFront(State& state);

bool rewriteFront(State& state, const BufferedRequest& request, RequestStore& store);
bool rewriteFront(State& state, const BufferedRequest& request);

} // namespace api::disk_buffer
