from __future__ import annotations

import asyncio
from dataclasses import dataclass


class VeSyncError(Exception):
    pass


class VeSyncDeviceNotFound(VeSyncError):
    pass


@dataclass
class HumidifierState:
    cid: str
    name: str
    device_type: str
    current_target_humidity: int | None


class LevoitVeSyncClient:
    def __init__(self, username: str, password: str, cid: str) -> None:
        self._username = username
        self._password = password
        self._cid = cid

    def fetch_state(self) -> HumidifierState:
        return asyncio.run(self._fetch_state())

    def set_humidity(self, humidity: int) -> None:
        asyncio.run(self._set_humidity(humidity))

    async def _connect(self):
        from pyvesync import VeSync  # noqa: PLC0415
        manager = VeSync(self._username, self._password)
        await manager.__aenter__()
        if not await manager.login():
            await manager.__aexit__(None, None, None)
            raise VeSyncError("VeSync login failed")
        await manager.get_devices()
        for device in manager.devices.humidifiers:
            if device.cid == self._cid:
                await device.update()
                return manager, device
        await manager.__aexit__(None, None, None)
        raise VeSyncDeviceNotFound(f"humidifier with CID {self._cid!r} not found")

    async def _fetch_state(self) -> HumidifierState:
        manager, device = await self._connect()
        try:
            target = device.state.auto_target_humidity
            return HumidifierState(
                cid=device.cid,
                name=device.device_name,
                device_type=device.device_type,
                current_target_humidity=int(target) if target is not None else None,
            )
        finally:
            await manager.__aexit__(None, None, None)

    async def _set_humidity(self, humidity: int) -> None:
        manager, device = await self._connect()
        try:
            ok = await device.set_humidity(humidity)
            if not ok:
                raise VeSyncError(
                    f"set_humidity({humidity}) returned false for device {device.device_name!r}"
                )
        finally:
            await manager.__aexit__(None, None, None)
