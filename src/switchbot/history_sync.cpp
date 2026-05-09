#include "history_sync.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>

#ifdef ARDUINO
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../log.h"
#endif

namespace switchbot {
namespace history {

const char* syncStatusName(SyncStatus status) {
    switch (status) {
        case SyncStatus::Ok:
            return "ok";
        case SyncStatus::InvalidRequest:
            return "invalid_request";
        case SyncStatus::ConnectFailed:
            return "connect_failed";
        case SyncStatus::ServiceNotFound:
            return "service_not_found";
        case SyncStatus::CharacteristicNotFound:
            return "characteristic_not_found";
        case SyncStatus::SubscribeFailed:
            return "subscribe_failed";
        case SyncStatus::WriteFailed:
            return "write_failed";
        case SyncStatus::Timeout:
            return "timeout";
        case SyncStatus::BadAck:
            return "bad_ack";
        case SyncStatus::BadMetadata:
            return "bad_metadata";
        case SyncStatus::BadPage:
            return "bad_page";
    }
    return "unknown";
}

#ifdef ARDUINO

namespace {

const NimBLEUUID kSwitchbotServiceUuid("cba20d00-224d-11e6-9fb8-0002a5d5c51b");
const NimBLEUUID kWriteCharacteristicUuid("cba20002-224d-11e6-9fb8-0002a5d5c51b");
const NimBLEUUID kNotifyCharacteristicUuid("cba20003-224d-11e6-9fb8-0002a5d5c51b");

constexpr uint32_t kScanDurationMs = 15000;


struct NotifyState {
    NotifyState()
        : ready(xSemaphoreCreateBinary()) {}

    ~NotifyState() {
        if (ready != nullptr) {
            vSemaphoreDelete(ready);
        }
    }

    NotifyState(const NotifyState&) = delete;
    NotifyState& operator=(const NotifyState&) = delete;

    bool valid() const { return ready != nullptr; }

    std::vector<uint8_t> value;
    SemaphoreHandle_t ready = nullptr;
};

SyncResult fail(SyncStatus status, const std::string& message) {
    SyncResult result;
    result.status = status;
    result.message = message;
    return result;
}

std::string normalizedMac(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}

void drainNotifications(NotifyState& state) {
    state.value.clear();
    while (state.ready != nullptr && xSemaphoreTake(state.ready, 0) == pdTRUE) {
    }
}

void logBytes(const char* label, const std::vector<uint8_t>& bytes) {
    logLine(
        LogLevel::Debug,
        std::string("switchbot_history_bytes,label=") + label +
        ",len=" + std::to_string(bytes.size()) +
        ",hex=" + bytesToHex(bytes)
    );
}

std::string responseSummary(const std::vector<uint8_t>& response) {
    return "len=" + std::to_string(response.size()) + ",raw=" + bytesToHex(response);
}

std::string badAckMessage(const char* command,
                          const std::vector<uint8_t>& response,
                          const char* expected) {
    return std::string("unexpected ") + command + " ack," +
           responseSummary(response) + ",expected=" + expected;
}

std::string badDecodeMessage(const char* context,
                             const std::vector<uint8_t>& response) {
    return std::string(context) + "," + responseSummary(response);
}

std::string badPageMessage(uint32_t pageIndex, const std::vector<uint8_t>& response) {
    return "page response decode failed,page_index=" + std::to_string(pageIndex) +
           "," + responseSummary(response);
}

void logProgress(const std::string& label, uint32_t done, uint32_t total) {
    if (total == 0) {
        return;
    }

    const uint32_t percent = std::min<uint32_t>(100, (done * 100U) / total);
    logLine(
        LogLevel::Info,
        "switchbot_history_progress," + label + "," +
        std::to_string(done) + "/" + std::to_string(total) + "," +
        std::to_string(percent) + "%"
    );
}

std::string requestLabel(const std::string& mac, const SyncRequest& request) {
    return request.progressLabel.empty() ? mac : request.progressLabel;
}

void logPageRequest(const std::string& mac,
                    const SyncRequest& request,
                    const Metadata& metadata,
                    uint32_t pageIndex,
                    uint32_t endExclusive,
                    uint8_t requestSampleCount) {
    const uint32_t pageEnd = pageIndex + requestSampleCount;
    const bool shortRequest = requestSampleCount < kSamplesPerPage;
    const char* shortReason = "none";
    if (shortRequest) {
        shortReason = pageEnd == metadata.endIndex ? "metadata_tail" : "window_tail";
    }

    logLine(
        LogLevel::Debug,
        "switchbot_history_page_request," + requestLabel(mac, request) +
        ",page_index=" + std::to_string(pageIndex) +
        ",count=" + std::to_string(requestSampleCount) +
        ",page_end=" + std::to_string(pageEnd) +
        ",end_exclusive=" + std::to_string(endExclusive) +
        ",metadata_end_index=" + std::to_string(metadata.endIndex) +
        ",aligned=" + ((pageIndex % kSamplesPerPage) == 0 ? "yes" : "no") +
        ",short=" + (shortRequest ? "yes" : "no") +
        ",short_reason=" + shortReason
    );
}

bool waitForNotify(NotifyState& state, uint32_t timeoutMs, std::vector<uint8_t>& out) {
    const TickType_t timeoutTicks = pdMS_TO_TICKS(timeoutMs);
    if (state.ready == nullptr || xSemaphoreTake(state.ready, timeoutTicks) != pdTRUE) {
        return false;
    }

    out = state.value;
    logBytes("notify", out);
    state.value.clear();
    return true;
}

bool writeAndWait(NimBLERemoteCharacteristic& writeChar,
                  NotifyState& notifyState,
                  const char* label,
                  const std::vector<uint8_t>& command,
                  uint32_t timeoutMs,
                  std::vector<uint8_t>& response) {
    drainNotifications(notifyState);
    logBytes(label, command);

    if (!writeChar.writeValue(command.data(), command.size(), true)) {
        return false;
    }

    return waitForNotify(notifyState, timeoutMs, response);
}

const NimBLEAdvertisedDevice* findAdvertisedDeviceByMac(const std::string& mac) {
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan == nullptr) {
        return nullptr;
    }

