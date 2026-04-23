from models import SwitchbotReading, XiaomiReading
from tests.helpers import make_switchbot_payload, make_xiaomi_payload, post_switchbot, post_xiaomi


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


def test_xiaomi_merge_stores_merged_fields(client, api_key, db_session):
    first_payload = make_xiaomi_payload()
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

    rows = db_session.query(XiaomiReading).all()
    assert len(rows) == 1

    row = rows[0]
    assert row.mac == first_payload["mac"].upper()
    assert row.temperature_c == first_payload["temperature_c"]
    assert row.moisture_pct == second_payload["moisture_pct"]
    assert row.light_lux == first_payload.get("light_lux")
    assert row.conductivity_us_cm == first_payload.get("conductivity_us_cm")
    assert row.timestamp.isoformat() == first_payload["timestamp"].replace("Z", "+00:00")


def test_xiaomi_conflict_does_not_write_conflicting_data(client, api_key, db_session):
    first_payload = make_xiaomi_payload()
    second_payload = make_xiaomi_payload(
        temperature_c=22.5,
    )

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

    rows = db_session.query(XiaomiReading).all()
    assert len(rows) == 1

    row = rows[0]
    assert row.mac == first_payload["mac"].upper()
    assert row.temperature_c == first_payload["temperature_c"]
    assert row.moisture_pct == first_payload.get("moisture_pct")
    assert row.light_lux == first_payload.get("light_lux")
    assert row.conductivity_us_cm == first_payload.get("conductivity_us_cm")
    assert row.timestamp.isoformat() == first_payload["timestamp"].replace("Z", "+00:00")


def test_xiaomi_conflict_merges_new_non_conflicting_data(client, api_key, db_session):
    first_payload = make_xiaomi_payload()
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

    rows = db_session.query(XiaomiReading).all()
    assert len(rows) == 1

    row = rows[0]
    assert row.mac == first_payload["mac"].upper()
    assert row.temperature_c == first_payload["temperature_c"]
    assert row.moisture_pct == second_payload["moisture_pct"]
    assert row.light_lux == first_payload.get("light_lux")
    assert row.conductivity_us_cm == first_payload.get("conductivity_us_cm")
    assert row.timestamp.isoformat() == first_payload["timestamp"].replace("Z", "+00:00")


def test_xiaomi_duplicate_does_not_create_extra_rows(client, api_key, db_session):
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

    rows = db_session.query(XiaomiReading).all()
    assert len(rows) == 1

    row = rows[0]
    assert row.mac == payload["mac"].upper()
    assert row.temperature_c == payload["temperature_c"]
    assert row.moisture_pct == payload["moisture_pct"]
    assert row.light_lux == payload.get("light_lux")
    assert row.conductivity_us_cm == payload.get("conductivity_us_cm")
    assert row.timestamp.isoformat() == payload["timestamp"].replace("Z", "+00:00")
