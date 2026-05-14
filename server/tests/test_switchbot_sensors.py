import logging
from datetime import datetime, timedelta, timezone
from uuid import UUID

from sqlalchemy.dialects.postgresql import insert as pg_insert

from app.models import SWITCHBOT_TYPE, Sensor, SwitchbotReading
from tests.helpers import (
    make_switchbot_payload,
    make_xiaomi_payload,
    post_switchbot,
    post_switchbot_sensors,
    post_xiaomi,
)


def test_switchbot_sensors_creates_unknown_sensor(client, api_key, db_session):
    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "aa:bb:cc:dd:ee:ff"}],
    )

    assert response.status_code == 200
    body = response.json()
    assert body["warnings"] == []
    assert len(body["sensors"]) == 1

    sensor = body["sensors"][0]
    assert sensor["mac"] == "AA:BB:CC:DD:EE:FF"
    assert UUID(sensor["sensor_id"])
    assert sensor["first_timestamp"] is None
    assert sensor["latest_timestamp"] is None
    assert sensor["sync_intervals"] == []
    assert sensor["sync_intervals_capped"] is False

    rows = db_session.query(Sensor).all()
    assert len(rows) == 1
    assert rows[0].mac == "AA:BB:CC:DD:EE:FF"
    assert rows[0].name == "AA:BB:CC:DD:EE:FF"
    assert rows[0].type == SWITCHBOT_TYPE


def test_switchbot_sensors_returns_existing_first_and_latest_timestamp(client, api_key):
    first = make_switchbot_payload(timestamp="2026-04-21T18:00:00Z")
    second = make_switchbot_payload(timestamp="2026-04-21T18:15:00Z")

    post_switchbot(client, api_key, first)
    post_switchbot(client, api_key, second)

    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "aa:bb:cc:dd:ee:ff"}],
    )

    assert response.status_code == 200
    sensor = response.json()["sensors"][0]
    assert sensor["first_timestamp"] == first["timestamp"]
    assert sensor["latest_timestamp"] == second["timestamp"]
    assert sensor["sync_intervals"] == []


def test_switchbot_sensors_returns_internal_sync_intervals_for_large_gaps(client, api_key):
    post_switchbot(client, api_key, make_switchbot_payload(timestamp="2026-04-21T18:00:00Z"))
    post_switchbot(client, api_key, make_switchbot_payload(timestamp="2026-04-21T18:15:00Z"))
    post_switchbot(client, api_key, make_switchbot_payload(timestamp="2026-04-21T18:40:00Z"))
    post_switchbot(client, api_key, make_switchbot_payload(timestamp="2026-04-21T19:00:00Z"))

    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "aa:bb:cc:dd:ee:ff"}],
    )

    assert response.status_code == 200
    sensor = response.json()["sensors"][0]
    assert sensor["sync_intervals"] == [
        {
            "start": "2026-04-21T18:15:00Z",
            "end": "2026-04-21T18:40:00Z",
        },
        {
            "start": "2026-04-21T18:40:00Z",
            "end": "2026-04-21T19:00:00Z",
        },
    ]


def test_switchbot_sensors_uses_configured_gap_threshold(client, api_key, app):
    app.state.config.switchbot_sync_gap_threshold_minutes = 30
    post_switchbot(client, api_key, make_switchbot_payload(timestamp="2026-04-21T18:00:00Z"))
    post_switchbot(client, api_key, make_switchbot_payload(timestamp="2026-04-21T18:25:00Z"))
    post_switchbot(client, api_key, make_switchbot_payload(timestamp="2026-04-21T18:55:00Z"))

    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "AA:BB:CC:DD:EE:FF"}],
    )

    assert response.status_code == 200
    assert response.json()["sensors"][0]["sync_intervals"] == [
        {
            "start": "2026-04-21T18:25:00Z",
            "end": "2026-04-21T18:55:00Z",
        }
    ]


