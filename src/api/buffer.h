#pragma once

#include <cstdint>
#include <ctime>
#include <deque>
#include <string>

#include "../config.h"
#include "../network.h"

namespace api {

class Client;

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
    const ApiBufferConfig& config
);

BufferDrainResult maybeDrainBuffer(
    BufferState& buffer,
    std::time_t now,
    const ApiBufferConfig& config,
    const Client& client
);

} // namespace api
