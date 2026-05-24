# Adding a New Device

**Editing rules:** ASCII only -- no Unicode symbols. This file is a living reference; delete completed items, rewrite state sections, never mark things done inline.

This document covers the data flow from BLE advertisement to server storage, using SwitchBot and Xiaomi as the reference implementations, so a new device type can be wired in without re-reading every source file.

**Before editing any code, read these files in order:**
1. `src/main.cpp`
2. `src/config.h` and `src/config.cpp`
3. `src/state.h` and `src/sensor_readings.h`
4. `src/update.cpp`
5. `src/api/state.cpp`, `src/api_sync.cpp`, `src/api/payloads.cpp`, `src/api/outbox_client.cpp`
6. An existing device pair: `src/switchbot/` or `src/xiaomi/`
7. Root `Makefile` -- desktop sources are explicitly listed and must be updated

**Server files are in the backend repo, not this firmware repo.** The `server/` section below describes the server-side contract for reference only; do not look for `server/app/*.py` in this tree. Backend behavior is documented here only to keep firmware payloads aligned; the source of truth is `server/docs/architecture.md`.

---

## Data flow overview

```
BLE radio
  -> ble::Scanner (arduino.cpp / desktop.cpp)
  -> ble::EventQueue
  -> tick(): bleEventQueue.pop(event)
  -> foo::Scanner::handleAdvertisement(event)   [device-specific]
  -> foo::SensorMap (keyed by MAC)
  -> updateFooState()                           [update.cpp]
  -> State::fooSensors[]                        [state.h]
  -> syncApiState()                             [api_sync.cpp]
  -> api::OutboxClient::postFooReading()        [outbox_client.cpp]
  -> encodeCompact() / toJson()                 [payloads.cpp]
  -> pqueue::http::Outbox::submitCompactPost()  (compact path, SwitchBot + single-field Xiaomi)
  -> pqueue::http::Outbox::submitPost()         (JSON path, multi-field Xiaomi fallback)
  -> [at drain time] expandCompact() -> JSON    [payloads.cpp, via ExpandBodyCallback]
  -> POST /foo/reading  (server)
  -> ingest_reading()   (server/app/service.py)
  -> foo_readings table
```

---

## Firmware layer

### BLE event queue (ble/)

`ble::Scanner` (platform-specific impl in `ble/arduino.cpp` / `ble/desktop.cpp`) receives raw BLE advertisements and pushes them into `ble::EventQueue` as `ble::AdvertisementEvent`:

```cpp
struct AdvertisementEvent {
    std::string address;
    std::map<uint16_t, vector<uint8_t>> manufacturerData;
    std::map<string, vector<uint8_t>> serviceData;
};
```

`tick()` calls `bleScanner.poll()` once per loop, then drains `bleEventQueue` and dispatches each event to every device scanner via `handleAdvertisement(event)`.

### Device scanner (foo/ble.h + foo/ble.cpp)

Each device type owns a `Scanner` class with two public methods:

```cpp
bool handleAdvertisement(const ble::AdvertisementEvent& event);
SensorMap snapshot() const;
```

`handleAdvertisement` returns true if the event matched this device type and updated internal state. `snapshot()` returns a copy of the current `SensorMap` (a `std::map<string, SensorReading>` keyed by MAC address).

The scanner identifies its advertisements by a known key in `manufacturerData` or `serviceData`. SwitchBot uses `manufacturerData[2409]`; Xiaomi uses service data. Both decode via a `protocol.cpp` helper.

Each scanner maintains an internal `sensors` map that persists across ticks. `snapshot()` returns a copy of all known sensors, not just ones seen this tick. When `updateFooState()` finds a MAC absent from the snapshot it means "never seen by the scanner" or "scanner lost it," not "no advert arrived this tick."

The scanner stores `lastSeenEpochS = 0` when `platform::hasValidTime()` is false, so the application layer can filter out readings with no valid timestamp.

### Config (config.h)

Each device type gets a config struct. SwitchBot has `SwitchbotConfig` (with a `sensors` vector of `SwitchbotSensorConfig { mac, name, shortName, addressType }`). Xiaomi has `XiaomiConfig` (with `updateIntervalMinutes` and a `sensors` vector). C++ struct fields use camelCase (`shortName`); the corresponding JSON keys use snake_case (`short_name`).

The top-level `Config` struct holds one instance of each device config. `config.json` is the source; `parseConfigText()` (in `config.cpp`) deserializes it.

