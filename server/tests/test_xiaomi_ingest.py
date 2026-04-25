import pytest

from tests.helpers import (
    get_sensor_id,
    get_sensor_readings,
    make_xiaomi_payload,
    post_xiaomi,
)


def test_xiaomi_create_accepts_partial_reading(client, api_key):
    payload = make_xiaomi_payload(
        temperature_c=None,
        moisture_pct=35,
    )

    response = post_xiaomi(client, api_key, payload)

    assert response.status_code == 200
    assert response.json()["result"] == "created"


def test_xiaomi_create_merges_complementary_partial_readings(client, api_key):
    first_payload = make_xiaomi_payload(temperature_c=21.5)
    second_payload = make_xiaomi_payload(
        temperature_c=None,
        moisture_pct=35,
    )

    first = post_xiaomi(client, api_key, first_payload)
    second = post_xiaomi(client, api_key, second_payload)

    assert first.status_code == 200
    assert first.json()["result"] == "created"

    assert second.status_code == 200
    assert second.json()["result"] == "merged"

    sensor_id = get_sensor_id(client, api_key, sensor_type="xiaomi")
    fetch = get_sensor_readings(client, api_key, sensor_id)

    assert fetch.status_code == 200
    assert fetch.json() == [
        {
            "timestamp": first_payload["timestamp"],
            "temperature_c": first_payload["temperature_c"],
            "moisture_pct": second_payload["moisture_pct"],
            "light_lux": first_payload.get("light_lux"),
            "conductivity_us_cm": first_payload.get("conductivity_us_cm"),
        }
    ]


def test_xiaomi_create_returns_duplicate_for_identical_partial_reading(client, api_key):
    payload = make_xiaomi_payload(
        temperature_c=None,
        moisture_pct=35,
    )

    first = post_xiaomi(client, api_key, payload)
    second = post_xiaomi(client, api_key, payload)

    assert first.status_code == 200
    assert first.json()["result"] == "created"

    assert second.status_code == 200
    assert second.json()["result"] == "duplicate"


def test_xiaomi_create_returns_conflict_for_single_field_conflict(client, api_key):
    first_payload = make_xiaomi_payload(temperature_c=21.5)
    second_payload = make_xiaomi_payload(temperature_c=22.5)

    first = post_xiaomi(client, api_key, first_payload)
    second = post_xiaomi(client, api_key, second_payload)

    assert first.status_code == 200
    assert first.json()["result"] == "created"

    assert second.status_code == 200
    assert second.json()["result"] == "conflict"
    assert second.json()["warnings"] == [
        {
            "code": "conflicting_field_ignored",
            "field": "temperature_c",
            "existing": first_payload["temperature_c"],
            "incoming": second_payload["temperature_c"],
        }
    ]


def test_xiaomi_create_returns_merged_with_conflict_when_conflict_and_new_data_are_mixed(client, api_key):
    first_payload = make_xiaomi_payload(temperature_c=21.5)
    second_payload = make_xiaomi_payload(
        temperature_c=22.5,
        moisture_pct=35,
    )

    first = post_xiaomi(client, api_key, first_payload)
    second = post_xiaomi(client, api_key, second_payload)

    assert first.status_code == 200
    assert first.json()["result"] == "created"

    assert second.status_code == 200
    assert second.json()["result"] == "merged_with_conflict"
    assert second.json()["warnings"] == [
        {
            "code": "conflicting_field_ignored",
            "field": "temperature_c",
            "existing": first_payload["temperature_c"],
            "incoming": second_payload["temperature_c"],
        }
    ]

    sensor_id = get_sensor_id(client, api_key, sensor_type="xiaomi")
    fetch = get_sensor_readings(client, api_key, sensor_id)

    assert fetch.status_code == 200
    assert fetch.json() == [
        {
            "timestamp": first_payload["timestamp"],
            "temperature_c": first_payload["temperature_c"],
            "moisture_pct": second_payload["moisture_pct"],
            "light_lux": first_payload.get("light_lux"),
            "conductivity_us_cm": first_payload.get("conductivity_us_cm"),
        }
    ]


@pytest.mark.parametrize(
    "payload",
    [
        make_xiaomi_payload(moisture_pct=-1, temperature_c=None),
        make_xiaomi_payload(moisture_pct=101, temperature_c=None),
    ],
    ids=[
        "moisture too low",
        "moisture too high",
    ],
)
def test_xiaomi_create_rejects_hard_range_violation_for_xiaomi_only_field(client, api_key, payload):
    response = post_xiaomi(client, api_key, payload)

    assert response.status_code == 422
