import logging
import math
import time
from datetime import datetime, timedelta, timezone

import numpy as np
from fastapi import APIRouter, Request
from fastapi.responses import JSONResponse
from sklearn.linear_model import Ridge

from . import switchbot as sb
from .models import SWITCHBOT_TYPE
from .openmeteo import _get_openmeteo_weather, _DEFAULT_LAT, _DEFAULT_LON
from .service import fetch_window_readings, list_sensors

logger = logging.getLogger(__name__)

router = APIRouter()

_CACHE_TTL = 30 * 60
_TRAIN_DAYS = 14
_FORECAST_DAYS = 7
_EXCLUDE_NAMES: set[str] = set()

_cache: dict[str, tuple[float, list[dict]]] = {}

# (temperature_2m, shortwave_radiation, cloud_cover, relative_humidity_2m)
OmEntry = tuple[float, float, float, float]


def _round_hour(dt: datetime) -> datetime:
    return dt.replace(minute=0, second=0, microsecond=0)


def _features(entry: OmEntry, hour_dt: datetime, om_by_hour: dict) -> list[float]:
    outdoor, radiation, cloud, humidity = entry
    feats = [outdoor, radiation, cloud, humidity,
             math.sin(2 * math.pi * hour_dt.hour / 24),
             math.cos(2 * math.pi * hour_dt.hour / 24)]
    for lag in (1, 2, 3):
        lagged = om_by_hour.get(hour_dt - timedelta(hours=lag))
        feats.append(lagged[0] if lagged else outdoor)
        feats.append(lagged[1] if lagged else radiation)
    return feats


def _calc_abs_humidity(temp_c: float, rh_pct: float) -> float:
    svp = 6.112 * math.exp((17.67 * temp_c) / (temp_c + 243.5))
    avp = svp * (rh_pct / 100)
    return (2.1674 * avp) / (273.15 + temp_c) * 100


def _calc_vpd(temp_c: float, rh_pct: float) -> float:
    svp = 0.6108 * math.exp((17.27 * temp_c) / (temp_c + 237.3))
    return svp * (1 - rh_pct / 100)


def _hourly_avgs(
    rows, start: datetime | None = None, end: datetime | None = None
) -> tuple[dict[datetime, float], dict[datetime, float]]:
    hourly_temp: dict[datetime, list[float]] = {}
    hourly_hum: dict[datetime, list[float]] = {}
    for row in rows:
        ts = row.timestamp
        if ts.tzinfo is None:
            ts = ts.replace(tzinfo=timezone.utc)
        if start and ts < start:
            continue
        if end and ts >= end:
            continue
        h = _round_hour(ts)
        if row.temperature_c is not None:
            hourly_temp.setdefault(h, []).append(float(row.temperature_c))
        if row.humidity_pct is not None:
            hourly_hum.setdefault(h, []).append(float(row.humidity_pct))
    return (
        {h: sum(v) / len(v) for h, v in hourly_temp.items()},
        {h: sum(v) / len(v) for h, v in hourly_hum.items()},
    )


def _build_xy(
    avg_temp: dict[datetime, float],
    avg_hum: dict[datetime, float],
    om_by_hour: dict[datetime, OmEntry],
) -> tuple[list, list, list, list]:
    Xt, yt, Xh, yh = [], [], [], []
    for hour in sorted(set(avg_temp) & set(avg_hum)):
        entry = om_by_hour.get(hour)
        if entry is None:
            continue
        feat = _features(entry, hour, om_by_hour)
        Xt.append(feat)
        yt.append(avg_temp[hour])
        Xh.append(feat)
        yh.append(avg_hum[hour])
    return Xt, yt, Xh, yh


def _fit_rf(Xt, yt, Xh, yh):
    return (
        Ridge(alpha=1.0).fit(np.array(Xt), np.array(yt)),
        Ridge(alpha=1.0).fit(np.array(Xh), np.array(yh)),
    )


