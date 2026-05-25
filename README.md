# ESP32 Salah + Forecast + Sensor Display

An ESP32-based home display that shows:

- the current salah phase and time remaining
- local weather forecast
- SwitchBot temperature and humidity readings

Xiaomi plant sensor readings (temperature, moisture, light, conductivity) are scanned and
synced to the backend but not shown on the display.

A backend server stores sensor history and serves a browser-based dashboard with graphs
and ML-based predictions.

## Features

**Firmware**

- Salah schedule with Hanafi Asr support and EU DST handling
- Open-Meteo weather forecast over HTTPS
- SwitchBot passive BLE scanning with up to 68 days of history sync
- Xiaomi BLE plant sensor support (temperature, moisture, light, conductivity)
- OLED partial redraws -- only changed regions are redrawn each tick
- Compact binary sensor records -- ~2.3x smaller than JSON on LittleFS
- Async WiFi -- connection time is credited against the budget, not wasted
- Shared HTTPS connections -- reused across forecast and API calls
- Rotating on-device log -- WARN+ and lifecycle events persisted to LittleFS
- pqueue doctor mode built into main firmware -- no separate firmware flash needed
- Desktop build for development and testing without hardware

**Backend**

- Sensor history storage with gap-filling via SwitchBot bulk sync
- Browser dashboard with live readings, history graphs, and ML-based predictions
- Levoit humidifier control driven by absolute humidity setpoint
- Per-device and per-browser rate limiting
- API key auth for the ESP32, session auth for the browser dashboard

## Hardware

Current embedded target:

- ESP32-S3
- SSD1309 128x64 OLED over SPI
- SwitchBot Meter / Meter Plus sensors
- Xiaomi BLE plant sensors

## Dependencies

### Desktop

- `g++` with C++20
- `libcurl`
- `sdbus-c++` (BlueZ D-Bus bindings, for BLE on Linux)
- `pkg-config`

### ESP32

- PlatformIO
- Submodules use SSH -- GitHub SSH access required

### Backend

Docker is the only runtime dependency. See `server/README.md`.

## Setup

Clone with submodules:

```bash
git clone --recurse-submodules <repo>
# or after cloning:
git submodule update --init
```

Install Python dependencies for the desktop build and tools:

```bash
pip install -r tools/requirements.txt
```

Copy the config example and fill in your values:

```bash
cp data/config.json.example data/config.json
```

At minimum, configure location, timezone, salah settings, Wi-Fi credentials, forecast
settings, and BLE sensors. The example file documents every field.

## Build

### Desktop build

```bash
make
./build/desktop/main
```

### POSIX tests

```bash
make run-tests
```

### Arduino tests (on device)

```bash
pio test -e esp32s3
```

### ESP32 build and upload

```bash
pio run -e esp32s3 -t upload        # firmware
pio run -e esp32s3 -t uploadfs      # LittleFS (config.json)
```

### Serial monitor

```bash
pio device monitor
```

## Backend

See `server/README.md`.

### Levoit humidifier controller

The server can drive a Levoit humidifier to maintain a target absolute humidity level,
using a SwitchBot sensor as the source reading. Config is split between
`server/config/env` (secrets) and `server/config/app.json` (tuning).

In `server/config/env`:

```
VESYNC_USERNAME=your-vesync-email
VESYNC_PASSWORD=your-vesync-password
VESYNC_DEVICE_CID=your-device-cid
```

In `server/config/app.json`:

```json
"levoit_ah_controller": {
  "switchbot_mac": "AA:BB:CC:DD:EE:FF",
  "target_absolute_humidity": 8.0
}
```

`switchbot_mac` is the MAC of the SwitchBot sensor used to drive the controller.
`target_absolute_humidity` is in g/m3.

Optional overrides (defaults shown):

```json
"levoit_ah_controller": {
  "minimum_humidity": 40,
  "maximum_humidity": 60,
  "reading_max_age_seconds": 900,
  "poll_interval_seconds": 300,
  "minimum_command_interval_seconds": 300,
  "humidity_change_threshold": 2.0
}
```

The controller runs as a separate Docker Compose service (`levoit-controller` in
`compose.yml`), not as a background task inside the server. It fetches live readings
from the server API and has no direct database access.

## Repository layout

**Firmware**

- `src/main.cpp` -- app orchestration and main loop
- `src/update.*` -- per-module update logic
- `src/timing.*` -- sleep scheduling and update timers
- `src/file_log.*` -- rotating on-device log (LittleFS)
- `src/log_download_main.cpp` -- serial dump of retained log files
- `src/network_connect_budget.h` -- WiFi connection timeout tracking
- `src/salah/` -- prayer time calculation and state
- `src/forecast/` -- Open-Meteo weather fetch and parse
- `src/switchbot/` -- BLE scanning, history protocol, history sync
- `src/xiaomi/` -- BLE scanning and protocol
- `src/ble/` -- shared BLE session management
- `src/api/` -- backend API client: payloads, outbox, write policy, dropped-reading log
- `src/ui/` -- OLED state machine and rendering

**Tests**

- `tests/` -- POSIX desktop unit tests (doctest, run with `make run-tests`)
- `test/` -- Arduino on-device tests (Unity/PlatformIO, run with `pio test`)

**Backend and tooling**

- `server/` -- Python FastAPI server and browser dashboard
- `pqueue/` -- persistent queue (git submodule)
- `data/` -- LittleFS image: config and TLS certificates
- `tools/` -- BLE probe scripts
- `third_party/` -- vendored libraries (ArduinoJson, PrayerTimes, doctest)

## Tools

`tools/` contains Python scripts for probing and reverse-engineering BLE sensor protocols:

- `switchbot_history_probe.py` -- probe SwitchBot history bank protocol
- `switchbot.py` / `switchbot.sh` -- SwitchBot BLE inspection
- `xiaomi.py` / `xiaomi.sh` -- Xiaomi BLE inspection

## Notes

- The display supports up to 4 SwitchBot sensors due to screen space.
- SwitchBot history sync covers up to 68 days; gaps are detected server-side and filled
  on the next sync.
- Desktop and ESP32 share interfaces but use separate platform implementations for BLE,
  networking, time, and storage.
- Forecast fetches are rate-limited on retry to avoid hammering Open-Meteo after a failure.

## Status

This is an active personal embedded project. The firmware, desktop build, tests, display,
forecast, salah, BLE sensors, history sync, backend, and persistent queue are all in
active use.

## License

MIT
