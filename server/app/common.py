from datetime import datetime, timedelta, timezone
from enum import Enum
import logging
import re

from .errors import BadRequestError
from .sensor_spec import DataField


logger = logging.getLogger(__name__)


class SensorType(str, Enum):
    switchbot = "switchbot"
    xiaomi = "xiaomi"


MAC_ADDRESS_RE = re.compile(r"^(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$")

READINGS_DEFAULT_LIMIT = 10
READINGS_MAX_LIMIT = 100

SWITCHBOT_BULK_DEFAULT_MAX_READINGS = 1000
BULK_ERROR_DETAIL_LIMIT = 20

SWITCHBOT_SYNC_DEFAULT_GAP_THRESHOLD_MINUTES = 20
SWITCHBOT_SYNC_DEFAULT_MAX_INTERVALS_PER_SENSOR = 100
SWITCHBOT_SYNC_DEFAULT_MAX_INTERVALS_TOTAL = 1000

QUERY_TIMESTAMP_MIN = datetime(2000, 1, 1, tzinfo=timezone.utc)
QUERY_TIMESTAMP_MAX_SKEW = timedelta(days=7)


def check_range(name: str, value, min_value, max_value):
    if value is None:
        return
    if value < min_value or value > max_value:
        raise ValueError(f"{name} must be in [{min_value}, {max_value}]")


def check_hard_ranges(reading, data_fields: tuple[DataField, ...]):
    for field in data_fields:
        if field.hard_range is None:
            continue
        value = field.getter(reading)
        if value is None:
            continue
        check_range(field.name, value, *field.hard_range)


def warn_if_suspicious(metric: str, value, mac: str):
    logger.warning("Suspicious %s=%s for sensor %s", metric, value, mac)


def warn_soft_ranges(reading, data_fields: tuple[DataField, ...]):
    for field in data_fields:
        if field.soft_range is None:
            continue
        value = field.getter(reading)
        if value is None:
            continue
        min_value, max_value = field.soft_range
        if value < min_value or value > max_value:
            warn_if_suspicious(field.name, value, reading.mac)


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
