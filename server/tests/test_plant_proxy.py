import uuid
from datetime import datetime, timezone
from types import SimpleNamespace

import httpx
import pytest
from fastapi.testclient import TestClient

from app import plant_proxy
from app.models import XIAOMI_TYPE, Sensor

MAC = "5C:85:7E:14:43:45"


def _cfg():
    # _get is monkeypatched in every test, so the URL/token are never used.
    return SimpleNamespace(plant_monitor_url="http://pi:8001/", plant_monitor_api_token="t")


def _latest_row(**overrides):
    row = {
        "mac": MAC,
        "name": "Cilantro",
        "recorded_at": "2026-06-11T14:15:00+00:00",
        "temperature_c": 27.6,
        "lux": 40663,
        "moisture_pct": 28,
        "conductivity_us_cm": 205,
    }
    row.update(overrides)
    return row


# --- fetch_plant_readings ---

def test_fetch_plant_readings_maps_fields_and_sorts_desc(monkeypatch):
    rows = [
        {"recorded_at": "2026-06-11T10:00:00+00:00", "temperature_c": 20.0, "lux": 100,
         "moisture_pct": 25, "conductivity_us_cm": 200},
        {"recorded_at": "2026-06-11T12:00:00+00:00", "temperature_c": 22.0, "lux": 300,
         "moisture_pct": 26, "conductivity_us_cm": 210},
    ]
    captured = {}

    def fake_get(config, path, params=None):
        captured["path"] = path
        captured["params"] = params
        return rows

    monkeypatch.setattr(plant_proxy, "_get", fake_get)

    start = datetime(2026, 6, 11, 0, 0, tzinfo=timezone.utc)
    end = datetime(2026, 6, 11, 23, 0, tzinfo=timezone.utc)
    result = plant_proxy.fetch_plant_readings(_cfg(), MAC, start, end, 96)

    # DESC by timestamp; recorded_at -> timestamp, lux -> light_lux
    assert [r.timestamp for r in result] == [
        datetime(2026, 6, 11, 12, 0, tzinfo=timezone.utc),
        datetime(2026, 6, 11, 10, 0, tzinfo=timezone.utc),
    ]
    assert result[0].light_lux == 300
    assert result[0].temperature_c == 22.0
    assert captured["path"] == f"/sensors/flower-care/{MAC}/readings"
    assert captured["params"]["max_points"] == 96
    assert "start_ts" in captured["params"] and "end_ts" in captured["params"]


def test_fetch_plant_readings_omits_max_points_when_none(monkeypatch):
    captured = {}

    def fake_get(config, path, params=None):
        captured["params"] = params
        return []

    monkeypatch.setattr(plant_proxy, "_get", fake_get)
    start = datetime(2026, 6, 11, 0, 0, tzinfo=timezone.utc)
    end = datetime(2026, 6, 11, 23, 0, tzinfo=timezone.utc)
    plant_proxy.fetch_plant_readings(_cfg(), MAC, start, end, None)
    assert "max_points" not in captured["params"]


def test_fetch_plant_readings_requires_window(monkeypatch):
    def fail(*args, **kwargs):
        pytest.fail("_get should not be called without a window")

    monkeypatch.setattr(plant_proxy, "_get", fail)
    assert plant_proxy.fetch_plant_readings(_cfg(), MAC, None, None, None) == []


# --- plant_latest_entries ---

def test_plant_latest_resolves_existing_sensor(db_session, monkeypatch):
    sensor = Sensor(mac=MAC, name="Cilantro", type=XIAOMI_TYPE)
    db_session.add(sensor)
    db_session.commit()
    monkeypatch.setattr(plant_proxy, "_get", lambda *a, **k: [_latest_row()])

    out = plant_proxy.plant_latest_entries(db_session, _cfg(), None)

    assert len(out) == 1
    entry = out[0]
    assert entry["mac"] == MAC
    assert entry["sensor_id"] == sensor.id
    assert entry["latest_timestamp"] == "2026-06-11T14:15:00+00:00"
    assert entry["reading"] == {
        "temperature_c": 27.6,
        "moisture_pct": 28,
        "light_lux": 40663,
        "conductivity_us_cm": 205,
    }


def test_plant_latest_provisions_missing_sensor(db_session, monkeypatch):
    monkeypatch.setattr(plant_proxy, "_get", lambda *a, **k: [_latest_row()])

    out = plant_proxy.plant_latest_entries(db_session, _cfg(), None)

    assert len(out) == 1
    row = db_session.query(Sensor).filter_by(mac=MAC).one()
    assert row.type == XIAOMI_TYPE
    assert row.name == "Cilantro"
    assert out[0]["sensor_id"] == row.id


def test_plant_latest_graceful_when_pi_unreachable(db_session, monkeypatch):
    def boom(*args, **kwargs):
        raise httpx.ConnectError("pi down")

    monkeypatch.setattr(plant_proxy, "_get", boom)
    assert plant_proxy.plant_latest_entries(db_session, _cfg(), None) == []


def test_plant_latest_respects_sensor_id_filter(db_session, monkeypatch):
    sensor = Sensor(mac=MAC, name="Cilantro", type=XIAOMI_TYPE)
    db_session.add(sensor)
    db_session.commit()
    monkeypatch.setattr(plant_proxy, "_get", lambda *a, **k: [_latest_row()])

    assert plant_proxy.plant_latest_entries(db_session, _cfg(), [uuid.uuid4()]) == []

    out = plant_proxy.plant_latest_entries(db_session, _cfg(), [sensor.id])
    assert len(out) == 1
    assert out[0]["sensor_id"] == sensor.id


# --- endpoint integration ---

def test_sensors_latest_includes_plant_when_configured(app, monkeypatch):
    app.state.config.plant_monitor_url = "http://pi:8001/"
    app.state.config.plant_monitor_api_token = "t"
    monkeypatch.setattr(
        plant_proxy,
        "_get",
        lambda config, path, params=None: [_latest_row()] if path.endswith("/latest") else [],
    )

    client = TestClient(app)
    response = client.get("/sensors/latest", headers={"x-api-key": app.state.config.api_key})

    assert response.status_code == 200
    plant = [s for s in response.json()["sensors"] if s["mac"] == MAC]
    assert len(plant) == 1
    assert plant[0]["reading"]["moisture_pct"] == 28
    assert plant[0]["reading"]["light_lux"] == 40663
