from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Literal


@dataclass
class LevoitControlDecision:
    action: Literal["set", "skip"]
    reason: str
    current_absolute_humidity: float | None
    ideal_humidity: float | None
    commanded_humidity: int | None


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


def compute_decision(
    *,
    temperature_c: float | None,
    humidity_pct: float | None,
    target_absolute_humidity: float,
    minimum_humidity: int,
    maximum_humidity: int,
    humidity_change_threshold: float,
    last_commanded_humidity: int | None = None,
    current_device_target_humidity: int | None = None,
) -> LevoitControlDecision:
    def _skip(
        reason: str,
        *,
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
        )

    if temperature_c is None:
        return _skip("temperature missing")

    current_ah = (
        absolute_humidity(temperature_c, humidity_pct)
        if humidity_pct is not None
        else None
    )

    ideal = target_relative_humidity(temperature_c, target_absolute_humidity)
    clamped = max(float(minimum_humidity), min(float(maximum_humidity), ideal))
    commanded = round(clamped)

    for ref, label in (
        (last_commanded_humidity, "last command"),
        (current_device_target_humidity, "device target"),
    ):
        if ref is not None and abs(commanded - ref) < humidity_change_threshold:
            return _skip(
                f"change within threshold ({commanded} vs {ref}, {label})",
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
    )
