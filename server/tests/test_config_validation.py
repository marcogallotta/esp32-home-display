import json
import os

import pytest

from app.config import Config, DatabaseConfig, LevoitAhControllerConfig, load_config, validate_config


def _db():
    return DatabaseConfig(
        driver="postgresql+psycopg",
        host="localhost",
        port=5432,
        name="test",
        user="postgres",
        password="test",
    )


def _config(**overrides) -> Config:
    defaults = dict(
        api_key="any-key",
        dashboard_password="test-dashboard-password",
        session_secret="test-session-secret",
        session_secure=False,
        database=_db(),
    )
    defaults.update(overrides)
    return Config(**defaults)


# --- Valid configs pass ---

def test_valid_dev_config_passes():
    validate_config(_config(), env="dev")


def test_valid_test_config_passes():
    validate_config(_config(), env="test")


def test_valid_prod_config_passes():
    cfg = _config(
        session_secret="a" * 32,
        dashboard_password="strongpassword1",
        session_secure=True,
    )
    validate_config(cfg, env="prod")


# --- api_key ---

def test_empty_api_key_fails():
    with pytest.raises(ValueError, match="api_key: required"):
        validate_config(_config(api_key=""), env="dev")


def test_non_string_api_key_fails():
    cfg = _config()
    cfg.api_key = 123  # type: ignore[assignment]
    with pytest.raises(ValueError, match="api_key: must be a string"):
        validate_config(cfg, env="dev")


def test_valid_api_key_passes():
    validate_config(_config(api_key="some-valid-key"), env="dev")


def test_prod_rejects_weak_api_key():
    cfg = _config(
        api_key="dev-api-key",
        session_secret="a" * 32,
        dashboard_password="strongpassword1",
        session_secure=True,
    )
    with pytest.raises(ValueError, match="api_key: must not be a known default"):
        validate_config(cfg, env="prod")


# --- Required fields ---

def test_empty_session_secret_fails():
    with pytest.raises(ValueError, match="session_secret: required"):
        validate_config(_config(session_secret=""), env="dev")


def test_empty_dashboard_password_fails():
    with pytest.raises(ValueError, match="dashboard_password: required"):
        validate_config(_config(dashboard_password=""), env="dev")


# --- session_secure type ---

def test_non_bool_session_secure_fails():
    cfg = _config()
    cfg.session_secure = "true"  # type: ignore[assignment]
    with pytest.raises(ValueError, match="session_secure: must be a boolean"):
        validate_config(cfg, env="dev")


# --- Prod-specific auth rules ---

def test_prod_rejects_weak_session_secret():
    cfg = _config(session_secret="short", session_secure=True, dashboard_password="strongpassword1")
    with pytest.raises(ValueError, match="session_secret"):
        validate_config(cfg, env="prod")


def test_prod_rejects_known_default_session_secret():
    cfg = _config(
        session_secret="test-session-secret",
        session_secure=True,
        dashboard_password="strongpassword1",
    )
    with pytest.raises(ValueError, match="session_secret"):
        validate_config(cfg, env="prod")


def test_prod_rejects_weak_dashboard_password():
    cfg = _config(
        session_secret="a" * 32,
        session_secure=True,
        dashboard_password="short",
    )
    with pytest.raises(ValueError, match="dashboard_password"):
        validate_config(cfg, env="prod")


def test_prod_rejects_known_default_dashboard_password():
    cfg = _config(
        session_secret="a" * 32,
        session_secure=True,
        dashboard_password="test-dashboard-password",
    )
    with pytest.raises(ValueError, match="dashboard_password"):
        validate_config(cfg, env="prod")


def test_prod_rejects_session_secure_false():
    cfg = _config(
        session_secret="a" * 32,
        dashboard_password="strongpassword1",
        session_secure=False,
    )
    with pytest.raises(ValueError, match="session_secure: must be true in prod"):
        validate_config(cfg, env="prod")


# --- Limit ranges ---

def test_bulk_max_readings_out_of_range_fails():
    with pytest.raises(ValueError, match="switchbot_bulk_max_readings"):
        validate_config(_config(switchbot_bulk_max_readings=0), env="dev")
    with pytest.raises(ValueError, match="switchbot_bulk_max_readings"):
        validate_config(_config(switchbot_bulk_max_readings=10_001), env="dev")


