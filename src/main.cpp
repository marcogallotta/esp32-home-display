#ifdef ARDUINO
#include <Arduino.h>
#include "pqueue_doctor_session.h"
#endif

#include <algorithm>
#include <ctime>
#include <cstdint>
#include <string>
#include <utility>

#include "api/outbox_client.h"
#include "api/state.h"
#include "api_sync.h"
#include "ble/event_queue.h"
#include "ble/scanner.h"
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
#include "switchbot/ble.h"
#ifdef ARDUINO
#include "switchbot/history_service.h"
#endif
#include "timing.h"
#include "ui/display.h"
#include "ui/state.h"
#include "update.h"
#include "xiaomi/ble.h"

namespace {

constexpr std::uint64_t kInvalidTimeApiSyncWarnAfterMs = 30ULL * 1000ULL;
constexpr std::uint64_t kInvalidTimeApiSyncErrorAfterMs = 120ULL * 1000ULL;
constexpr std::uint64_t kInvalidTimeApiSyncWarnRepeatMs = 60ULL * 1000ULL;
constexpr std::uint64_t kInvalidTimeApiSyncErrorRepeatMs = 5ULL * 60ULL * 1000ULL;

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

#ifdef ARDUINO
    switchbot::history::HistoryServiceState historyServiceState;
#endif

    State currentState;
    State previousState;

    api::State apiState;
    api::OutboxClient apiOutboxClient;
    bool pqueueMoreCompactionLikely = false;

    UiState currentUiState;
    bool hasPreviousState = false;

    switchbot::Scanner switchbotScanner;
    xiaomi::Scanner xiaomiScanner;
    ble::EventQueue bleEventQueue;
    ble::Scanner bleScanner;