### State (state.h, sensor_readings.h)

`State` holds one `vector<FooSensorState>` per device type. Each element is:

```cpp
struct FooSensorState {
    SensorIdentity identity;   // mac, name, shortName
    FooReading reading;        // all fields optional
};
```

`FooReading` (defined in `sensor_readings.h`) uses `std::optional` for every measurement. `lastSeenEpochS` is `optional<int64_t>` and is null until the device is seen after time is valid.

### State update (update.cpp)

`updateFooState(config, now, fooScanner, state)` calls `fooScanner.snapshot()`, iterates `state.fooSensors` by index (parallel to `config.foo.sensors`), looks up each sensor's MAC in the snapshot, and copies fields into the reading. If the MAC is not in the snapshot the reading is cleared to an empty `FooReading{}`.

This function is called from `tick()` when the timer fires (`areFooDue()`). There are two timing models in use; choose one for a new device:

- **Continuous with fallback (SwitchBot):** Update every tick when new BLE data has arrived; `nextSensorsDueEpochS` is a fallback to force a refresh even when no new adverts arrive.
- **Interval with early flush (Xiaomi):** Update on `nextXiaomiDueEpochS` expiry, but also flush early once a complete reading set has been buffered. Use this when a device sends partial measurements across separate advertisements and you want to aggregate them before sending.

### Timing (timing.h)

`TimingState` holds the timers used by each update model (e.g. `nextSensorsDueEpochS` shared by SwitchBot, `nextXiaomiDueEpochS` for Xiaomi). A new device that needs its own interval adds a `nextFooDueEpochS` field. `markFooUpdated()` resets it to `now + intervalSeconds`. `computeSleepMs()` returns the minimum time until any timer fires, which is how long `tick()` sleeps before the next iteration.

---

## API sync layer

### Write policy (api/state.h, api/state.cpp)

`api::State` holds `lastSent` (a `vector<FooReading>`) parallel to `state.fooSensors`. `initState()` initializes these vectors to the same length as the sensor lists.

`shouldSendFoo()` is implemented manually per device type (e.g. `shouldSendSwitchbot()`, `shouldSendXiaomi()`) in `api/state.cpp`. It compares `current` against `lastSent` using two conditions:

1. **Heartbeat:** if `current.lastSeenEpochS - lastSent.lastSeenEpochS >= heartbeatMinutes * 60`, send regardless of value change.
2. **Delta threshold:** if any measured field changed by at least its configured delta (e.g. `temperatureDeltaC = 0.3`, `humidityDeltaPct = 2`), send.

Both conditions are OR'd. The thresholds come from `SensorWritePolicyConfig` in `config.h`, and are the same for all sensors of a given type.

Note: `equalsForApi()` on the reading struct exists and is tested, but it does not drive `shouldSendFoo()`. The send policy does its own per-field delta comparisons directly.

New devices must choose a payload completeness model:
- **Complete-only (SwitchBot):** `makeFooPayload()` returns `nullopt` if any required field is missing. Nothing is sent until a full reading is available.
- **Partial allowed (Xiaomi):** Any non-null subset of fields is a valid payload.
- **Pending aggregation window (Xiaomi):** Because Xiaomi advertises each measurement (temperature, moisture, lux, conductivity) in separate BLE frames, the application buffers for up to `kXiaomiPendingWindowSeconds` (60 s) waiting for a complete set before flushing. A flush is triggered earlier if all fields arrive before the timeout. A device that delivers complete readings in a single advertisement does not need this.

### syncApiState (api_sync.cpp)

Called every tick from `syncOutputs()` after domain state is updated. For each sensor:

1. Check `shouldSendFoo()`. If false, skip.
2. Check `hasValidApiTimestamp()` (lastSeenEpochS > 0). If false, log and skip.
3. Call `client.postFooReading(identity, reading)`.
4. On `WriteStatus::Queued`: update `lastSent` immediately (the reading is durably queued).
5. On `WriteStatus::Sent` + accepted backend result: update `lastSent`.
6. On `WriteStatus::Sent` + `result: "conflict"`: reset `lastSent` to an empty reading (forces resend next tick).

### ApiWriter / OutboxClient (api/outbox_client.h)

`api::ApiWriter` is a pure virtual interface with one method per device type:

```cpp
virtual WriteResult postFooReading(const SensorIdentity&, const FooReading&) = 0;
```

`api::OutboxClient` implements it. Each `postFooReading` method:

