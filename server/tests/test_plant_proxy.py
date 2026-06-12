import uuid
from datetime import datetime, timezone
from types import SimpleNamespace

import httpx
import pytest
from fastapi.testclient import TestClient

from app import plant_proxy
from app.config import PlantSensorConfig
from app.models import SWITCHBOT_TYPE, XIAOMI_TYPE, Sensor
from tests.helpers import make_switchbot_payload, post_switchbot

MAC = "5C:85:7E:14:43:45"
METER_MAC = "D5:3A:42:86:2C:63"


def _meter_latest_row(**overrides):
    row = {
        "mac": METER_MAC,
        "name": "South",
        "recorded_at": "2026-06-12T06:30:00+00:00",
        "temperature_c": 22.2,
        "humidity_pct": 32.0,
        "stale": False,
    }
    row.update(overrides)
    return row


def _cfg():
    # _get is monkeypatched in every test, so the URL/token are never used.
    return SimpleNamespace(
        plant_monitor_url="http://pi:8001/",
        plant_monitor_api_token="t",
        plant_monitor_sensors=[
            PlantSensorConfig(mac=MAC, slug="cilantro", name="Cilantro"),
            PlantSensorConfig(mac=METER_MAC, slug="south", name="South"),
        ],
    )


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
    assert captured["path"] == "/sensors/cilantro/readings"
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
    monkeypatch.setattr(plant_proxy, "_get", lambda *a, **k: {"sensors": [_latest_row()]})

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
    monkeypatch.setattr(plant_proxy, "_get", lambda *a, **k: {"sensors": [_latest_row()]})

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
    monkeypatch.setattr(plant_proxy, "_get", lambda *a, **k: {"sensors": [_latest_row()]})

    assert plant_proxy.plant_latest_entries(db_session, _cfg(), [uuid.uuid4()]) == []

    out = plant_proxy.plant_latest_entries(db_session, _cfg(), [sensor.id])
    assert len(out) == 1
    assert out[0]["sensor_id"] == sensor.id


# --- fetch_meter_readings ---

def test_fetch_meter_readings_maps_fields_and_sorts_desc(monkeypatch):
    rows = [
        {"recorded_at": "2026-06-12T02:00:00+00:00", "temperature_c": 18.0, "humidity_pct": 30.0},
        {"recorded_at": "2026-06-12T06:00:00+00:00", "temperature_c": 22.0, "humidity_pct": 32.0},
    ]
    captured = {}

    def fake_get(config, path, params=None):
        captured["path"] = path
        captured["params"] = params
        return rows

    monkeypatch.setattr(plant_proxy, "_get", fake_get)

    start = datetime(2026, 6, 12, 0, 0, tzinfo=timezone.utc)
    end = datetime(2026, 6, 12, 8, 0, tzinfo=timezone.utc)
    result = plant_proxy.fetch_meter_readings(_cfg(), METER_MAC, start, end, 48)

    assert [r.timestamp for r in result] == [
        datetime(2026, 6, 12, 6, 0, tzinfo=timezone.utc),
        datetime(2026, 6, 12, 2, 0, tzinfo=timezone.utc),
    ]
    assert result[0].temperature_c == 22.0
    assert result[0].humidity_pct == 32.0
    assert captured["path"] == "/sensors/south/readings"
    assert captured["params"]["max_points"] == 48
    assert "start_ts" in captured["params"] and "end_ts" in captured["params"]


def test_fetch_meter_readings_requires_window(monkeypatch):
    def fail(*args, **kwargs):
        pytest.fail("_get should not be called without a window")

    monkeypatch.setattr(plant_proxy, "_get", fail)
    assert plant_proxy.fetch_meter_readings(_cfg(), METER_MAC, None, None, None) == []


# --- meter_latest_entries ---

def test_meter_latest_resolves_existing_sensor(db_session, monkeypatch):
    sensor = Sensor(mac=METER_MAC, name="South", type=SWITCHBOT_TYPE)
    db_session.add(sensor)
    db_session.commit()
    monkeypatch.setattr(plant_proxy, "_get", lambda *a, **k: {"sensors": [_meter_latest_row()]})

    out = plant_proxy.meter_latest_entries(db_session, _cfg(), None)

    assert len(out) == 1
    assert out[0]["mac"] == METER_MAC
    assert out[0]["sensor_id"] == sensor.id
    assert out[0]["latest_timestamp"] == "2026-06-12T06:30:00+00:00"
    assert out[0]["reading"] == {"temperature_c": 22.2, "humidity_pct": 32.0}


