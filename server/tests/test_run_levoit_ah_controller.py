from unittest.mock import MagicMock

from app.config import Config, DatabaseConfig, LevoitAhControllerConfig
from app.levoit_api_client import LevoitApiError, SwitchbotLatestReading, SwitchbotSensorNotFound
from app.levoit_controller import LevoitControlDecision
from app.levoit_runner import format_decision, run_once


def _db():
    return DatabaseConfig(
        driver="postgresql+psycopg",
        host="localhost", port=5432,
        name="test", user="postgres", password="test",
    )


def _levoit_cfg(**kwargs) -> LevoitAhControllerConfig:
    defaults = dict(
        switchbot_mac="AA:BB:CC:DD:EE:FF",
        target_absolute_humidity=8.0,
        server_base_url="https://laptop.local:8000/",
    )
    defaults.update(kwargs)
    return LevoitAhControllerConfig(**defaults)


def _config(**kwargs) -> Config:
    return Config(
        api_key="test-key",
        dashboard_password="test-pw",
        session_secret="test-secret",
        session_secure=False,
        database=_db(),
        levoit_ah_controller=_levoit_cfg(**kwargs),
    )


def _reading(**kwargs) -> SwitchbotLatestReading:
    defaults = dict(
        sensor_id="11111111-1111-1111-1111-111111111111",
        mac="AA:BB:CC:DD:EE:FF",
        timestamp="2026-04-21T11:55:00Z",
        temperature_c=20.0,
        humidity_pct=50.0,
    )
    defaults.update(kwargs)
    return SwitchbotLatestReading(**defaults)


def _mock_client(reading=None, error=None):
    client = MagicMock()
    if error:
        client.fetch_switchbot_latest.side_effect = error
    else:
        client.fetch_switchbot_latest.return_value = reading or _reading()
    return client


def _run(**kwargs):
    kwargs.setdefault("api_client", _mock_client())
    return run_once(_config(), **kwargs)


# --- format_decision ---

def test_format_decision_contains_all_expected_fields():
    reading = _reading()
    decision = LevoitControlDecision(
        action="set",
        reason="ok",
        current_absolute_humidity=8.64,
        ideal_humidity=46.3,
        commanded_humidity=46,
    )
    out = format_decision(reading, decision, _levoit_cfg())
    assert "AA:BB:CC:DD:EE:FF" in out
    assert "20.0" in out
    assert "50.0" in out
    assert "8.0" in out
    assert "46" in out
    assert "set" in out
    assert "ok" in out


# --- missing required config fields ---

def test_missing_mac_exits_nonzero():
    cfg = _config(switchbot_mac=None)
    code, msg = run_once(cfg, api_client=_mock_client())
    assert code == 1
    assert "mac" in msg.lower()


def test_missing_target_ah_exits_nonzero():
    cfg = _config(target_absolute_humidity=None)
    code, msg = run_once(cfg, api_client=_mock_client())
    assert code == 1
    assert "target_absolute_humidity" in msg


def test_missing_server_base_url_exits_nonzero():
    cfg = _config(server_base_url=None)
    code, msg = run_once(cfg, api_client=_mock_client())
    assert code == 1
    assert "server_base_url" in msg


# --- set decision ---

def test_set_decision_exits_zero_and_contains_output():
    code, msg = _run()
    assert code == 0
    assert "set" in msg
    assert "action" in msg
    assert "commanded_humidity" in msg


def test_dry_run_prefixes_output():
    code, msg = _run(dry_run=True)
    assert code == 0
    assert msg.startswith("[DRY RUN]")


# --- skip decisions ---

def test_missing_temperature_exits_zero_with_skip():
    code, msg = _run(api_client=_mock_client(reading=_reading(temperature_c=None)))
    assert code == 0
    assert "skip" in msg
    assert "temperature" in msg


def test_missing_humidity_does_not_block_set():
    code, msg = _run(api_client=_mock_client(reading=_reading(humidity_pct=None)))
    assert code == 0
    assert "set" in msg


# --- API errors ---

def test_sensor_not_found_exits_nonzero():
    code, msg = _run(api_client=_mock_client(error=SwitchbotSensorNotFound("not found")))
    assert code == 1
    assert "not found" in msg.lower()


def test_api_error_exits_nonzero():
    code, msg = _run(api_client=_mock_client(error=LevoitApiError("HTTP 503")))
    assert code == 1
    assert "API error" in msg or "503" in msg


def test_url_is_read_from_config_not_hardcoded():
    import inspect
    from app.levoit_runner import run_once as _run_once
    sig = inspect.signature(_run_once)
    assert "base_url" not in sig.parameters


# --- no pyvesync ---

def test_no_pyvesync_import():
    import sys
    import app.levoit_runner
    import app.levoit_controller
    import app.levoit_api_client
    assert "pyvesync" not in sys.modules
