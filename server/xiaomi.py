from datetime import datetime

from pydantic import BaseModel, model_validator

from common import check_hard_ranges
from models import XIAOMI_TYPE, XiaomiReading
from sensor_spec import SensorSpec


class ReadingIn(BaseModel):
    mac: str
    name: str | None = None
    timestamp: datetime
    temperature_c: float | None = None
    moisture_pct: int | None = None
    light_lux: int | None = None
    conductivity_us_cm: int | None = None

    @model_validator(mode="after")
    def validate_fields(self):
        if self.name == "":
            raise ValueError("name must not be empty")

        if (
            self.temperature_c is None
            and self.moisture_pct is None
            and self.light_lux is None
            and self.conductivity_us_cm is None
        ):
            raise ValueError("at least one reading field must be non-null")

        check_hard_ranges(self, SENSOR.hard_ranges)
        return self


class ReadingOut(BaseModel):
    timestamp: datetime
    temperature_c: float | None
    moisture_pct: int | None
    light_lux: int | None
    conductivity_us_cm: int | None


SENSOR = SensorSpec(
    db_sensor_type=XIAOMI_TYPE,
    reading_model=XiaomiReading,
    unique_constraint_name="xiaomi_readings_mac_timestamp_uniq",
    data_fields=[
        "temperature_c",
        "moisture_pct",
        "light_lux",
        "conductivity_us_cm",
    ],
    hard_ranges={
        "temperature_c": (-40.0, 125.0),
        "moisture_pct": (0, 100),
        "light_lux": (0, 1_000_000),
        "conductivity_us_cm": (0, 100_000),
    },
    soft_ranges={
        "temperature_c": (-20.0, 60.0),
        "light_lux": (0, 200_000),
        "conductivity_us_cm": (0, 10_000),
    },
)
