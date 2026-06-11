from app.models import SwitchbotReading
from tests.helpers import make_switchbot_payload, post_switchbot


def test_switchbot_create_stores_row_correctly(client, api_key, db_session):
    payload = make_switchbot_payload()

    response = post_switchbot(client, api_key, payload)

    assert response.status_code == 200
    assert response.json()["result"] == "created"

    rows = db_session.query(SwitchbotReading).all()
    assert len(rows) == 1

    row = rows[0]
    assert row.mac == payload["mac"].upper()
    assert row.temperature_c == payload["temperature_c"]
    assert row.humidity_pct == payload["humidity_pct"]
    assert row.timestamp.isoformat() == payload["timestamp"].replace("Z", "+00:00")


def test_switchbot_conflict_does_not_write_conflicting_data(client, api_key, db_session):
    first_payload = make_switchbot_payload(temperature_c=21.5)
    second_payload = make_switchbot_payload(temperature_c=22.5)

    first = post_switchbot(client, api_key, first_payload)
    second = post_switchbot(client, api_key, second_payload)

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

    rows = db_session.query(SwitchbotReading).all()
    assert len(rows) == 1

    row = rows[0]
    assert row.mac == first_payload["mac"].upper()
    assert row.temperature_c == first_payload["temperature_c"]
    assert row.humidity_pct == first_payload["humidity_pct"]
    assert row.timestamp.isoformat() == first_payload["timestamp"].replace("Z", "+00:00")
