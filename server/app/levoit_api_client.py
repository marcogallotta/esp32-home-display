from __future__ import annotations

from dataclasses import dataclass

import httpx


class LevoitApiError(Exception):
    """HTTP request failed or response was malformed."""


class SwitchbotSensorNotFound(Exception):
    """Configured switchbot_mac was not present in /sensors/latest response."""


@dataclass
class SwitchbotLatestReading:
    sensor_id: str
    mac: str
    timestamp: str
    temperature_c: float | None
    humidity_pct: float | None


class LevoitApiClient:
    def __init__(
        self,
        base_url: str,
        api_key: str,
        switchbot_mac: str,
        http_client: httpx.Client | None = None,
    ) -> None:
        self._base_url = base_url.rstrip("/")
        self._api_key = api_key
        self._switchbot_mac = switchbot_mac.upper()
        self._http = http_client or httpx.Client()

    def fetch_switchbot_latest(self) -> SwitchbotLatestReading:
        try:
            response = self._http.get(
                f"{self._base_url}/sensors/latest",
                headers={"x-api-key": self._api_key},
            )
            response.raise_for_status()
        except httpx.HTTPStatusError as exc:
            raise LevoitApiError(
                f"GET /sensors/latest failed: HTTP {exc.response.status_code}"
            ) from exc
        except httpx.RequestError as exc:
            raise LevoitApiError(f"GET /sensors/latest request error: {exc}") from exc

        try:
            sensors = response.json()["sensors"]
        except (ValueError, KeyError, TypeError) as exc:
            raise LevoitApiError(f"GET /sensors/latest malformed response: {exc}") from exc

        for entry in sensors:
            try:
                mac = entry["mac"]
            except (KeyError, TypeError):
                continue
            if mac.upper() == self._switchbot_mac:
                try:
                    reading = entry.get("reading") or {}
                    return SwitchbotLatestReading(
                        sensor_id=str(entry["sensor_id"]),
                        mac=mac,
                        timestamp=entry["latest_timestamp"],
                        temperature_c=reading.get("temperature_c"),
                        humidity_pct=reading.get("humidity_pct"),
                    )
                except (KeyError, TypeError) as exc:
                    raise LevoitApiError(
                        f"GET /sensors/latest malformed matching sensor entry: {exc}"
                    ) from exc

        raise SwitchbotSensorNotFound(
            f"MAC {self._switchbot_mac} not found in /sensors/latest response"
        )