def test_meter_latest_provisions_missing_sensor(db_session, monkeypatch):
    monkeypatch.setattr(plant_proxy, "_get", lambda *a, **k: {"sensors": [_meter_latest_row()]})

    out = plant_proxy.meter_latest_entries(db_session, _cfg(), None)

    assert len(out) == 1
    row = db_session.query(Sensor).filter_by(mac=METER_MAC).one()
    assert row.type == SWITCHBOT_TYPE
    assert row.name == "South"
    assert out[0]["sensor_id"] == row.id


def test_meter_latest_graceful_when_pi_unreachable(db_session, monkeypatch):
    def boom(*args, **kwargs):
        raise httpx.ConnectError("pi down")

    monkeypatch.setattr(plant_proxy, "_get", boom)
    assert plant_proxy.meter_latest_entries(db_session, _cfg(), None) == []


# --- meter_latest_v2 ---

def test_meter_latest_v2_proxies_upstream_shape(monkeypatch):
    captured = {}

    def fake_get(config, path, params=None):
        captured["path"] = path
        return {
            "sensors": [_meter_latest_row()],
            "retry_after_secs": 177,
        }

    monkeypatch.setattr(plant_proxy, "_get", fake_get)

    out = plant_proxy.meter_latest_v2(_cfg())

    assert captured["path"] == "/sensors/meter/latest"
    assert out == {
        "sensors": [_meter_latest_row()],
        "retry_after_secs": 177,
    }


def test_meter_latest_v2_graceful_when_pi_rejects_auth(monkeypatch):
    def boom(*args, **kwargs):
        request = httpx.Request("GET", "http://pi:8001/sensors/meter/latest")
        response = httpx.Response(401, request=request)
        raise httpx.HTTPStatusError("unauthorized", request=request, response=response)

    monkeypatch.setattr(plant_proxy, "_get", boom)

    assert plant_proxy.meter_latest_v2(_cfg()) == {
        "sensors": [],
        "retry_after_secs": 300,
    }


def test_meter_latest_v2_endpoint_empty_when_proxy_unconfigured(app):
    app.state.config.plant_monitor_url = None
    app.state.config.plant_monitor_api_token = None

    client = TestClient(app)
    response = client.get(
        "/sensors/meter/latest",
        headers={"x-api-key": app.state.config.api_key},
    )

    assert response.status_code == 200
    assert response.json() == {"sensors": [], "retry_after_secs": 300}


def test_latest_v2_entries_aggregates_meter_and_flower_care(db_session, monkeypatch):
    calls = []

    def fake_get(config, path, params=None):
        calls.append(path)
        if path == "/sensors/meter/latest":
            return {"sensors": [_meter_latest_row()], "retry_after_secs": 177}
        if path == "/sensors/flower-care/latest":
            return {"sensors": [_latest_row(stale=True)], "retry_after_secs": 3400}
        raise AssertionError(path)

    monkeypatch.setattr(plant_proxy, "_get", fake_get)

    out = plant_proxy.latest_v2_entries(db_session, _cfg(), None)

    assert calls == ["/sensors/meter/latest", "/sensors/flower-care/latest"]
    assert out["retry_after_secs"] == 177
    by_type = {row["type"]: row for row in out["sensors"]}
    assert by_type["switchbot"]["recorded_at"] == "2026-06-12T06:30:00+00:00"
    assert by_type["switchbot"]["reading"] == {"temperature_c": 22.2, "humidity_pct": 32.0}
    assert by_type["switchbot"]["stale"] is False
    assert by_type["xiaomi"]["recorded_at"] == "2026-06-11T14:15:00+00:00"
    assert by_type["xiaomi"]["reading"] == {
        "temperature_c": 27.6,
        "moisture_pct": 28,
        "light_lux": 40663,
        "conductivity_us_cm": 205,
    }
    assert by_type["xiaomi"]["stale"] is True


def test_latest_v2_entries_respects_sensor_id_filter(db_session, monkeypatch):
    monkeypatch.setattr(
        plant_proxy,
        "_get",
        lambda config, path, params=None: (
            {"sensors": [_meter_latest_row()], "retry_after_secs": 177}
            if "meter" in path
            else {"sensors": [_latest_row()], "retry_after_secs": 3400}
        ),
    )

    first = plant_proxy.latest_v2_entries(db_session, _cfg(), None)
    xiaomi_id = next(row["sensor_id"] for row in first["sensors"] if row["type"] == "xiaomi")

    out = plant_proxy.latest_v2_entries(db_session, _cfg(), [xiaomi_id])

    assert [row["type"] for row in out["sensors"]] == ["xiaomi"]
    assert out["retry_after_secs"] == 177


