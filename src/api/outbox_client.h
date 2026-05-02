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
    Queued,
    DroppedPermanent,
    DroppedQueueFull,
};

enum class WriteQueueReason {
    None,
    BacklogPresent,
    RetryableFailure,
};

struct WriteResult {
    WriteStatus status = WriteStatus::DroppedPermanent;
    BackendWriteResult backendResult = BackendWriteResult::Failed;
    int httpStatusCode = 0;
    std::string body;
    WriteQueueReason queueReason = WriteQueueReason::None;
};

struct OutboxDrainResult {
    int attempted = 0;
    int sent = 0;
    int dropped = 0;
    bool blockedByRetryableFailure = false;
    bool notDueYet = false;
};

struct OutboxClientImpl;

class OutboxClient {
public:
    explicit OutboxClient(const ::Config& config);
    ~OutboxClient();

    WriteResult postSwitchbotReading(
        const SensorIdentity& identity,
        const SwitchbotReading& reading
    );

    WriteResult postXiaomiReading(
        const SensorIdentity& identity,
        const XiaomiReading& reading
    );

    WriteResult send(ApiRequest request);
    OutboxDrainResult drainPending(std::uint64_t nowMs);

private:
    const ::Config& config_;
    std::unique_ptr<OutboxClientImpl> pqueue_;
};

} // namespace api
