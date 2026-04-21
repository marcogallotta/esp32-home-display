import pytest


def make_payload(**overrides):
    payload = {
        "mac": "AA:BB:CC:DD:EE:FF",
        "name": "living room",
        "timestamp": "2026-04-21T18:00:00Z",
        "temperature_c": 21.5,
        "humidity_pct": 48.0,
    }
    payload.update(overrides)
    return payload


def post_switchbot(client, api_key, payload):
    return client.post(
        "/switchbot/reading",
        headers={"x-api-key": api_key},
        json=payload,
    )


def test_switchbot_create_accepts_valid_payload(client, api_key):
    response = post_switchbot(client, api_key, make_payload())

    assert response.status_code == 200
    assert response.json() == {
        "status": "ok",
        "result": "created",
    }


@pytest.mark.parametrize(
    "mac",
    [
        "AA:BB:CC:DD:EE:FF",
        "aa:bb:cc:dd:ee:ff",
    ],
    ids=[
        "uppercase",
        "lowercase",
    ],
)
def test_switchbot_create_accepts_valid_mac(client, api_key, mac):
    response = post_switchbot(client, api_key, make_payload(mac=mac))

    assert response.status_code == 200
    assert response.json()["result"] == "created"


@pytest.mark.parametrize(
    "mac",
    [
        "AA:BB:CC:DD:EE:FG",
        "AA:BB:CC:DD:EE",
        "AA-BB-CC-DD-EE-FF",
        "AABBCCDDEEFF",
    ],
    ids=[
        "invalid character",
        "too short",
        "wrong separator",
        "no separator",
    ],
)
def test_switchbot_create_rejects_invalid_mac(client, api_key, mac):
    response = post_switchbot(client, api_key, make_payload(mac=mac))

    assert response.status_code == 400


def test_switchbot_create_rejects_missing_mac(client, api_key):
    payload = make_payload()
    del payload["mac"]

    response = post_switchbot(client, api_key, payload)

    assert response.status_code == 422


@pytest.mark.parametrize(
    ("name", "expected_status"),
    [
        ("living room", 200),
        (None, 400),
        ("", 422),  # requires explicit validation in app code
    ],
    ids=[
        "valid name",
        "missing name",
        "empty string",
    ],
)
def test_switchbot_create_validates_name(client, api_key, name, expected_status):
    payload = make_payload()
    if name is None:
        del payload["name"]
    else:
        payload["name"] = name

    response = post_switchbot(client, api_key, payload)

    assert response.status_code == expected_status


@pytest.mark.parametrize(
    ("timestamp", "expected_status"),
    [
        ("2026-04-21T18:00:00Z", 200),
        ("2026-04-21T20:00:00+02:00", 200),
        ("2026-04-21T18:00:00", 400),
        (None, 422),
    ],
    ids=[
        "utc",
        "offset timezone",
        "missing timezone",
        "missing field",
    ],
)
def test_switchbot_create_validates_timestamp(client, api_key, timestamp, expected_status):
    payload = make_payload()
    if timestamp is None:
        del payload["timestamp"]
    else:
        payload["timestamp"] = timestamp

    response = post_switchbot(client, api_key, payload)

    assert response.status_code == expected_status


@pytest.mark.parametrize(
    ("temperature_c", "expected_status"),
    [
        (21.5, 200),
        (21, 200),
        (-5.0, 200),
        (-41.0, 422),
        (126.0, 422),
        (None, 422),
    ],
    ids=[
        "float",
        "int",
        "negative valid",
        "hard low",
        "hard high",
        "missing field",
    ],
)
def test_switchbot_create_validates_temperature(client, api_key, temperature_c, expected_status):
    payload = make_payload()
    if temperature_c is None:
        del payload["temperature_c"]
    else:
        payload["temperature_c"] = temperature_c

    response = post_switchbot(client, api_key, payload)

    assert response.status_code == expected_status


def test_switchbot_create_accepts_soft_out_of_range_temperature_and_logs_warning(client, api_key, caplog):
    response = post_switchbot(client, api_key, make_payload(temperature_c=-25.0))

    assert response.status_code == 200
    assert response.json() == {
        "status": "ok",
        "result": "created",
    }

    messages = [record.getMessage() for record in caplog.records]
    assert any(
        "Suspicious temperature_c=-25.0" in message and "AA:BB:CC:DD:EE:FF" in message
        for message in messages
    )


@pytest.mark.parametrize(
    ("humidity_pct", "expected_status"),
    [
        (48.0, 200),
        (48, 200),
        (-1.0, 422),
        (101.0, 422),
        (None, 422),
    ],
    ids=[
        "float",
        "int",
        "hard low",
        "hard high",
        "missing field",
    ],
)
def test_switchbot_create_validates_humidity(client, api_key, humidity_pct, expected_status):
    payload = make_payload()
    if humidity_pct is None:
        del payload["humidity_pct"]
    else:
        payload["humidity_pct"] = humidity_pct

    response = post_switchbot(client, api_key, payload)

    assert response.status_code == expected_status


def test_switchbot_create_rejects_missing_temperature_and_humidity(client, api_key):
    payload = make_payload()
    del payload["temperature_c"]
    del payload["humidity_pct"]

    response = post_switchbot(client, api_key, payload)

    assert response.status_code == 422
