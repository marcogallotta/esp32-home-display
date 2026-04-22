from datetime import datetime, timedelta, timezone
from enum import Enum
import logging
import re

from errors import BadRequestError


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


def check_hard_ranges(reading, hard_ranges: dict[str, tuple[int | float, int | float]]):
    for field, (min_value, max_value) in hard_ranges.items():
        check_range(field, getattr(reading, field), min_value, max_value)


def warn_if_suspicious(metric: str, value, mac: str):
    logger.warning("Suspicious %s=%s for sensor %s", metric, value, mac)


def warn_soft_ranges(reading, soft_ranges: dict[str, tuple[int | float, int | float]]):
    for field, (min_value, max_value) in soft_ranges.items():
        value = getattr(reading, field)
        if value is None:
            continue
        if value < min_value or value > max_value:
            warn_if_suspicious(field, value, reading.mac)


def validate_mac_address(mac: str) -> str:
    if not MAC_ADDRESS_RE.fullmatch(mac):
        raise BadRequestError("invalid mac format")
    return mac.upper()


def normalize_timestamp_to_utc(value: datetime) -> datetime:
    if value.tzinfo is None:
        raise BadRequestError("timestamp must include timezone")
    return value.astimezone(timezone.utc)


def validate_query_timestamp(name: str, value: datetime | None) -> datetime | None:
    if value is None:
        return None

    if value.tzinfo is None:
        raise BadRequestError(f"{name} must include timezone")

    now = datetime.now(timezone.utc)
    if value < QUERY_TIMESTAMP_MIN:
        raise BadRequestError(f"{name} is too old")
    if value > now + QUERY_TIMESTAMP_MAX_SKEW:
        raise BadRequestError(f"{name} is too far in the future")

    return value.astimezone(timezone.utc)
