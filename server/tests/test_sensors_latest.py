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


def test_latest_mac_filter(authed_client, api_key):
    post_switchbot(authed_client, api_key, make_switchbot_payload(
        mac="AA:BB:CC:DD:EE:FF", name="Sensor A", timestamp="2026-04-21T10:00:00Z",
    ))
    post_switchbot(authed_client, api_key, make_switchbot_payload(
        mac="11:22:33:44:55:66", name="Sensor B", timestamp="2026-04-21T10:00:00Z",
    ))

    response = authed_client.get("/sensors/latest", params={"mac": "AA:BB:CC:DD:EE:FF"})

    assert response.status_code == 200
    sensors = response.json()["sensors"]
    assert len(sensors) == 1
    assert sensors[0]["mac"] == "AA:BB:CC:DD:EE:FF"


def test_latest_mac_filter_multiple(authed_client, api_key):
    for mac, name in [
        ("AA:BB:CC:DD:EE:FF", "A"),
        ("11:22:33:44:55:66", "B"),
        ("AA:BB:CC:DD:EE:00", "C"),
    ]:
        post_switchbot(authed_client, api_key, make_switchbot_payload(
            mac=mac, name=name, timestamp="2026-04-21T10:00:00Z",
        ))

    response = authed_client.get(
        "/sensors/latest",
        params=[("mac", "AA:BB:CC:DD:EE:FF"), ("mac", "11:22:33:44:55:66")],
    )

    assert response.status_code == 200
    macs = {s["mac"] for s in response.json()["sensors"]}
    assert macs == {"AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66"}


def test_latest_mac_filter_invalid_format(authed_client):
    response = authed_client.get("/sensors/latest", params={"mac": "not-a-mac"})

    assert response.status_code == 400


def test_latest_mac_filter_normalizes_case(authed_client, api_key):
    post_switchbot(authed_client, api_key, make_switchbot_payload(
        mac="AA:BB:CC:DD:EE:FF", name="Sensor A", timestamp="2026-04-21T10:00:00Z",
    ))

    response = authed_client.get("/sensors/latest", params={"mac": "aa:bb:cc:dd:ee:ff"})

    assert response.status_code == 200
    assert len(response.json()["sensors"]) == 1


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
