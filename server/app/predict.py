import logging
import time
from datetime import datetime, timedelta, timezone

import numpy as np
from fastapi import APIRouter, Request
from fastapi.responses import JSONResponse

from . import switchbot as sb
from .models import SWITCHBOT_TYPE
from .openmeteo import _get_openmeteo_weather, _DEFAULT_LAT, _DEFAULT_LON
from .service import fetch_window_readings, list_sensors

logger = logging.getLogger(__name__)

router = APIRouter()

_CACHE_TTL = 30 * 60
_TRAIN_DAYS = 7
_FORECAST_DAYS = 7
_EXCLUDE_NAMES: set[str] = set()

_cache: dict[str, tuple[float, list[dict]]] = {}


def _round_hour(dt: datetime) -> datetime:
    return dt.replace(minute=0, second=0, microsecond=0)


def _fit_and_predict(sensor_mac: str, db, om_by_hour: dict[datetime, tuple[float, float, float]]) -> list[dict] | None:
    now = datetime.now(timezone.utc)
    train_start = now - timedelta(days=_TRAIN_DAYS)

    rows = fetch_window_readings(
        db=db,
        mac=sensor_mac,
        start_ts=train_start,
        end_ts=now,
        max_points=None,
        sensor=sb.SENSOR,
    )

    hourly: dict[datetime, list[float]] = {}
    for row in rows:
        ts = row.timestamp
        if ts.tzinfo is None:
            ts = ts.replace(tzinfo=timezone.utc)
        if row.temperature_c is None:
            continue
        hourly.setdefault(_round_hour(ts), []).append(float(row.temperature_c))

    hourly_avg = {h: sum(v) / len(v) for h, v in hourly.items()}

    X, y = [], []
    for hour, temp in hourly_avg.items():
        entry = om_by_hour.get(hour)
        if entry is None:
            continue
        outdoor, radiation, cloud = entry
        h = hour.hour
        X.append([1.0, outdoor, radiation, cloud, np.sin(2 * np.pi * h / 24), np.cos(2 * np.pi * h / 24)])
        y.append(temp)

    if len(X) < 24:
        return None

    coeffs, _, _, _ = np.linalg.lstsq(np.array(X), np.array(y), rcond=None)

    forecast_start = _round_hour(now)
    predictions = []
    for hour_dt in sorted(om_by_hour):
        if hour_dt < forecast_start:
            continue
        outdoor, radiation, cloud = om_by_hour[hour_dt]
        h = hour_dt.hour
        x = np.array([1.0, outdoor, radiation, cloud, np.sin(2 * np.pi * h / 24), np.cos(2 * np.pi * h / 24)])
        predictions.append({
            "timestamp": hour_dt.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "temperature_c": round(float(np.dot(coeffs, x)), 1),
        })

    return predictions


@router.get("/predict/temperature")
def get_temperature_predictions(request: Request):
    ow = request.app.state.config.openmeteo
    lat = ow.get("latitude", _DEFAULT_LAT)
    lon = ow.get("longitude", _DEFAULT_LON)

    cache_key = f"{lat}_{lon}"
    cached = _cache.get(cache_key)
    if cached and time.monotonic() - cached[0] < _CACHE_TTL:
        return cached[1]

    now = datetime.now(timezone.utc)
    train_start = (now - timedelta(days=_TRAIN_DAYS)).isoformat()
    forecast_end = (now + timedelta(days=_FORECAST_DAYS)).isoformat()

    try:
        om_data = _get_openmeteo_weather(train_start, forecast_end, lat, lon)
    except Exception:
        logger.exception("Failed to fetch OpenMeteo data for prediction")
        return JSONResponse(status_code=502, content={"detail": "Prediction unavailable"})

    om_by_hour: dict[datetime, tuple[float, float]] = {}
    for pt in om_data:
        if pt.get("temperature_2m") is None:
            continue
        ts = datetime.fromisoformat(pt["timestamp"].replace("Z", "+00:00"))
        om_by_hour[_round_hour(ts)] = (pt["temperature_2m"], pt.get("shortwave_radiation") or 0.0, pt.get("cloud_cover") or 0.0)

    db = request.app.state.session_factory()
    try:
        sensors = list_sensors(db)
        result = []
        for sensor in sensors:
            if sensor.type != SWITCHBOT_TYPE or sensor.name in _EXCLUDE_NAMES:
                continue
            try:
                preds = _fit_and_predict(sensor.mac, db, om_by_hour)
            except Exception:
                logger.warning("Prediction failed for sensor %s", sensor.name, exc_info=True)
                preds = None
            if preds:
                result.append({
                    "sensor_id": str(sensor.id),
                    "sensor_name": sensor.name,
                    "predictions": preds,
                })
        _cache[cache_key] = (time.monotonic(), result)
        return result
    except Exception:
        logger.exception("Temperature prediction endpoint failed")
        return JSONResponse(status_code=502, content={"detail": "Prediction unavailable"})
    finally:
        db.close()
