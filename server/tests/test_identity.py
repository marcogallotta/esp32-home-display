import logging

from app.models import XIAOMI_TYPE, Sensor
from tests.helpers import make_switchbot_payload, post_switchbot


def test_name_change_succeeds_and_updates_sensor(authed_client, api_key, caplog):
    post_switchbot(authed_client, api_key, make_switchbot_payload())

    with caplog.at_level(logging.WARNING, logger="service"):
        second = post_switchbot(
            authed_client,
            api_key,
            make_switchbot_payload(
                name="location B",
                timestamp="2026-04-21T18:05:00Z",
            ),
        )

    assert second.status_code == 200
    assert second.json() == {"result": "created", "warnings": []}

    sensors = authed_client.get("/sensors").json()
    names = [s["name"] for s in sensors]
    assert "location B" in names
    assert "location A" not in names

    warnings = [r for r in caplog.records if r.levelno == logging.WARNING]
    assert len(warnings) == 1
    assert "location A" in warnings[0].message
    assert "location B" in warnings[0].message


def test_cross_sensor_type_mismatch_is_rejected(client, api_key, db_session):
    db_session.add(Sensor(mac="AA:BB:CC:DD:EE:FF", name="Cilantro", type=XIAOMI_TYPE))
    db_session.commit()

    response = post_switchbot(client, api_key, make_switchbot_payload())

    assert response.status_code == 400
    assert response.json() == {"detail": "sensor type does not match existing sensor"}
