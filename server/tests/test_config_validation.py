import json
import os

import pytest

from app.config import Config, DatabaseConfig, load_config, validate_config


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

def _write_config(tmp_path, env_name, cfg_data):
    config_dir = tmp_path / "config"
    config_dir.mkdir(exist_ok=True)
    (config_dir / f"{env_name}.json").write_text(json.dumps(cfg_data))


_BASE_DB = {
    "driver": "postgresql+psycopg",
    "host": "localhost",
    "port": 5432,
    "name": "test",
    "user": "postgres",
    "password": "test",
}


def test_load_config_fails_on_invalid_field(tmp_path, monkeypatch):
    cfg_data = {
        "api_key": "key",
        "dashboard_password": "",
        "session_secret": "",
        "session_secure": False,
        "database": _BASE_DB,
    }
    _write_config(tmp_path, "dev", cfg_data)
    monkeypatch.setenv("ENV", "dev")

    with pytest.raises(ValueError, match="session_secret"):
        load_config(config_dir=tmp_path / "config")


def test_load_config_rejects_unknown_env(monkeypatch):
    monkeypatch.setenv("ENV", "production")
    with pytest.raises(ValueError, match="Unknown ENV="):
        load_config()


def test_load_config_fails_on_missing_field(tmp_path, monkeypatch):
    cfg_data = {
        "api_key": "key",
        "dashboard_password": "test-dashboard-password",
        # session_secret intentionally omitted
        "session_secure": False,
        "database": _BASE_DB,
    }
    _write_config(tmp_path, "dev", cfg_data)
    monkeypatch.setenv("ENV", "dev")

    with pytest.raises(ValueError, match="session_secret"):
        load_config(config_dir=tmp_path / "config")