1. Calls `makeFooPayload(identity, reading)` (from `api/payloads.h`) to build a `FooPayload` struct. Returns `nullopt` if the reading is not valid (missing required fields or timestamp), which results in `WriteStatus::DroppedPermanent`.
2. Encodes via `encodeCompact(payload)` (compact binary path) or `toJson(payload)` (JSON path) -- see Compact binary encoding below.
3. Calls `sendCompact()` -> `submitCompactPost()` for compact records, or `send()` -> `submitPost()` for JSON records.

`send()` / `sendCompact()` call into `pqueue::http::Outbox`. The outbox either delivers immediately (Sent) or enqueues on disk (Queued) when the network is unavailable. The pqueue spool lives at `/pqueue_api_spool` on LittleFS (ESP32) or `build/pqueue-spools/pqueue_api_spool` on desktop.

`drainPending()` is called once per tick before `syncApiState()`. It drains up to `drainRateCap` queued requests. Compact records are expanded to JSON at drain time via the `ExpandBodyCallback` (`expandCompact`, registered in `makeHttpConfig()`). Do not change pqueue/outbox drain semantics while adding a device unless the task is explicitly about queue behavior.

### Compact binary encoding (api/payloads.h, api/payloads.cpp)

Sensor records are stored on disk as compact binary rather than full JSON to reduce flash usage (~2.3x for SwitchBot: ~185 B JSON vs ~81 B compact). JSON is reconstructed from binary at drain time, just before the HTTP send.

**Current encoding policy:**

- SwitchBot: always compact (`encodeCompact(SwitchbotPayload)`).
- Xiaomi single-field (timeout flush with partial data): compact (`encodeCompact(XiaomiPayload)` requires exactly one measurement field). Use `isSingleFieldXiaomiPayload()` before calling `encodeCompact` -- an empty return means encode error, not multi-field.
- Xiaomi multi-field (normal flush with complete data): JSON fallback via `send()`.

**Binary format** (all multi-byte integers little-endian):

```
[1]  version = 1
[1]  type: 0x01=switchbot  0x02=xiaomi_temp  0x03=xiaomi_moisture
          0x04=xiaomi_lux  0x05=xiaomi_conductivity
[6]  mac: raw 6 bytes
[4]  epoch_s: uint32
[1]  name_len: uint8
[N]  name: UTF-8, no null terminator
[fields per type:]
  switchbot (0x01):     [2] temp_x10 int16  [1] humidity_pct uint8
  xiaomi_temp (0x02):   [2] temp_x10 int16
  xiaomi_moisture(0x03):[1] moisture_pct uint8
  xiaomi_lux (0x04):    [4] lux uint32
  xiaomi_conductivity(0x05): [2] conductivity uint16
```

Encode returns an empty vector on any error (bad MAC, name > 255, out-of-range value, non-finite temperature). Range checks: `epochS` in `[0, UINT32_MAX]`, `temp_x10` in `[INT16_MIN, INT16_MAX]`, `lightLux >= 0`, `conductivityUsCm` in `[0, UINT16_MAX]`.

`expandCompact` matches the `ExpandBodyCallback` signature and reconstructs JSON using the same `toJson()` path. It is registered on `pqueue::http::Config::expandBody` in `OutboxClientImpl::makeHttpConfig()`.

**Adding compact encoding for a new device type:** add a type constant, implement `encodeCompact(FooPayload)` and a decode branch in `expandCompact`, add round-trip tests in `tests/api_payloads.cpp`.

**Downgrade note:** `encodeRequestEnvelope` always writes format version 2. Old firmware rejects all v2 records (including Raw) on version mismatch. Downgrading after any new records have been written to the spool requires a spool wipe.

---

## Config JSON parsing (src/config.cpp)

`parseConfigText()` uses ArduinoJson (`StaticJsonDocument<4096>`) to deserialize `config.json`. Preferred new-code pattern: read and validate into locals, then assign to `config.*` only after all validation passes. Existing SwitchBot/Xiaomi parsing still partially mutates sensor vectors during parsing; do not copy that pattern for new device code.

The pattern for a new device section:

