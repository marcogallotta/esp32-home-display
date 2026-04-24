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


def get_sensor_id(client, api_key, *, name="location A", sensor_type):
    response = client.get("/sensors", headers=auth_headers(api_key))
    assert response.status_code == 200

    matches = [
        sensor
        for sensor in response.json()
        if sensor["name"] == name and sensor["type"] == sensor_type
    ]

    assert len(matches) == 1
    return matches[0]["id"]


def get_sensor_readings(client, api_key, sensor_id, params=None):
    return client.get(
        f"/sensors/{sensor_id}/readings",
        headers=auth_headers(api_key),
        params=params or {},
    )
