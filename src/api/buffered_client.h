#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "../config.h"
#include "../sensor_readings.h"
#include "backend_result.h"
#include "types.h"

namespace api {

enum class WriteStatus {
    Sent,
    Buffered,
    DroppedPermanent,
    DroppedBufferFull,
};

enum class WriteBufferReason {
    None,
    BacklogPresent,
    RetryableFailure,
};

struct WriteResult {
    WriteStatus status = WriteStatus::DroppedPermanent;
    BackendWriteResult backendResult = BackendWriteResult::Failed;
    int httpStatusCode = 0;
    std::string body;
    WriteBufferReason bufferReason = WriteBufferReason::None;
};

struct BufferDrainResult {
    int attempted = 0;
    int sent = 0;
    int dropped = 0;
    bool blockedByRetryableFailure = false;
    bool notDueYet = false;
};

struct BufferedClientPqueue;

class BufferedClient {
public:
    explicit BufferedClient(const ::Config& config);
    ~BufferedClient();

    WriteResult postSwitchbotReading(
        const SensorIdentity& identity,
        const SwitchbotReading& reading
    );

    WriteResult postXiaomiReading(
        const SensorIdentity& identity,
        const XiaomiReading& reading
    );

    WriteResult send(ApiRequest request);
    BufferDrainResult drainPending(std::uint64_t nowMs);

private:
    const ::Config& config_;
    std::unique_ptr<BufferedClientPqueue> pqueue_;
};

} // namespace api
