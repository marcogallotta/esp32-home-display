import json
import os
from dataclasses import dataclass, field
from pathlib import Path

from .common import (
    MAC_ADDRESS_RE,
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
class RateLimit:
    limit: int
    period: int


@dataclass
class Esp32AppRateLimits:
    read: RateLimit
    live_write: RateLimit
    bulk_write: RateLimit
    burst: bool = True


@dataclass
class RateLimitsConfig:
    esp32_app: Esp32AppRateLimits
    frontend: RateLimit
    login: RateLimit


def _default_rate_limits() -> RateLimitsConfig:
    return RateLimitsConfig(
        esp32_app=Esp32AppRateLimits(
            read=RateLimit(limit=60, period=60),
            live_write=RateLimit(limit=10, period=60),
            bulk_write=RateLimit(limit=120, period=60),
            burst=True,
        ),
        frontend=RateLimit(limit=120, period=60),
        login=RateLimit(limit=3, period=60),
    )


@dataclass
class LevoitAhControllerConfig:
    switchbot_mac: str | None = None
    target_absolute_humidity: float | None = None
    server_base_url: str | None = None
    minimum_humidity: int = 40
    maximum_humidity: int = 60
    poll_interval_seconds: int = 300
    humidity_change_threshold: float = 1.0


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
    rate_limits: RateLimitsConfig = field(default_factory=_default_rate_limits)
    openmeteo: dict = field(default_factory=dict)
    levoit_ah_controller: LevoitAhControllerConfig = field(default_factory=LevoitAhControllerConfig)
    vesync_username: str | None = None
    vesync_password: str | None = None
    vesync_device_cid: str | None = None


def _check_str(errors: list[str], name: str, value: object) -> bool:
    if not isinstance(value, str):
        errors.append(f"{name}: must be a string")
        return False
    return True


def _check_positive_int(errors: list[str], name: str, value: object) -> None:
    if _check_int(errors, name, value) and value <= 0:  # type: ignore[operator]
        errors.append(f"{name}: must be a positive integer")


def _validate_rate_limit(errors: list[str], prefix: str, rl: RateLimit) -> None:
    _check_positive_int(errors, f"{prefix}.limit", rl.limit)
    _check_positive_int(errors, f"{prefix}.period", rl.period)


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

    rl = config.rate_limits
    _validate_rate_limit(errors, "rate_limits.esp32_app.read", rl.esp32_app.read)
    _validate_rate_limit(errors, "rate_limits.esp32_app.live_write", rl.esp32_app.live_write)
    _validate_rate_limit(errors, "rate_limits.esp32_app.bulk_write", rl.esp32_app.bulk_write)
    if not isinstance(rl.esp32_app.burst, bool):
        errors.append("rate_limits.esp32_app.burst: must be a boolean")
    _validate_rate_limit(errors, "rate_limits.frontend", rl.frontend)
    _validate_rate_limit(errors, "rate_limits.login", rl.login)

    _validate_levoit_ah_controller(errors, config.levoit_ah_controller)

    if errors:
        raise ValueError("Invalid configuration:\n" + "\n".join(f"  {e}" for e in errors))


def _validate_levoit_ah_controller(errors: list[str], cfg: LevoitAhControllerConfig) -> None:
    p = "levoit_ah_controller"

    if cfg.switchbot_mac is not None:
        if not isinstance(cfg.switchbot_mac, str) or not MAC_ADDRESS_RE.fullmatch(cfg.switchbot_mac):
            errors.append(f"{p}.switchbot_mac: invalid MAC address format")

    if cfg.target_absolute_humidity is not None:
        if (
            isinstance(cfg.target_absolute_humidity, bool)
            or not isinstance(cfg.target_absolute_humidity, (int, float))
        ):
            errors.append(f"{p}.target_absolute_humidity: must be a number")
        elif cfg.target_absolute_humidity <= 0:
            errors.append(f"{p}.target_absolute_humidity: must be a positive number")

    if cfg.server_base_url is not None:
        if not isinstance(cfg.server_base_url, str) or not cfg.server_base_url:
            errors.append(f"{p}.server_base_url: must be a non-empty string")
        elif not cfg.server_base_url.startswith(("http://", "https://")):
            errors.append(f"{p}.server_base_url: must start with http:// or https://")

    min_ok = _check_int(errors, f"{p}.minimum_humidity", cfg.minimum_humidity)
    max_ok = _check_int(errors, f"{p}.maximum_humidity", cfg.maximum_humidity)
    if min_ok and not (0 <= cfg.minimum_humidity <= 100):
        errors.append(f"{p}.minimum_humidity: must be between 0 and 100")
        min_ok = False
    if max_ok and not (0 <= cfg.maximum_humidity <= 100):
        errors.append(f"{p}.maximum_humidity: must be between 0 and 100")
        max_ok = False
    if min_ok and max_ok and cfg.minimum_humidity > cfg.maximum_humidity:
        errors.append(f"{p}.minimum_humidity: must be <= maximum_humidity")

    _check_positive_int(errors, f"{p}.poll_interval_seconds", cfg.poll_interval_seconds)

    if (
        isinstance(cfg.humidity_change_threshold, bool)
        or not isinstance(cfg.humidity_change_threshold, (int, float))
    ):
        errors.append(f"{p}.humidity_change_threshold: must be a number")
    elif cfg.humidity_change_threshold < 0:
        errors.append(f"{p}.humidity_change_threshold: must be >= 0")


def _parse_levoit_ah_controller(raw: object) -> LevoitAhControllerConfig:
    if raw is None:
        return LevoitAhControllerConfig()
    if not isinstance(raw, dict):
        raise ValueError("levoit_ah_controller: must be an object")
    kwargs = {
        k: raw[k]
        for k in (
            "switchbot_mac",
            "target_absolute_humidity",
            "server_base_url",
            "minimum_humidity",
            "maximum_humidity",
            "poll_interval_seconds",
            "humidity_change_threshold",
        )
        if k in raw
    }
    if "switchbot_mac" in kwargs:
        mac = kwargs["switchbot_mac"]
        if isinstance(mac, str):
            if not MAC_ADDRESS_RE.fullmatch(mac):
                raise ValueError("levoit_ah_controller.switchbot_mac: invalid MAC address format")
            kwargs["switchbot_mac"] = mac.upper()
    return LevoitAhControllerConfig(**kwargs)


_KNOWN_ENVS = {"dev", "test", "prod"}


def _require_env(name: str) -> str:
    value = os.environ.get(name)
    if value is None:
        raise ValueError(f"{name} environment variable is required")
    return value


def load_config(config_dir: Path | None = None) -> Config:
    env = os.getenv("ENV", "dev")
    if env not in _KNOWN_ENVS:
        raise ValueError(f"Unknown ENV={env!r}; must be one of: {', '.join(sorted(_KNOWN_ENVS))}")

    if config_dir is None:
        config_dir = Path(__file__).parent.parent / "config"

    with (config_dir / "app.json").open(encoding="utf-8") as f:
        data = json.load(f)

    raw_db = data.pop("database", {})
    raw_rl = data.pop("rate_limits", None)
    raw_levoit = data.pop("levoit_ah_controller", None)
    rate_limits = _parse_rate_limits(raw_rl) if raw_rl is not None else _default_rate_limits()
    levoit_ah_controller = _parse_levoit_ah_controller(raw_levoit)
    levoit_server_base_url = os.environ.get("SERVER_BASE_URL")
    if levoit_server_base_url is not None:
        levoit_ah_controller.server_base_url = levoit_server_base_url

    data["api_key"] = _require_env("API_KEY")
    data["session_secret"] = _require_env("SESSION_SECRET")
    data["dashboard_password"] = _require_env("DASHBOARD_PASSWORD")

    session_secure_env = os.environ.get("SESSION_SECURE")
    if session_secure_env is not None:
        if session_secure_env.lower() not in ("true", "false"):
            raise ValueError(f"SESSION_SECURE must be 'true' or 'false', got {session_secure_env!r}")
        data["session_secure"] = session_secure_env.lower() == "true"

    db = DatabaseConfig(
        driver=raw_db.get("driver", "postgresql+psycopg"),
        host=_require_env("DATABASE_HOST"),
        port=int(_require_env("DATABASE_PORT")),
        name=_require_env("DATABASE_NAME"),
        user=_require_env("DATABASE_USER"),
        password=_require_env("DATABASE_PASSWORD"),
    )

    config = Config(
        database=db,
        rate_limits=rate_limits,
        levoit_ah_controller=levoit_ah_controller,
        vesync_username=os.environ.get("VESYNC_USERNAME"),
        vesync_password=os.environ.get("VESYNC_PASSWORD"),
        vesync_device_cid=os.environ.get("VESYNC_DEVICE_CID"),
        **data,
    )
    validate_config(config, env)
    return config


def _parse_rate_limits(raw: dict) -> RateLimitsConfig:
    def _rl(d: dict) -> RateLimit:
        return RateLimit(**d)

    esp = raw["esp32_app"]
    fe = raw["frontend"]
    login = raw["login"]
    return RateLimitsConfig(
        esp32_app=Esp32AppRateLimits(
            read=_rl(esp["read"]),
            live_write=_rl(esp["live_write"]),
            bulk_write=_rl(esp["bulk_write"]),
            burst=esp.get("burst", True),
        ),
        frontend=_rl(fe),
        login=_rl(login),
    )
