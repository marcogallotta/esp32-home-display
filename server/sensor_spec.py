from dataclasses import dataclass
from typing import Any, Callable


@dataclass(frozen=True)
class SensorSpec:
    db_sensor_type: int
    reading_model: Any
    data_fields: list[str]
    maybe_warn: Callable[[Any], None]
