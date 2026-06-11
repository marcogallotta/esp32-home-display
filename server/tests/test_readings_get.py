import pytest

from app import switchbot as sb
from app.errors import BadRequestError
from app.models import SWITCHBOT_TYPE, XIAOMI_TYPE, Sensor
from app.service import fetch_readings
from tests.helpers import (
    get_sensor_id,
    get_sensor_readings,
    make_switchbot_payload,
    post_switchbot,
)


def test_switchbot_get_returns_basic_fetch(authed_client, api_key):
    payload = make_switchbot_payload()
    post_switchbot(authed_client, api_key, payload)
    sensor_id = get_sensor_id(authed_client, sensor_type="switchbot")

    response = get_sensor_readings(authed_client, sensor_id)

    assert response.status_code == 200
    assert response.json() == [
        {
            "timestamp": payload["timestamp"],
            "temperature_c": payload["temperature_c"],
            "humidity_pct": payload["humidity_pct"],
        }
    ]


def test_switchbot_get_returns_descending_timestamp_order(authed_client, api_key):
    first = make_switchbot_payload(timestamp="2026-04-21T18:00:00Z", temperature_c=21.5)
    second = make_switchbot_payload(timestamp="2026-04-21T18:10:00Z", temperature_c=22.5)
    third = make_switchbot_payload(timestamp="2026-04-21T18:05:00Z", temperature_c=23.5)

    post_switchbot(authed_client, api_key, first)
    post_switchbot(authed_client, api_key, second)
    post_switchbot(authed_client, api_key, third)
    sensor_id = get_sensor_id(authed_client, sensor_type="switchbot")

    response = get_sensor_readings(authed_client, sensor_id)

    assert response.status_code == 200
    assert [row["timestamp"] for row in response.json()] == [
        second["timestamp"],
        third["timestamp"],
        first["timestamp"],
    ]


def test_switchbot_get_respects_limit(authed_client, api_key):
    first = make_switchbot_payload(timestamp="2026-04-21T18:00:00Z", temperature_c=21.5)
    second = make_switchbot_payload(timestamp="2026-04-21T18:10:00Z", temperature_c=22.5)
    third = make_switchbot_payload(timestamp="2026-04-21T18:05:00Z", temperature_c=23.5)

    post_switchbot(authed_client, api_key, first)
    post_switchbot(authed_client, api_key, second)
    post_switchbot(authed_client, api_key, third)
    sensor_id = get_sensor_id(authed_client, sensor_type="switchbot")

    response = get_sensor_readings(authed_client, sensor_id, {"limit": 2})

    assert response.status_code == 200
    assert len(response.json()) == 2
    assert [row["timestamp"] for row in response.json()] == [
        second["timestamp"],
        third["timestamp"],
    ]


def test_switchbot_get_rejects_limit_too_large(authed_client):
    sensor_id = "00000000-0000-0000-0000-000000000000"

    response = get_sensor_readings(authed_client, sensor_id, {"limit": 101})

    assert response.status_code == 422


def test_switchbot_get_rejects_negative_limit(authed_client):
    sensor_id = "00000000-0000-0000-0000-000000000000"

    response = get_sensor_readings(authed_client, sensor_id, {"limit": -1})

    assert response.status_code == 422


def test_switchbot_get_rejects_after_greater_than_before(authed_client, api_key):
    payload = make_switchbot_payload()
    post_switchbot(authed_client, api_key, payload)
    sensor_id = get_sensor_id(authed_client, sensor_type="switchbot")

    response = get_sensor_readings(
        authed_client,
        sensor_id,
        {
            "after": "2026-04-21T18:10:00Z",
            "before": "2026-04-21T18:05:00Z",
        },
    )

    assert response.status_code == 400
    assert response.json() == {"detail": "after must be <= before"}