def _fit_models(sensor_mac: str, db, om_by_hour: dict[datetime, OmEntry]) -> dict | None:
    now = datetime.now(timezone.utc)
    train_start = now - timedelta(days=_TRAIN_DAYS)
    holdout_start = now - timedelta(days=1)

    rows = fetch_window_readings(
        db=db,
        mac=sensor_mac,
        start_ts=train_start,
        end_ts=now,
        max_points=None,
        sensor=sb.SENSOR,
    )

    avg_temp_train, avg_hum_train = _hourly_avgs(rows, end=holdout_start)
    avg_temp_holdout, avg_hum_holdout = _hourly_avgs(rows, start=holdout_start)

    Xt, yt, Xh, yh = _build_xy(avg_temp_train, avg_hum_train, om_by_hour)
    if len(Xt) < 24:
        return None

    # backtest: train on days 1-6, predict day 7, compare to actual
    bt_model_t, bt_model_h = _fit_rf(Xt, yt, Xh, yh)
    backtest = []
    bt_errs_t, bt_errs_h = [], []
    for hour_dt in sorted(avg_temp_holdout.keys() & avg_hum_holdout.keys()):
        entry = om_by_hour.get(hour_dt)
        if entry is None:
            continue
        feat = [_features(entry, hour_dt, om_by_hour)]
        pred_t = float(bt_model_t.predict(feat)[0])
        pred_h = max(0.0, min(100.0, float(bt_model_h.predict(feat)[0])))
        actual_t = avg_temp_holdout[hour_dt]
        actual_h = avg_hum_holdout[hour_dt]
        bt_errs_t.append((pred_t - actual_t) ** 2)
        bt_errs_h.append((pred_h - actual_h) ** 2)
        backtest.append({
            "timestamp": hour_dt.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "temperature_c": round(pred_t, 1),
            "humidity_pct": round(pred_h, 1),
            "actual_temperature_c": round(actual_t, 1),
            "actual_humidity_pct": round(actual_h, 1),
        })

    backtest_rmse = {
        "temperature_c": round(math.sqrt(sum(bt_errs_t) / len(bt_errs_t)), 2) if bt_errs_t else None,
        "humidity_pct": round(math.sqrt(sum(bt_errs_h) / len(bt_errs_h)), 2) if bt_errs_h else None,
    }

    # full model: retrain on all data for forward predictions
    avg_temp_all, avg_hum_all = _hourly_avgs(rows)
    Xt_all, yt_all, Xh_all, yh_all = _build_xy(avg_temp_all, avg_hum_all, om_by_hour)
    model_t, model_h = _fit_rf(Xt_all, yt_all, Xh_all, yh_all)

    forecast_start = _round_hour(now)
    predictions = []
    for hour_dt in sorted(om_by_hour):
        if hour_dt < forecast_start:
            continue
        entry = om_by_hour[hour_dt]
        feat = [_features(entry, hour_dt, om_by_hour)]
        pred_temp = float(model_t.predict(feat)[0])
        pred_hum = max(0.0, min(100.0, float(model_h.predict(feat)[0])))
        predictions.append({
            "timestamp": hour_dt.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "temperature_c": round(pred_temp, 1),
            "humidity_pct": round(pred_hum, 1),
            "abs_humidity": round(_calc_abs_humidity(pred_temp, pred_hum), 2),
            "vpd": round(_calc_vpd(pred_temp, pred_hum), 3),
        })

    return {"predictions": predictions, "backtest": backtest, "backtest_rmse": backtest_rmse}


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
                fit = _fit_models(sensor.mac, db, om_by_hour)
            except Exception:
                logger.warning("Prediction failed for sensor %s", sensor.name, exc_info=True)
                fit = None
            if fit:
                result.append({
                    "sensor_id": str(sensor.id),
                    "sensor_name": sensor.name,
                    "predictions": fit["predictions"],
                    "backtest": fit["backtest"],
                    "backtest_rmse": fit["backtest_rmse"],
                })
        if result:
            _cache[cache_key] = (time.monotonic(), result)
        return result
    except Exception:
        logger.exception("Prediction endpoint failed")
        return JSONResponse(status_code=502, content={"detail": "Prediction unavailable"})
    finally:
        db.close()
