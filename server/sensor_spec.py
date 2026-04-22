from dataclasses import dataclass
from typing import Any


Range = tuple[int | float, int | float]


@dataclass(frozen=True)
class SensorSpec:
    db_sensor_type: int
    reading_model: Any
    data_fields: list[str]
    hard_ranges: dict[str, Range]
    soft_ranges: dict[str, Range]
