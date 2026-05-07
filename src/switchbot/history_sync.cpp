#include "history_sync.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>

#ifdef ARDUINO
#include <Arduino.h>
#include <NimBLEDevice.h>
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

#if defined(SWITCHBOT_HISTORY_TEST_DEBUG) && SWITCHBOT_HISTORY_TEST_DEBUG
#define HISTORY_DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define HISTORY_DBG_PRINTF(...)
#endif

struct NotifyState {
    std::vector<uint8_t> value;
    volatile bool hasValue = false;
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
    state.hasValue = false;
}

void logBytes(const char* label, const std::vector<uint8_t>& bytes) {
#if defined(SWITCHBOT_HISTORY_TEST_DEBUG) && SWITCHBOT_HISTORY_TEST_DEBUG
    Serial.printf("[switchbot history debug] %s len=%u hex=%s\n",
                  label,
                  static_cast<unsigned>(bytes.size()),
                  bytesToHex(bytes).c_str());
#else
    (void)label;
    (void)bytes;
#endif
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

bool waitForNotify(NotifyState& state, uint32_t timeoutMs, std::vector<uint8_t>& out) {
    const uint32_t start = millis();
    while (!state.hasValue) {
        if (millis() - start >= timeoutMs) {
            HISTORY_DBG_PRINTF("[switchbot history debug] notify timeout after %lu ms\n",
                               static_cast<unsigned long>(timeoutMs));
            return false;
        }
        delay(10);
    }

    out = state.value;
    logBytes("notify", out);
    drainNotifications(state);
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
        HISTORY_DBG_PRINTF("[switchbot history debug] write failed label=%s\n", label);
        return false;
    }

    return waitForNotify(notifyState, timeoutMs, response);
}

const NimBLEAdvertisedDevice* findAdvertisedDeviceByMac(const std::string& mac) {
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan == nullptr) {
        HISTORY_DBG_PRINTF("[switchbot history debug] NimBLEDevice::getScan returned null\n");
        return nullptr;
    }

    const std::string wanted = normalizedMac(mac);
    HISTORY_DBG_PRINTF("[switchbot history debug] scanning for mac=%s duration_ms=%lu\n",
                       wanted.c_str(),
                       static_cast<unsigned long>(kScanDurationMs));

    scan->clearResults();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->setMaxResults(40);

    NimBLEScanResults results = scan->getResults(kScanDurationMs, false);
    const int count = results.getCount();
    HISTORY_DBG_PRINTF("[switchbot history debug] scan complete count=%d\n", count);

    for (int i = 0; i < count; ++i) {
        const NimBLEAdvertisedDevice* device = results.getDevice(static_cast<uint32_t>(i));
        if (device == nullptr) {
            continue;
        }

        const NimBLEAddress& address = device->getAddress();
        const std::string seen = normalizedMac(address.toString());
        const bool match = seen == wanted;
        HISTORY_DBG_PRINTF(
            "[switchbot history debug] scan[%d] addr=%s type=%u rssi=%d connectable=%u match=%u\n",
            i,
            seen.c_str(),
            static_cast<unsigned>(device->getAddressType()),
            device->getRSSI(),
            static_cast<unsigned>(device->isConnectable()),
            static_cast<unsigned>(match));

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

uint32_t indexForEpochCeil(uint32_t startEpoch, uint32_t epoch, uint16_t intervalSeconds) {
    if (intervalSeconds == 0 || epoch <= startEpoch) {
        return 0;
    }

    const uint32_t delta = epoch - startEpoch;
    return (delta + static_cast<uint32_t>(intervalSeconds) - 1) /
        static_cast<uint32_t>(intervalSeconds);
}

uint32_t pageStartForIndex(uint32_t index) {
    return index - (index % 6U);
}

uint32_t rangeStartIndex(const Metadata& metadata, const SyncRequest& request) {
    if (request.startEpoch == 0 && request.endEpoch == 0) {
        // The metadata endIndex behaves like an exclusive end cursor.
        // Requesting endIndex - 5 with count 6 asks one sample past the end and
        // SwitchBot replies with 0x02. The last full 6-sample page starts at
        // endIndex - 6.
        return metadata.endIndex >= 6 ? metadata.endIndex - 6 : 0;
    }

    return pageStartForIndex(indexForEpochCeil(
        metadata.startEpoch,
        request.startEpoch,
        metadata.intervalSeconds
    ));
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

uint32_t progressTotalSamples(const Metadata& metadata,
                              const SyncRequest& request,
                              uint32_t endExclusiveIndex) {
    if (request.startEpoch == 0 && request.endEpoch == 0) {
        return endExclusiveIndex >= rangeStartIndex(metadata, request)
            ? endExclusiveIndex - rangeStartIndex(metadata, request)
            : 0;
    }

    const uint32_t firstWanted = indexForEpochCeil(
        metadata.startEpoch,
        request.startEpoch,
        metadata.intervalSeconds
    );
    return endExclusiveIndex > firstWanted ? endExclusiveIndex - firstWanted : 0;
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

    HISTORY_DBG_PRINTF("[switchbot history debug] connecting addr=%s type=%u\n",
                       device->getAddress().toString().c_str(),
                       static_cast<unsigned>(device->getAddressType()));

    if (!client->connect(device)) {
        HISTORY_DBG_PRINTF("[switchbot history debug] connect failed last_error=%d\n", client->getLastError());
        cleanup();
        return fail(SyncStatus::ConnectFailed, "connect failed");
    }

    HISTORY_DBG_PRINTF("[switchbot history debug] connected peer=%s rssi=%d mtu=%u\n",
                       client->getPeerAddress().toString().c_str(),
                       client->getRssi(),
                       static_cast<unsigned>(client->getMTU()));

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
            notifyState.hasValue = true;
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

    Metadata metadata;
    if (!parseMetadataResponse(response, metadata)) {
        logBytes("bad-metadata", response);
        cleanup();
        return fail(SyncStatus::BadMetadata, badDecodeMessage("metadata response decode failed", response));
    }

    SyncResult result;
    result.metadata = metadata;

    const uint32_t firstPage = rangeStartIndex(result.metadata, request);
    const uint32_t endExclusive = rangeEndExclusiveIndex(result.metadata, request);
    const uint32_t totalProgressSamples = progressTotalSamples(result.metadata, request, endExclusive);
    uint32_t keptSamples = 0;
    uint32_t nextProgressAt = totalProgressSamples >= 60 ? 60 : totalProgressSamples;

    for (uint32_t pageIndex = firstPage; pageIndex < endExclusive; pageIndex += 6) {
        delay(50);

        if (!writeAndWait(*writeChar, notifyState, "page", buildPageCommand(pageIndex, 6), request.commandTimeoutMs, response)) {
            cleanup();
            return fail(SyncStatus::Timeout, "page command timed out or write failed");
        }

        const std::vector<Sample> samples = decodePageResponse(
            response,
            pageIndex,
            result.metadata.startEpoch,
            result.metadata.intervalSeconds
        );
        if (samples.empty()) {
            logBytes("bad-page", response);
            cleanup();
            return fail(SyncStatus::BadPage, badPageMessage(pageIndex, response));
        }

        for (const Sample& sample : samples) {
            if (keepSample(sample, request, endExclusive)) {
                result.samples.push_back(sample);
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
