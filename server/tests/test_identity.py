from tests.helpers import auth_headers, make_switchbot_payload, make_xiaomi_payload


def test_name_mismatch_is_rejected(client, api_key):
    first = client.post(
        "/switchbot/reading",
        headers=auth_headers(api_key),
        json=make_switchbot_payload(),
    )

    second = client.post(
        "/switchbot/reading",
        headers=auth_headers(api_key),
        json=make_switchbot_payload(
            name="location B",
            timestamp="2026-04-21T18:05:00Z",
        ),
    )

    assert first.status_code == 200
    assert first.json() == {"status": "ok", "result": "created"}

    assert second.status_code == 400
    assert second.json() == {"detail": "sensor name does not match existing sensor"}


def test_cross_sensor_type_mismatch_is_rejected(client, api_key):
    first = client.post(
        "/switchbot/reading",
        headers=auth_headers(api_key),
        json=make_switchbot_payload(),
    )

    second = client.post(
        "/xiaomi/reading",
        headers=auth_headers(api_key),
        json=make_xiaomi_payload(),
    )

    assert first.status_code == 200
    assert first.json() == {"status": "ok", "result": "created"}

    assert second.status_code == 400
    assert second.json() == {"detail": "sensor type does not match existing sensor"}
