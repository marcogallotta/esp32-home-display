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
    assert len(body["sensors"]) == 1

    sensor = body["sensors"][0]
    assert sensor["mac"] == "AA:BB:CC:DD:EE:FF"
    assert UUID(sensor["sensor_id"])
    assert sensor["latest_timestamp"] is None

    rows = db_session.query(Sensor).all()
    assert len(rows) == 1
    assert rows[0].mac == "AA:BB:CC:DD:EE:FF"
    assert rows[0].name == "AA:BB:CC:DD:EE:FF"
    assert rows[0].type == SWITCHBOT_TYPE


def test_switchbot_sensors_returns_existing_latest_timestamp(client, api_key):
    first = make_switchbot_payload(timestamp="2026-04-21T18:00:00Z")
    second = make_switchbot_payload(timestamp="2026-04-21T18:31:00Z")

    post_switchbot(client, api_key, first)
    post_switchbot(client, api_key, second)

    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "aa:bb:cc:dd:ee:ff"}],
    )

    assert response.status_code == 200
    assert response.json()["sensors"][0]["latest_timestamp"] == second["timestamp"]


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
    assert reading.json() == {"status": "ok", "result": "created"}
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
