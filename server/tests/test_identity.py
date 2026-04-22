from tests.helpers import make_switchbot_payload, make_xiaomi_payload, post_switchbot, post_xiaomi


def test_name_mismatch_is_rejected(client, api_key):
    first = post_switchbot(
        client,
        api_key,
        make_switchbot_payload(),
    )

    second = post_switchbot(
        client,
        api_key,
        make_switchbot_payload(
            name="location B",
            timestamp="2026-04-21T18:05:00Z",
        ),
    )

    assert first.status_code == 200
    assert first.json() == {"status": "ok", "result": "created"}

    assert second.status_code == 400
    assert second.json() == {"detail": "sensor name does not match existing sensor"}


def test_cross_sensor_type_mismatch_is_rejected(client, api_key):
    first = post_switchbot(
        client,
        api_key,
        make_switchbot_payload(),
    )

    second = post_xiaomi(
        client,
        api_key,
        make_xiaomi_payload(),
    )

    assert first.status_code == 200
    assert first.json() == {"status": "ok", "result": "created"}

    assert second.status_code == 400
    assert second.json() == {"detail": "sensor type does not match existing sensor"}
