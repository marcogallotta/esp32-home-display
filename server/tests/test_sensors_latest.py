from tests.helpers import (
    make_switchbot_payload,
    make_xiaomi_payload,
    post_switchbot,
    post_xiaomi,
)


def test_latest_empty_when_no_readings(authed_client):
    response = authed_client.get("/sensors/latest")

    assert response.status_code == 200
    assert response.json() == {"sensors": []}


def test_latest_requires_session(client):
    response = client.get("/sensors/latest")

    assert response.status_code == 401


def test_latest_switchbot_returns_most_recent(authed_client, api_key):
    post_switchbot(authed_client, api_key, make_switchbot_payload(timestamp="2026-04-21T10:00:00Z", temperature_c=20.0))
    post_switchbot(authed_client, api_key, make_switchbot_payload(timestamp="2026-04-21T11:00:00Z", temperature_c=21.5))
    post_switchbot(authed_client, api_key, make_switchbot_payload(timestamp="2026-04-21T09:00:00Z", temperature_c=19.0))

    response = authed_client.get("/sensors/latest")

    assert response.status_code == 200
    sensors = response.json()["sensors"]
    assert len(sensors) == 1
    s = sensors[0]
    assert s["mac"] == "AA:BB:CC:DD:EE:FF"
    assert s["latest_timestamp"] == "2026-04-21T11:00:00Z"
    assert s["reading"]["temperature_c"] == 21.5


def test_latest_xiaomi_returns_most_recent(authed_client, api_key):
    post_xiaomi(authed_client, api_key, make_xiaomi_payload(
        mac="11:22:33:44:55:66",
        name="Plant A",
        timestamp="2026-04-21T10:00:00Z",
        moisture_pct=30,
        light_lux=200,
    ))
    post_xiaomi(authed_client, api_key, make_xiaomi_payload(
        mac="11:22:33:44:55:66",
        name="Plant A",
        timestamp="2026-04-21T12:00:00Z",
        moisture_pct=35,
        light_lux=250,
    ))

    response = authed_client.get("/sensors/latest")

    assert response.status_code == 200
    sensors = response.json()["sensors"]
    assert len(sensors) == 1
    s = sensors[0]
    assert s["mac"] == "11:22:33:44:55:66"
    assert s["latest_timestamp"] == "2026-04-21T12:00:00Z"
    assert s["reading"]["moisture_pct"] == 35
    assert s["reading"]["light_lux"] == 250


def test_latest_returns_both_sensor_types(authed_client, api_key):
    post_switchbot(authed_client, api_key, make_switchbot_payload(
        mac="AA:BB:CC:DD:EE:FF", name="Bedroom", timestamp="2026-04-21T10:00:00Z",
    ))
    post_xiaomi(authed_client, api_key, make_xiaomi_payload(
        mac="11:22:33:44:55:66", name="Plant A", timestamp="2026-04-21T10:00:00Z",
    ))

    response = authed_client.get("/sensors/latest")

    assert response.status_code == 200
    sensors = response.json()["sensors"]
    assert len(sensors) == 2
    macs = {s["mac"] for s in sensors}
    assert macs == {"AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66"}


def test_latest_one_entry_per_sensor(authed_client, api_key):
    for mac in ["AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66"]:
        post_switchbot(authed_client, api_key, make_switchbot_payload(
            mac=mac, name=f"Sensor {mac}", timestamp="2026-04-21T10:00:00Z",
            temperature_c=20.0,
        ))
        post_switchbot(authed_client, api_key, make_switchbot_payload(
            mac=mac, name=f"Sensor {mac}", timestamp="2026-04-21T11:00:00Z",
            temperature_c=22.0,
        ))

    response = authed_client.get("/sensors/latest")

    assert response.status_code == 200
    sensors = response.json()["sensors"]
    assert len(sensors) == 2
    for s in sensors:
        assert s["latest_timestamp"] == "2026-04-21T11:00:00Z"
        assert s["reading"]["temperature_c"] == 22.0


def test_latest_response_shape(authed_client, api_key):
    post_switchbot(authed_client, api_key, make_switchbot_payload(
        timestamp="2026-04-21T10:00:00Z", temperature_c=21.0,
    ))

    response = authed_client.get("/sensors/latest")

    assert response.status_code == 200
    s = response.json()["sensors"][0]
    assert set(s.keys()) == {"mac", "sensor_id", "latest_timestamp", "reading"}
    assert set(s["reading"].keys()) == {"temperature_c", "humidity_pct"}


def test_latest_xiaomi_nullable_fields(authed_client, api_key):
    post_xiaomi(authed_client, api_key, make_xiaomi_payload(
        mac="11:22:33:44:55:66",
        name="Plant",
        timestamp="2026-04-21T10:00:00Z",
        temperature_c=None,
        moisture_pct=40,
    ))

    response = authed_client.get("/sensors/latest")

    assert response.status_code == 200
    reading = response.json()["sensors"][0]["reading"]
    assert reading["moisture_pct"] == 40
    assert reading["temperature_c"] is None
    assert reading["light_lux"] is None
    assert reading["conductivity_us_cm"] is None


# --- sensor_id filter ---

def _post_and_get_sensor_id(client, api_key, mac, name):
    post_switchbot(client, api_key, make_switchbot_payload(mac=mac, name=name, timestamp="2026-04-21T10:00:00Z"))
    latest = client.get("/sensors/latest", headers={"x-api-key": api_key}).json()
    return next(s["sensor_id"] for s in latest["sensors"] if s["mac"] == mac.upper())


def test_latest_sensor_id_filter_returns_only_that_sensor(authed_client, api_key):
    id_a = _post_and_get_sensor_id(authed_client, api_key, "AA:BB:CC:DD:EE:FF", "Sensor A")
    post_switchbot(authed_client, api_key, make_switchbot_payload(
        mac="11:22:33:44:55:66", name="Sensor B", timestamp="2026-04-21T10:00:00Z",
    ))

    response = authed_client.get("/sensors/latest", params={"sensor_id": id_a})

    assert response.status_code == 200
    sensors = response.json()["sensors"]
    assert len(sensors) == 1
    assert sensors[0]["sensor_id"] == id_a
    assert sensors[0]["mac"] == "AA:BB:CC:DD:EE:FF"


def test_latest_sensor_id_filter_unknown_uuid_returns_empty(authed_client):
    response = authed_client.get(
        "/sensors/latest",
        params={"sensor_id": "00000000-0000-0000-0000-000000000000"},
    )

    assert response.status_code == 200
    assert response.json() == {"sensors": []}


def test_latest_sensor_id_filter_works_with_api_key_auth(client, api_key):
    sensor_id = _post_and_get_sensor_id(client, api_key, "AA:BB:CC:DD:EE:FF", "Sensor A")

    response = client.get(
        "/sensors/latest",
        headers={"x-api-key": api_key},
        params={"sensor_id": sensor_id},
    )

    assert response.status_code == 200
    sensors = response.json()["sensors"]
    assert len(sensors) == 1
    assert sensors[0]["sensor_id"] == sensor_id


def test_latest_sensor_id_filter_works_with_session_auth(authed_client, api_key):
    sensor_id = _post_and_get_sensor_id(authed_client, api_key, "AA:BB:CC:DD:EE:FF", "Sensor A")

    response = authed_client.get("/sensors/latest", params={"sensor_id": sensor_id})

    assert response.status_code == 200
    sensors = response.json()["sensors"]
    assert len(sensors) == 1
    assert sensors[0]["sensor_id"] == sensor_id
