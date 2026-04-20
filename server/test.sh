curl -X POST http://127.0.0.1:8000/switchbot/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:FF",
    "name": "Bed",
    "type": "switchbot",
    "timestamp": "2026-04-20T12:00:00Z",
    "temperature_c": 20.1,
    "humidity_pct": 29
  }'

curl -X POST http://127.0.0.1:8000/switchbot/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:FF",
    "type": "switchbot",
    "timestamp": "2026-04-20T12:15:00Z",
    "temperature_c": 20.3,
    "humidity_pct": 30
  }'

curl -X POST http://127.0.0.1:8000/switchbot/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:FF",
    "type": "switchbot",
    "timestamp": "2026-04-20T12:30:00Z",
    "temperature_c": 20.0,
    "humidity_pct": 28
  }'

curl -X POST http://127.0.0.1:8000/xiaomi/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "11:22:33:44:55:66",
    "name": "Cilantro",
    "type": "xiaomi",
    "timestamp": "2026-04-20T12:00:00Z",
    "temperature_c": 16.8,
    "moisture_pct": 14,
    "light_lux": 88,
    "conductivity_us_cm": 134
  }'

curl -X POST http://127.0.0.1:8000/xiaomi/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "11:22:33:44:55:66",
    "type": "xiaomi",
    "timestamp": "2026-04-20T12:15:00Z",
    "temperature_c": 17.0,
    "moisture_pct": 15,
    "light_lux": 120,
    "conductivity_us_cm": 140
  }'

curl -X POST http://127.0.0.1:8000/xiaomi/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "11:22:33:44:55:66",
    "type": "xiaomi",
    "timestamp": "2026-04-20T12:30:00Z",
    "temperature_c": 17.2,
    "moisture_pct": 16,
    "light_lux": 200,
    "conductivity_us_cm": 150
  }'

curl 'http://127.0.0.1:8000/switchbot/readings?mac=AA:BB:CC:DD:EE:FF&limit=10'
