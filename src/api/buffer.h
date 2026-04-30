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

struct ApiRequest {
    std::string path;
    std::string mac;
    std::string body;
    int timeoutRetryCount = 0;
    int tlsRetryCount = 0;
};

struct BufferState {
    std::deque<ApiRequest> requests;
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
    ApiRequest request,
    const ApiBufferConfig& config,
    RequestStore& store
);

bool bufferHasBacklog(BufferState& buffer, RequestStore& store);

bool peekBufferedRequest(
    BufferState& buffer,
    ApiRequest& out,
    RequestStore& store
);

bool consumeBufferedRequest(BufferState& buffer, RequestStore& store);
bool dropBufferedRequest(BufferState& buffer, RequestStore& store);

bool rewriteBufferedRequest(
    BufferState& buffer,
    const ApiRequest& request,
    RequestStore& store
);


} // namespace api
