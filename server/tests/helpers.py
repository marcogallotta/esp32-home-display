def auth_headers(api_key):
    return {"x-api-key": api_key}


def make_sensor_payload(**overrides):
    payload = {
        "mac": "AA:BB:CC:DD:EE:FF",
        "name": "location A",
        "timestamp": "2026-04-21T18:00:00Z",
        "temperature_c": 21.5,
    }
    payload.update(overrides)
    return payload


def make_switchbot_payload(**overrides):
    return make_sensor_payload(
        humidity_pct=48.0,
        **overrides,
    )


def make_xiaomi_payload(**overrides):
    return make_sensor_payload(**overrides)


def post_switchbot(client, api_key, payload):
    return client.post(
        "/switchbot/reading",
        headers=auth_headers(api_key),
        json=payload,
    )


def post_xiaomi(client, api_key, payload):
    return client.post(
        "/xiaomi/reading",
        headers=auth_headers(api_key),
        json=payload,
    )
