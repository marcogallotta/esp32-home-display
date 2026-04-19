#include <algorithm>
#include <atomic>
#include <ctime>
#include <cstdint>
#include <string>

#include "ble/scanner.h"
#include "config.h"
#include "forecast/openmeteo.h"
#include "platform.h"
#include "salah/types.h"
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

bool hasAnyXiaomiReading(const UiState& uiState) {
    for (const auto& row : uiState.xiaomiSensors) {
        if (row.hasReading) {
            return true;
        }
    }
    return false;
}

bool allXiaomiRowsComplete(const UiState& uiState) {
    for (const auto& row : uiState.xiaomiSensors) {
        if (!row.hasReading) {
            return false;
        }
        if (!row.hasTemperature || !row.hasLux || !row.hasMoisture || !row.hasConductivity) {
            return false;
        }
    }
    return true;
}

} // namespace

void run() {
    Config tmpConfig;
    if (!loadConfig(tmpConfig)) {
        platform::printLine("Failed to load config");
        return;
    }
    const Config config = tmpConfig;

    const std::size_t sensorCount = config.switchbot.sensors.size();
    const std::size_t xiaomiSensorCount = config.xiaomi.sensors.size();

#ifdef ARDUINO
    if (sensorCount > kMaxVisibleSensorRows) {
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

    UiState currentUiState;
    UiState previousUiState;
    currentUiState.sensors.resize(sensorCount);
    previousUiState.sensors.resize(sensorCount);
    currentUiState.xiaomiSensors.resize(xiaomiSensorCount);
    previousUiState.xiaomiSensors.resize(xiaomiSensorCount);
    bool hasPreviousUiState = false;

    switchbot::Scanner scanner(config.switchbot);
    xiaomi::Scanner xiaomiScanner(config.xiaomi);

    std::atomic<bool> switchbotUpdatePending{false};
    scanner.setUpdateCallback([&]() {
        switchbotUpdatePending.store(true);
    });

    std::atomic<bool> xiaomiUpdatePending{false};
    xiaomiScanner.setUpdateCallback([&]() {
        xiaomiUpdatePending.store(true);
    });

    ble::Scanner bleScanner([&](const ble::AdvertisementEvent& event) {
        scanner.handleAdvertisement(event);
        xiaomiScanner.handleAdvertisement(event);
    });
    bleScanner.start();

#ifdef ARDUINO
    initDisplay();
#endif

    while (true) {
        const std::time_t now = std::time(nullptr);

        previousUiState = currentUiState;
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
                updateSalahState(config, localTime, oldDay, today, tomorrow, currentUiState);
                markSalahUpdated(now2, timing);
            }
        }

        if (switchbotUpdatePending.exchange(false) || areSensorsDue(now, timing)) {
            currentUiState.sensors.assign(sensorCount, SensorRowState{});
            updateSensorState(config, now, scanner, currentUiState);
            markSensorsUpdated(now, timing);
        }

        if (xiaomiUpdatePending.exchange(false) || areXiaomiDue(now, timing)) {
            currentUiState.xiaomiSensors.assign(xiaomiSensorCount, XiaomiRowState{});
            updateXiaomiState(config, now, xiaomiScanner, currentUiState);

            if (allXiaomiRowsComplete(currentUiState)) {
                markXiaomiUpdated(now, config, timing);
            } else {
                // TODO: change this retry from 1 minute to 5 minutes after debugging.
                timing.nextXiaomiDueEpochS = now + 60;
            }
        }

        if (isForecastDue(now, timing)) {
            if (updateForecastState(config, currentUiState)) {
                lastForecastData = currentUiState.forecast;
                hasLastForecastData = currentUiState.hasForecast;
                markForecastUpdatedSuccess(now, config, timing);
            } else {
                if (hasLastForecastData) {
                    currentUiState.forecast = lastForecastData;
                    currentUiState.hasForecast = true;
                }
                markForecastUpdatedFailure(now, timing);
            }
        } else if (hasLastForecastData) {
            currentUiState.forecast = lastForecastData;
            currentUiState.hasForecast = true;
        }

        DirtyRegions dirty;
        [[maybe_unused]] bool doFullDraw = false;

        if (!hasPreviousUiState) {
            dirty.salahName = true;
            dirty.minutes = true;
            dirty.sensorsAny = true;
            dirty.sensorRows.assign(sensorCount, true);
            dirty.forecast = true;
            hasPreviousUiState = true;
            doFullDraw = true;
        } else {
            dirty = computeDirtyRegions(previousUiState, currentUiState);
        }

        platform::printLine("Dirty:");
        platform::printLine(std::string("  salahName=") + (dirty.salahName ? "yes" : "no"));
        platform::printLine(std::string("  minutes=") + (dirty.minutes ? "yes" : "no"));
        platform::printLine(std::string("  sensorsAny=") + (dirty.sensorsAny ? "yes" : "no"));
        for (std::size_t i = 0; i < sensorCount; ++i) {
            platform::printLine(
                "  sensor[" + std::to_string(i) + "]=" + (dirty.sensorRows[i] ? "yes" : "no")
            );
        }
        platform::printLine("");

#ifdef ARDUINO
        if (doFullDraw) {
            drawAllRegions(currentUiState);
        } else {
            if (dirty.salahName) {
                drawSalahNameRegion(currentUiState);
                updateSalahNameRegion();
            }

            if (dirty.minutes) {
                drawMinutesRegion(currentUiState);
                updateMinutesRegion();
            }

            const int visibleRows = std::min<int>(
                static_cast<int>(sensorCount),
                kMaxVisibleSensorRows
            );
            for (int rowIndex = 0; rowIndex < visibleRows; ++rowIndex) {
                if (dirty.sensorRows[rowIndex]) {
                    drawSensorRowRegion(currentUiState, rowIndex);
                    updateSensorRowRegion(rowIndex);
                }
            }

            if (dirty.forecast) {
                drawForecastRegion(currentUiState);
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
