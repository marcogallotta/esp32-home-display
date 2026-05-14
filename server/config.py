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

_PROD_WEAK_API_KEYS = {
    "dev-api-key",
    "test-api-key",
}

_PROD_WEAK_SECRETS = {
    "a-long-random-secret-string",
    "test-session-secret",
}

_PROD_WEAK_PASSWORDS = {
    "your-dashboard-password",
    "test-dashboard-password",
}

BULK_MAX_READINGS_RANGE = (1, 10_000)
SYNC_GAP_THRESHOLD_RANGE = (1, 10_080)  # minutes; 10 080 = 1 week
SYNC_MAX_INTERVALS_PER_SENSOR_RANGE = (1, 10_000)
SYNC_MAX_INTERVALS_TOTAL_RANGE = (1, 100_000)


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
    switchbot_sync_gap_threshold_minutes: int = SWITCHBOT_SYNC_DEFAULT_GAP_THRESHOLD_MINUTES
    switchbot_sync_max_intervals_per_sensor: int = SWITCHBOT_SYNC_DEFAULT_MAX_INTERVALS_PER_SENSOR
    switchbot_sync_max_intervals_total: int = SWITCHBOT_SYNC_DEFAULT_MAX_INTERVALS_TOTAL
    switchbot_bulk_max_readings: int = SWITCHBOT_BULK_DEFAULT_MAX_READINGS


def _check_str(errors: list[str], name: str, value: object) -> bool:
    if not isinstance(value, str):
        errors.append(f"{name}: must be a string")
        return False
    return True


def _check_int(errors: list[str], name: str, value: object) -> bool:
    # bool is an int subclass — reject it explicitly
    if isinstance(value, bool) or not isinstance(value, int):
        errors.append(f"{name}: must be an integer")
        return False
    return True


def validate_config(config: Config, env: str) -> None:
    errors: list[str] = []

    api_key_ok = _check_str(errors, "api_key", config.api_key)
    if api_key_ok and not config.api_key:
        errors.append("api_key: required")
        api_key_ok = False

    secret_ok = _check_str(errors, "session_secret", config.session_secret)
    if secret_ok and not config.session_secret:
        errors.append("session_secret: required")
        secret_ok = False

    pw_ok = _check_str(errors, "dashboard_password", config.dashboard_password)
    if pw_ok and not config.dashboard_password:
        errors.append("dashboard_password: required")
        pw_ok = False

    secure_ok = isinstance(config.session_secure, bool)
    if not secure_ok:
        errors.append("session_secure: must be a boolean")

    if env == "prod":
        if api_key_ok and config.api_key and config.api_key in _PROD_WEAK_API_KEYS:
            errors.append("api_key: must not be a known default")
        if secret_ok and config.session_secret and (
            len(config.session_secret) < 32
            or config.session_secret in _PROD_WEAK_SECRETS
        ):
            errors.append("session_secret: must be at least 32 characters and not a known default")
        if pw_ok and config.dashboard_password and (
            len(config.dashboard_password) < 12
            or config.dashboard_password in _PROD_WEAK_PASSWORDS
        ):
            errors.append("dashboard_password: must be at least 12 characters and not a known default")
        if secure_ok and not config.session_secure:
            errors.append("session_secure: must be true in prod")

    lo, hi = BULK_MAX_READINGS_RANGE
    if _check_int(errors, "switchbot_bulk_max_readings", config.switchbot_bulk_max_readings):
        if not (lo <= config.switchbot_bulk_max_readings <= hi):
            errors.append(f"switchbot_bulk_max_readings: must be in [{lo}, {hi}]")

    lo, hi = SYNC_GAP_THRESHOLD_RANGE
    if _check_int(errors, "switchbot_sync_gap_threshold_minutes", config.switchbot_sync_gap_threshold_minutes):
        if not (lo <= config.switchbot_sync_gap_threshold_minutes <= hi):
            errors.append(f"switchbot_sync_gap_threshold_minutes: must be in [{lo}, {hi}]")

    lo, hi = SYNC_MAX_INTERVALS_PER_SENSOR_RANGE
    if _check_int(errors, "switchbot_sync_max_intervals_per_sensor", config.switchbot_sync_max_intervals_per_sensor):
        if not (lo <= config.switchbot_sync_max_intervals_per_sensor <= hi):
            errors.append(f"switchbot_sync_max_intervals_per_sensor: must be in [{lo}, {hi}]")

    lo, hi = SYNC_MAX_INTERVALS_TOTAL_RANGE
    if _check_int(errors, "switchbot_sync_max_intervals_total", config.switchbot_sync_max_intervals_total):
        if not (lo <= config.switchbot_sync_max_intervals_total <= hi):
            errors.append(f"switchbot_sync_max_intervals_total: must be in [{lo}, {hi}]")

    if errors:
        raise ValueError("Invalid configuration:\n" + "\n".join(f"  {e}" for e in errors))


_KNOWN_ENVS = {"dev", "test", "prod"}
_REQUIRED_KEYS = {"api_key", "session_secret", "dashboard_password", "database"}


def load_config(config_dir: Path | None = None) -> Config:
    env = os.getenv("ENV", "dev")
    if env not in _KNOWN_ENVS:
        raise ValueError(f"Unknown ENV={env!r}; must be one of: {', '.join(sorted(_KNOWN_ENVS))}")
    if config_dir is None:
        config_dir = Path(__file__).parent / "config"
    path = config_dir / f"{env}.json"

    with path.open(encoding="utf-8") as f:
        data = json.load(f)

    missing = _REQUIRED_KEYS - data.keys()
    if missing:
        raise ValueError(f"Missing required config fields: {', '.join(sorted(missing))}")

    raw_db = data.pop("database")
    config = Config(database=DatabaseConfig(**raw_db), **data)
    validate_config(config, env)
    return config
