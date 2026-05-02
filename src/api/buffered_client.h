#pragma once

#include <memory>
#include <string>

#include "../config.h"
#include "../sensor_readings.h"
#include "backend_result.h"
#include "buffer.h"
#include "client.h"
#include "poster.h"
#include "record_store.h"

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

struct BufferedClientDesktopPqueue;

class BufferedClient {
public:
    BufferedClient(
        const ::Config& config,
        BufferState& buffer,
        const ApiPoster& poster,
        RecordStore& store
    );

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
    ~BufferedClient();

private:
    void delayNextDrain(std::uint64_t nowMs);
    const ::Config& config_;
    BufferState& buffer_;
    const ApiPoster& poster_;
    RecordStore& store_;
    std::uint64_t nextDrainAllowedAtMs_ = 0;
#ifndef ARDUINO
    std::unique_ptr<BufferedClientDesktopPqueue> desktopPqueue_;
#endif
};

} // namespace api
