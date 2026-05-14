import pytest
from sqlalchemy import select

import switchbot as sb
import xiaomi as xm
from errors import BadRequestError
from models import SWITCHBOT_TYPE, XIAOMI_TYPE, Sensor
from service import fetch_readings
from tests.helpers import (
    get_sensor_id,
    get_sensor_readings,
    make_switchbot_payload,
    make_xiaomi_payload,
    post_switchbot,
    post_xiaomi,
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


def test_xiaomi_get_returns_basic_fetch(authed_client, api_key):
    payload = make_xiaomi_payload(temperature_c=None, moisture_pct=35)
    post_xiaomi(authed_client, api_key, payload)
    sensor_id = get_sensor_id(authed_client, sensor_type="xiaomi")

    response = get_sensor_readings(authed_client, sensor_id)

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


def test_xiaomi_get_normalizes_timestamp_to_utc(authed_client, api_key):
    payload = make_xiaomi_payload(timestamp="2026-04-21T20:00:00+02:00")
    post_xiaomi(authed_client, api_key, payload)
    sensor_id = get_sensor_id(authed_client, sensor_type="xiaomi")

    response = get_sensor_readings(authed_client, sensor_id)

    assert response.status_code == 200
    assert response.json()[0]["timestamp"] == "2026-04-21T18:00:00Z"


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


def test_fetch_readings_rejects_xiaomi_sensor_when_switchbot_expected(
    authed_client, api_key, db_session
):
    post_xiaomi(authed_client, api_key, make_xiaomi_payload())
    sensor = db_session.execute(
        select(Sensor).where(Sensor.type == XIAOMI_TYPE)
    ).scalar_one()

    with pytest.raises(BadRequestError):
        _call_fetch_readings(db_session, sensor.mac, SWITCHBOT_TYPE, sb.SENSOR)


def test_fetch_readings_rejects_switchbot_sensor_when_xiaomi_expected(
    authed_client, api_key, db_session
):
    post_switchbot(authed_client, api_key, make_switchbot_payload())
    sensor = db_session.execute(
        select(Sensor).where(Sensor.type == SWITCHBOT_TYPE)
    ).scalar_one()

    with pytest.raises(BadRequestError):
        _call_fetch_readings(db_session, sensor.mac, XIAOMI_TYPE, xm.SENSOR)


def test_fetch_readings_accepts_matching_switchbot_type(authed_client, api_key):
    post_switchbot(authed_client, api_key, make_switchbot_payload())
    sensor_id = get_sensor_id(authed_client, sensor_type="switchbot")

    response = get_sensor_readings(authed_client, sensor_id)

    assert response.status_code == 200
    assert len(response.json()) == 1


def test_fetch_readings_accepts_matching_xiaomi_type(authed_client, api_key):
    post_xiaomi(authed_client, api_key, make_xiaomi_payload())
    sensor_id = get_sensor_id(authed_client, sensor_type="xiaomi")

    response = get_sensor_readings(authed_client, sensor_id)

    assert response.status_code == 200
    assert len(response.json()) == 1
