#pragma once

#include <cstdint>
#include <ctime>
#include <deque>
#include <string>

#include "../config.h"
#include "../network.h"
#include "poster.h"
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

struct BufferedRequest {
    std::string path;
    std::string mac;
    std::string body;
    int timeoutRetryCount = 0;
    int tlsRetryCount = 0;
};

struct BufferState {
    std::deque<BufferedRequest> requests;
    std::time_t nextDrainAllowedAtEpochS = 0;
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

BufferInsertResult bufferRequest(
    BufferState& buffer,
    BufferedRequest request,
    const ApiBufferConfig& config,
    RequestStore& store
);

BufferDrainResult maybeDrainBuffer(
    BufferState& buffer,
    std::time_t now,
    const ApiBufferConfig& config,
    const ApiPoster& poster,
    RequestStore& store
);

} // namespace api