def test_switchbot_sensors_caps_sync_intervals_per_sensor(client, api_key, app, caplog):
    app.state.config.switchbot_sync_max_intervals_per_sensor = 2
    caplog.set_level(logging.WARNING)

    for timestamp in [
        "2026-04-21T18:00:00Z",
        "2026-04-21T18:30:00Z",
        "2026-04-21T19:00:00Z",
        "2026-04-21T19:30:00Z",
    ]:
        post_switchbot(client, api_key, make_switchbot_payload(timestamp=timestamp))

    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "AA:BB:CC:DD:EE:FF"}],
    )

    assert response.status_code == 200
    body = response.json()
    assert len(body["sensors"][0]["sync_intervals"]) == 2
    assert body["sensors"][0]["sync_intervals_capped"] is True
    assert body["warnings"] == [
        {
            "code": "sync_intervals_capped",
            "message": (
                "sync intervals were capped; client should upload returned "
                "intervals and request /switchbot/sensors again"
            ),
        }
    ]
    assert "switchbot_sync_intervals_capped" in caplog.text


def test_switchbot_sensors_caps_sync_intervals_across_response(client, api_key, app):
    app.state.config.switchbot_sync_max_intervals_per_sensor = 10
    app.state.config.switchbot_sync_max_intervals_total = 1

    for mac in ["AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66"]:
        post_switchbot(
            client,
            api_key,
            make_switchbot_payload(mac=mac, timestamp="2026-04-21T18:00:00Z"),
        )
        post_switchbot(
            client,
            api_key,
            make_switchbot_payload(mac=mac, timestamp="2026-04-21T18:30:00Z"),
        )

    response = post_switchbot_sensors(
        client,
        api_key,
        [
            {"mac": "AA:BB:CC:DD:EE:FF"},
            {"mac": "11:22:33:44:55:66"},
        ],
    )

    assert response.status_code == 200
    body = response.json()
    assert len(body["sensors"][0]["sync_intervals"]) == 1
    assert body["sensors"][0]["sync_intervals_capped"] is False
    assert body["sensors"][1]["sync_intervals"] == []
    assert body["sensors"][1]["sync_intervals_capped"] is True
    assert body["warnings"][0]["code"] == "sync_intervals_capped"


def test_switchbot_sensors_per_sensor_cap_returns_newest_intervals(client, api_key, app):
    app.state.config.switchbot_sync_max_intervals_per_sensor = 2

    for timestamp in [
        "2026-04-21T18:00:00Z",
        "2026-04-21T18:30:00Z",
        "2026-04-21T19:00:00Z",
        "2026-04-21T19:30:00Z",
    ]:
        post_switchbot(client, api_key, make_switchbot_payload(timestamp=timestamp))

    response = post_switchbot_sensors(client, api_key, [{"mac": "AA:BB:CC:DD:EE:FF"}])

    assert response.status_code == 200
    body = response.json()
    # 3 gaps total; cap is 2 — expect the 2 newest
    assert body["sensors"][0]["sync_intervals"] == [
        {"start": "2026-04-21T18:30:00Z", "end": "2026-04-21T19:00:00Z"},
        {"start": "2026-04-21T19:00:00Z", "end": "2026-04-21T19:30:00Z"},
    ]
    assert body["sensors"][0]["sync_intervals_capped"] is True
    # first_timestamp/latest_timestamp reflect true DB bounds, not the capped window
    assert body["sensors"][0]["first_timestamp"] == "2026-04-21T18:00:00Z"
    assert body["sensors"][0]["latest_timestamp"] == "2026-04-21T19:30:00Z"


def test_switchbot_sensors_total_cap_returns_newest_intervals(client, api_key, app):
    app.state.config.switchbot_sync_max_intervals_per_sensor = 10
    app.state.config.switchbot_sync_max_intervals_total = 1

    for mac in ["AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66"]:
        for timestamp in [
            "2026-04-21T18:00:00Z",
            "2026-04-21T18:30:00Z",
            "2026-04-21T19:00:00Z",
        ]:
            post_switchbot(client, api_key, make_switchbot_payload(mac=mac, timestamp=timestamp))

    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "AA:BB:CC:DD:EE:FF"}, {"mac": "11:22:33:44:55:66"}],
    )

    assert response.status_code == 200
    body = response.json()
    # first sensor gets 1 slot (total cap) — it should be the newest gap
    assert body["sensors"][0]["sync_intervals"] == [
        {"start": "2026-04-21T18:30:00Z", "end": "2026-04-21T19:00:00Z"},
    ]
    assert body["sensors"][0]["sync_intervals_capped"] is True
    # second sensor gets 0 slots — remaining_total exhausted
    assert body["sensors"][1]["sync_intervals"] == []
    assert body["sensors"][1]["sync_intervals_capped"] is True
    assert body["warnings"][0]["code"] == "sync_intervals_capped"


