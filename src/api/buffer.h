#pragma once

#include <cstdint>
#include <ctime>
#include <deque>
#include <string>

#include "../config.h"
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
    std::uint64_t nextDrainAllowedAtMs = 0;
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
    RequestStore& store,
    std::uint64_t nowMs
);

bool bufferHasBacklog(BufferState& buffer, RequestStore& store);

bool peekBufferedRequest(
    BufferState& buffer,
    BufferedRequest& out,
    RequestStore& store
);

bool consumeBufferedRequest(BufferState& buffer, RequestStore& store);
bool dropBufferedRequest(BufferState& buffer, RequestStore& store);

bool rewriteBufferedRequest(
    BufferState& buffer,
    const BufferedRequest& request,
    RequestStore& store
);

std::uint64_t bufferDrainDelayMs(const ApiBufferConfig& config);
void delayNextBufferDrain(BufferState& buffer, const ApiBufferConfig& config, std::uint64_t nowMs);

} // namespace api