def test_sync_gap_threshold_out_of_range_fails():
    with pytest.raises(ValueError, match="switchbot_sync_gap_threshold_minutes"):
        validate_config(_config(switchbot_sync_gap_threshold_minutes=0), env="dev")
    with pytest.raises(ValueError, match="switchbot_sync_gap_threshold_minutes"):
        validate_config(_config(switchbot_sync_gap_threshold_minutes=99_999), env="dev")


def test_sync_max_intervals_per_sensor_out_of_range_fails():
    with pytest.raises(ValueError, match="switchbot_sync_max_intervals_per_sensor"):
        validate_config(_config(switchbot_sync_max_intervals_per_sensor=0), env="dev")


def test_sync_max_intervals_total_out_of_range_fails():
    with pytest.raises(ValueError, match="switchbot_sync_max_intervals_total"):
        validate_config(_config(switchbot_sync_max_intervals_total=0), env="dev")


def test_multiple_errors_reported_together():
    cfg = _config(session_secret="", dashboard_password="")
    with pytest.raises(ValueError) as exc_info:
        validate_config(cfg, env="dev")
    msg = str(exc_info.value)
    assert "session_secret" in msg
    assert "dashboard_password" in msg


# --- Type checks ---

def test_session_secret_non_string_fails():
    cfg = _config()
    cfg.session_secret = 123  # type: ignore[assignment]
    with pytest.raises(ValueError, match="session_secret: must be a string"):
        validate_config(cfg, env="dev")


def test_dashboard_password_non_string_fails():
    cfg = _config()
    cfg.dashboard_password = 123  # type: ignore[assignment]
    with pytest.raises(ValueError, match="dashboard_password: must be a string"):
        validate_config(cfg, env="dev")


def test_limit_as_string_fails():
    cfg = _config()
    cfg.switchbot_bulk_max_readings = "100"  # type: ignore[assignment]
    with pytest.raises(ValueError, match="switchbot_bulk_max_readings: must be an integer"):
        validate_config(cfg, env="dev")


def test_limit_as_none_fails():
    cfg = _config()
    cfg.switchbot_sync_max_intervals_total = None  # type: ignore[assignment]
    with pytest.raises(ValueError, match="switchbot_sync_max_intervals_total: must be an integer"):
        validate_config(cfg, env="dev")


def test_limit_as_bool_fails():
    cfg = _config()
    cfg.switchbot_bulk_max_readings = True  # type: ignore[assignment]
    with pytest.raises(ValueError, match="switchbot_bulk_max_readings: must be an integer"):
        validate_config(cfg, env="dev")


# --- load_config() startup boundary ---

def _write_app_json(tmp_path):
    config_dir = tmp_path / "config"
    config_dir.mkdir(exist_ok=True)
    (config_dir / "app.json").write_text(json.dumps({
        "session_secure": False,
        "database": {"driver": "postgresql+psycopg"},
    }))
    return config_dir


def _set_base_env(monkeypatch, **overrides):
    env = {
        "ENV": "dev",
        "API_KEY": "key",
        "SESSION_SECRET": "test-session-secret",
        "DASHBOARD_PASSWORD": "test-dashboard-password",
        "SESSION_SECURE": "false",
        "DATABASE_HOST": "localhost",
        "DATABASE_PORT": "5432",
        "DATABASE_NAME": "test",
        "DATABASE_USER": "postgres",
        "DATABASE_PASSWORD": "test",
    }
    env.update(overrides)
    for k, v in env.items():
        if v is None:
            monkeypatch.delenv(k, raising=False)
        else:
            monkeypatch.setenv(k, v)


def test_load_config_fails_on_invalid_field(tmp_path, monkeypatch):
    config_dir = _write_app_json(tmp_path)
    _set_base_env(monkeypatch, SESSION_SECRET="")

    with pytest.raises(ValueError, match="session_secret"):
        load_config(config_dir=config_dir)


def test_load_config_rejects_unknown_env(monkeypatch):
    monkeypatch.setenv("ENV", "production")
    with pytest.raises(ValueError, match="Unknown ENV="):
        load_config()


