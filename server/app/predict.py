import logging
import math
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

# (temperature_2m, shortwave_radiation, cloud_cover, relative_humidity_2m)
OmEntry = tuple[float, float, float, float]


def _round_hour(dt: datetime) -> datetime:
    return dt.replace(minute=0, second=0, microsecond=0)


def _features(outdoor: float, radiation: float, cloud: float, humidity: float, hour: int) -> list[float]:
    return [1.0, outdoor, radiation, cloud, humidity, math.sin(2 * math.pi * hour / 24), math.cos(2 * math.pi * hour / 24)]


def _calc_abs_humidity(temp_c: float, rh_pct: float) -> float:
    svp = 6.112 * math.exp((17.67 * temp_c) / (temp_c + 243.5))
    avp = svp * (rh_pct / 100)
    return (2.1674 * avp) / (273.15 + temp_c) * 100


def _calc_vpd(temp_c: float, rh_pct: float) -> float:
    svp = 0.6108 * math.exp((17.27 * temp_c) / (temp_c + 237.3))
    return svp * (1 - rh_pct / 100)


def _fit_models(sensor_mac: str, db, om_by_hour: dict[datetime, OmEntry]) -> list[dict] | None:
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

    hourly_temp: dict[datetime, list[float]] = {}
    hourly_hum: dict[datetime, list[float]] = {}
    for row in rows:
        ts = row.timestamp
        if ts.tzinfo is None:
            ts = ts.replace(tzinfo=timezone.utc)
        h = _round_hour(ts)
        if row.temperature_c is not None:
            hourly_temp.setdefault(h, []).append(float(row.temperature_c))
        if row.humidity_pct is not None:
            hourly_hum.setdefault(h, []).append(float(row.humidity_pct))

    avg_temp = {h: sum(v) / len(v) for h, v in hourly_temp.items()}
    avg_hum = {h: sum(v) / len(v) for h, v in hourly_hum.items()}

    Xt, yt, Xh, yh = [], [], [], []
    for hour in sorted(set(avg_temp) & set(avg_hum)):
        entry = om_by_hour.get(hour)
        if entry is None:
            continue
        outdoor, radiation, cloud, om_hum = entry
        feat = _features(outdoor, radiation, cloud, om_hum, hour.hour)
        Xt.append(feat)
        yt.append(avg_temp[hour])
        Xh.append(feat)
        yh.append(avg_hum[hour])

    if len(Xt) < 24:
        return None

    coeffs_t, _, _, _ = np.linalg.lstsq(np.array(Xt), np.array(yt), rcond=None)
    coeffs_h, _, _, _ = np.linalg.lstsq(np.array(Xh), np.array(yh), rcond=None)

    forecast_start = _round_hour(now)
    predictions = []
    for hour_dt in sorted(om_by_hour):
        if hour_dt < forecast_start:
            continue
        outdoor, radiation, cloud, om_hum = om_by_hour[hour_dt]
        feat = np.array(_features(outdoor, radiation, cloud, om_hum, hour_dt.hour))
        pred_temp = float(np.dot(coeffs_t, feat))
        pred_hum = float(np.dot(coeffs_h, feat))
        pred_hum = max(0.0, min(100.0, pred_hum))
        predictions.append({
            "timestamp": hour_dt.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "temperature_c": round(pred_temp, 1),
            "humidity_pct": round(pred_hum, 1),
            "abs_humidity": round(_calc_abs_humidity(pred_temp, pred_hum), 2),
            "vpd": round(_calc_vpd(pred_temp, pred_hum), 3),
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

    om_by_hour: dict[datetime, OmEntry] = {}
    for pt in om_data:
        if pt.get("temperature_2m") is None:
            continue
        ts = datetime.fromisoformat(pt["timestamp"].replace("Z", "+00:00"))
        om_by_hour[_round_hour(ts)] = (
            pt["temperature_2m"],
            pt.get("shortwave_radiation") or 0.0,
            pt.get("cloud_cover") or 0.0,
            pt.get("relative_humidity_2m") or 0.0,
        )

    db = request.app.state.session_factory()
    try:
        sensors = list_sensors(db)
        result = []
        for sensor in sensors:
            if sensor.type != SWITCHBOT_TYPE or sensor.name in _EXCLUDE_NAMES:
                continue
            try:
                preds = _fit_models(sensor.mac, db, om_by_hour)
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
        logger.exception("Prediction endpoint failed")
        return JSONResponse(status_code=502, content={"detail": "Prediction unavailable"})
    finally:
        db.close()
