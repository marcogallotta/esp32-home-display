"""Proxy to the external plant-monitoring backend (Raspberry Pi, GATT-based).

The Pi owns Flower Care (Xiaomi) data now: it reads over an active BLE GATT
connection and persists history. This module adapts the Pi's API to the shapes
the dashboard already expects, so the frontend stays single-origin against this
server and unaware that Xiaomi data lives elsewhere.

Field/shape mapping (kept here so the Pi API stays minimal):
  Pi `recorded_at` -> dashboard `timestamp`
  Pi `lux`         -> dashboard `light_lux`
  Pi returns ASC; the dashboard contract is DESC (frontend re-reverses to ASC).
  Pi latest is a top-level list; dashboard latest wraps each row with sensor_id.

Auth: Bearer token (PLANT_MONITOR_API_KEY), honored only on the flower-care paths.
"""

import logging
from datetime import datetime
from typing import Any

import httpx
from sqlalchemy.orm import Session

from . import xiaomi as xm
from .config import Config
from .service import get_sensor_by_mac

logger = logging.getLogger(__name__)

_TIMEOUT_SECONDS = 10


def _get(config: Config, path: str, params: dict[str, Any] | None = None) -> Any:
    base = config.plant_monitor_url.rstrip("/")
    headers = {"Authorization": f"Bearer {config.plant_monitor_api_token}"}
    with httpx.Client(timeout=_TIMEOUT_SECONDS) as client:
        resp = client.get(base + path, params=params, headers=headers)
        resp.raise_for_status()
        return resp.json()


def _map_reading(row: dict[str, Any]) -> xm.ReadingOut:
    return xm.ReadingOut(
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
) -> list[xm.ReadingOut]:
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


def plant_latest_entries(
    db: Session,
    config: Config,
    sensor_ids: list[Any] | None,
) -> list[dict[str, Any]]:
    """Latest Flower Care readings in the dashboard's LatestReadingOut shape.

    Resolves each Pi MAC to this server's sensor row (still present in the
    sensors table) for its stable UUID. Degrades gracefully: if the Pi is
    unreachable the dashboard still renders the other sensors."""
    try:
        rows = _get(config, "/sensors/flower-care/latest")
    except httpx.HTTPError as exc:
        logger.warning("plant monitor latest fetch failed: %s", exc)
        return []

    out: list[dict[str, Any]] = []
    for row in rows:
        sensor_row = get_sensor_by_mac(db, row["mac"])
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