def test_load_config_fails_on_missing_field(tmp_path, monkeypatch):
    config_dir = _write_app_json(tmp_path)
    _set_base_env(monkeypatch, SESSION_SECRET=None)

    with pytest.raises(ValueError, match="SESSION_SECRET"):
        load_config(config_dir=config_dir)


# --- LevoitAhControllerConfig ---

def _levoit(**overrides) -> LevoitAhControllerConfig:
    defaults = dict(
        enabled=False,
        switchbot_mac=None,
        target_absolute_humidity=None,
    )
    defaults.update(overrides)
    return LevoitAhControllerConfig(**defaults)


def test_levoit_absent_uses_defaults():
    cfg = _config()
    validate_config(cfg, env="dev")
    assert cfg.levoit_ah_controller.enabled is False


def test_levoit_disabled_no_mac_passes():
    cfg = _config(levoit_ah_controller=_levoit(enabled=False))
    validate_config(cfg, env="dev")


def test_levoit_enabled_with_valid_mac_passes():
    lev = _levoit(enabled=True, switchbot_mac="aa:bb:cc:dd:ee:ff", target_absolute_humidity=8.0)
    cfg = _config(levoit_ah_controller=lev)
    validate_config(cfg, env="dev")


def test_levoit_enabled_missing_mac_fails():
    lev = _levoit(enabled=True, switchbot_mac=None, target_absolute_humidity=8.0)
    with pytest.raises(ValueError, match="switchbot_mac: required when enabled is true"):
        validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_invalid_mac_fails():
    lev = _levoit(enabled=True, switchbot_mac="not-a-mac", target_absolute_humidity=8.0)
    with pytest.raises(ValueError, match="switchbot_mac: invalid MAC address format"):
        validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_invalid_mac_when_disabled_still_fails():
    lev = _levoit(enabled=False, switchbot_mac="bad")
    with pytest.raises(ValueError, match="switchbot_mac: invalid MAC address format"):
        validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_zero_target_ah_fails():
    lev = _levoit(enabled=True, switchbot_mac="AA:BB:CC:DD:EE:FF", target_absolute_humidity=0.0)
    with pytest.raises(ValueError, match="target_absolute_humidity: must be a positive number"):
        validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_negative_target_ah_fails():
    lev = _levoit(enabled=True, switchbot_mac="AA:BB:CC:DD:EE:FF", target_absolute_humidity=-1.0)
    with pytest.raises(ValueError, match="target_absolute_humidity: must be a positive number"):
        validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_non_number_target_ah_fails():
    lev = _levoit(enabled=False)
    lev.target_absolute_humidity = "eight"  # type: ignore[assignment]
    with pytest.raises(ValueError, match="target_absolute_humidity: must be a number"):
        validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_enabled_missing_target_ah_fails():
    lev = _levoit(enabled=True, switchbot_mac="AA:BB:CC:DD:EE:FF", target_absolute_humidity=None)
    with pytest.raises(ValueError, match="target_absolute_humidity: required when enabled is true"):
        validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_min_humidity_above_100_fails():
    lev = _levoit()
    lev.minimum_humidity = 101
    with pytest.raises(ValueError, match="minimum_humidity: must be between 0 and 100"):
        validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_max_humidity_below_0_fails():
    lev = _levoit()
    lev.maximum_humidity = -1
    with pytest.raises(ValueError, match="maximum_humidity: must be between 0 and 100"):
        validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_min_humidity_exceeds_max_fails():
    lev = _levoit()
    lev.minimum_humidity = 70
    lev.maximum_humidity = 60
    with pytest.raises(ValueError, match="minimum_humidity: must be <= maximum_humidity"):
        validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_zero_reading_max_age_fails():
    lev = _levoit()
    lev.reading_max_age_seconds = 0
    with pytest.raises(ValueError, match="reading_max_age_seconds: must be a positive integer"):
        validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_zero_poll_interval_fails():
    lev = _levoit()
    lev.poll_interval_seconds = 0
    with pytest.raises(ValueError, match="poll_interval_seconds: must be a positive integer"):
        validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_zero_minimum_command_interval_fails():
    lev = _levoit()
    lev.minimum_command_interval_seconds = 0
    with pytest.raises(ValueError, match="minimum_command_interval_seconds: must be a positive integer"):
        validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_negative_humidity_change_threshold_fails():
    lev = _levoit()
    lev.humidity_change_threshold = -0.1
    with pytest.raises(ValueError, match="humidity_change_threshold: must be >= 0"):
        validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_zero_humidity_change_threshold_passes():
    lev = _levoit()
    lev.humidity_change_threshold = 0
    validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_non_bool_enabled_fails():
    lev = _levoit()
    lev.enabled = "true"  # type: ignore[assignment]
    with pytest.raises(ValueError, match="levoit_ah_controller.enabled: must be a boolean"):
        validate_config(_config(levoit_ah_controller=lev), env="dev")


