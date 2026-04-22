from models import SwitchbotReading, XiaomiReading
from tests.helpers import make_switchbot_payload, make_xiaomi_payload, post_switchbot, post_xiaomi


def test_switchbot_create_stores_row_correctly(client, api_key, db_session):
    response = post_switchbot(
        client,
        api_key,
        make_switchbot_payload(),
    )

    assert response.status_code == 200
    assert response.json() == {"status": "ok", "result": "created"}

    rows = db_session.query(SwitchbotReading).all()
    assert len(rows) == 1

    row = rows[0]
    assert row.mac == "AA:BB:CC:DD:EE:FF"
    assert row.temperature_c == 21.5
    assert row.humidity_pct == 48.0
    assert row.timestamp.isoformat() == "2026-04-21T18:00:00+00:00"


def test_xiaomi_merge_stores_merged_fields(client, api_key, db_session):
    first = post_xiaomi(
        client,
        api_key,
        make_xiaomi_payload(),
    )
    second = post_xiaomi(
        client,
        api_key,
        make_xiaomi_payload(
            temperature_c=None,
            moisture_pct=35,
        ),
    )

    assert first.status_code == 200
    assert first.json() == {"status": "ok", "result": "created"}

    assert second.status_code == 200
    assert second.json() == {"status": "ok", "result": "merged"}

    rows = db_session.query(XiaomiReading).all()
    assert len(rows) == 1

    row = rows[0]
    assert row.mac == "AA:BB:CC:DD:EE:FF"
    assert row.temperature_c == 21.5
    assert row.moisture_pct == 35
    assert row.light_lux is None
    assert row.conductivity_us_cm is None
    assert row.timestamp.isoformat() == "2026-04-21T18:00:00+00:00"


def test_xiaomi_conflict_does_not_write_conflicting_or_new_mixed_in_data(client, api_key, db_session):
    first = post_xiaomi(
        client,
        api_key,
        make_xiaomi_payload(),
    )
    second = post_xiaomi(
        client,
        api_key,
        make_xiaomi_payload(
            temperature_c=22.5,
            moisture_pct=35,
        ),
    )

    assert first.status_code == 200
    assert first.json() == {"status": "ok", "result": "created"}

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

    rows = db_session.query(XiaomiReading).all()
    assert len(rows) == 1

    row = rows[0]
    assert row.temperature_c == 21.5
    assert row.moisture_pct is None
    assert row.light_lux is None
    assert row.conductivity_us_cm is None


def test_xiaomi_duplicate_does_not_create_extra_rows(client, api_key, db_session):
    first = post_xiaomi(
        client,
        api_key,
        make_xiaomi_payload(
            temperature_c=None,
            moisture_pct=35,
        ),
    )
    second = post_xiaomi(
        client,
        api_key,
        make_xiaomi_payload(
            temperature_c=None,
            moisture_pct=35,
        ),
    )

    assert first.status_code == 200
    assert first.json() == {"status": "ok", "result": "created"}

    assert second.status_code == 200
    assert second.json() == {"status": "ok", "result": "duplicate"}

    rows = db_session.query(XiaomiReading).all()
    assert len(rows) == 1

    row = rows[0]
    assert row.temperature_c is None
    assert row.moisture_pct == 35
