. .env
read -r API_KEY < "$API_KEY_FILE"

curl -H "X-API-Key: $API_KEY" -X POST http://127.0.0.1:8000/switchbot/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:FF",
    "name": "Bed",
    "type": "switchbot",
    "timestamp": "2026-04-20T12:00:00Z",
    "temperature_c": 20.1,
    "humidity_pct": 29
  }'

curl -H "X-API-Key: $API_KEY" -X POST http://127.0.0.1:8000/switchbot/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:FF",
    "type": "switchbot",
    "timestamp": "2026-04-20T12:00:00Z",
    "temperature_c": 20.1,
    "humidity_pct": 29
  }'

curl -H "X-API-Key: $API_KEY" -X POST http://127.0.0.1:8000/switchbot/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:FF",
    "type": "switchbot",
    "timestamp": "2026-04-20T12:00:00Z",
    "temperature_c": 20.4,
    "humidity_pct": 29
  }'

curl -H "X-API-Key: $API_KEY" -X POST http://127.0.0.1:8000/switchbot/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:FF",
    "type": "switchbot",
    "timestamp": "2026-04-20T12:01:00Z",
    "temperature_c": 20.4,
    "humidity_pct": 29
  }'

curl -H "X-API-Key: $API_KEY" -X POST http://127.0.0.1:8000/xiaomi/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:00",
    "name": "Cilantro",
    "type": "xiaomi",
    "timestamp": "2026-04-20T12:00:00Z",
    "temperature_c": 20.1
  }'

curl -H "X-API-Key: $API_KEY" -X POST http://127.0.0.1:8000/xiaomi/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:00",
    "name": "Cilantro",
    "type": "xiaomi",
    "timestamp": "2026-04-20T12:00:00Z",
    "temperature_c": 20.1
  }'

curl -H "X-API-Key: $API_KEY" -X POST http://127.0.0.1:8000/xiaomi/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:00",
    "name": "Cilantro",
    "type": "xiaomi",
    "timestamp": "2026-04-20T12:00:00Z",
    "temperature_c": 20.4
  }'

curl -H "X-API-Key: $API_KEY" -X POST http://127.0.0.1:8000/xiaomi/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:00",
    "name": "Cilantro",
    "type": "xiaomi",
    "timestamp": "2026-04-20T12:00:00Z",
    "temperature_c": 20.4,
    "moisture_pct": 50
  }'

curl -H "X-API-Key: $API_KEY" -X POST http://127.0.0.1:8000/xiaomi/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:00",
    "name": "Cilantro",
    "type": "xiaomi",
    "timestamp": "2026-04-20T12:00:00Z",
    "temperature_c": 20.1,
    "moisture_pct": 50
  }'

