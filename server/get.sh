curl -sS -H "X-api-key: $(python3 -c 'from config import load_config; print(load_config()["api_key"])')" \
  'http://127.0.0.1:8000/xiaomi/readings?mac=5C:85:7E:14:43:45&limit=10' | jq

curl -sS -H "X-api-key: $(python3 -c 'from config import load_config; print(load_config()["api_key"])')" \
  'http://127.0.0.1:8000/switchbot/readings?mac=EC:2E:84:06:4E:9A&limit=10' | jq
