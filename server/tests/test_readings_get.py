from tests.helpers import (
    auth_headers,
    get_sensor_id,
    get_sensor_readings,
    make_switchbot_payload,
    make_xiaomi_payload,
    post_switchbot,
    post_xiaomi,
)


def test_switchbot_get_returns_basic_fetch(client, api_key):
    payload = make_switchbot_payload()
    post_switchbot(client, api_key, payload)
    sensor_id = get_sensor_id(client, api_key, sensor_type="switchbot")

    response = get_sensor_readings(client, api_key, sensor_id)

    assert response.status_code == 200
    assert response.json() == [
        {
            "timestamp": payload["timestamp"],
            "temperature_c": payload["temperature_c"],
            "humidity_pct": payload["humidity_pct"],
        }
    ]


def test_switchbot_get_returns_descending_timestamp_order(client, api_key):
    first = make_switchbot_payload(timestamp="2026-04-21T18:00:00Z", temperature_c=21.5)
    second = make_switchbot_payload(timestamp="2026-04-21T18:10:00Z", temperature_c=22.5)
    third = make_switchbot_payload(timestamp="2026-04-21T18:05:00Z", temperature_c=23.5)

    post_switchbot(client, api_key, first)
    post_switchbot(client, api_key, second)
    post_switchbot(client, api_key, third)
    sensor_id = get_sensor_id(client, api_key, sensor_type="switchbot")

    response = get_sensor_readings(client, api_key, sensor_id)

    assert response.status_code == 200
    assert [row["timestamp"] for row in response.json()] == [
        second["timestamp"],
        third["timestamp"],
        first["timestamp"],
    ]


def test_switchbot_get_respects_limit(client, api_key):
    first = make_switchbot_payload(timestamp="2026-04-21T18:00:00Z", temperature_c=21.5)
    second = make_switchbot_payload(timestamp="2026-04-21T18:10:00Z", temperature_c=22.5)
    third = make_switchbot_payload(timestamp="2026-04-21T18:05:00Z", temperature_c=23.5)

    post_switchbot(client, api_key, first)
    post_switchbot(client, api_key, second)
    post_switchbot(client, api_key, third)
    sensor_id = get_sensor_id(client, api_key, sensor_type="switchbot")

    response = get_sensor_readings(client, api_key, sensor_id, {"limit": 2})

    assert response.status_code == 200
    assert len(response.json()) == 2
    assert [row["timestamp"] for row in response.json()] == [
        second["timestamp"],
        third["timestamp"],
    ]


def test_switchbot_get_rejects_limit_too_large(client, api_key):
    sensor_id = "00000000-0000-0000-0000-000000000000"

    response = get_sensor_readings(client, api_key, sensor_id, {"limit": 101})

    assert response.status_code == 422


def test_switchbot_get_rejects_negative_limit(client, api_key):
    sensor_id = "00000000-0000-0000-0000-000000000000"

    response = get_sensor_readings(client, api_key, sensor_id, {"limit": -1})

    assert response.status_code == 422


def test_switchbot_get_rejects_after_greater_than_before(client, api_key):
    payload = make_switchbot_payload()
    post_switchbot(client, api_key, payload)
    sensor_id = get_sensor_id(client, api_key, sensor_type="switchbot")

    response = get_sensor_readings(
        client,
        api_key,
        sensor_id,
        {
            "after": "2026-04-21T18:10:00Z",
            "before": "2026-04-21T18:05:00Z",
        },
    )

    assert response.status_code == 400
    assert response.json() == {"detail": "after must be <= before"}


def test_switchbot_get_respects_before_and_after(client, api_key):
    first = make_switchbot_payload(timestamp="2026-04-21T18:00:00Z", temperature_c=21.5)
    middle = make_switchbot_payload(timestamp="2026-04-21T18:05:00Z", temperature_c=22.5)
    last = make_switchbot_payload(timestamp="2026-04-21T18:10:00Z", temperature_c=23.5)

    post_switchbot(client, api_key, first)
    post_switchbot(client, api_key, middle)
    post_switchbot(client, api_key, last)
    sensor_id = get_sensor_id(client, api_key, sensor_type="switchbot")

    response = get_sensor_readings(
        client,
        api_key,
        sensor_id,
        {
            "after": "2026-04-21T18:02:00Z",
            "before": "2026-04-21T18:08:00Z",
        },
    )

    assert response.status_code == 200
    assert response.json() == [
        {
            "timestamp": middle["timestamp"],
            "temperature_c": middle["temperature_c"],
            "humidity_pct": middle["humidity_pct"],
        }
    ]


def test_switchbot_get_returns_empty_list_for_unknown_sensor(client, api_key):
    response = get_sensor_readings(
        client,
        api_key,
        "00000000-0000-0000-0000-000000000000",
    )

    assert response.status_code == 200
    assert response.json() == []


def test_switchbot_get_normalizes_timestamp_to_utc(client, api_key):
    payload = make_switchbot_payload(timestamp="2026-04-21T20:00:00+02:00")
    post_switchbot(client, api_key, payload)
    sensor_id = get_sensor_id(client, api_key, sensor_type="switchbot")

    response = get_sensor_readings(client, api_key, sensor_id)

    assert response.status_code == 200
    assert response.json() == [
        {
            "timestamp": "2026-04-21T18:00:00Z",
            "temperature_c": payload["temperature_c"],
            "humidity_pct": payload["humidity_pct"],
        }
    ]


def test_xiaomi_get_returns_basic_fetch(client, api_key):
    payload = make_xiaomi_payload(temperature_c=None, moisture_pct=35)
    post_xiaomi(client, api_key, payload)
    sensor_id = get_sensor_id(client, api_key, sensor_type="xiaomi")

    response = get_sensor_readings(client, api_key, sensor_id)

    assert response.status_code == 200
    assert response.json() == [
        {
            "timestamp": payload["timestamp"],
            "temperature_c": payload["temperature_c"],
            "moisture_pct": payload["moisture_pct"],
            "light_lux": payload.get("light_lux"),
            "conductivity_us_cm": payload.get("conductivity_us_cm"),
        }
    ]
