#include <Arduino.h>
#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

#include "config.h"
#include "platform.h"
#include "switchbot/history_sync.h"

#ifndef SWITCHBOT_HISTORY_TEST_START_EPOCH
#define SWITCHBOT_HISTORY_TEST_START_EPOCH 0
#endif

#ifndef SWITCHBOT_HISTORY_TEST_END_EPOCH
#define SWITCHBOT_HISTORY_TEST_END_EPOCH 0
#endif

#ifndef SWITCHBOT_HISTORY_TEST_COMMAND_TIMEOUT_MS
#define SWITCHBOT_HISTORY_TEST_COMMAND_TIMEOUT_MS 5000
#endif

#ifndef SWITCHBOT_HISTORY_TEST_DEBUG
#define SWITCHBOT_HISTORY_TEST_DEBUG 0
#endif

#if SWITCHBOT_HISTORY_TEST_DEBUG
#define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define DBG_PRINTLN(x) Serial.println(x)
#else
#define DBG_PRINTF(...)
#define DBG_PRINTLN(x)
#endif

namespace {

void dbgMetadata(const SwitchbotSensorConfig& sensor, const switchbot::history::SyncResult& result) {
#if SWITCHBOT_HISTORY_TEST_DEBUG
    Serial.printf(
        "[switchbot history test debug] %s metadata start=%lu end=%lu end_index=%lu interval=%u samples=%u\n",
        sensor.name.c_str(),
        static_cast<unsigned long>(result.metadata.startEpoch),
        static_cast<unsigned long>(result.metadata.endEpoch),
        static_cast<unsigned long>(result.metadata.endIndex),
        static_cast<unsigned>(result.metadata.intervalSeconds),
        static_cast<unsigned>(result.samples.size())
    );
#else
    (void)sensor;
    (void)result;
#endif
}

void dbgSample(const SwitchbotSensorConfig& sensor, const switchbot::history::Sample& sample) {
#if SWITCHBOT_HISTORY_TEST_DEBUG
    Serial.printf(
        "[switchbot history test debug] %s sample index=%lu epoch=%lu temp=%.1f humidity=%u\n",
        sensor.name.c_str(),
        static_cast<unsigned long>(sample.index),
        static_cast<unsigned long>(sample.epoch),
        static_cast<double>(sample.temperatureC),
        static_cast<unsigned>(sample.humidityPct)
    );
#else
    (void)sensor;
    (void)sample;
#endif
}

bool validateResult(const SwitchbotSensorConfig& sensor,
                    const switchbot::history::SyncResult& result,
                    std::string& failure) {
    if (!result.ok()) {
        failure = sensor.name + ": sync failed: " +
            switchbot::history::syncStatusName(result.status) + " " + result.message;
        return false;
    }

    if (result.metadata.intervalSeconds != 60) {
        failure = sensor.name + ": unexpected interval";
        return false;
    }
    if (result.metadata.endIndex == 0) {
        failure = sensor.name + ": endIndex is zero";
        return false;
    }
    if (result.metadata.endEpoch <= result.metadata.startEpoch) {
        failure = sensor.name + ": metadata epochs are invalid";
        return false;
    }
    if (result.samples.empty()) {
        failure = sensor.name + ": no samples returned";
        return false;
    }

    for (size_t i = 0; i < result.samples.size(); ++i) {
        const switchbot::history::Sample& sample = result.samples[i];

        if (sample.temperatureC < -40.0 || sample.temperatureC > 80.0) {
            failure = sensor.name + ": temperature out of sane range";
            return false;
        }
        if (sample.humidityPct > 100) {
            failure = sensor.name + ": humidity out of sane range";
            return false;
        }

        if (i > 0) {
            const switchbot::history::Sample& previous = result.samples[i - 1];

            if (sample.index != previous.index + 1) {
                failure = sensor.name + ": sample indices are not contiguous";
                return false;
            }
            if (sample.epoch != previous.epoch + result.metadata.intervalSeconds) {
                failure = sensor.name + ": sample timestamps are not contiguous";
                return false;
            }
        }
    }

    dbgMetadata(sensor, result);
    for (const switchbot::history::Sample& sample : result.samples) {
        dbgSample(sensor, sample);
    }

    return true;
}

void test_switchbot_history_sync_for_configured_sensors() {
    Config config;
    TEST_ASSERT_TRUE_MESSAGE(loadConfig(config), "loadConfig failed; upload data/config.json to LittleFS first");
    TEST_ASSERT_FALSE_MESSAGE(config.switchbot.sensors.empty(), "config has no SwitchBot sensors");
    TEST_ASSERT_TRUE_MESSAGE(platform::initTime(config, 20000), "time sync failed");

    switchbot::history::SyncRequest request;
    request.startEpoch = static_cast<std::uint32_t>(SWITCHBOT_HISTORY_TEST_START_EPOCH);
    request.endEpoch = static_cast<std::uint32_t>(SWITCHBOT_HISTORY_TEST_END_EPOCH);
    request.timeSyncEpoch = static_cast<std::uint32_t>(std::time(nullptr));
    request.commandTimeoutMs = static_cast<std::uint32_t>(SWITCHBOT_HISTORY_TEST_COMMAND_TIMEOUT_MS);

    bool allOk = true;
    std::string failures;

    for (const SwitchbotSensorConfig& sensor : config.switchbot.sensors) {
        DBG_PRINTF("[switchbot history test debug] syncing %s %s\n", sensor.name.c_str(), sensor.mac.c_str());

        const switchbot::history::SyncResult result =
            switchbot::history::syncSensorHistory(sensor.mac, request);

        std::string failure;
        if (!validateResult(sensor, result, failure)) {
            allOk = false;
            failures += failure + "\n";
            Serial.println(failure.c_str());
        }

        delay(250);
    }

    TEST_ASSERT_TRUE_MESSAGE(allOk, failures.c_str());
}

}  // namespace

void setup() {
    delay(2000);

    UNITY_BEGIN();
    RUN_TEST(test_switchbot_history_sync_for_configured_sensors);
    UNITY_END();
}

void loop() {}
