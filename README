# ESP32 Salah + Forecast + Sensor Display

An ESP32-based home display that shows:

- current salah phase and time remaining
- local weather forecast
- SwitchBot temperature / humidity sensor readings

It also supports a desktop build for faster development and testing.

## Features

- Salah schedule calculation with Hanafi Asr support
- EU DST handling
- Open-Meteo forecast fetch over HTTPS
- SwitchBot passive BLE sensor scanning
- OLED partial redraws for efficient updates
- Desktop test/build path
- JSON config file

## Hardware

Current embedded target:

- ESP32-S3
- SSD1309 128x64 OLED over SPI
- up to 4 SwitchBot Meter / Meter Plus sensors

## Project structure

- `src/main.cpp` — app orchestration
- `src/update.*` — module update logic
- `src/timing.*` — scheduling / due times
- `src/ui/state.*` — UI state + dirty-region detection
- `src/ui/display.*` — OLED rendering
- `src/salah/*` — prayer schedule/state logic
- `src/forecast/*` — weather fetch + parse
- `src/switchbot/*` — BLE sensor handling
- `tests/*` — desktop tests

## Build

### Desktop build

```bash
make
./build/main
```

### Run tests

```bash
make run-tests
```

### ESP32 build

```bash
make esp32-compile
```

### Upload to ESP32

```bash
make esp32-upload
```

### Serial monitor

```bash
make esp32-monitor
```

## Configuration

The app reads a `config.json` file.

On desktop:
- `config.json` is read from the project root

On ESP32:
- `config.json` is packed into SPIFFS and flashed separately

A full example lives in `config.json.example`.

Config includes:

- forecast settings
- location and timezone
- salah settings
- SwitchBot sensors
- Wi-Fi credentials

Example shape:

```json
{
  "forecast": {
    "openmeteo_pem": "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n",
    "update_interval_minutes": 60
  },
  "location": {
    "latitude": 48.9,
    "longitude": 2.3,
    "timezone": "Europe/Paris",
    "timezone_long": "CET-1CEST,M3.5.0/2,M10.5.0/3"
  },
  "salah": {
    "timezone_offset_minutes": 60,
    "dst_rule": "eu",
    "asr_makruh_minutes": 20,
    "hanafi_asr": true
  },
  "switchbot": {
    "sensors": [
      { "mac": "AA:BB:CC:DD:EE:FF", "name": "Room 1", "short_name": "R1" },
      { "mac": "11:22:33:44:55:66", "name": "Room 2", "short_name": "R2" }
    ]
  },
  "wifi": {
    "ssid": "YOUR_WIFI_SSID",
    "password": "YOUR_WIFI_PASSWORD"
  }
}
```

## Notes

- ESP32 UI currently supports up to 4 sensors because of screen space.
- Forecast retries are rate-limited after failure.
- Desktop and ESP32 use different platform implementations behind shared interfaces.

## Status

This is a personal systems / embedded project focused on:

- embedded C++
- cross-platform structure
- hardware integration
- testable logic separation

## License

MIT
