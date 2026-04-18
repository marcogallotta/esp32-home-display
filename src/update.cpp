#include "update.h"

#include <exception>
#include <string>
#include <tuple>

#include "forecast/network.h"
#include "forecast/openmeteo.h"
#include "platform.h"
#include "salah/service.h"
#include "salah/state.h"

void updateSalahState(
    const Config& config,
    const std::tm& localTime,
    int& oldDay,
    salah::Schedule& today,
    salah::Schedule& tomorrow,
    UiState& uiState
) {
    if (localTime.tm_mday != oldDay) {
        salah::Schedule newToday;
        salah::Schedule newTomorrow;
        if (!salah::computeSchedules(localTime, config, newToday, newTomorrow)) {
            platform::printLine("Error computing salah schedules");
            return;
        }
        today = newToday;
        tomorrow = newTomorrow;
        oldDay = localTime.tm_mday;
    }

    uiState.salah =
        salah::computeState(today, tomorrow, salah::minutesSinceMidnight(localTime));
    uiState.hasSalah = true;

    platform::printLine(std::string("Current: ") + toString(uiState.salah.current));
    platform::printLine(std::string("Next: ") + toString(uiState.salah.next));
    platform::printLine(
        "Remaining: " + std::to_string(uiState.salah.minutesRemaining) + " min"
    );
    platform::printLine("");
}

void updateSensorState(
    const Config& config,
    const std::time_t now,
    switchbot::Scanner& scanner,
    UiState& uiState
) {
    try {
        scanner.poll();
        const auto sensors = scanner.snapshot();

        for (std::size_t i = 0; i < uiState.sensors.size(); ++i) {
            const auto& sensorConfig = config.switchbot.sensors[i];
            SensorRowState& row = uiState.sensors[i];
            row.name = sensorConfig.name;
            row.shortName = sensorConfig.shortName.empty() ? '?' : sensorConfig.shortName[0];

            const auto it = sensors.find(sensorConfig.mac);
            if (it == sensors.end()) {
                continue;
            }

            const auto& reading = it->second;
            row.hasReading = true;
            row.name = reading.name;
            row.shortName = reading.shortName.empty() ? '?' : reading.shortName[0];
            row.temperatureC = reading.temperature_c;
            row.humidity = reading.humidity;
            row.lastSeenEpochS = reading.last_seen_epoch_s;
            row.rssi = reading.rssi;
        }

        platform::printLine("Sensors:");
        for (std::size_t i = 0; i < uiState.sensors.size(); ++i) {
            const auto& sensorConfig = config.switchbot.sensors[i];
            const SensorRowState& row = uiState.sensors[i];

            if (!row.hasReading) {
#ifdef ARDUINO
                platform::printLine(sensorConfig.shortName + ": no reading yet");
#else
                platform::printLine(sensorConfig.name + ": no reading yet");
#endif
                continue;
            }

#ifdef ARDUINO
            const std::string label(1, row.shortName);
#else
            const std::string label = row.name;
#endif

            platform::printLine(
                label + ": " +
                std::to_string(row.temperatureC) + "C, " +
                std::to_string(static_cast<int>(row.humidity)) + "%, " +
                std::to_string((now - row.lastSeenEpochS) / 60) + "m, " +
                "rssi=" + std::to_string(row.rssi)
            );
        }
        platform::printLine("");
    } catch (const std::exception& e) {
        platform::printLine(std::string("Switchbot module error: ") + e.what());
    } catch (...) {
        platform::printLine("Switchbot module error: unknown");
    }
}

bool updateForecastState(const Config& config, UiState& uiState) {
    try {
        auto& p = forecast::platform(config.wifi);
        const std::string url = forecast::openmeteoUrl(config.location);
        const auto r = p.httpGet(url, config.forecast.openmeteoPem);
        if (r.status_code == 200) {
            forecast::ForecastData data;
            if (forecast::parseForecastJson(r.body, data)) {
                uiState.forecast = data;
                uiState.hasForecast = true;

                for (int i = 0; i < data.count; ++i) {
                    platform::printLine(
                        data.days[i].date +
                        " code=" + std::to_string(data.days[i].weatherCode) +
                        " max=" + std::to_string(data.days[i].tempMax) +
                        " min=" + std::to_string(data.days[i].tempMin) +
                        " rain=" + std::to_string(data.days[i].rainProbMax)
                    );
                }
                platform::printLine("");
                return true;
            }
        } else {
            p.log("HTTP failed: " + r.error);
        }
    } catch (const std::exception& e) {
        platform::printLine(std::string("Forecast module error: ") + e.what());
    } catch (...) {
        platform::printLine("Forecast module error: unknown");
    }

    return false;
}
