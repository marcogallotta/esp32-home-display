#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <algorithm>
#include <atomic>
#include <ctime>
#include <cstdint>
#include <string>
#include <utility>

#include "api/buffered_client.h"
#include "api/client.h"
#include "api/request_file_store.h"
#include "api/state.h"
#include "api_sync.h"
#include "ble/scanner.h"
#include "config.h"
#include "forecast/openmeteo.h"
#include "log.h"
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

    api::State apiState;
    api::Client apiClient;
    api::BufferedClient bufferedApiClient;

    UiState currentUiState;
    bool hasPreviousState = false;

    switchbot::Scanner switchbotScanner;
    xiaomi::Scanner xiaomiScanner;
    ble::Scanner bleScanner;

    std::atomic<bool> switchbotUpdatePending{false};
    std::atomic<bool> xiaomiUpdatePending{false};

    explicit AppContext(const Config& cfg)
        : config(cfg),
          apiClient(config),
          bufferedApiClient(config, apiState.buffer, apiClient, api::request_file_store::defaultStore()),
          switchbotScanner(config.switchbot),
          xiaomiScanner(config.xiaomi),
          bleScanner([this](const ble::AdvertisementEvent& event) {
              switchbotScanner.handleAdvertisement(event);
              xiaomiScanner.handleAdvertisement(event);
          }) {
    }
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
    const std::size_t xiaomiSensorCount = app.config.xiaomi.sensors.size();

    app.currentState.switchbotSensors.resize(switchbotSensorCount);
    app.previousState.switchbotSensors.resize(switchbotSensorCount);
    app.currentState.xiaomiSensors.resize(xiaomiSensorCount);
    app.previousState.xiaomiSensors.resize(xiaomiSensorCount);

    api::initState(app.currentState, app.apiState);
}

void initCallbacks(AppContext& app) {
    app.switchbotScanner.setUpdateCallback([&]() {
        app.switchbotUpdatePending.store(true);
    });

    app.xiaomiScanner.setUpdateCallback([&]() {
        app.xiaomiUpdatePending.store(true);
    });
}

void initPlatform(AppContext& app) {
    app.hasValidTime = platform::initTime(app.config);
    app.bleScanner.start();

#ifdef ARDUINO
    initDisplay();
#endif
}

bool initApp(AppContext& app) {
    if (!validateConfig(app.config)) {
        return false;
    }

    initStateStorage(app);
    initCallbacks(app);
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

    if (current.xiaomiSensors.size() != previous.xiaomiSensors.size()) {
        current.xiaomiSensors.resize(previous.xiaomiSensors.size());
    }
    for (std::size_t i = 0; i < previous.xiaomiSensors.size(); ++i) {
        current.xiaomiSensors[i].identity = previous.xiaomiSensors[i].identity;
        current.xiaomiSensors[i].reading = previous.xiaomiSensors[i].reading;
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
    const std::tm localTime = *std::localtime(&now2);
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
    if (!app.switchbotUpdatePending.exchange(false) && !areSensorsDue(now, app.timing)) {
        return;
    }

    updateSwitchbotState(app.config, now, app.switchbotScanner, app.currentState);
    markSensorsUpdated(now, app.timing);
}

void updateXiaomiIfDue(AppContext& app, std::time_t now) {
    if (!app.xiaomiUpdatePending.exchange(false) && !areXiaomiDue(now, app.timing)) {
        return;
    }

    updateXiaomiState(app.config, now, app.xiaomiScanner, app.currentState);

    if (allXiaomiRowsComplete(app.currentState)) {
        markXiaomiUpdated(now, app.config, app.timing);
    } else {
        app.timing.nextXiaomiDueEpochS = now + 60;
        logLine(LogLevel::Debug, "Xiaomi reading incomplete; retrying in 60 seconds");
    }
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
    updateXiaomiIfDue(app, now);
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

void renderUi(const AppContext& app, bool doFullDraw) {
#ifdef ARDUINO
    if (doFullDraw) {
        drawAllRegions(app.currentState);
        return;
    }

    if (app.currentUiState.dirty.salahName) {
        drawSalahNameRegion(app.currentState);
        updateSalahNameRegion();
    }

    if (app.currentUiState.dirty.minutes) {
        drawMinutesRegion(app.currentState);
        updateMinutesRegion();
    }

    const int visibleRows = std::min<int>(
        static_cast<int>(app.currentState.switchbotSensors.size()),
        kMaxVisibleSensorRows
    );

    for (int rowIndex = 0; rowIndex < visibleRows; ++rowIndex) {
        if (app.currentUiState.dirty.sensorRows[rowIndex]) {
            drawSensorRowRegion(app.currentState, rowIndex);
            updateSensorRowRegion(rowIndex);
        }
    }

    if (app.currentUiState.dirty.forecast) {
        drawForecastRegion(app.currentState);
        updateForecastRegion();
    }
#else
    (void)app;
    (void)doFullDraw;
#endif
}

void syncOutputs(AppContext& app, std::time_t now) {
    syncApiState(app.currentState, app.apiState, app.bufferedApiClient);
    api::maybeDrainBuffer(
        app.apiState.buffer,
        now,
        app.config.api.buffer,
        app.apiClient,
        api::request_file_store::defaultStore()
    );

    bool doFullDraw = false;
    updateUiDirtyState(app, doFullDraw);
    logDirtyRegions(app);
    renderUi(app, doFullDraw);
}

void sleepUntilNextDue(AppContext& app) {
    const int delayMs = computeSleepMs(std::time(nullptr), app.timing);
    logLine(LogLevel::Debug, "Next update check in " + std::to_string(delayMs / 1000) + " seconds");
    platform::delayMs(delayMs);
}

void tick(AppContext& app) {
    const std::time_t now = std::time(nullptr);

    std::swap(app.previousState, app.currentState);
    prepareCurrentState(app.currentState, app.previousState);

    app.bleScanner.poll();
    updateDomainState(app, now);
    syncOutputs(app, now);
    sleepUntilNextDue(app);
}

} // namespace

void run() {
    Config tmpConfig;
    if (!loadConfig(tmpConfig)) {
        logLine(LogLevel::Error, "Failed to load config");
        return;
    }

    AppContext app(tmpConfig);
    if (!initApp(app)) {
        return;
    }

    while (true) {
        tick(app);
    }
}

#ifdef ARDUINO
void setup() {
    Serial.begin(115200);
    run();
}

void loop() {
}
#else
int main() {
    run();
    return 0;
}
#endif