    const std::string wanted = normalizedMac(mac);
    scan->clearResults();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->setMaxResults(40);

    NimBLEScanResults results = scan->getResults(kScanDurationMs, false);
    const int count = results.getCount();
    for (int i = 0; i < count; ++i) {
        const NimBLEAdvertisedDevice* device = results.getDevice(static_cast<uint32_t>(i));
        if (device == nullptr) {
            continue;
        }

        const NimBLEAddress& address = device->getAddress();
        const std::string seen = normalizedMac(address.toString());
        const bool match = seen == wanted;
        if (match) {
            return device;
        }
    }

    return nullptr;
}

uint32_t chooseTimeSyncEpoch(const SyncRequest& request) {
    if (request.timeSyncEpoch != 0) {
        return request.timeSyncEpoch;
    }

    const time_t now = std::time(nullptr);
    if (now > 1700000000) {
        return static_cast<uint32_t>(now);
    }

    if (request.endEpoch != 0) {
        return request.endEpoch;
    }

    return 0;
}

uint32_t rangeStartIndex(const Metadata& metadata, const SyncRequest& request) {
    if (request.startEpoch == 0 && request.endEpoch == 0) {
        // The metadata endIndex behaves like an exclusive end cursor.
        // The device rejects reads that spill past that cursor, so the latest
        // one-page request starts exactly one full page before endIndex.
        return metadata.endIndex >= kSamplesPerPage ? metadata.endIndex - kSamplesPerPage : 0;
    }

    const uint32_t firstWantedIndex = indexForEpochCeil(
        metadata.startEpoch,
        request.startEpoch,
        metadata.intervalSeconds
    );

    // Page reads should start on a six-sample page boundary; samples before the
    // requested window are trimmed locally.
    return pageStartForIndex(firstWantedIndex);
}

uint32_t rangeEndExclusiveIndex(const Metadata& metadata, const SyncRequest& request) {
    if (request.startEpoch == 0 && request.endEpoch == 0) {
        return metadata.endIndex;
    }

    return std::min(
        indexForEpochCeil(metadata.startEpoch, request.endEpoch, metadata.intervalSeconds),
        metadata.endIndex
    );
}

struct RequestedSampleRange {
    uint32_t firstRequestIndex = 0;
    uint32_t endExclusiveIndex = 0;
    uint32_t progressTotalSamples = 0;
};

void logPagePlan(const std::string& mac,
                 const SyncRequest& request,
                 const Metadata& metadata,
                 const RequestedSampleRange& range) {
    const bool explicitWindow = request.startEpoch != 0 || request.endEpoch != 0;
    logLine(
        LogLevel::Debug,
        "switchbot_history_plan," + requestLabel(mac, request) +
        ",request_start=" + std::to_string(request.startEpoch) +
        ",request_end=" + std::to_string(request.endEpoch) +
        ",metadata_start=" + std::to_string(metadata.startEpoch) +
        ",metadata_end_index=" + std::to_string(metadata.endIndex) +
        ",metadata_end_epoch=" + std::to_string(epochForIndex(metadata.startEpoch, metadata.endIndex, metadata.intervalSeconds)) +
        ",interval=" + std::to_string(metadata.intervalSeconds) +
        ",first_page=" + std::to_string(range.firstRequestIndex) +
        ",end_exclusive=" + std::to_string(range.endExclusiveIndex) +
        ",progress_total=" + std::to_string(range.progressTotalSamples) +
        ",explicit_window=" + (explicitWindow ? "yes" : "no") +
        ",first_page_aligned=" + ((range.firstRequestIndex % kSamplesPerPage) == 0 ? "yes" : "no")
    );
}

uint32_t progressTotalSamples(const Metadata& metadata,
                              const SyncRequest& request,
                              uint32_t endExclusiveIndex) {
    if (request.startEpoch == 0 && request.endEpoch == 0) {
        const uint32_t firstRequestIndex = rangeStartIndex(metadata, request);
        return endExclusiveIndex >= firstRequestIndex ? endExclusiveIndex - firstRequestIndex : 0;
    }

    const uint32_t firstWantedIndex = indexForEpochCeil(
        metadata.startEpoch,
        request.startEpoch,
        metadata.intervalSeconds
    );
    return endExclusiveIndex > firstWantedIndex ? endExclusiveIndex - firstWantedIndex : 0;
}

RequestedSampleRange planRequestedSampleRange(const Metadata& metadata, const SyncRequest& request) {
    RequestedSampleRange range;
    range.firstRequestIndex = rangeStartIndex(metadata, request);
    range.endExclusiveIndex = rangeEndExclusiveIndex(metadata, request);
    range.progressTotalSamples = progressTotalSamples(metadata, request, range.endExclusiveIndex);
    return range;
}

bool keepSample(const Sample& sample, const SyncRequest& request, uint32_t endExclusiveIndex) {
    if (sample.index >= endExclusiveIndex) {
        return false;
    }

    if (request.startEpoch == 0 && request.endEpoch == 0) {
        return true;
    }

    return sample.epoch >= request.startEpoch && sample.epoch < request.endEpoch;
}

}  // namespace

SyncResult syncSensorHistory(const std::string& mac, const SyncRequest& request) {
    if ((request.startEpoch == 0) != (request.endEpoch == 0)) {
        return fail(SyncStatus::InvalidRequest, "startEpoch and endEpoch must both be set or both be zero");
    }
    if (request.startEpoch != 0 && request.startEpoch >= request.endEpoch) {
        return fail(SyncStatus::InvalidRequest, "startEpoch must be before endEpoch");
    }

    static bool nimbleInitialised = false;
    if (!nimbleInitialised) {
        NimBLEDevice::init("");
        nimbleInitialised = true;
    }

    NotifyState notifyState;
    if (!notifyState.valid()) {
        return fail(SyncStatus::ConnectFailed, "notify semaphore allocation failed");
    }

    NimBLEClient* client = NimBLEDevice::createClient();
    if (client == nullptr) {
        return fail(SyncStatus::ConnectFailed, "NimBLEDevice::createClient failed");
    }

    auto cleanup = [&]() {
        if (client->isConnected()) {
            client->disconnect();
        }
        NimBLEDevice::deleteClient(client);
    };

    const NimBLEAdvertisedDevice* device = findAdvertisedDeviceByMac(mac);
    if (device == nullptr) {
        cleanup();
        return fail(SyncStatus::ConnectFailed, "device not found during scan");
    }

    if (!client->connect(device)) {
        cleanup();
        return fail(SyncStatus::ConnectFailed, "connect failed");
    }

    NimBLERemoteService* service = client->getService(kSwitchbotServiceUuid);
    if (service == nullptr) {
        cleanup();
        return fail(SyncStatus::ServiceNotFound, "SwitchBot service not found");
    }

    NimBLERemoteCharacteristic* writeChar = service->getCharacteristic(kWriteCharacteristicUuid);
    NimBLERemoteCharacteristic* notifyChar = service->getCharacteristic(kNotifyCharacteristicUuid);
    if (writeChar == nullptr || notifyChar == nullptr) {
        cleanup();
        return fail(SyncStatus::CharacteristicNotFound, "write or notify characteristic not found");
    }

    const bool subscribed = notifyChar->subscribe(
        true,
        [&notifyState](NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
            notifyState.value.assign(data, data + length);
            if (notifyState.ready != nullptr) {
                xSemaphoreGive(notifyState.ready);
            }
        }
    );
    if (!subscribed) {
        cleanup();
        return fail(SyncStatus::SubscribeFailed, "notify subscribe failed");
    }

    std::vector<uint8_t> response;
    const uint32_t timeSyncEpoch = chooseTimeSyncEpoch(request);
    if (timeSyncEpoch == 0) {
        cleanup();
        return fail(SyncStatus::InvalidRequest, "no valid time for time sync");
    }

    if (!writeAndWait(*writeChar, notifyState, "time-sync", buildTimeSyncCommand(timeSyncEpoch), request.commandTimeoutMs, response)) {
        cleanup();
        return fail(SyncStatus::Timeout, "time sync command timed out or write failed");
    }
    if (response.size() != 1 || response[0] != 0x01) {
        cleanup();
        return fail(SyncStatus::BadAck, badAckMessage("time-sync", response, "01"));
    }

    delay(50);

    if (!writeAndWait(*writeChar, notifyState, "start", buildStartCommand(), request.commandTimeoutMs, response)) {
        cleanup();
        return fail(SyncStatus::Timeout, "start command timed out or write failed");
    }
    // Observed devices return different 0x01-prefixed start ACK variants.
    // Keep metadata parsing as the strict validation gate for the history session.
    if (response.empty() || response[0] != 0x01) {
        cleanup();
        return fail(SyncStatus::BadAck, badAckMessage("start", response, "0x01-prefixed ack"));
    }

    delay(50);

    if (!writeAndWait(*writeChar, notifyState, "metadata", buildMetadataCommand(), request.commandTimeoutMs, response)) {
        cleanup();
        return fail(SyncStatus::Timeout, "metadata command timed out or write failed");
    }

    const auto metadata = parseMetadataResponse(response);
    if (!metadata.has_value()) {
        logBytes("bad-metadata", response);
        cleanup();
        return fail(SyncStatus::BadMetadata, badDecodeMessage("metadata response decode failed", response));
    }

    SyncResult result;
    result.metadata = *metadata;

    const RequestedSampleRange requestedRange = planRequestedSampleRange(result.metadata, request);
    const uint32_t firstPage = requestedRange.firstRequestIndex;
    const uint32_t endExclusive = requestedRange.endExclusiveIndex;
    const uint32_t totalProgressSamples = requestedRange.progressTotalSamples;

    logPagePlan(mac, request, result.metadata, requestedRange);

    uint32_t keptSamples = 0;
    uint32_t nextProgressAt = totalProgressSamples >= 60 ? 60 : totalProgressSamples;
    bool haveLastKeptIndex = false;
    uint32_t lastKeptIndex = 0;

    for (uint32_t pageIndex = firstPage; pageIndex < endExclusive; pageIndex += kSamplesPerPage) {
        if (pageIndex + kSamplesPerPage > result.metadata.endIndex) {
            logLine(
                LogLevel::Debug,
                "switchbot_history_drop_partial_metadata_page," + requestLabel(mac, request) +
                ",page_index=" + std::to_string(pageIndex) +
                ",page_end=" + std::to_string(pageIndex + kSamplesPerPage) +
                ",metadata_end_index=" + std::to_string(result.metadata.endIndex) +
                ",end_exclusive=" + std::to_string(endExclusive)
            );
            break;
        }

        delay(50);

        const uint8_t requestSampleCount = kSamplesPerPage;

        logPageRequest(mac, request, result.metadata, pageIndex, endExclusive, requestSampleCount);

        if (!writeAndWait(*writeChar, notifyState, "page", buildPageCommand(pageIndex), request.commandTimeoutMs, response)) {
            cleanup();
            return fail(SyncStatus::Timeout, "page command timed out or write failed");
        }

        const auto samples = decodePageResponse(
            response,
            pageIndex,
            result.metadata.startEpoch,
            result.metadata.intervalSeconds
        );
        if (!samples.has_value()) {
            logBytes("bad-page", response);
            cleanup();
            return fail(SyncStatus::BadPage, badPageMessage(pageIndex, response));
        }

        for (const Sample& sample : *samples) {
            if (keepSample(sample, request, endExclusive)) {
                if (haveLastKeptIndex && sample.index <= lastKeptIndex) {
                    continue;
                }
                result.samples.push_back(sample);
                haveLastKeptIndex = true;
                lastKeptIndex = sample.index;
                ++keptSamples;
                if (nextProgressAt != 0 && keptSamples >= nextProgressAt) {
                    logProgress(request.progressLabel.empty() ? mac : request.progressLabel, keptSamples, totalProgressSamples);
                    nextProgressAt += 60;
                    if (nextProgressAt > totalProgressSamples) {
                        nextProgressAt = totalProgressSamples > keptSamples ? totalProgressSamples : 0;
                    }
                }
            }
        }
    }

    cleanup();
    return result;
}

#endif  // ARDUINO

}  // namespace history
}  // namespace switchbot
