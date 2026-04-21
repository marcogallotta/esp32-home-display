import pytest


def make_payload(**overrides):
    payload = {
        "mac": "AA:BB:CC:DD:EE:FF",
        "name": "ficus",
        "timestamp": "2026-04-21T18:00:00Z",
        "temperature_c": 21.5,
    }
    payload.update(overrides)
    return payload


def post_xiaomi(client, api_key, payload):
    return client.post(
        "/xiaomi/reading",
        headers={"x-api-key": api_key},
        json=payload,
    )


def test_xiaomi_create_accepts_partial_reading(client, api_key):
    response = post_xiaomi(
        client,
        api_key,
        make_payload(
            temperature_c=None,
            moisture_pct=35,
        ),
    )

    assert response.status_code == 200
    assert response.json() == {
        "status": "ok",
        "result": "created",
    }


def test_xiaomi_create_merges_complementary_partial_readings(client, api_key):
    first = post_xiaomi(
        client,
        api_key,
        make_payload(temperature_c=21.5),
    )
    second = post_xiaomi(
        client,
        api_key,
        make_payload(
            temperature_c=None,
            moisture_pct=35,
        ),
    )

    assert first.status_code == 200
    assert first.json() == {
        "status": "ok",
        "result": "created",
    }

    assert second.status_code == 200
    assert second.json() == {
        "status": "ok",
        "result": "merged",
    }

    fetch = client.get(
        "/xiaomi/readings",
        headers={"x-api-key": api_key},
        params={"mac": "AA:BB:CC:DD:EE:FF"},
    )

    assert fetch.status_code == 200
    assert fetch.json() == [
        {
            "timestamp": "2026-04-21T18:00:00Z",
            "temperature_c": 21.5,
            "moisture_pct": 35,
            "light_lux": None,
            "conductivity_us_cm": None,
        }
    ]


def test_xiaomi_create_returns_duplicate_for_identical_partial_reading(client, api_key):
    first = post_xiaomi(
        client,
        api_key,
        make_payload(
            temperature_c=None,
            moisture_pct=35,
        ),
    )
    second = post_xiaomi(
        client,
        api_key,
        make_payload(
            temperature_c=None,
            moisture_pct=35,
        ),
    )

    assert first.status_code == 200
    assert first.json() == {
        "status": "ok",
        "result": "created",
    }

    assert second.status_code == 200
    assert second.json() == {
        "status": "ok",
        "result": "duplicate",
    }


def test_xiaomi_create_returns_conflict_for_single_field_conflict(client, api_key):
    first = post_xiaomi(
        client,
        api_key,
        make_payload(temperature_c=21.5),
    )
    second = post_xiaomi(
        client,
        api_key,
        make_payload(temperature_c=22.5),
    )

    assert first.status_code == 200
    assert first.json() == {
        "status": "ok",
        "result": "created",
    }

    assert second.status_code == 200
    assert second.json() == {
        "status": "ok",
        "result": "conflict",
        "warnings": [
            {
                "code": "conflicting_field_ignored",
                "field": "temperature_c",
                "existing": 21.5,
                "incoming": 22.5,
            }
        ],
    }


def test_xiaomi_create_returns_conflict_when_conflict_and_new_data_are_mixed(client, api_key):
    first = post_xiaomi(
        client,
        api_key,
        make_payload(temperature_c=21.5),
    )
    second = post_xiaomi(
        client,
        api_key,
        make_payload(
            temperature_c=22.5,
            moisture_pct=35,
        ),
    )

    assert first.status_code == 200
    assert first.json() == {
        "status": "ok",
        "result": "created",
    }

    assert second.status_code == 200
    assert second.json() == {
        "status": "ok",
        "result": "conflict",
        "warnings": [
            {
                "code": "conflicting_field_ignored",
                "field": "temperature_c",
                "existing": 21.5,
                "incoming": 22.5,
            }
        ],
    }

    fetch = client.get(
        "/xiaomi/readings",
        headers={"x-api-key": api_key},
        params={"mac": "AA:BB:CC:DD:EE:FF"},
    )

    assert fetch.status_code == 200
    assert fetch.json() == [
        {
            "timestamp": "2026-04-21T18:00:00Z",
            "temperature_c": 21.5,
            "moisture_pct": None,
            "light_lux": None,
            "conductivity_us_cm": None,
        }
    ]


@pytest.mark.parametrize(
    "payload",
    [
        make_payload(moisture_pct=-1, temperature_c=None),
        make_payload(moisture_pct=101, temperature_c=None),
    ],
    ids=[
        "moisture too low",
        "moisture too high",
    ],
)
def test_xiaomi_create_rejects_hard_range_violation_for_xiaomi_only_field(client, api_key, payload):
    response = post_xiaomi(client, api_key, payload)

    assert response.status_code == 422