```cpp
const JsonObject foo = json["foo"];
if (foo.isNull()) {
    return fail("foo is not an object");
}

// Required fields: validate type before reading.
if (!foo["update_interval_minutes"].isNull() && !foo["update_interval_minutes"].is<int>()) {
    return fail("foo.update_interval_minutes is not an int");
}
const int fooUpdateIntervalMinutes = foo["update_interval_minutes"] | config.foo.updateIntervalMinutes;
if (fooUpdateIntervalMinutes <= 0) {
    return fail("foo.update_interval_minutes must be > 0");
}

// Sensor array -- build into a local vector; assign to config only at the very end.
const JsonArray fooSensors = foo["sensors"];
if (!foo["sensors"].isNull() && fooSensors.isNull()) {
    return fail("foo.sensors is not an array");
}
std::vector<FooSensorConfig> parsedFooSensors;
{
    std::set<std::string> seenMacs;
    for (JsonObject s : fooSensors) {
        // validate mac, name, short_name as strings ...
        const std::string normalized = normalizeMac(s["mac"].as<const char*>());
        if (normalized.empty()) return fail("foo.sensors[].mac is invalid");
        if (!seenMacs.insert(normalized).second) return fail("foo.sensors[].mac is a duplicate");
        FooSensorConfig sensor;
        sensor.mac = normalized;
        sensor.name = s["name"].as<const char*>();
        sensor.shortName = s["short_name"].as<const char*>();  // JSON key: short_name; C++ field: shortName
        parsedFooSensors.push_back(sensor);
    }
}
// ... validate remaining fields into locals ...
// Only write to config after all validation passes:
config.foo.updateIntervalMinutes = fooUpdateIntervalMinutes;
config.foo.sensors = std::move(parsedFooSensors);
```

The write-at-end pattern is important: all fields are read into local variables first, then assigned to `config.*` together after all validation succeeds. Do not write to `config` before returning false.

Note: the existing SwitchBot and Xiaomi parsers still use the older pattern of calling `config.foo.sensors.clear()` and `push_back()` directly inside the loop. That means a parse failure leaves `config.foo.sensors` partially mutated. It is safe in practice because the function returns `false` on failure and the caller discards the config, but new device parsers should follow the local-vector pattern shown above.

`normalizeMac()` is a file-scope helper that strips separators, uppercases, and checks for exactly 12 hex digits. Always run MAC strings through it.

The `StaticJsonDocument<4096>` size is shared across the entire config. Adding a device with a large sensor list may require increasing this constant.

---

## Display / UI layer (src/ui/)

The display is an OLED (SSD1309 128x64, driven via U8G2). It only shows SwitchBot sensors (up to 4 rows, enforced by `kMaxVisibleSensorRows` in `main.cpp`). The dirty-region system (`DirtyRegions` in `ui/state.h`) tracks per-row changes for SwitchBot only -- `sensorRows` is sized to `switchbotSensors.size()`.

**If the new device should appear on the display:**

- Add display fields or rows to `DirtyRegions` (e.g. `bool fooAny`, `vector<bool> fooRows`).
- Extend `computeDirtyRegions()` in `ui/state.cpp` to compare the new sensor state between `previous` and `current`.
- Add draw functions in `ui/display.h` / `ui/display.cpp` and call them from `renderUi()`.
- `renderUi()` calls `drawAllRegions()` on full draw or individual `drawFooRegion()` / `updateFooRegion()` pairs for partial updates.
- Update `updateUiDirtyState()` in `main.cpp` to initialize the new dirty flags to `true` on first draw (`!app.hasPreviousState`).
- Update `equalsForDisplay()` on `FooReading` in `sensor_readings.h` -- this is what dirty tracking compares.

**If the new device is background-only (data to server, not display):**

No UI changes needed. `equalsForDisplay()` can be a stub returning true. The `shortName` field in `SensorIdentity` is only used by the display, so it can be an empty string in config if unused.

---

## Server layer contract

### Route registration (server/app/main.py)

Each device type gets a POST route under the `device` router (requires `x-api-key`):

```python
@device.post("/foo/reading", response_model=IngestResponse)
def create_foo_reading(reading: foo.ReadingIn, db: Session = Depends(get_db)):
    return ingest_reading(db=db, reading=reading, sensor=foo.SENSOR)
```

`SENSOR_SPECS` (a dict keyed by type int) must include the new device's `SensorSpec` for the dashboard's generic read endpoints to work.

### SensorSpec (server/app/foo.py + sensor_spec.py)

Each device module defines a `SENSOR = SensorSpec(...)` instance with:

