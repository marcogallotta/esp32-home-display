#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <ctime>
#include <cstdint>
#include <string>
#include <utility>

#include "config.h"
#include "forecast/openmeteo.h"
#include "log.h"
#ifdef ARDUINO
#include "file_log.h"
#endif
#include "network.h"
#include "platform.h"
#include "salah/types.h"
#include "state.h"
#include "timing.h"
#include "ui/display.h"
#include "ui/state.h"
#include "update.h"

namespace {

#ifdef ARDUINO
constexpr int kMaxVisibleSensorRows = 4;
#endif

struct AppContext {
    Config config;

    int oldDay = -1;
    salah::Schedule today;
    salah::Schedule tomorrow;

    forecast::ForecastData lastForecastData;
    bool hasLastForecastData = false;

    TimingState timing;
    bool hasValidTime = false;

    State currentState;
    State previousState;

    UiState currentUiState;
    bool hasPreviousState = false;

    explicit AppContext(const Config& cfg) : config(cfg) {}
};

bool validateConfig([[maybe_unused]] const Config& config) {
#ifdef ARDUINO
    if (config.switchbot.sensors.size() > kMaxVisibleSensorRows) {
        logLine(LogLevel::Error, "Config error: OLED supports at most 4 SwitchBot sensor rows");
        return false;
    }
#endif
    return true;
}

void initStateStorage(AppContext& app) {
    const std::size_t switchbotSensorCount = app.config.switchbot.sensors.size();

    app.currentState.switchbotSensors.resize(switchbotSensorCount);
    app.previousState.switchbotSensors.resize(switchbotSensorCount);
}

void initPlatform(AppContext& app) {
    app.hasValidTime = platform::initTime(app.config);

#ifdef ARDUINO
    initDisplay();
#endif
}

bool initApp(AppContext& app) {
    if (!validateConfig(app.config)) {
        return false;
    }

    initStateStorage(app);
    initPlatform(app);
    return true;
}

void prepareCurrentState(State& current, const State& previous) {
    current.hasSalah = previous.hasSalah;
    current.salah = previous.salah;

    current.hasForecast = previous.hasForecast;
    current.forecast = previous.forecast;

    if (current.switchbotSensors.size() != previous.switchbotSensors.size()) {
        current.switchbotSensors.resize(previous.switchbotSensors.size());
    }
    for (std::size_t i = 0; i < previous.switchbotSensors.size(); ++i) {
        current.switchbotSensors[i].identity = previous.switchbotSensors[i].identity;
        current.switchbotSensors[i].reading = previous.switchbotSensors[i].reading;
    }
}

void updateSalahIfDue(AppContext& app, std::time_t now) {
    if (!isSalahDue(now, app.timing)) {
        return;
    }

    if (!app.hasValidTime) {
        app.hasValidTime = platform::initTime(app.config);
        if (!app.hasValidTime) {
            logLine(LogLevel::Warn, "Time sync failed");
            markSalahUpdated(now, app.timing);
            return;
        }
        app.oldDay = -1;
    }

    const std::time_t now2 = std::time(nullptr);
    std::tm localTime;
    localtime_r(&now2, &localTime);
    updateSalahState(
        app.config,
        localTime,
        app.oldDay,
        app.today,
        app.tomorrow,
        app.currentState
    );
    markSalahUpdated(now2, app.timing);
}

void updateSwitchbotIfDue(AppContext& app, std::time_t now) {
    if (!areSensorsDue(now, app.timing)) {
        return;
    }

    // Advance timer regardless of success/failure to prevent hammering on error.
    // A failed fetch leaves the previous reading in state (stale but visible).
    updateSwitchbotFromBackend(app.config, now, app.currentState);
    markSensorsUpdated(now, app.timing);
}

void updateForecastIfDue(AppContext& app, std::time_t now) {
    if (isForecastDue(now, app.timing)) {
        if (updateForecastState(app.config, app.currentState)) {
            app.lastForecastData = app.currentState.forecast;
            app.hasLastForecastData = app.currentState.hasForecast;
            markForecastUpdatedSuccess(now, app.config, app.timing);
        } else {
            if (app.hasLastForecastData) {
                app.currentState.forecast = app.lastForecastData;
                app.currentState.hasForecast = true;
                logLine(LogLevel::Info, "Using cached forecast");
            }
            markForecastUpdatedFailure(now, app.timing);
        }
        return;
    }

    if (app.hasLastForecastData) {
        app.currentState.forecast = app.lastForecastData;
        app.currentState.hasForecast = true;
    }
}

void updateDomainState(AppContext& app, std::time_t now) {
    updateSalahIfDue(app, now);
    updateSwitchbotIfDue(app, now);
    updateForecastIfDue(app, now);
}

void updateUiDirtyState(AppContext& app, bool& doFullDraw) {
    doFullDraw = false;

    if (!app.hasPreviousState) {
        app.currentUiState.dirty.salahName = true;
        app.currentUiState.dirty.minutes = true;
        app.currentUiState.dirty.sensorsAny = true;
        app.currentUiState.dirty.sensorRows.assign(app.currentState.switchbotSensors.size(), true);
        app.currentUiState.dirty.forecast = true;
        app.hasPreviousState = true;
        doFullDraw = true;
        return;
    }

    app.currentUiState.dirty = computeDirtyRegions(app.previousState, app.currentState);
}

std::string dirtyParts(const DirtyRegions& dirty) {
    std::string out;
    bool first = true;

    auto append = [&](const char* part) {
        if (!first) {
            out += ", ";
        }
        out += part;
        first = false;
    };

    if (dirty.salahName) {
        append("salah");
    }
    if (dirty.minutes) {
        append("minutes");
    }
    if (dirty.sensorsAny) {
        append("sensors");
    }
    if (dirty.forecast) {
        append("forecast");
    }

    return out.empty() ? "none" : out;
}

std::string dirtyRowList(const DirtyRegions& dirty) {
    std::string out;
    bool first = true;

    for (std::size_t i = 0; i < dirty.sensorRows.size(); ++i) {
        if (!dirty.sensorRows[i]) {
            continue;
        }

        if (!first) {
            out += ",";
        }
        out += std::to_string(i);
        first = false;
    }

    return out.empty() ? "none" : out;
}

void logDirtyRegions(const AppContext& app) {
    const DirtyRegions& dirty = app.currentUiState.dirty;

    logLine(
        LogLevel::Debug,
        "UI update: [" + dirtyParts(dirty) + "], rows=" + dirtyRowList(dirty)
    );
}


void syncOutputs(AppContext& app) {
    bool doFullDraw = false;
    updateUiDirtyState(app, doFullDraw);
    logDirtyRegions(app);
    renderUi(app.currentState, app.currentUiState, doFullDraw);
}

void sleepUntilNextDue(AppContext& app) {
    const int totalMs = computeSleepMs(std::time(nullptr), app.timing);
    logLine(LogLevel::Debug, "Next update check in " + std::to_string(totalMs / 1000) + " seconds");
    platform::delayMs(totalMs);
}

void tick(AppContext& app) {
#ifdef ARDUINO
    network::platform(app.config.wifi).kickConnect();
#endif
    const std::time_t now = std::time(nullptr);

    std::swap(app.previousState, app.currentState);
    prepareCurrentState(app.currentState, app.previousState);

    updateDomainState(app, now);
    syncOutputs(app);
    sleepUntilNextDue(app);
}

} // namespace

void run() {
    Config tmpConfig;
    if (!loadConfig(tmpConfig)) {
        logLine(LogLevel::Error, "Failed to load config");
        return;
    }

#ifdef ARDUINO
    if (!initFileLogging()) {
        logLine(LogLevel::Warn, "File logging unavailable");
    }
#endif

    AppContext app(tmpConfig);
    if (!initApp(app)) {
        return;
    }

    while (true) {
        tick(app);
    }
}

#ifdef ARDUINO
// Override weak symbol from framework main.cpp — mbedTLS SSL handshake needs ~8KB alone.
size_t getArduinoLoopTaskStackSize() { return 16384; }

void setup() {
    Serial.begin(115200);
    run();
}

void loop() {
}
#else
int main() {
    network::initCurl();
    run();
    network::cleanupCurl();
    return 0;
}
#endif
