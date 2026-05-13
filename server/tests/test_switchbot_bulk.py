from uuid import uuid4

from models import SwitchbotReading
from tests.helpers import (
    make_switchbot_payload,
    make_xiaomi_payload,
    post_switchbot,
    post_switchbot_bulk,
    post_xiaomi,
    resolve_switchbot_sensor_id,
    get_sensor_id,
)


def bulk_reading(**overrides):
    payload = {
        "timestamp": "2026-04-21T18:00:00Z",
        "temperature_c": 21.5,
        "humidity_pct": 48.0,
    }
    payload.update(overrides)
    return payload


def test_switchbot_bulk_inserts_multiple_readings(client, api_key, db_session):
    sensor_id = resolve_switchbot_sensor_id(client, api_key)

    response = post_switchbot_bulk(
        client,
        api_key,
        sensor_id,
        [
            bulk_reading(timestamp="2026-04-21T18:00:00Z"),
            bulk_reading(timestamp="2026-04-21T18:01:00Z", temperature_c=22.0),
        ],
    )

    assert response.status_code == 200
    assert response.json() == {
        "status": "ok",
        "received": 2,
        "created": 2,
        "duplicate": 0,
        "conflict": 0,
        "invalid": 0,
        "errors": [],
    }
    assert db_session.query(SwitchbotReading).count() == 2


def test_switchbot_bulk_is_idempotent_for_duplicate_upload(client, api_key):
    sensor_id = resolve_switchbot_sensor_id(client, api_key)
    readings = [
        bulk_reading(timestamp="2026-04-21T18:00:00Z"),
        bulk_reading(timestamp="2026-04-21T18:01:00Z", temperature_c=22.0),
    ]

    first = post_switchbot_bulk(client, api_key, sensor_id, readings)
    second = post_switchbot_bulk(client, api_key, sensor_id, readings)

    assert first.status_code == 200
    assert second.status_code == 200
    assert second.json() == {
        "status": "ok",
        "received": 2,
        "created": 0,
        "duplicate": 2,
        "conflict": 0,
        "invalid": 0,
        "errors": [],
    }


def test_switchbot_bulk_counts_conflicts_without_overwriting_existing_reading(
    client,
    api_key,
    db_session,
):
    original = make_switchbot_payload(timestamp="2026-04-21T18:00:00Z", temperature_c=21.5)
    post_switchbot(client, api_key, original)
    sensor_id = get_sensor_id(client, api_key, sensor_type="switchbot")

    response = post_switchbot_bulk(
        client,
        api_key,
        sensor_id,
        [bulk_reading(timestamp="2026-04-21T18:00:00Z", temperature_c=99.0)],
    )

    assert response.status_code == 200
    assert response.json()["created"] == 0
    assert response.json()["duplicate"] == 0
    assert response.json()["conflict"] == 1
    assert response.json()["invalid"] == 0
    assert response.json()["errors"] == [
        {
            "index": 0,
            "code": "conflict",
            "message": "conflicting reading ignored",
        }
    ]

    stored = db_session.query(SwitchbotReading).one()
    assert stored.temperature_c == original["temperature_c"]


def test_switchbot_bulk_partially_accepts_valid_rows_when_other_rows_are_invalid(client, api_key):
    sensor_id = resolve_switchbot_sensor_id(client, api_key)

    response = post_switchbot_bulk(
        client,
        api_key,
        sensor_id,
        [
            bulk_reading(timestamp="2026-04-21T18:00:00Z"),
            bulk_reading(timestamp="2026-04-21T18:01:00"),
            bulk_reading(timestamp="2026-04-21T18:02:00Z", temperature_c=-41.0),
            "not an object",
        ],
    )

    assert response.status_code == 200
    body = response.json()
    assert body["received"] == 4
    assert body["created"] == 1
    assert body["duplicate"] == 0
    assert body["conflict"] == 0
    assert body["invalid"] == 3
    assert [error["index"] for error in body["errors"]] == [1, 2, 3]


def test_switchbot_bulk_caps_error_details(client, api_key):
    sensor_id = resolve_switchbot_sensor_id(client, api_key)

    response = post_switchbot_bulk(
        client,
        api_key,
        sensor_id,
        [
            bulk_reading(timestamp="2026-04-21T18:00:00", temperature_c=20.0)
            for _ in range(25)
        ],
    )

    assert response.status_code == 200
    body = response.json()
    assert body["invalid"] == 25
    assert len(body["errors"]) == 20


def test_switchbot_bulk_rejects_unknown_sensor_id(client, api_key):
    response = post_switchbot_bulk(client, api_key, str(uuid4()), [bulk_reading()])

    assert response.status_code == 422
    assert response.json() == {"detail": "unknown sensor_id"}


def test_switchbot_bulk_rejects_non_switchbot_sensor_id(client, api_key):
    post_xiaomi(client, api_key, make_xiaomi_payload())
    sensor_id = get_sensor_id(client, api_key, sensor_type="xiaomi")

    response = post_switchbot_bulk(client, api_key, sensor_id, [bulk_reading()])

    assert response.status_code == 422
    assert response.json() == {"detail": "sensor_id is not a SwitchBot sensor"}


def test_switchbot_bulk_rejects_empty_readings(client, api_key):
    sensor_id = resolve_switchbot_sensor_id(client, api_key)

    response = post_switchbot_bulk(client, api_key, sensor_id, [])

    assert response.status_code == 422


def test_switchbot_bulk_rejects_more_than_configured_limit(client, api_key, app):
    app.state.config["switchbot_bulk_max_readings"] = 1
    sensor_id = resolve_switchbot_sensor_id(client, api_key)

    response = post_switchbot_bulk(
        client,
        api_key,
        sensor_id,
        [
            bulk_reading(timestamp="2026-04-21T18:00:00Z"),
            bulk_reading(timestamp="2026-04-21T18:01:00Z"),
        ],
    )

    assert response.status_code == 422
    assert response.json() == {"detail": "readings must contain at most 1 items"}