def test_switchbot_get_respects_before_and_after(authed_client, api_key):
    first = make_switchbot_payload(timestamp="2026-04-21T18:00:00Z", temperature_c=21.5)
    middle = make_switchbot_payload(timestamp="2026-04-21T18:05:00Z", temperature_c=22.5)
    last = make_switchbot_payload(timestamp="2026-04-21T18:10:00Z", temperature_c=23.5)

    post_switchbot(authed_client, api_key, first)
    post_switchbot(authed_client, api_key, middle)
    post_switchbot(authed_client, api_key, last)
    sensor_id = get_sensor_id(authed_client, sensor_type="switchbot")

    response = get_sensor_readings(
        authed_client,
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


def test_switchbot_get_returns_empty_list_for_unknown_sensor(authed_client):
    response = get_sensor_readings(
        authed_client,
        "00000000-0000-0000-0000-000000000000",
    )

    assert response.status_code == 200
    assert response.json() == []


def test_switchbot_get_normalizes_timestamp_to_utc(authed_client, api_key):
    payload = make_switchbot_payload(timestamp="2026-04-21T20:00:00+02:00")
    post_switchbot(authed_client, api_key, payload)
    sensor_id = get_sensor_id(authed_client, sensor_type="switchbot")

    response = get_sensor_readings(authed_client, sensor_id)

    assert response.status_code == 200
    assert response.json() == [
        {
            "timestamp": "2026-04-21T18:00:00Z",
            "temperature_c": payload["temperature_c"],
            "humidity_pct": payload["humidity_pct"],
        }
    ]


@pytest.mark.parametrize(
    ("param", "value"),
    [
        ("before", "2026-04-21T18:00:00"),
        ("after", "2026-04-21T18:00:00"),
        ("start_ts", "2026-04-21T18:00:00"),
        ("end_ts", "2026-04-21T18:00:00"),
    ],
    ids=["before", "after", "start_ts", "end_ts"],
)
def test_get_rejects_naive_query_timestamp(authed_client, api_key, param, value):
    payload = make_switchbot_payload()
    post_switchbot(authed_client, api_key, payload)
    sensor_id = get_sensor_id(authed_client, sensor_type="switchbot")

    response = get_sensor_readings(authed_client, sensor_id, {param: value})

    assert response.status_code == 400
    assert param in response.json()["detail"]


def test_get_before_after_offset_timezone_interpreted_as_utc(authed_client, api_key):
    first = make_switchbot_payload(timestamp="2026-04-21T18:00:00Z", temperature_c=21.5)
    middle = make_switchbot_payload(timestamp="2026-04-21T18:05:00Z", temperature_c=22.5)
    last = make_switchbot_payload(timestamp="2026-04-21T18:10:00Z", temperature_c=23.5)

    post_switchbot(authed_client, api_key, first)
    post_switchbot(authed_client, api_key, middle)
    post_switchbot(authed_client, api_key, last)
    sensor_id = get_sensor_id(authed_client, sensor_type="switchbot")

    # 20:02+02:00 == 18:02Z, 20:08+02:00 == 18:08Z — should match only middle
    response = get_sensor_readings(
        authed_client,
        sensor_id,
        {
            "after": "2026-04-21T20:02:00+02:00",
            "before": "2026-04-21T20:08:00+02:00",
        },
    )

    assert response.status_code == 200
    assert len(response.json()) == 1
    assert response.json()[0]["timestamp"] == middle["timestamp"]


def test_get_start_ts_end_ts_offset_timezone_interpreted_as_utc(authed_client, api_key):
    first = make_switchbot_payload(timestamp="2026-04-21T18:00:00Z", temperature_c=21.5)
    middle = make_switchbot_payload(timestamp="2026-04-21T18:05:00Z", temperature_c=22.5)
    last = make_switchbot_payload(timestamp="2026-04-21T18:10:00Z", temperature_c=23.5)

    post_switchbot(authed_client, api_key, first)
    post_switchbot(authed_client, api_key, middle)
    post_switchbot(authed_client, api_key, last)
    sensor_id = get_sensor_id(authed_client, sensor_type="switchbot")

    # 20:02+02:00 == 18:02Z, 20:08+02:00 == 18:08Z — should match only middle
    response = get_sensor_readings(
        authed_client,
        sensor_id,
        {
            "start_ts": "2026-04-21T20:02:00+02:00",
            "end_ts": "2026-04-21T20:08:00+02:00",
        },
    )

    assert response.status_code == 200
    assert len(response.json()) == 1
    assert response.json()[0]["timestamp"] == middle["timestamp"]


def _call_fetch_readings(db, mac, expected_type, sensor):
    return fetch_readings(
        db=db,
        mac=mac,
        limit=100,
        before=None,
        after=None,
        start_ts=None,
        end_ts=None,
        max_points=None,
        sensor=sensor,
        expected_type=expected_type,
    )


def test_fetch_readings_rejects_sensor_when_type_mismatches(
    authed_client, api_key, db_session
):
    # A non-SwitchBot sensor row (the plant sensor is type 2) must be rejected
    # when SwitchBot readings are requested for it.
    db_session.add(Sensor(mac="11:22:33:44:55:66", name="Cilantro", type=XIAOMI_TYPE))
    db_session.commit()

    with pytest.raises(BadRequestError):
        _call_fetch_readings(db_session, "11:22:33:44:55:66", SWITCHBOT_TYPE, sb.SENSOR)


def test_plant_sensor_readings_empty_when_proxy_unconfigured(authed_client, api_key, db_session):
    # plant_monitor_url is unset in the test config, so a plant (type-2) sensor's
    # readings request must return [] rather than 500 (no SENSOR_SPECS entry).
    sensor = Sensor(mac="5C:85:7E:14:43:45", name="Cilantro", type=XIAOMI_TYPE)
    db_session.add(sensor)
    db_session.commit()

    response = get_sensor_readings(authed_client, str(sensor.id))

    assert response.status_code == 200
    assert response.json() == []


def test_fetch_readings_accepts_matching_switchbot_type(authed_client, api_key):
    post_switchbot(authed_client, api_key, make_switchbot_payload())
    sensor_id = get_sensor_id(authed_client, sensor_type="switchbot")

    response = get_sensor_readings(authed_client, sensor_id)

    assert response.status_code == 200
    assert len(response.json()) == 1