def test_switchbot_sensors_is_idempotent(client, api_key, db_session):
    first = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "AA:BB:CC:DD:EE:FF"}],
    )
    second = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "aa:bb:cc:dd:ee:ff"}],
    )

    assert first.status_code == 200
    assert second.status_code == 200
    assert first.json()["sensors"][0]["sensor_id"] == second.json()["sensors"][0]["sensor_id"]
    assert db_session.query(Sensor).count() == 1


def test_switchbot_sensors_placeholder_allows_later_name_assignment(client, api_key, db_session):
    created = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "AA:BB:CC:DD:EE:FF"}],
    )
    reading = post_switchbot(
        client,
        api_key,
        make_switchbot_payload(name="Bedroom"),
    )

    assert created.status_code == 200
    assert reading.status_code == 200
    assert reading.json() == {"result": "created", "warnings": []}
    assert db_session.query(Sensor).one().name == "Bedroom"


def test_switchbot_sensors_rejects_invalid_mac(client, api_key):
    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "AA-BB-CC-DD-EE-FF"}],
    )

    assert response.status_code == 400
    assert response.json() == {"detail": "invalid mac format"}


def test_switchbot_sensors_rejects_cross_type_mac(client, api_key):
    post_xiaomi(client, api_key, make_xiaomi_payload())

    response = post_switchbot_sensors(
        client,
        api_key,
        [{"mac": "AA:BB:CC:DD:EE:FF"}],
    )

    assert response.status_code == 400
    assert response.json() == {"detail": "sensor type does not match existing sensor"}


def test_switchbot_sensors_respects_retention_and_timestamp_cap(client, api_key, db_session, monkeypatch):
    """Timestamps outside 68-day window and beyond the cap are excluded from gap analysis,
    while first_timestamp/latest_timestamp always reflect true DB min/max."""
    import service
    monkeypatch.setattr(service, "SYNC_TIMESTAMPS_MAX", 20)

    mac = "AA:BB:CC:DD:EE:FF"

    resp = post_switchbot_sensors(client, api_key, [{"mac": mac}])
    assert resp.status_code == 200
    sensor_id = UUID(resp.json()["sensors"][0]["sensor_id"])

    now = datetime.now(timezone.utc).replace(microsecond=0)

    # Outside the 68-day retention window — must not contribute to sync intervals
    t_old = now - timedelta(days=70)

    # Oldest timestamp inside the retention window.
    # With 21 in-window timestamps and cap=20, this one is dropped by the cap.
    # A 30-min gap from t0→t1 would appear if t0 were included; its absence proves the cap.
    t0 = now - timedelta(days=67)
    t1 = t0 + timedelta(minutes=30)

    # 21 in-window timestamps: t0, then t1 and 19 more at 1-min intervals (no gaps ≥ 20 min)
    in_window = [t0] + [t1 + timedelta(minutes=i) for i in range(20)]
    t_latest = in_window[-1]

    db_session.execute(
        pg_insert(SwitchbotReading)
        .values([
            {"sensor_id": sensor_id, "mac": mac, "timestamp": ts, "temperature_c": 21.0, "humidity_pct": 50.0}
            for ts in [t_old, *in_window]
        ])
        .on_conflict_do_nothing()
    )
    db_session.commit()

    response = post_switchbot_sensors(client, api_key, [{"mac": mac}])
    assert response.status_code == 200
    sensor = response.json()["sensors"][0]

    def fmt(ts: datetime) -> str:
        return ts.strftime("%Y-%m-%dT%H:%M:%SZ")

    # True DB bounds are preserved regardless of retention/cap
    assert sensor["first_timestamp"] == fmt(t_old)
    assert sensor["latest_timestamp"] == fmt(t_latest)

    # No gap from t_old→t0 (retention excludes t_old)
    # No gap from t0→t1 (cap drops t0; only newest 20 in-window are processed)
    # Remaining processed timestamps are 1 min apart — no gaps ≥ 20 min
    assert sensor["sync_intervals"] == []
    assert sensor["sync_intervals_capped"] is False
