#pragma once

#include <cstdint>

#include "../config.h"
#include "buffer.h"

namespace api::disk_buffer {

struct State {
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
    bool loaded = false;
};

bool load(State& state);

bool enqueue(
    State& state,
    const BufferedRequest& request,
    const ApiBufferConfig& config
);

bool peek(State& state, BufferedRequest& out);
bool consume(State& state);
bool dropFront(State& state);
bool rewriteFront(State& state, const BufferedRequest& request);

} // namespace api::disk_buffer
