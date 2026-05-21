from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from app.levoit_vesync import LevoitVeSyncClient, VeSyncDeviceNotFound, VeSyncError


def _client(cid="test-cid"):
    return LevoitVeSyncClient(
        username="user@example.com",
        password="password",
        cid=cid,
    )


def _mock_device(cid="test-cid", name="Bedroom Humidifier", device_type="LUH-A602S",
                 target_humidity=45):
    device = MagicMock()
    device.cid = cid
    device.device_name = name
    device.device_type = device_type
    device.state.auto_target_humidity = target_humidity
    device.update = AsyncMock()
    device.set_humidity = AsyncMock(return_value=True)
    return device


def _mock_manager(devices=None, login_ok=True):
    manager = MagicMock()
    manager.login = AsyncMock(return_value=login_ok)
    manager.get_devices = AsyncMock()
    manager.__aenter__ = AsyncMock(return_value=manager)
    manager.__aexit__ = AsyncMock(return_value=None)
    type(manager.devices).humidifiers = property(lambda self: devices or [])
    return manager


def _patch_vesync(manager):
    return patch("pyvesync.VeSync", return_value=manager)


# --- fetch_state ---

def test_fetch_state_returns_device_info():
    device = _mock_device(target_humidity=44)
    manager = _mock_manager(devices=[device])
    with _patch_vesync(manager):
        state = _client().fetch_state()
    assert state.cid == "test-cid"
    assert state.name == "Bedroom Humidifier"
    assert state.device_type == "LUH-A602S"
    assert state.current_target_humidity == 44


def test_fetch_state_device_not_found_raises():
    manager = _mock_manager(devices=[])
    with _patch_vesync(manager):
        with pytest.raises(VeSyncDeviceNotFound, match="test-cid"):
            _client().fetch_state()


def test_fetch_state_login_failed_raises():
    manager = _mock_manager(login_ok=False)
    with _patch_vesync(manager):
        with pytest.raises(VeSyncError, match="login failed"):
            _client().fetch_state()


# --- set_humidity ---

def test_set_humidity_calls_device():
    device = _mock_device()
    manager = _mock_manager(devices=[device])
    with _patch_vesync(manager):
        _client().set_humidity(50)
    device.set_humidity.assert_awaited_once_with(50)


def test_set_humidity_false_raises():
    device = _mock_device()
    device.set_humidity = AsyncMock(return_value=False)
    manager = _mock_manager(devices=[device])
    with _patch_vesync(manager):
        with pytest.raises(VeSyncError, match="returned false"):
            _client().set_humidity(50)


def test_set_humidity_wrong_cid_raises():
    device = _mock_device(cid="other-cid")
    manager = _mock_manager(devices=[device])
    with _patch_vesync(manager):
        with pytest.raises(VeSyncDeviceNotFound):
            _client(cid="test-cid").set_humidity(50)
