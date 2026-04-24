#pragma once

#include <string>

#include "../config.h"
#include "../sensor_readings.h"
#include "backend_result.h"
#include "buffer.h"
#include "client.h"

namespace api {

enum class WriteStatus {
    Sent,
    Buffered,
    DroppedPermanent,
    DroppedBufferFull,
};

struct WriteResult {
    WriteStatus status = WriteStatus::DroppedPermanent;
    BackendWriteResult backendResult = BackendWriteResult::Failed;
    int httpStatusCode = 0;
    std::string body;
};

class BufferedClient {
public:
    BufferedClient(
        const Config& config,
        BufferState& buffer,
        const Client& client
    );

    WriteResult postSwitchbotReading(
        const SensorIdentity& identity,
        const SwitchbotReading& reading
    );

    WriteResult postXiaomiReading(
        const SensorIdentity& identity,
        const XiaomiReading& reading
    );

private:
    WriteResult postBufferedRequest(BufferedRequest request);

    const Config& config_;
    BufferState& buffer_;
    const Client& client_;
};

} // namespace api