- `db_sensor_type`: integer constant from `models.py` (e.g. `SWITCHBOT_TYPE = 1`, `XIAOMI_TYPE = 2`).
- `reading_model`: SQLAlchemy model class.
- `reading_out`: Pydantic response model.
- `unique_constraint_name`: name of the `(mac, timestamp)` unique constraint on the readings table.
- `data_fields`: tuple of `DataField` (column, getter, optional hard/soft ranges). Hard-range violations are rejected with 422; soft-range violations produce warnings in the response.

`ReadingIn` (Pydantic, also in the device module) is the inbound wire format. SwitchBot requires temperature and humidity. Xiaomi has all fields optional but requires at least one to be non-null. Hard-range validation runs in the `@model_validator`.

### Database models (server/app/models.py)

Two tables per device type:
- `sensors`: shared across all types. Columns: `mac` (PK), `id` (UUID), `name`, `type` (SmallInteger).
- `foo_readings`: `id`, `sensor_id` (FK to sensors.id), `mac` (FK to sensors.mac), `timestamp`, plus all measurement columns. Unique constraint on `(mac, timestamp)`.

The `type` column uses a `CheckConstraint` listing all valid type integers. A new device type requires an Alembic migration to add its type constant and readings table.

---

## Adding a new device: checklist

**Firmware:**

1. `src/foo/protocol.{h,cpp}` -- BLE advertisement parsing and decoding.
2. `src/foo/ble.{h,cpp}` -- `foo::Scanner` with `handleAdvertisement` + `snapshot`.
3. `src/sensor_readings.h` -- add `FooReading` (optional fields + `lastSeenEpochS`). Add `equalsForDisplay` and `equalsForApi`.
4. `src/state.h` -- add `FooSensorState` struct and `vector<FooSensorState> fooSensors` to `State`.
5. `src/config.h` -- add `FooSensorConfig`, `FooConfig`, field on `Config`.
6. `src/config.cpp` -- add JSON parsing for the `foo` section and sensor array following the write-at-end pattern.
7. `src/update.{h,cpp}` -- add `updateFooState()`.
8. `src/timing.{h,cpp}` -- add `nextFooDueEpochS`, `areFooDue()`, `markFooUpdated()` if the device needs its own update interval (skip if it can share `nextSensorsDueEpochS`).
9. `src/api/payloads.{h,cpp}` -- add `FooPayload`, `makeFooPayload()`, `toJson(FooPayload)`.
10. `src/api/state.{h,cpp}` -- add `FooApiState` (lastSent vector), extend `api::State`, extend `initState()`, add `shouldSendFoo()`.
11. `src/api/outbox_client.{h,cpp}` -- add `postFooReading()` to `ApiWriter` (virtual) and `OutboxClient` (impl).
12. `src/api_sync.cpp` -- add the per-sensor loop calling `postFooReading`.
13. `src/main.cpp` -- instantiate `foo::Scanner` in `AppContext`, wire `handleAdvertisement`, call `updateFooState` and `updateFooIfDue`, resize `fooSensors` in `initStateStorage` and `prepareCurrentState`.
14. `src/ui/` -- if the device appears on the display: extend `DirtyRegions`, `computeDirtyRegions`, `renderUi`, and the draw functions. If background-only, no UI changes needed.
15. Root `Makefile` -- add `src/foo/*.cpp` to `COMMON_SRC`. PlatformIO discovers sources by filter automatically, but the desktop build does not; omitting this step silently excludes the new files from the desktop binary and tests.

**Tests:**

1. `tests/foo_protocol.cpp` -- advertisement decoding: good bytes, bad bytes, edge cases.
2. `tests/foo_ble.cpp` -- `Scanner::handleAdvertisement` with synthetic events.
3. `tests/config.cpp` -- extend with `foo` section: valid config, missing required field, duplicate MAC.
4. `tests/api_payloads.cpp` -- extend with `makeFooPayload` cases.
5. `tests/api_sensor_write_policy.cpp` -- extend with `shouldSendFoo` cases.
6. `tests/api_sync.cpp` -- extend with the Foo sensor loop behavior.
7. `tests/foo_api_integration.cpp` -- end-to-end path with mock transport (model on `xiaomi_api_integration.cpp`).
8. Root `Makefile` -- add `tests/foo_*.cpp` to `TEST_SRC` (same reason as `COMMON_SRC` above).
9. `test/test_foo_*/` -- device firmware test (Unity/PlatformIO) only if the device has hardware-specific behavior not exercisable on the POSIX backend (e.g. a GATT connection sequence). Pure advertisement parsing does not need one.

**Server:**

