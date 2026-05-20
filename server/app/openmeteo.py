import logging
import time
from datetime import datetime, timezone

import httpx
from fastapi import APIRouter, Request
from fastapi.responses import JSONResponse

logger = logging.getLogger(__name__)

router = APIRouter()

_ARCHIVE_URL = "https://archive-api.open-meteo.com/v1/archive"
_FORECAST_URL = "https://api.open-meteo.com/v1/forecast"
_HOURLY_VARS = "temperature_2m,relative_humidity_2m,dew_point_2m,rain,showers,snowfall,wind_speed_10m,wind_gusts_10m,shortwave_radiation"

_DEFAULT_LAT = 45.737
_DEFAULT_LON = 7.321
_CACHE_TTL = 30 * 60  # 30 minutes

_cache: dict[str, tuple[float, list[dict]]] = {}


def _fetch_meteo(url: str, params: dict, source: str) -> list[dict]:
    with httpx.Client(timeout=10) as client:
        r = client.get(url, params=params)
        r.raise_for_status()
    hourly = r.json().get("hourly", {})
    times = hourly.get("time", [])
    var_names = _HOURLY_VARS.split(",")
    result = []
    for i, t in enumerate(times):
        point: dict = {"timestamp": t + ":00Z", "source": source}
        for var in var_names:
            vals = hourly.get(var)
            point[var] = vals[i] if vals and i < len(vals) else None
        result.append(point)
    return result


def _get_openmeteo_weather(start_ts: str, end_ts: str, lat: float, lon: float) -> list[dict]:
    now = datetime.now(timezone.utc)
    today = now.date()
    start_date = datetime.fromisoformat(start_ts.replace("Z", "+00:00")).date()
    end_date = datetime.fromisoformat(end_ts.replace("Z", "+00:00")).date()

    cache_key = f"{start_date}_{end_date}_{lat}_{lon}"
    cached = _cache.get(cache_key)
    if cached and time.monotonic() - cached[0] < _CACHE_TTL:
        return cached[1]

    base = {"latitude": lat, "longitude": lon, "timezone": "UTC", "hourly": _HOURLY_VARS}
    points: list[dict] = []

    archive_end = min(end_date, today)
    if start_date <= archive_end:
        try:
            points.extend(_fetch_meteo(
                _ARCHIVE_URL,
                {**base, "start_date": str(start_date), "end_date": str(archive_end)},
                "archive",
            ))
        except Exception:
            logger.warning("Open-Meteo archive fetch failed", exc_info=True)

    forecast_start = max(start_date, today)
    if forecast_start <= end_date:
        try:
            points.extend(_fetch_meteo(
                _FORECAST_URL,
                {**base, "start_date": str(forecast_start), "end_date": str(end_date)},
                "forecast",
            ))
        except Exception:
            logger.warning("Open-Meteo forecast fetch failed", exc_info=True)

    seen: dict[str, dict] = {}
    for p in points:
        ts = p["timestamp"]
        if ts not in seen or p["source"] == "forecast":
            seen[ts] = p
    result = sorted(seen.values(), key=lambda p: p["timestamp"])

    _cache[cache_key] = (time.monotonic(), result)
    return result


@router.get("/openmeteo/weather")
def get_openmeteo_weather(request: Request, start_ts: str, end_ts: str):
    ow = request.app.state.config.openmeteo
    lat = ow.get("latitude", _DEFAULT_LAT)
    lon = ow.get("longitude", _DEFAULT_LON)
    try:
        return _get_openmeteo_weather(start_ts, end_ts, lat, lon)
    except Exception:
        logger.exception("Open-Meteo endpoint failed")
        return JSONResponse(status_code=502, content={"detail": "Open-Meteo unavailable"})
