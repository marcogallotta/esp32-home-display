"""Proxy to the external plant-monitoring backend (Raspberry Pi, GATT-based).

The Pi owns SwitchBot meter and Flower Care (Xiaomi) data: SwitchBot is read
directly via BLE and persisted on the Pi; Flower Care is read over an active
GATT connection. This module adapts the Pi's API to the shapes the dashboard
already expects, so the frontend stays single-origin and unaware that sensor
data lives elsewhere.

Field/shape mapping (kept here so the Pi API stays minimal):
  Pi `recorded_at` -> dashboard `timestamp`
  Pi `lux`         -> dashboard `light_lux`  (Flower Care only)
  Pi returns ASC; the dashboard contract is DESC (frontend re-reverses to ASC).
  Pi latest is a top-level list; dashboard latest wraps each row with sensor_id.

Auth: Bearer token (PLANT_MONITOR_API_KEY).
"""

import logging
from datetime import datetime
from typing import Any

import httpx
from pydantic import BaseModel
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session

from . import switchbot as sb
from .config import Config
from .models import SWITCHBOT_TYPE, XIAOMI_TYPE, Sensor
from .service import get_sensor_by_mac

logger = logging.getLogger(__name__)

_TIMEOUT_SECONDS = 10
_DEFAULT_METER_RETRY_AFTER_SECS = 5 * 60


class PlantReadingOut(BaseModel):
    """Dashboard-facing shape for a Flower Care reading. Field names match the
    rest of the sensor read API (timestamp/light_lux), not the Pi's wire format
    (recorded_at/lux); the mapping happens in _map_reading()."""

    timestamp: datetime
    temperature_c: float | None
    moisture_pct: int | None
    light_lux: int | None
    conductivity_us_cm: int | None


def _get(config: Config, path: str, params: dict[str, Any] | None = None) -> Any:
    base = config.plant_monitor_url.rstrip("/")
    headers = {"Authorization": f"Bearer {config.plant_monitor_api_token}"}
    with httpx.Client(timeout=_TIMEOUT_SECONDS) as client:
        resp = client.get(base + path, params=params, headers=headers)
        resp.raise_for_status()
        return resp.json()


def _map_reading(row: dict[str, Any]) -> PlantReadingOut:
    return PlantReadingOut(
        timestamp=row["recorded_at"],
        temperature_c=row.get("temperature_c"),
        moisture_pct=row.get("moisture_pct"),
        light_lux=row.get("lux"),
        conductivity_us_cm=row.get("conductivity_us_cm"),
    )


def fetch_plant_readings(
    config: Config,
    mac: str,
    start_ts: datetime | None,
    end_ts: datetime | None,
    max_points: int | None,
) -> list[PlantReadingOut]:
    """Window-mode readings for one Flower Care sensor, mapped to the dashboard
    contract (DESC). The dashboard only ever requests window mode; raw
    before/after paging is not proxied (returns empty)."""
    if start_ts is None or end_ts is None:
        return []

    params: dict[str, Any] = {
        "start_ts": start_ts.isoformat(),
        "end_ts": end_ts.isoformat(),
    }
    if max_points is not None:
        params["max_points"] = max_points

    rows = _get(config, f"/sensors/flower-care/{mac}/readings", params)
    readings = [_map_reading(r) for r in rows]
    readings.sort(key=lambda r: r.timestamp, reverse=True)
    return readings


def _ensure_plant_sensor(db: Session, mac: str, name: str) -> Sensor:
    """Resolve the plant sensor row, creating it from the Pi's identity if it
    does not exist yet. The Pi is the source of truth for plant sensors now, so
    this is the production path that provisions (and back-fills, e.g. after a DB
    reset) the sensors-table row -- the old Xiaomi ingest used to do this."""
    sensor_row = get_sensor_by_mac(db, mac)
    if sensor_row is not None:
        return sensor_row

    sensor_row = Sensor(mac=mac, name=name, type=XIAOMI_TYPE)
    db.add(sensor_row)
    try:
        db.commit()
    except IntegrityError:
        # Concurrent request created it first; re-fetch the winner.
        db.rollback()
        sensor_row = get_sensor_by_mac(db, mac)
    return sensor_row


def plant_latest_entries(
    db: Session,
    config: Config,
    sensor_ids: list[Any] | None,
) -> list[dict[str, Any]]:
    """Latest Flower Care readings in the dashboard's LatestReadingOut shape.

    Resolves each Pi MAC to this server's sensor row (provisioning it from the
    Pi's identity if missing) for its stable UUID. Degrades gracefully: if the
    Pi is unreachable the dashboard still renders the other sensors."""
    try:
        rows = _get(config, "/sensors/flower-care/latest")
    except httpx.HTTPError as exc:
        logger.warning("plant monitor latest fetch failed: %s", exc)
        return []

    out: list[dict[str, Any]] = []
    for row in rows:
        sensor_row = _ensure_plant_sensor(db, row["mac"], row["name"])
        if sensor_row is None:
            continue
        if sensor_ids is not None and sensor_row.id not in sensor_ids:
            continue
        out.append(
            {
                "mac": sensor_row.mac,
                "sensor_id": sensor_row.id,
                "latest_timestamp": row["recorded_at"],
                "reading": {
                    "temperature_c": row.get("temperature_c"),
                    "moisture_pct": row.get("moisture_pct"),
                    "light_lux": row.get("lux"),
                    "conductivity_us_cm": row.get("conductivity_us_cm"),
                },
            }
        )
    return out


