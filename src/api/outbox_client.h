#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "../config.h"
#include "../sensor_readings.h"
#include "pqueue/outbox.h"
#include "backend_result.h"
#include "types.h"

namespace pqueue::http { class Transport; }

namespace api {

#ifndef ARDUINO
namespace detail {
std::string resolveDesktopPemPathForApiOutbox(const std::string& pemFile);
}
#endif

enum class WriteStatus {
    Sent,
    Queued,
    DroppedPermanent,
    DroppedQueueFull,
    FailedTemporary,
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
    std::uint32_t removedQueuedBytes = 0;
    bool blockedByRetryableFailure = false;
    bool notDueYet = false;
};

struct OutboxClientImpl;

class ApiWriter {
public:
    virtual ~ApiWriter() = default;

    virtual WriteResult postSwitchbotReading(
        const SensorIdentity& identity,
        const SwitchbotReading& reading
    ) = 0;

    virtual WriteResult postXiaomiReading(
        const SensorIdentity& identity,
        const XiaomiReading& reading
    ) = 0;
};

class OutboxClient : public ApiWriter {
public:
    explicit OutboxClient(const ::Config& config);
#ifndef ARDUINO
    // Test seam: inject transport/clock/spool without using curl or global time.
    OutboxClient(
        const ::Config& config,
        pqueue::http::Transport& transport,
        std::string queueBasePath,
        pqueue::ClockCallback clock,
        void* clockContext
    );
#endif
    ~OutboxClient();

    WriteResult postSwitchbotReading(
        const SensorIdentity& identity,
        const SwitchbotReading& reading
    ) override;

    WriteResult postXiaomiReading(
        const SensorIdentity& identity,
        const XiaomiReading& reading
    ) override;

    WriteResult send(ApiRequest request);
    OutboxDrainResult drainPending(std::uint64_t nowMs);
    pqueue::CompactIdleResult compactIdle(size_t maxSteps);

private:
    const ::Config& config_;
    std::unique_ptr<OutboxClientImpl> pqueue_;
};

} // namespace api
