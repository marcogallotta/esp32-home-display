#include <algorithm>
#include <atomic>
#include <ctime>
#include <cstdint>
#include <string>

#include "api/client.h"
#include "api/state.h"
#include "api_sync.h"
#include "ble/scanner.h"
#include "config.h"
#include "forecast/openmeteo.h"
#include "platform.h"
#include "salah/types.h"
#include "state.h"
#include "switchbot/ble.h"
#include "timing.h"
#include "ui/display.h"
#include "ui/state.h"
#include "update.h"
#include "xiaomi/ble.h"

namespace {
#ifdef ARDUINO
constexpr int kMaxVisibleSensorRows = 4;
#endif

bool hasAnyXiaomiReading(const State& state) {
    for (const auto& row : state.xiaomiSensors) {
        if (row.reading.hasAnyValue()) {
            return true;
        }
    }
    return false;
}

bool allXiaomiRowsComplete(const State& state) {
    for (const auto& row : state.xiaomiSensors) {
        if (!row.reading.temperatureC.has_value() ||
            !row.reading.lux.has_value() ||
            !row.reading.moisturePct.has_value() ||
            !row.reading.conductivityUsCm.has_value()) {
            return false;
        }
    }
    return true;
}

} // namespace

void run() {
    Config config;
    if (!loadConfig(config)) {
        platform::printLine("Failed to load config");
        return;
    }

    const std::size_t switchbotSensorCount = config.switchbot.sensors.size();
    const std::size_t xiaomiSensorCount = config.xiaomi.sensors.size();

#ifdef ARDUINO
    if (switchbotSensorCount > kMaxVisibleSensorRows) {
        platform::printLine("Config error: OLED supports at most 4 sensors");
        return;
    }
#endif

    int oldDay = -1;
    salah::Schedule today, tomorrow;
    forecast::ForecastData lastForecastData;
    bool hasLastForecastData = false;
    TimingState timing;
    bool hasValidTime = platform::initTime(config);

    State currentState;
    State previousState;
    currentState.switchbotSensors.resize(switchbotSensorCount);
    previousState.switchbotSensors.resize(switchbotSensorCount);
    currentState.xiaomiSensors.resize(xiaomiSensorCount);
    previousState.xiaomiSensors.resize(xiaomiSensorCount);

    api::State apiState;
    api::initState(currentState, apiState);
    api::Client apiClient(config);

    UiState currentUiState;
    bool hasPreviousState = false;

    switchbot::Scanner switchbotScanner(config.switchbot);
    xiaomi::Scanner xiaomiScanner(config.xiaomi);

    std::atomic<bool> switchbotUpdatePending{false};
    switchbotScanner.setUpdateCallback([&]() {
        switchbotUpdatePending.store(true);
    });

    std::atomic<bool> xiaomiUpdatePending{false};
    xiaomiScanner.setUpdateCallback([&]() {
        xiaomiUpdatePending.store(true);
    });

    ble::Scanner bleScanner([&](const ble::AdvertisementEvent& event) {
        switchbotScanner.handleAdvertisement(event);
        xiaomiScanner.handleAdvertisement(event);
    });
    bleScanner.start();

#ifdef ARDUINO
    initDisplay();
#endif

    while (true) {
        const std::time_t now = std::time(nullptr);

        previousState = currentState;
        bleScanner.poll();

        if (isSalahDue(now, timing)) {
            if (!hasValidTime) {
                hasValidTime = platform::initTime(config);
                if (!hasValidTime) {
                    platform::printLine("Time sync failed");
                    markSalahUpdated(now, timing);
                } else {
                    oldDay = -1;
                }
            }

            if (hasValidTime) {
                const std::time_t now2 = std::time(nullptr);
                const std::tm localTime = *std::localtime(&now2);
                updateSalahState(config, localTime, oldDay, today, tomorrow, currentState);
                markSalahUpdated(now2, timing);
            }
        }

        if (switchbotUpdatePending.exchange(false) || areSensorsDue(now, timing)) {
            updateSwitchbotState(config, now, switchbotScanner, currentState);
            markSensorsUpdated(now, timing);
        }

        if (xiaomiUpdatePending.exchange(false) || areXiaomiDue(now, timing)) {
            updateXiaomiState(config, now, xiaomiScanner, currentState);

            if (allXiaomiRowsComplete(currentState)) {
                markXiaomiUpdated(now, config, timing);
            } else {
                // TODO: change this retry from 1 minute to 5 minutes after debugging.
                timing.nextXiaomiDueEpochS = now + 60;
            }
        }

        if (isForecastDue(now, timing)) {
            if (updateForecastState(config, currentState)) {
                lastForecastData = currentState.forecast;
                hasLastForecastData = currentState.hasForecast;
                markForecastUpdatedSuccess(now, config, timing);
            } else {
                if (hasLastForecastData) {
                    currentState.forecast = lastForecastData;
                    currentState.hasForecast = true;
                }
                markForecastUpdatedFailure(now, timing);
            }
        } else if (hasLastForecastData) {
            currentState.forecast = lastForecastData;
            currentState.hasForecast = true;
        }

        syncApiState(currentState, apiState, apiClient);

        [[maybe_unused]] bool doFullDraw = false;

        if (!hasPreviousState) {
            currentUiState.dirty.salahName = true;
            currentUiState.dirty.minutes = true;
            currentUiState.dirty.sensorsAny = true;
            currentUiState.dirty.sensorRows.assign(switchbotSensorCount, true);
            currentUiState.dirty.forecast = true;
            hasPreviousState = true;
            doFullDraw = true;
        } else {
            currentUiState.dirty = computeDirtyRegions(previousState, currentState);
        }

        platform::printLine("Dirty:");
        platform::printLine(std::string("  salahName=") + (currentUiState.dirty.salahName ? "yes" : "no"));
        platform::printLine(std::string("  minutes=") + (currentUiState.dirty.minutes ? "yes" : "no"));
        platform::printLine(std::string("  sensorsAny=") + (currentUiState.dirty.sensorsAny ? "yes" : "no"));
        for (std::size_t i = 0; i < switchbotSensorCount; ++i) {
            platform::printLine(
                "  sensor[" + std::to_string(i) + "]=" +
                (currentUiState.dirty.sensorRows[i] ? "yes" : "no")
            );
        }
        platform::printLine("");

#ifdef ARDUINO
        if (doFullDraw) {
            drawAllRegions(currentState);
        } else {
            if (currentUiState.dirty.salahName) {
                drawSalahNameRegion(currentState);
                updateSalahNameRegion();
            }

            if (currentUiState.dirty.minutes) {
                drawMinutesRegion(currentState);
                updateMinutesRegion();
            }

            const int visibleRows = std::min<int>(
                static_cast<int>(switchbotSensorCount),
                kMaxVisibleSensorRows
            );
            for (int rowIndex = 0; rowIndex < visibleRows; ++rowIndex) {
                if (currentUiState.dirty.sensorRows[rowIndex]) {
                    drawSensorRowRegion(currentState, rowIndex);
                    updateSensorRowRegion(rowIndex);
                }
            }

            if (currentUiState.dirty.forecast) {
                drawForecastRegion(currentState);
                updateForecastRegion();
            }
        }
#endif

        const int delayMs = computeSleepMs(std::time(nullptr), timing);
        platform::printLine("Sleeping for " + std::to_string(delayMs / 1000) + " seconds...");
        platform::printLine("");
        platform::delayMs(delayMs);
    }
}

#ifndef ARDUINO
int main() {
    run();
    return 0;
}
#endif
