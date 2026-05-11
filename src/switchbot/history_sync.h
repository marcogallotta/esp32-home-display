#pragma once

#include "history_protocol.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace switchbot {
namespace history {

struct SyncRequest {
    // If startEpoch/endEpoch are both zero, fetch the latest 6 samples.
    // Otherwise, fetch samples in [startEpoch, endEpoch).
    uint32_t startEpoch = 0;
    uint32_t endEpoch = 0;
    uint32_t timeSyncEpoch = 0;
    uint32_t commandTimeoutMs = 3000;
    std::string progressLabel;
};

enum class SyncStatus {
    Ok,
    InvalidRequest,
    ConnectFailed,
    ServiceNotFound,
    CharacteristicNotFound,
    SubscribeFailed,
    WriteFailed,
    Timeout,
    BadAck,
    BadMetadata,
    BadPage,
};

struct SyncResult {
    SyncStatus status = SyncStatus::Ok;
    std::string message;
    Metadata metadata;
    std::vector<Sample> samples;

    bool ok() const { return status == SyncStatus::Ok; }
};

const char* syncStatusName(SyncStatus status);

#ifdef ARDUINO

struct SensorHistorySessionImpl;

class SensorHistorySession {
public:
    explicit SensorHistorySession(std::string mac);
    ~SensorHistorySession();

    SensorHistorySession(const SensorHistorySession&) = delete;
    SensorHistorySession& operator=(const SensorHistorySession&) = delete;

    SyncResult open();
    SyncResult fetch(const SyncRequest& request = SyncRequest{});

    bool isOpen() const;
    const Metadata& metadata() const;

private:
    std::unique_ptr<SensorHistorySessionImpl> impl_;
};

SyncResult syncSensorHistory(const std::string& mac, const SyncRequest& request = SyncRequest{});
#endif

}  // namespace history
}  // namespace switchbot
