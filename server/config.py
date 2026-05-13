import json
import os
from dataclasses import dataclass
from pathlib import Path

from common import (
    SWITCHBOT_BULK_DEFAULT_MAX_READINGS,
    SWITCHBOT_SYNC_DEFAULT_GAP_THRESHOLD_MINUTES,
    SWITCHBOT_SYNC_DEFAULT_MAX_INTERVALS_PER_SENSOR,
    SWITCHBOT_SYNC_DEFAULT_MAX_INTERVALS_TOTAL,
)


@dataclass
class DatabaseConfig:
    driver: str
    host: str
    port: int
    name: str
    user: str
    password: str


@dataclass
class Config:
    api_key: str
    dashboard_password: str
    session_secret: str
    database: DatabaseConfig
    session_secure: bool = True
    switchbot_sync_gap_threshold_minutes: int | float = SWITCHBOT_SYNC_DEFAULT_GAP_THRESHOLD_MINUTES
    switchbot_sync_max_intervals_per_sensor: int = SWITCHBOT_SYNC_DEFAULT_MAX_INTERVALS_PER_SENSOR
    switchbot_sync_max_intervals_total: int = SWITCHBOT_SYNC_DEFAULT_MAX_INTERVALS_TOTAL
    switchbot_bulk_max_readings: int = SWITCHBOT_BULK_DEFAULT_MAX_READINGS


def load_config() -> Config:
    env = os.getenv("ENV", "dev")
    path = Path("config") / f"{env}.json"

    with path.open(encoding="utf-8") as f:
        data = json.load(f)

    raw_db = data.pop("database")
    return Config(database=DatabaseConfig(**raw_db), **data)
