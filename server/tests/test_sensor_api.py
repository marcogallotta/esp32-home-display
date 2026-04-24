from tests.helpers import (
    auth_headers,
    make_switchbot_payload,
    make_xiaomi_payload,
    post_switchbot,
    post_xiaomi,
)


def test_get_sensors_lists_created_sensors(client, api_key):
    post_switchbot(client, api_key, make_switchbot_payload(name="Bedroom"))
    post_xiaomi(
        client,
        api_key,
        make_xiaomi_payload(name="Cilantro", mac="11:22:33:44:55:66"),
    )

    response = client.get("/sensors", headers=auth_headers(api_key))

    assert response.status_code == 200
    assert sorted((s["name"], s["type"]) for s in response.json()) == [
        ("Bedroom", "switchbot"),
        ("Cilantro", "xiaomi"),
    ]


def test_get_readings_by_sensor_id(client, api_key):
    payload = make_switchbot_payload()
    post_switchbot(client, api_key, payload)

    sensors = client.get("/sensors", headers=auth_headers(api_key)).json()
    sensor_id = sensors[0]["id"]

    response = client.get(
        f"/sensors/{sensor_id}/readings",
        headers=auth_headers(api_key),
    )

    assert response.status_code == 200
    assert response.json() == [
        {
            "timestamp": payload["timestamp"],
            "temperature_c": payload["temperature_c"],
            "humidity_pct": payload["humidity_pct"],
        }
    ]


def test_get_windowed_readings_by_sensor_id(client, api_key):
    first = make_switchbot_payload(timestamp="2026-04-21T18:00:00Z", temperature_c=20.0)
    second = make_switchbot_payload(timestamp="2026-04-21T18:01:00Z", temperature_c=21.0)
    third = make_switchbot_payload(timestamp="2026-04-21T18:02:00Z", temperature_c=22.0)

    post_switchbot(client, api_key, first)
    post_switchbot(client, api_key, second)
    post_switchbot(client, api_key, third)

    sensor_id = client.get("/sensors", headers=auth_headers(api_key)).json()[0]["id"]

    response = client.get(
        f"/sensors/{sensor_id}/readings",
        headers=auth_headers(api_key),
        params={
            "start_ts": "2026-04-21T18:00:00Z",
            "end_ts": "2026-04-21T18:02:00Z",
            "max_points": 5000,
        },
    )

    assert response.status_code == 200
    assert [row["timestamp"] for row in response.json()] == [
        third["timestamp"],
        second["timestamp"],
        first["timestamp"],
    ]
