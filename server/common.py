from datetime import datetime, timedelta, timezone
from enum import Enum
import logging
import re

from fastapi import HTTPException


logger = logging.getLogger(__name__)


class SensorType(str, Enum):
    switchbot = "switchbot"
    xiaomi = "xiaomi"


MAC_ADDRESS_RE = re.compile(r"^(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$")

READINGS_DEFAULT_LIMIT = 10
READINGS_MAX_LIMIT = 100

QUERY_TIMESTAMP_MIN = datetime(2000, 1, 1, tzinfo=timezone.utc)
QUERY_TIMESTAMP_MAX_SKEW = timedelta(days=7)


def check_range(name: str, value, min_value, max_value):
    if value is None:
        return
    if value < min_value or value > max_value:
        raise ValueError(f"{name} must be in [{min_value}, {max_value}]")


def warn_if_suspicious(metric: str, value, mac: str):
    logger.warning("Suspicious %s=%s for sensor %s", metric, value, mac)


def validate_mac_address(mac: str) -> str:
    if not MAC_ADDRESS_RE.fullmatch(mac):
        raise HTTPException(status_code=400, detail="invalid mac format")
    return mac.upper()


def normalize_timestamp_to_utc(value: datetime) -> datetime:
    if value.tzinfo is None:
        raise HTTPException(status_code=400, detail="timestamp must include timezone")
    return value.astimezone(timezone.utc)


def validate_query_timestamp(name: str, value: datetime | None) -> datetime | None:
    if value is None:
        return None

    if value.tzinfo is None:
        raise HTTPException(status_code=400, detail=f"{name} must include timezone")

    now = datetime.now(timezone.utc)
    if value < QUERY_TIMESTAMP_MIN:
        raise HTTPException(status_code=400, detail=f"{name} is too old")
    if value > now + QUERY_TIMESTAMP_MAX_SKEW:
        raise HTTPException(status_code=400, detail=f"{name} is too far in the future")

    return value.astimezone(timezone.utc)