    explicit AppContext(const Config& cfg)
        : config(cfg),
          apiOutboxClient(config),
          switchbotScanner(config.switchbot),
          xiaomiScanner(config.xiaomi),
          bleScanner(bleEventQueue) {
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

    logLine(LogLevel::Info, "API uses pqueue HTTP outbox");
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

void updateSwitchbotIfDue(AppContext& app, std::time_t now, bool newData) {
    if (!newData && !areSensorsDue(now, app.timing)) {
        return;
    }

    updateSwitchbotState(app.config, now, app.switchbotScanner, app.currentState);
    markSensorsUpdated(now, app.timing);
}

void updateXiaomiIfDue(AppContext& app, std::time_t now, bool newData) {
    if (!newData && !areXiaomiDue(now, app.timing)) {
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

void updateDomainState(AppContext& app, std::time_t now, bool switchbotUpdated, bool xiaomiUpdated) {
    updateSalahIfDue(app, now);
    updateSwitchbotIfDue(app, now, switchbotUpdated);
    updateXiaomiIfDue(app, now, xiaomiUpdated);
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


void logInvalidTimeApiSyncSkipped(std::uint64_t nowMs) {
    if (nowMs >= kInvalidTimeApiSyncErrorAfterMs) {
        rateLimitedLog(
            LogLevel::Error,
            "api_time_not_initialized",
            "API sync disabled: time still not initialized after 120 seconds",
            kInvalidTimeApiSyncErrorRepeatMs
        );
        return;
    }

    if (nowMs >= kInvalidTimeApiSyncWarnAfterMs) {
        rateLimitedLog(
            LogLevel::Warn,
            "api_time_not_initialized",
            "API sync skipped: time not initialized yet",
            kInvalidTimeApiSyncWarnRepeatMs
        );
    }
}

void syncOutputs(AppContext& app, std::time_t now) {
    (void)now;

    const std::uint64_t nowMs = platform::millis();

    if (!app.hasValidTime && platform::hasValidTime()) {
        app.hasValidTime = true;
    }

    const auto drain = app.apiOutboxClient.drainPending(nowMs);

    const bool followup = app.pqueueMoreCompactionLikely;
    const bool drained = drain.removedQueuedBytes > 0;

    if (app.config.api.outbox.idleCompactSteps > 0 && (drained || followup)) {
        const size_t maxSteps = static_cast<size_t>(app.config.api.outbox.idleCompactSteps);

        size_t steps = maxSteps;
        if (!followup) {
            const size_t bytes = static_cast<size_t>(drain.removedQueuedBytes);
            const size_t perStep = static_cast<size_t>(app.config.api.outbox.compactBytesPerStep);
            steps = std::max<size_t>(1, (bytes + perStep - 1) / perStep);
            steps = std::min(maxSteps, steps);
        }

        const auto cr = app.apiOutboxClient.compactIdle(steps);
        app.pqueueMoreCompactionLikely = cr.status.ok() && cr.moreWorkLikely;

        if (cr.compactions > 0 || !cr.status.ok()) {
            std::string msg = "pqueue idle compaction:"
                " budgetSteps=" + std::to_string(steps) +
                " steps=" + std::to_string(cr.stepsRun) +
                " compactions=" + std::to_string(cr.compactions) +
                " noOps=" + std::to_string(cr.noOps) +
                " removedBytes=" + std::to_string(drain.removedQueuedBytes) +
                " reclaimed=" + std::to_string(cr.bytesReclaimed) +
                " deadBefore=" + std::to_string(cr.deadBytesBefore) +
                " deadRemaining=" + std::to_string(cr.remainingDeadBytes) +
                " inSegs=" + std::to_string(cr.inputSegments) +
                " outSegs=" + std::to_string(cr.outputSegments);
            if (!cr.status.ok()) {
                msg += " error=";
                msg += pqueue::statusCodeName(cr.status.code);
                if (cr.status.backendCode != 0) {
                    msg += " backend=";
                    msg += std::to_string(cr.status.backendCode);
                }
            }
            if (cr.moreWorkLikely) {
                msg += " moreWork=1";
            }
            logLine(LogLevel::Info, msg);
        }
    }

    if (app.hasValidTime) {
        syncApiState(app.config, app.currentState, app.apiState, app.apiOutboxClient);
    } else {
        logInvalidTimeApiSyncSkipped(nowMs);
    }

    bool doFullDraw = false;
    updateUiDirtyState(app, doFullDraw);
    logDirtyRegions(app);
    renderUi(app.currentState, app.currentUiState, doFullDraw);
}

#ifdef ARDUINO
// Accumulates Serial bytes and returns true when "PQUEUE_DOCTOR\n" is seen.
bool checkDoctorTrigger() {
    static std::string sBuf;
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') continue;
        if (c == '\n') {
            const bool hit = (sBuf == "PQUEUE_DOCTOR");
            sBuf.clear();
            if (hit) return true;
        } else {
            sBuf += c;
            if (sBuf.size() > 32) sBuf.clear();
        }
    }
    return false;
}

void enterDoctorMode() {
    platform::setDoctorMode(true);
    pqueue::doctor::runSession();
    delay(100);
    ESP.restart();
}
#endif

void sleepUntilNextDue(AppContext& app) {
    const int totalMs = computeSleepMs(std::time(nullptr), app.timing);
    logLine(LogLevel::Debug, "Next update check in " + std::to_string(totalMs / 1000) + " seconds");
#ifdef ARDUINO
    int remaining = totalMs;
    while (remaining > 0) {
        const int step = std::min(remaining, 100);
        platform::delayMs(step);
        remaining -= step;
        if (checkDoctorTrigger()) enterDoctorMode();
    }
#else
    platform::delayMs(totalMs);
#endif
}

void logHeapStats(const char* label, const platform::HeapStats& stats) {
#ifdef ARDUINO
    const int fragPct = stats.freeBytes > 0
        ? static_cast<int>(100 - (stats.largestFreeBlock * 100 / stats.freeBytes))
        : 0;
    logLine(LogLevel::Debug,
        std::string("heap ") + label +
        ": free=" + std::to_string(stats.freeBytes) +
        " largest=" + std::to_string(stats.largestFreeBlock) +
        " frag=" + std::to_string(fragPct) + "%"
    );
#else
    (void)label;
    (void)stats;
#endif
}

void tick(AppContext& app) {
    const std::time_t now = std::time(nullptr);

    std::swap(app.previousState, app.currentState);
    prepareCurrentState(app.currentState, app.previousState);

    const platform::HeapStats heapPreBle = platform::heapStats();
    app.bleScanner.poll();

    bool switchbotUpdated = false;
    bool xiaomiUpdated = false;
    int bleEventsProcessed = 0;
    ble::AdvertisementEvent event;
    while (app.bleEventQueue.pop(event)) {
        if (app.switchbotScanner.handleAdvertisement(event)) switchbotUpdated = true;
        if (app.xiaomiScanner.handleAdvertisement(event)) xiaomiUpdated = true;
        ++bleEventsProcessed;
    }

    const platform::HeapStats heapPostBle = platform::heapStats();
    logHeapStats("pre-BLE", heapPreBle);
    logHeapStats("post-BLE", heapPostBle);
#ifdef ARDUINO
    if (bleEventsProcessed > 0) {
        logLine(LogLevel::Debug,
            "BLE drained " + std::to_string(bleEventsProcessed) +
            " events, largest-block delta=" +
            std::to_string(static_cast<int>(heapPostBle.largestFreeBlock) -
                           static_cast<int>(heapPreBle.largestFreeBlock))
        );
    }
#endif

    updateDomainState(app, now, switchbotUpdated, xiaomiUpdated);
#ifdef ARDUINO
    switchbot::history::maybeRunStartupHistorySync(
        app.config,
        app.bleScanner,
        app.hasValidTime,
        app.historyServiceState
    );
#endif
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