1. `server/app/models.py` -- add `FOO_TYPE = N`, `FooReading` SQLAlchemy model, extend `CheckConstraint`.
2. `server/app/foo.py` -- `ReadingIn`, `ReadingOut`, `SENSOR = SensorSpec(...)`.
3. `server/app/main.py` -- `POST /foo/reading` route, add `FOO_TYPE: foo.SENSOR` to `SENSOR_SPECS` and `SENSOR_TYPE_NAMES`.
4. `server/alembic/versions/` -- migration adding `FOO_TYPE` and `foo_readings` table.

**Config:**

- `data/config.json` -- add `foo` section with `sensors` list (mac, name, short_name, plus any device-specific knobs).
- `data/config.json.example` -- mirror the same change here; `tests/config.cpp` loads this file as its test fixture.

---

## Tests

### Desktop unit tests (tests/)

One `.cpp` file per module, built with `make -j12` (POSIX backend, no real hardware). Each file uses doctest. The naming convention mirrors the source module:

| Source module | Test file |
|---|---|
| `src/switchbot/protocol.cpp` | `tests/switchbot_protocol.cpp` |
| `src/switchbot/ble.cpp` | `tests/switchbot_ble.cpp` |
| `src/config.cpp` | `tests/config.cpp` |
| `src/api/payloads.cpp` | `tests/api_payloads.cpp` |
| `src/api/state.cpp` | `tests/api_sensor_write_policy.cpp` |
| `src/api_sync.cpp` | `tests/api_sync.cpp` |
| `src/api/outbox_client.cpp` | `tests/api_outbox_client.cpp` |

For a new device, add at minimum:

- `tests/foo_protocol.cpp` -- raw advertisement decoding: known good bytes produce correct fields, bad bytes return nothing.
- `tests/foo_ble.cpp` -- `Scanner::handleAdvertisement` with synthetic `AdvertisementEvent`s: known MAC is parsed, unknown MAC is ignored, snapshot returns correct values.
- `tests/config.cpp` -- extend the existing config tests with a `foo` section: required fields absent returns false, valid JSON populates the struct, duplicate MACs are rejected.
- `tests/api_payloads.cpp` -- extend with `makeFooPayload` cases: valid reading produces payload, missing timestamp returns nullopt.
- `tests/api_sensor_write_policy.cpp` -- extend with `shouldSendFoo` cases: first send, heartbeat, delta thresholds.
- `tests/api_sync.cpp` -- extend with the Foo sensor loop: skips invalid timestamp, updates lastSent on Queued, resets lastSent on Conflict.

Integration test file `tests/foo_api_integration.cpp` (modelled on `xiaomi_api_integration.cpp` / `switchbot_api_integration.cpp`) exercises the full firmware-side path end-to-end using an injected mock transport: BLE data in, HTTP POST out, response handled.

### Device firmware tests (test/)

`test/` contains Arduino/Unity tests that run on real hardware via PlatformIO. The only existing test there is `test/test_switchbot_history_sync/`, which is specific to the SwitchBot BLE history-sync protocol (a GATT exchange that only runs on device). A new device needs a firmware test here only if it has hardware-specific behavior that cannot be exercised with the POSIX backend -- for example, a custom BLE GATT connection sequence. Pure advertisement parsing does not need a firmware test; the desktop tests are sufficient.

---

## Key invariants

- Sensor arrays in `State`, `api::State` (lastSent, pending), and `Config.foo.sensors` are always parallel -- same length, same index meaning the same physical sensor. Resizing any one requires resizing all.
- `lastSeenEpochS = 0` means "not seen since boot" or "seen before time was valid". The API layer treats it as an invalid timestamp and skips sending. Never send a reading with epoch 0 to the server.
- `lastSent` is updated on `Queued` (not just `Sent`) because a queued reading is durably stored in pqueue and will be delivered. Updating on Queued prevents duplicate sends when the queue drains.
- Conflict response from the server is HTTP 200 with `result: "conflict"` -- the server already has a reading at that timestamp with different values. The correct firmware response is to reset `lastSent` to empty so the next tick re-evaluates whether to send.
- The server deduplicates on `(mac, timestamp)`. Duplicate timestamps with identical values return 200 with `result: "duplicate"`. Duplicate timestamps with different values return 200 with `result: "conflict"` (not 409).
- All pqueue records written by current firmware use envelope version 2 (both compact and raw). Old firmware rejects version 2 records with a version mismatch. Downgrading firmware after any new records are written requires wiping the spool (`/pqueue_api_spool`).
