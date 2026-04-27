#ifndef ARDUINO

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>

#include "api/buffer.h"
#include "api/buffered_client.h"
#include "api/client.h"
#include "api/disk_buffer.h"
#include "api/request_file_store.h"
#include "api/state.h"
#include "config.h"
#include "log.h"
#include "platform.h"
#include "sensor_readings.h"

namespace {

struct Options {
    int count = 0;              // 0 = forever
    int intervalMs = 1000;
    bool drainOnly = false;
    bool switchbotOnly = false;
    bool xiaomiOnly = false;
    std::string baseUrl;
    std::string apiKey;
};

void printUsage(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " [options]\n"
        << "\n"
        << "Options:\n"
        << "  --count N          Send N simulated requests, then exit. Default: forever\n"
        << "  --interval-ms N    Delay between cycles. Default: 1000\n"
        << "  --drain-only       Do not create new requests; only drain existing buffer\n"
        << "  --switchbot-only   Send only SwitchBot payloads\n"
        << "  --xiaomi-only      Send only Xiaomi payloads\n"
        << "  --base-url URL     Override config.api.baseUrl\n"
        << "  --api-key KEY      Override config.api.apiKey\n";
}

bool parseInt(const char* s, int& out) {
    char* end = nullptr;
    const long value = std::strtol(s, &end, 10);
    if (end == s || *end != '\0') {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

bool parseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        }

        if (arg == "--count" && i + 1 < argc) {
            if (!parseInt(argv[++i], options.count)) {
                return false;
            }
        } else if (arg == "--interval-ms" && i + 1 < argc) {
            if (!parseInt(argv[++i], options.intervalMs)) {
                return false;
            }
        } else if (arg == "--drain-only") {
            options.drainOnly = true;
        } else if (arg == "--switchbot-only") {
            options.switchbotOnly = true;
        } else if (arg == "--xiaomi-only") {
            options.xiaomiOnly = true;
        } else if (arg == "--base-url" && i + 1 < argc) {
            options.baseUrl = argv[++i];
        } else if (arg == "--api-key" && i + 1 < argc) {
            options.apiKey = argv[++i];
        } else {
            return false;
        }
    }

    return !(options.switchbotOnly && options.xiaomiOnly) && options.intervalMs > 0;
}

SensorIdentity switchbotIdentity(const Config& config) {
    if (!config.switchbot.sensors.empty()) {
        const auto& s = config.switchbot.sensors.front();
        return SensorIdentity{s.mac, s.name, s.shortName};
    }
    return SensorIdentity{"AA:BB:CC:DD:EE:01", "API Harness SwitchBot", "harness-sb"};
}

SensorIdentity xiaomiIdentity(const Config& config) {
    if (!config.xiaomi.sensors.empty()) {
        const auto& s = config.xiaomi.sensors.front();
        return SensorIdentity{s.mac, s.name, s.shortName};
    }
    return SensorIdentity{"AA:BB:CC:DD:EE:02", "API Harness Xiaomi", "harness-xm"};
}

SwitchbotReading makeSwitchbotReading(std::int64_t epochS, std::uint64_t seq) {
    SwitchbotReading reading;
    reading.temperatureC = 20.0f + static_cast<float>(seq % 10) * 0.1f;
    reading.humidityPct = static_cast<std::uint8_t>(40 + (seq % 10));
    reading.lastSeenEpochS = epochS;
    reading.rssi = -60;
    return reading;
}

XiaomiReading makeXiaomiReading(std::int64_t epochS, std::uint64_t seq) {
    XiaomiReading reading;
    reading.temperatureC = 19.0f + static_cast<float>(seq % 10) * 0.1f;
    reading.moisturePct = static_cast<std::uint8_t>(10 + (seq % 5));
    reading.lux = 1000 + static_cast<int>((seq % 20) * 100);
    reading.conductivityUsCm = 80 + static_cast<int>(seq % 10);
    reading.lastSeenEpochS = epochS;
    reading.rssi = -70;
    return reading;
}

template <typename ClientT>
auto postSwitchbotCompat(
    ClientT& client,
    const SensorIdentity& identity,
    const SwitchbotReading& reading,
    std::uint64_t nowMs,
    int
) -> decltype(client.postSwitchbotReading(identity, reading, nowMs)) {
    return client.postSwitchbotReading(identity, reading, nowMs);
}

template <typename ClientT>
auto postSwitchbotCompat(
    ClientT& client,
    const SensorIdentity& identity,
    const SwitchbotReading& reading,
    std::uint64_t,
    long
) -> decltype(client.postSwitchbotReading(identity, reading)) {
    return client.postSwitchbotReading(identity, reading);
}

template <typename ClientT>
auto postXiaomiCompat(
    ClientT& client,
    const SensorIdentity& identity,
    const XiaomiReading& reading,
    std::uint64_t nowMs,
    int
) -> decltype(client.postXiaomiReading(identity, reading, nowMs)) {
    return client.postXiaomiReading(identity, reading, nowMs);
}

template <typename ClientT>
auto postXiaomiCompat(
    ClientT& client,
    const SensorIdentity& identity,
    const XiaomiReading& reading,
    std::uint64_t,
    long
) -> decltype(client.postXiaomiReading(identity, reading)) {
    return client.postXiaomiReading(identity, reading);
}

