# ESP32 Salah + Forecast + Sensor Display

An ESP32-based home display that shows:

- the current salah phase and time remaining
- local weather forecast
- SwitchBot temperature / humidity readings

Xiaomi plant sensor readings (temperature, moisture, light, conductivity) are scanned and synced to the backend but not shown on the display.

It also supports a desktop build for faster development and testing, a backend server for sensor history, and a browser-based graph view.

## Features

- Salah schedule calculation with Hanafi Asr support
- EU DST handling
- Open-Meteo forecast fetch over HTTPS
- SwitchBot passive BLE scanning + historical data sync
- Xiaomi BLE plant sensor support (temperature, moisture, light, conductivity)
- Backend server for sensor history storage and gap-filling
- Browser-based sensor history graphs
- OLED partial redraws for efficient updates
- Desktop test/build path
- JSON config file

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
- `sdbus-c++`
- `pkg-config`

### ESP32

- PlatformIO
- Submodules use SSH — GitHub SSH access required

### Backend

- Docker (for Postgres)
- Python 3

## Setup

Clone with submodules:

```bash
git clone --recurse-submodules <repo>
# or after cloning:
git submodule update --init
```

### Desktop / tools dependencies

```bash
pip install -r tools/requirements.txt
```

Copy the config example:

```bash
cp data/config.json.example data/config.json
```

### Backend dependencies

```bash
pip install -r server/requirements.txt
cp server/config/dev.example.json server/config/dev.json
cd server && ./tools/gen_certs.sh
```

Copy the generated cert into `data/` and reference it in `config.json` under `api.pem_file`.

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

### ESP32 build + upload

```bash
pio run -e esp32s3 -t upload        # firmware
pio run -e esp32s3 -t uploadfs      # LittleFS (config.json)
```

### Serial monitor

```bash
pio device monitor
```

## Backend

The backend is a FastAPI server backed by PostgreSQL (run via Docker). It stores sensor history uploaded by the device and serves a browser-based graph view.

```bash
cd server
make db-start       # start Postgres in Docker
make run-server     # start uvicorn (HTTPS)
```

Run tests:

```bash
make test
```

The graph UI is at `https://localhost/static/overview.html`.

## Configuration

The app reads a `config.json` file.

On desktop:
- `data/config.json` is read from the project root

On ESP32:
- `config.json` is packed into LittleFS and flashed separately

A full example lives in `data/config.json.example`.

At minimum, configure:

- location and timezone
- salah settings
- Wi-Fi credentials
- forecast settings
- BLE sensors

## Repository layout

- `data/` — LittleFS contents: config and certificate files
- `src/main.cpp` — app orchestration
- `src/update.*` — module update logic
- `src/timing.*` — scheduling / due times
- `src/ui/state.*` — UI state + dirty-region detection
- `src/ui/display.*` — OLED rendering
- `src/salah/*` — prayer schedule/state logic
- `src/forecast/*` — weather fetch + parse
- `src/switchbot/*` — BLE scanning + history sync
- `src/xiaomi/*` — Xiaomi BLE sensor handling
- `src/ble/*` — shared BLE session management
- `src/api/*` — backend API client
- `server/` — Python backend + graph frontend
- `pqueue/` — persistent queue (being extracted to its own repo)
- `tests/*` — POSIX desktop tests
- `test/*` — Arduino (on-device) tests
- `tools/*` — BLE protocol probing scripts
- `third_party/` — submodules (ArduinoJson, PrayerTimes, doctest)

## Tools

`tools/` contains Python scripts used to probe and reverse-engineer BLE sensor protocols:

- `switchbot_history_probe.py` — probe SwitchBot history bank protocol
- `switchbot.py` / `switchbot.sh` — SwitchBot BLE inspection
- `xiaomi.py` / `xiaomi.sh` — Xiaomi BLE inspection

## Notes

- ESP32 UI currently supports up to 4 sensors because of screen space.
- Forecast retries are rate-limited after failure.
- Desktop and ESP32 use different platform implementations behind shared interfaces.
- SwitchBot history sync downloads up to 68 days of data from sensors and syncs gaps with the backend.

## Status

This is an active personal embedded project.

The firmware, desktop build, tests, display rendering, forecast fetch, salah calculation, and BLE sensor paths are in active use. SwitchBot history sync, backend gap filling, and the persistent queue are still evolving.

## License

MIT