def test_levoit_non_object_fails(tmp_path, monkeypatch):
    config_dir = tmp_path / "config"
    config_dir.mkdir()
    (config_dir / "app.json").write_text(json.dumps({
        "session_secure": False,
        "database": {"driver": "postgresql+psycopg"},
        "levoit_ah_controller": "yes please",
    }))
    _set_base_env(monkeypatch)
    with pytest.raises(ValueError, match="levoit_ah_controller: must be an object"):
        load_config(config_dir=config_dir)


def test_levoit_load_config_invalid_mac_in_json_fails(tmp_path, monkeypatch):
    config_dir = tmp_path / "config"
    config_dir.mkdir()
    (config_dir / "app.json").write_text(json.dumps({
        "session_secure": False,
        "database": {"driver": "postgresql+psycopg"},
        "levoit_ah_controller": {
            "enabled": False,
            "switchbot_mac": "not-a-mac",
        },
    }))
    _set_base_env(monkeypatch)
    with pytest.raises(ValueError, match="switchbot_mac"):
        load_config(config_dir=config_dir)


def test_levoit_load_config_parses_and_normalizes_mac(tmp_path, monkeypatch):
    config_dir = tmp_path / "config"
    config_dir.mkdir()
    (config_dir / "app.json").write_text(json.dumps({
        "session_secure": False,
        "database": {"driver": "postgresql+psycopg"},
        "levoit_ah_controller": {
            "enabled": True,
            "switchbot_mac": "aa:bb:cc:dd:ee:ff",
            "target_absolute_humidity": 8.0,
        },
    }))
    _set_base_env(monkeypatch)
    cfg = load_config(config_dir=config_dir)
    assert cfg.levoit_ah_controller.enabled is True
    assert cfg.levoit_ah_controller.switchbot_mac == "AA:BB:CC:DD:EE:FF"
    assert cfg.levoit_ah_controller.target_absolute_humidity == 8.0


def test_levoit_load_config_absent_uses_defaults(tmp_path, monkeypatch):
    config_dir = _write_app_json(tmp_path)
    _set_base_env(monkeypatch)
    cfg = load_config(config_dir=config_dir)
    lev = cfg.levoit_ah_controller
    assert lev.enabled is False
    assert lev.switchbot_mac is None
    assert lev.minimum_humidity == 40
    assert lev.maximum_humidity == 60
    assert lev.reading_max_age_seconds == 900
    assert lev.poll_interval_seconds == 300
    assert lev.minimum_command_interval_seconds == 300
    assert lev.humidity_change_threshold == 2.0


def test_levoit_default_overrides_from_app_json(tmp_path, monkeypatch):
    config_dir = tmp_path / "config"
    config_dir.mkdir()
    (config_dir / "app.json").write_text(json.dumps({
        "session_secure": False,
        "database": {"driver": "postgresql+psycopg"},
        "levoit_ah_controller": {
            "enabled": False,
            "poll_interval_seconds": 120,
            "humidity_change_threshold": 0.5,
        },
    }))
    _set_base_env(monkeypatch)
    cfg = load_config(config_dir=config_dir)
    assert cfg.levoit_ah_controller.poll_interval_seconds == 120
    assert cfg.levoit_ah_controller.humidity_change_threshold == 0.5
    assert cfg.levoit_ah_controller.minimum_humidity == 40  # default unchanged
