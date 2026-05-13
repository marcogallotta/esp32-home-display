import logging
from uuid import UUID

from models import SWITCHBOT_TYPE, Sensor
from tests.helpers import (
    make_switchbot_payload,
    make_xiaomi_payload,
    post_switchbot,
    post_switchbot_sensors,
    post_xiaomi,
)


def test_switchbot_sensors_creates_unknown_sensor(client, api_key, db_session):
    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "aa:bb:cc:dd:ee:ff"}],
    )

    assert response.status_code == 200
    body = response.json()
    assert body["warnings"] == []
    assert len(body["sensors"]) == 1

    sensor = body["sensors"][0]
    assert sensor["mac"] == "AA:BB:CC:DD:EE:FF"
    assert UUID(sensor["sensor_id"])
    assert sensor["first_timestamp"] is None
    assert sensor["latest_timestamp"] is None
    assert sensor["sync_intervals"] == []
    assert sensor["sync_intervals_capped"] is False

    rows = db_session.query(Sensor).all()
    assert len(rows) == 1
    assert rows[0].mac == "AA:BB:CC:DD:EE:FF"
    assert rows[0].name == "AA:BB:CC:DD:EE:FF"
    assert rows[0].type == SWITCHBOT_TYPE


def test_switchbot_sensors_returns_existing_first_and_latest_timestamp(client, api_key):
    first = make_switchbot_payload(timestamp="2026-04-21T18:00:00Z")
    second = make_switchbot_payload(timestamp="2026-04-21T18:15:00Z")

    post_switchbot(client, api_key, first)
    post_switchbot(client, api_key, second)

    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "aa:bb:cc:dd:ee:ff"}],
    )

    assert response.status_code == 200
    sensor = response.json()["sensors"][0]
    assert sensor["first_timestamp"] == first["timestamp"]
    assert sensor["latest_timestamp"] == second["timestamp"]
    assert sensor["sync_intervals"] == []


def test_switchbot_sensors_returns_internal_sync_intervals_for_large_gaps(client, api_key):
    post_switchbot(client, api_key, make_switchbot_payload(timestamp="2026-04-21T18:00:00Z"))
    post_switchbot(client, api_key, make_switchbot_payload(timestamp="2026-04-21T18:15:00Z"))
    post_switchbot(client, api_key, make_switchbot_payload(timestamp="2026-04-21T18:40:00Z"))
    post_switchbot(client, api_key, make_switchbot_payload(timestamp="2026-04-21T19:00:00Z"))

    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "aa:bb:cc:dd:ee:ff"}],
    )

    assert response.status_code == 200
    sensor = response.json()["sensors"][0]
    assert sensor["sync_intervals"] == [
        {
            "start": "2026-04-21T18:15:00Z",
            "end": "2026-04-21T18:40:00Z",
        },
        {
            "start": "2026-04-21T18:40:00Z",
            "end": "2026-04-21T19:00:00Z",
        },
    ]


def test_switchbot_sensors_uses_configured_gap_threshold(client, api_key, app):
    app.state.config.switchbot_sync_gap_threshold_minutes = 30
    post_switchbot(client, api_key, make_switchbot_payload(timestamp="2026-04-21T18:00:00Z"))
    post_switchbot(client, api_key, make_switchbot_payload(timestamp="2026-04-21T18:25:00Z"))
    post_switchbot(client, api_key, make_switchbot_payload(timestamp="2026-04-21T18:55:00Z"))

    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "AA:BB:CC:DD:EE:FF"}],
    )

    assert response.status_code == 200
    assert response.json()["sensors"][0]["sync_intervals"] == [
        {
            "start": "2026-04-21T18:25:00Z",
            "end": "2026-04-21T18:55:00Z",
        }
    ]


def test_switchbot_sensors_caps_sync_intervals_per_sensor(client, api_key, app, caplog):
    app.state.config.switchbot_sync_max_intervals_per_sensor = 2
    caplog.set_level(logging.WARNING)

    for timestamp in [
        "2026-04-21T18:00:00Z",
        "2026-04-21T18:30:00Z",
        "2026-04-21T19:00:00Z",
        "2026-04-21T19:30:00Z",
    ]:
        post_switchbot(client, api_key, make_switchbot_payload(timestamp=timestamp))

    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "AA:BB:CC:DD:EE:FF"}],
    )

    assert response.status_code == 200
    body = response.json()
    assert len(body["sensors"][0]["sync_intervals"]) == 2
    assert body["sensors"][0]["sync_intervals_capped"] is True
    assert body["warnings"] == [
        {
            "code": "sync_intervals_capped",
            "message": (
                "sync intervals were capped; client should upload returned "
                "intervals and request /switchbot/sensors again"
            ),
        }
    ]
    assert "switchbot_sync_intervals_capped" in caplog.text


def test_switchbot_sensors_caps_sync_intervals_across_response(client, api_key, app):
    app.state.config.switchbot_sync_max_intervals_per_sensor = 10
    app.state.config.switchbot_sync_max_intervals_total = 1

    for mac in ["AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66"]:
        post_switchbot(
            client,
            api_key,
            make_switchbot_payload(mac=mac, timestamp="2026-04-21T18:00:00Z"),
        )
        post_switchbot(
            client,
            api_key,
            make_switchbot_payload(mac=mac, timestamp="2026-04-21T18:30:00Z"),
        )

    response = post_switchbot_sensors(
        client,
        api_key,
        [
            {"mac": "AA:BB:CC:DD:EE:FF"},
            {"mac": "11:22:33:44:55:66"},
        ],
    )

    assert response.status_code == 200
    body = response.json()
    assert len(body["sensors"][0]["sync_intervals"]) == 1
    assert body["sensors"][0]["sync_intervals_capped"] is False
    assert body["sensors"][1]["sync_intervals"] == []
    assert body["sensors"][1]["sync_intervals_capped"] is True
    assert body["warnings"][0]["code"] == "sync_intervals_capped"


def test_switchbot_sensors_is_idempotent(client, api_key, db_session):
    first = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "AA:BB:CC:DD:EE:FF"}],
    )
    second = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "aa:bb:cc:dd:ee:ff"}],
    )

    assert first.status_code == 200
    assert second.status_code == 200
    assert first.json()["sensors"][0]["sensor_id"] == second.json()["sensors"][0]["sensor_id"]
    assert db_session.query(Sensor).count() == 1


def test_switchbot_sensors_placeholder_allows_later_name_assignment(client, api_key, db_session):
    created = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "AA:BB:CC:DD:EE:FF"}],
    )
    reading = post_switchbot(
        client,
        api_key,
        make_switchbot_payload(name="Bedroom"),
    )

    assert created.status_code == 200
    assert reading.status_code == 200
    assert reading.json() == {"result": "created", "warnings": []}
    assert db_session.query(Sensor).one().name == "Bedroom"


def test_switchbot_sensors_rejects_invalid_mac(client, api_key):
    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "AA-BB-CC-DD-EE-FF"}],
    )

    assert response.status_code == 400
    assert response.json() == {"detail": "invalid mac format"}


def test_switchbot_sensors_rejects_cross_type_mac(client, api_key):
    post_xiaomi(client, api_key, make_xiaomi_payload())

    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "AA:BB:CC:DD:EE:FF"}],
    )

    assert response.status_code == 400
    assert response.json() == {"detail": "sensor type does not match existing sensor"}