const char* writeStatusName(api::WriteStatus status) {
    switch (status) {
        case api::WriteStatus::Sent: return "sent";
        case api::WriteStatus::Buffered: return "buffered";
        case api::WriteStatus::DroppedPermanent: return "dropped_permanent";
        case api::WriteStatus::DroppedBufferFull: return "dropped_buffer_full";
    }
    return "unknown";
}

const char* writeBufferReasonName(api::WriteBufferReason reason) {
    switch (reason) {
        case api::WriteBufferReason::None: return "none";
        case api::WriteBufferReason::BacklogPresent: return "backlog_present_post_skipped";
        case api::WriteBufferReason::RetryableFailure: return "retryable_failure";
    }
    return "unknown";
}

void logWrite(const std::string& label, const api::WriteResult& result) {
    std::string message =
        label + " write result: " + writeStatusName(result.status);

    if (result.status == api::WriteStatus::Buffered) {
        message += ", reason " + std::string(writeBufferReasonName(result.bufferReason));
        if (result.bufferReason == api::WriteBufferReason::BacklogPresent) {
            message += ", HTTP not attempted";
        } else {
            message += ", HTTP " + std::to_string(result.httpStatusCode);
        }
    } else {
        message += ", HTTP " + std::to_string(result.httpStatusCode);
    }

    logLine(LogLevel::Info, message);
}

void logDrain(const api::BufferDrainResult& result, const api::BufferState& buffer) {
    logLine(
        LogLevel::Info,
        "Harness buffer state: attempted " + std::to_string(result.attempted) +
        ", sent " + std::to_string(result.sent) +
        ", dropped " + std::to_string(result.dropped) +
        ", blocked " + std::to_string(result.blockedByRetryableFailure ? 1 : 0) +
        ", drainNotDue " + std::to_string(result.notDueYet ? 1 : 0) +
        ", RAM " + std::to_string(buffer.requests.size()) +
        ", disk " + std::to_string(buffer.disk.count) +
        ", nextDrainMs " + std::to_string(buffer.nextDrainAllowedAtMs)
    );
}

bool shouldSendXiaomi(const Options& options, std::uint64_t seq) {
    if (options.switchbotOnly) {
        return false;
    }
    if (options.xiaomiOnly) {
        return true;
    }
    return seq % 5 == 4;
}

void logTimeInvalid(std::uint64_t nowMs) {
    logLine(
        LogLevel::Error,
        "API harness skipped simulated write: time not initialized, uptimeMs=" +
        std::to_string(nowMs)
    );
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        printUsage(argv[0]);
        return 2;
    }

    Config config;
    if (!loadConfig(config)) {
        logLine(LogLevel::Error, "Failed to load desktop config from data/config.json");
        return 1;
    }

    if (!options.baseUrl.empty()) {
        config.api.baseUrl = options.baseUrl;
    }
    if (!options.apiKey.empty()) {
        config.api.apiKey = options.apiKey;
    }

    api::request_file_store::setBasePath("spool_harness");

    if (!api::request_file_store::mount()) {
        logLine(LogLevel::Error, "Failed to mount request file store");
        return 1;
    }

    platform::initTime(config);

    api::State apiState;
    auto& store = api::request_file_store::defaultStore();
    api::disk_buffer::load(apiState.buffer.disk, store);
    logLine(
        LogLevel::Info,
        "API disk buffer loaded: head " + std::to_string(apiState.buffer.disk.head) +
        ", tail " + std::to_string(apiState.buffer.disk.tail) +
        ", count " + std::to_string(apiState.buffer.disk.count)
    );

    api::Client apiClient(config);
    api::BufferedClient bufferedClient(config, apiState.buffer, apiClient, store);

    const SensorIdentity sb = switchbotIdentity(config);
    const SensorIdentity xm = xiaomiIdentity(config);

    bool hasValidTime = platform::hasValidTime();

    logLine(
        LogLevel::Info,
        "API harness started: baseUrl=" + config.api.baseUrl +
        ", intervalMs=" + std::to_string(options.intervalMs) +
        ", count=" + std::to_string(options.count) +
        ", drainOnly=" + std::to_string(options.drainOnly ? 1 : 0) +
        ", initialDisk=" + std::to_string(apiState.buffer.disk.count)
    );

    std::uint64_t seq = 0;
    while (options.count == 0 || static_cast<int>(seq) < options.count) {
        const std::uint64_t nowMs = platform::millis();

        if (!hasValidTime && platform::hasValidTime()) {
            hasValidTime = true;
        }

        const api::BufferDrainResult drain = api::maybeDrainBuffer(
            apiState.buffer,
            nowMs,
            config.api.buffer,
            apiClient,
            store
        );
        logDrain(drain, apiState.buffer);

        if (!options.drainOnly) {
            if (!hasValidTime) {
                logTimeInvalid(nowMs);
            } else {
                const std::time_t epoch = std::time(nullptr);

                if (shouldSendXiaomi(options, seq)) {
                    const auto result = postXiaomiCompat(
                        bufferedClient,
                        xm,
                        makeXiaomiReading(static_cast<std::int64_t>(epoch), seq),
                        nowMs,
                        0
                    );
                    logWrite("Xiaomi", result);
                } else {
                    const auto result = postSwitchbotCompat(
                        bufferedClient,
                        sb,
                        makeSwitchbotReading(static_cast<std::int64_t>(epoch), seq),
                        nowMs,
                        0
                    );
                    logWrite("SwitchBot", result);
                }
            }
        }

        ++seq;
        platform::delayMs(options.intervalMs);
    }

    return 0;
}
#endif
