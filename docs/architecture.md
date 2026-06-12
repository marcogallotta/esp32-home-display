# Firmware Architecture

**Editing rules:** ASCII only -- no Unicode symbols. This file is a living reference; delete completed items, rewrite state sections, never mark things done inline.

Note: SwitchBot BLE scanning, history protocol, history sync, the pqueue outbox, and the
API sync layer have been removed. The firmware now polls the backend at GET /sensors/latest
for current SwitchBot readings. The removed code remains in git history (before the removal
commit) if it is ever needed as a reference.

**Before editing any code, read these files in order:**
1. `src/main.cpp`
2. `src/config.h` and `src/config.cpp`
3. `src/state.h` and `src/sensor_readings.h`
4. `src/update.cpp`
5. Root `Makefile` -- desktop sources are explicitly listed and must be updated

**Server files are in the backend repo, not this firmware repo.** The `server/` section
below describes the server-side contract for reference only. Backend behavior is documented
here only to keep firmware requests aligned; the source of truth is `server/docs/architecture.md`.

---

## Data flow overview

```
tick()
  -> updateSwitchbotIfDue()    (every 5 minutes)
     -> updateSwitchbotFromBackend()   [update.cpp]
     -> GET /sensors/meter/latest  (backend)
     -> parse JSON response
     -> State::switchbotSensors[]      [state.h]
  -> updateSalahIfDue()
  -> updateForecastIfDue()
  -> syncOutputs()
     -> computeDirtyRegions()
     -> renderUi()
```

---

## Firmware layer

### Config (config.h)

Each module gets a config struct. SwitchBot has `SwitchbotConfig` (with a `sensors` vector
of `SwitchbotSensorConfig { mac, name, shortName }`). The top-level `Config` struct holds
one instance of each module config. `config.json` is the source; `parseConfigText()`
(in `config.cpp`) deserialises it.

The `api` section of config still exists and is required. `api.base_url`, `api.api_key`,
and `api.pem_file` are used by `updateSwitchbotFromBackend()` to authenticate the backend
fetch. C++ struct fields use camelCase (`shortName`); the corresponding JSON keys use
snake_case (`short_name`).

The `StaticJsonDocument<4096>` size is shared across the entire config. Adding a device
with a large sensor list may require increasing this constant.

### State (state.h, sensor_readings.h)

`State` holds one `vector<FooSensorState>` per device type. Each element is:

```cpp
struct FooSensorState {
    SensorIdentity identity;   // mac, name, shortName
    FooReading reading;        // all fields optional
};
```

`FooReading` (defined in `sensor_readings.h`) uses `std::optional` for every measurement.
`lastSeenEpochS` is `optional<int64_t>` and is null until the device is seen after time is valid.

### State update (update.cpp)

`updateSwitchbotFromBackend(config, now, state)` fires when `areSensorsDue()` is true
(every 5 minutes). It:

1. Issues `GET /sensors/meter/latest` with the API key header.
2. Parses the JSON response into a MAC -> reading map.
3. Iterates `state.switchbotSensors` by index (parallel to `config.switchbot.sensors`),
   copies fields from the map into each row. If the MAC is absent from the response, the
   reading is cleared to an empty `SwitchbotReading{}`.
4. Logs a summary line.

The timer advances regardless of success or failure to prevent hammering on error. A failed
fetch leaves the previous reading in state (stale but visible).

### Timing (timing.h)

`TimingState` holds the per-module timers. `areSensorsDue()` / `markSensorsUpdated()`
drive the SwitchBot poll interval. `computeSleepMs()` returns the minimum time until any
timer fires, which is how long `tick()` sleeps before the next iteration.

---

## Config JSON parsing (src/config.cpp)

`parseConfigText()` uses ArduinoJson (`StaticJsonDocument<4096>`). Preferred pattern: read
and validate into locals, then assign to `config.*` only after all validation passes.

`normalizeMac()` is a file-scope helper that strips separators, uppercases, and checks for
exactly 12 hex digits. Always run MAC strings through it.

---

## Display / UI layer (src/ui/)

The display is an OLED (SSD1309 128x64, driven via U8G2). It only shows SwitchBot sensors
(up to 4 rows, enforced by `kMaxVisibleSensorRows` in `main.cpp`). The dirty-region system
(`DirtyRegions` in `ui/state.h`) tracks per-row changes for SwitchBot only -- `sensorRows`
is sized to `switchbotSensors.size()`.

`computeDirtyRegions()` in `ui/state.cpp` compares `previous` and `current` state. Changed
regions are redrawn; unchanged regions are skipped. `equalsForDisplay()` on `SwitchbotReading`
is what dirty tracking compares.

---

## Server layer contract

### Backend sensor endpoint

`GET /sensors/meter/latest` (requires `x-api-key` header) returns:

```json
{
  "sensors": [
    {
      "id": "south",
      "mac": "AA:BB:CC:DD:EE:FF",
      "name": "South",
      "type": "meter",
      "recorded_at": "2024-01-01T12:00:00+00:00",
      "temperature_c": 21.5,
      "humidity_pct": 55,
      "stale": false
    }
  ],
  "retry_after_secs": 900
}
```

The firmware matches entries by MAC against `config.switchbot.sensors`. Unmatched MACs are
ignored. `recorded_at` is parsed as UTC ISO 8601 and stored as `lastSeenEpochS`.

---

## Tests

### Desktop unit tests (tests/)

One `.cpp` file per module, built with `make -j12` (POSIX backend, no real hardware). Each
file uses doctest. The naming convention mirrors the source module:

| Source module | Test file |
|---|---|
| `src/config.cpp` | `tests/config.cpp` |
| `src/forecast/openmeteo.cpp` | `tests/forecast_openmeteo.cpp` |

### Device firmware tests (test/)

`test/` contains Arduino/Unity tests that run on real hardware via PlatformIO. A device
needs a firmware test here only if it has hardware-specific behaviour that cannot be
exercised with the POSIX backend.

---

## Key invariants

- Sensor arrays in `State` and `Config.foo.sensors` are always parallel -- same length,
  same index meaning the same physical sensor. Resizing one requires resizing all.
- `lastSeenEpochS = 0` (or null) means "not seen since boot" or "seen before time was
  valid". The display layer can filter on this to avoid showing stale zeros.
- A failed backend fetch leaves the previous reading in state. The timer still advances to
  prevent retry hammering. The display shows stale data until the next successful fetch.