def test_sensors_latest_v2_endpoint_uses_retry_hint(app, monkeypatch):
    app.state.config.plant_monitor_url = "http://pi:8001/"
    app.state.config.plant_monitor_api_token = "t"

    monkeypatch.setattr(
        plant_proxy,
        "_get",
        lambda config, path, params=None: (
            {"sensors": [_meter_latest_row()], "retry_after_secs": 177}
            if "meter" in path
            else {"sensors": [_latest_row()], "retry_after_secs": 3400}
        ),
    )

    client = TestClient(app)
    response = client.get("/sensors/latest", headers={"x-api-key": app.state.config.api_key})

    assert response.status_code == 200
    body = response.json()
    assert body["retry_after_secs"] == 177
    assert {row["type"] for row in body["sensors"]} == {"switchbot", "xiaomi"}
    meter = next(row for row in body["sensors"] if row["type"] == "switchbot")
    assert meter["recorded_at"] == "2026-06-12T06:30:00Z"


# --- endpoint integration ---

def test_sensors_latest_includes_plant_when_configured(app, monkeypatch):
    app.state.config.plant_monitor_url = "http://pi:8001/"
    app.state.config.plant_monitor_api_token = "t"
    monkeypatch.setattr(
        plant_proxy,
        "_get",
        lambda config, path, params=None: (
            {"sensors": [_latest_row()], "retry_after_secs": 3600}
            if "flower-care" in path
            else {"sensors": [], "retry_after_secs": 300}
        ),
    )

    client = TestClient(app)
    response = client.get("/sensors/latest", headers={"x-api-key": app.state.config.api_key})

    assert response.status_code == 200
    plant = [s for s in response.json()["sensors"] if s["mac"] == MAC]
    assert len(plant) == 1
    assert plant[0]["reading"]["moisture_pct"] == 28
    assert plant[0]["reading"]["light_lux"] == 40663


def test_sensors_latest_includes_meter_when_configured(app, monkeypatch):
    app.state.config.plant_monitor_url = "http://pi:8001/"
    app.state.config.plant_monitor_api_token = "t"

    def fake_get(config, path, params=None):
        if "meter" in path:
            return {"sensors": [_meter_latest_row()], "retry_after_secs": 177}
        return {"sensors": [], "retry_after_secs": 3600}

    monkeypatch.setattr(plant_proxy, "_get", fake_get)

    client = TestClient(app)
    response = client.get("/sensors/latest", headers={"x-api-key": app.state.config.api_key})

    assert response.status_code == 200
    meters = [s for s in response.json()["sensors"] if s["mac"] == METER_MAC]
    assert len(meters) == 1
    assert meters[0]["reading"] == {"temperature_c": 22.2, "humidity_pct": 32.0}
    assert meters[0]["recorded_at"] == "2026-06-12T06:30:00Z"


def test_sensor_readings_proxies_switchbot_to_meter_endpoint(app, monkeypatch):
    app.state.config.plant_monitor_url = "http://pi:8001/"
    app.state.config.plant_monitor_api_token = "t"

    api_key = app.state.config.api_key
    client = TestClient(app)

    # Provision a SwitchBot sensor via live ingest so the DB has a sensor row.
    post_switchbot(client, api_key, make_switchbot_payload(mac=METER_MAC, name="South"))
    sensors = client.get("/sensors", headers={"x-api-key": api_key}).json()
    sensor_id = next(s["id"] for s in sensors if s["mac"] == METER_MAC)

    captured = {}

    def fake_get(config, path, params=None):
        captured["path"] = path
        captured["params"] = params
        return [{"recorded_at": "2026-06-12T06:00:00+00:00", "temperature_c": 22.0, "humidity_pct": 31.0}]

    monkeypatch.setattr(plant_proxy, "_get", fake_get)

    response = client.get(
        f"/sensors/{sensor_id}/readings",
        headers={"x-api-key": api_key},
        params={"start_ts": "2026-06-12T00:00:00Z", "end_ts": "2026-06-12T08:00:00Z"},
    )

    assert response.status_code == 200
    assert captured["path"] == "/sensors/south/readings"
    rows = response.json()
    assert len(rows) == 1
    assert rows[0]["temperature_c"] == 22.0
    assert rows[0]["humidity_pct"] == 31.0
