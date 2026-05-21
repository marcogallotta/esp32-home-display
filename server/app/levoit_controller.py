from __future__ import annotations

import math
from dataclasses import dataclass
from datetime import datetime
from typing import Literal


@dataclass
class LevoitControlDecision:
    action: Literal["set", "skip"]
    reason: str
    current_absolute_humidity: float | None
    ideal_humidity: float | None
    commanded_humidity: int | None
    age_seconds: float | None


def _saturation_vapour_pressure(temperature_c: float) -> float:
    """Magnus formula, result in hPa."""
    return 6.112 * math.exp(17.67 * temperature_c / (temperature_c + 243.5))


def absolute_humidity(temperature_c: float, humidity_pct: float) -> float:
    """Absolute humidity in g/m³ from temperature (°C) and relative humidity (%)."""
    es = _saturation_vapour_pressure(temperature_c)
    return 216.7 * es * humidity_pct / 100.0 / (temperature_c + 273.15)


def target_relative_humidity(temperature_c: float, target_ah: float) -> float:
    """RH (%) that achieves target_ah (g/m³) at temperature_c (°C)."""
    es = _saturation_vapour_pressure(temperature_c)
    return target_ah * (temperature_c + 273.15) * 100.0 / (216.7 * es)


def _parse_timestamp(timestamp: str | None) -> tuple[datetime | None, str | None]:
    if not timestamp:
        return None, "reading timestamp missing"
    try:
        return datetime.fromisoformat(timestamp.replace("Z", "+00:00")), None
    except (ValueError, AttributeError):
        return None, f"reading timestamp invalid: {timestamp!r}"


def compute_decision(
    *,
    timestamp: str | None,
    temperature_c: float | None,
    humidity_pct: float | None,
    now: datetime,
    target_absolute_humidity: float,
    minimum_humidity: int,
    maximum_humidity: int,
    reading_max_age_seconds: int,
    minimum_command_interval_seconds: int,
    humidity_change_threshold: float,
    last_command_time: datetime | None = None,
    last_commanded_humidity: int | None = None,
    current_device_target_humidity: int | None = None,
) -> LevoitControlDecision:
    def _skip(
        reason: str,
        *,
        age: float | None = None,
        current_ah: float | None = None,
        ideal: float | None = None,
        commanded: int | None = None,
    ) -> LevoitControlDecision:
        return LevoitControlDecision(
            action="skip",
            reason=reason,
            current_absolute_humidity=current_ah,
            ideal_humidity=ideal,
            commanded_humidity=commanded,
            age_seconds=age,
        )

    ts, err = _parse_timestamp(timestamp)
    if ts is None:
        return _skip(err)  # type: ignore[arg-type]

    age_seconds = (now - ts).total_seconds()
    if age_seconds > reading_max_age_seconds:
        return _skip(f"reading too old ({age_seconds:.0f}s)", age=age_seconds)

    if temperature_c is None:
        return _skip("temperature missing", age=age_seconds)

    current_ah = (
        absolute_humidity(temperature_c, humidity_pct)
        if humidity_pct is not None
        else None
    )

    ideal = target_relative_humidity(temperature_c, target_absolute_humidity)
    clamped = max(float(minimum_humidity), min(float(maximum_humidity), ideal))
    commanded = round(clamped)

    if last_command_time is not None:
        seconds_since = (now - last_command_time).total_seconds()
        if seconds_since < minimum_command_interval_seconds:
            return _skip(
                f"command interval too recent ({seconds_since:.0f}s < {minimum_command_interval_seconds}s)",
                age=age_seconds,
                current_ah=current_ah,
                ideal=ideal,
                commanded=commanded,
            )

    if last_commanded_humidity is not None:
        if abs(commanded - last_commanded_humidity) < humidity_change_threshold:
            return _skip(
                f"change within threshold vs last command ({commanded} vs {last_commanded_humidity})",
                age=age_seconds,
                current_ah=current_ah,
                ideal=ideal,
                commanded=commanded,
            )

    if current_device_target_humidity is not None:
        if abs(commanded - current_device_target_humidity) < humidity_change_threshold:
            return _skip(
                f"change within threshold vs device target ({commanded} vs {current_device_target_humidity})",
                age=age_seconds,
                current_ah=current_ah,
                ideal=ideal,
                commanded=commanded,
            )

    return LevoitControlDecision(
        action="set",
        reason="ok",
        current_absolute_humidity=current_ah,
        ideal_humidity=ideal,
        commanded_humidity=commanded,
        age_seconds=age_seconds,
    )