def _ensure_meter_sensor(db: Session, mac: str, name: str) -> Sensor | None:
    sensor_row = get_sensor_by_mac(db, mac)
    if sensor_row is not None:
        return sensor_row
    sensor_row = Sensor(mac=mac, name=name, type=SWITCHBOT_TYPE)
    db.add(sensor_row)
    try:
        db.commit()
    except IntegrityError:
        db.rollback()
        sensor_row = get_sensor_by_mac(db, mac)
    return sensor_row


def fetch_meter_readings(
    config: Config,
    mac: str,
    start_ts: datetime | None,
    end_ts: datetime | None,
    max_points: int | None,
) -> list[sb.ReadingOut]:
    """Window-mode readings for one SwitchBot meter, mapped to DESC order."""
    if start_ts is None or end_ts is None:
        return []

    params: dict[str, Any] = {
        "start_ts": start_ts.isoformat(),
        "end_ts": end_ts.isoformat(),
    }
    if max_points is not None:
        params["max_points"] = max_points

    rows = _get(config, f"/sensors/meter/{mac}/readings", params)
    readings = [
        sb.ReadingOut(
            timestamp=r["recorded_at"],
            temperature_c=r["temperature_c"],
            humidity_pct=r["humidity_pct"],
        )
        for r in rows
    ]
    readings.sort(key=lambda r: r.timestamp, reverse=True)
    return readings



def meter_latest_v2(config: Config) -> dict[str, Any]:
    """Proxy the plant monitor's v2 SwitchBot meter latest endpoint.

    Wire contract returned to ESP32:
      {
        "sensors": [
          {
            "mac": str,
            "name": str | None,
            "recorded_at": ISO datetime string,
            "temperature_c": float | None,
            "humidity_pct": float | None,
            "stale": bool,
          }
        ],
        "retry_after_secs": int,
      }

    This intentionally preserves the upstream v2 shape instead of adapting it to
    the dashboard /sensors/latest shape.
    """
    try:
        body = _get(config, "/v2/sensors/meter/latest")
    except httpx.HTTPError as exc:
        logger.warning("plant monitor meter latest v2 fetch failed: %s", exc)
        return {"sensors": [], "retry_after_secs": _DEFAULT_METER_RETRY_AFTER_SECS}

    if not isinstance(body, dict):
        logger.warning("plant monitor meter latest v2 malformed response: expected object")
        return {"sensors": [], "retry_after_secs": _DEFAULT_METER_RETRY_AFTER_SECS}

    sensors = body.get("sensors")
    if not isinstance(sensors, list):
        logger.warning("plant monitor meter latest v2 malformed response: missing sensors list")
        return {"sensors": [], "retry_after_secs": _DEFAULT_METER_RETRY_AFTER_SECS}

    try:
        retry_after_secs = int(body.get("retry_after_secs", _DEFAULT_METER_RETRY_AFTER_SECS))
    except (TypeError, ValueError):
        retry_after_secs = _DEFAULT_METER_RETRY_AFTER_SECS
    if retry_after_secs <= 0:
        retry_after_secs = _DEFAULT_METER_RETRY_AFTER_SECS

    out: list[dict[str, Any]] = []
    for row in sensors:
        if not isinstance(row, dict):
            continue
        mac = row.get("mac")
        recorded_at = row.get("recorded_at")
        if not isinstance(mac, str) or not recorded_at:
            continue
        out.append({
            "mac": mac,
            "name": row.get("name"),
            "recorded_at": recorded_at,
            "temperature_c": row.get("temperature_c"),
            "humidity_pct": row.get("humidity_pct"),
            "stale": bool(row.get("stale", False)),
        })

    return {"sensors": out, "retry_after_secs": retry_after_secs}

def meter_latest_entries(
    db: Session,
    config: Config,
    sensor_ids: list[Any] | None,
) -> list[dict[str, Any]]:
    """Latest SwitchBot meter readings in the dashboard's LatestReadingOut shape.

    Degrades gracefully: if the Pi is unreachable the dashboard still renders
    the other sensors."""
    try:
        rows = _get(config, "/sensors/meter/latest")
    except httpx.HTTPError as exc:
        logger.warning("plant monitor meter latest fetch failed: %s", exc)
        return []

    out: list[dict[str, Any]] = []
    for row in rows:
        sensor_row = _ensure_meter_sensor(db, row["mac"], row["name"])
        if sensor_row is None:
            continue
        if sensor_ids is not None and sensor_row.id not in sensor_ids:
            continue
        out.append(
            {
                "mac": sensor_row.mac,
                "sensor_id": sensor_row.id,
                "latest_timestamp": row["recorded_at"],
                "reading": {
                    "temperature_c": row.get("temperature_c"),
                    "humidity_pct": row.get("humidity_pct"),
                },
            }
        )
    return out
