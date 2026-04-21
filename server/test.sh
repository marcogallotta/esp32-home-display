curl -H 'X-api-key: happydevilelephantsmoking' -X POST http://127.0.0.1:8000/switchbot/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:FF",
    "name": "Bed",
    "type": "switchbot",
    "timestamp": "2026-04-20T12:00:00Z",
    "temperature_c": 20.1,
    "humidity_pct": 29
  }'

curl -H 'X-api-key: happydevilelephantsmoking' -X POST http://127.0.0.1:8000/switchbot/reading \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:FF",
    "type": "switchbot",
    "timestamp": "2026-04-20T12:00:00Z",
    "temperature_c": 20.1,
    "humidity_pct": 29
  }'
