from datetime import datetime

from pydantic import BaseModel, model_validator

from common import check_hard_ranges
from models import XIAOMI_TYPE, XiaomiReading
from sensor_spec import DataField, SensorSpec


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

        check_hard_ranges(self, SENSOR.data_fields)
        return self


class ReadingOut(BaseModel):
    timestamp: datetime
    temperature_c: float | None
    moisture_pct: int | None
    light_lux: int | None
    conductivity_us_cm: int | None


SENSOR = SensorSpec[ReadingIn, ReadingOut, XiaomiReading](
    db_sensor_type=XIAOMI_TYPE,
    reading_model=XiaomiReading,
    reading_out=ReadingOut,
    unique_constraint_name="xiaomi_readings_mac_timestamp_uniq",
    data_fields=(
        DataField("temperature_c", XiaomiReading.temperature_c, lambda r: r.temperature_c, hard_range=(-40.0, 125.0), soft_range=(-20.0, 60.0)),
        DataField("moisture_pct", XiaomiReading.moisture_pct, lambda r: r.moisture_pct, hard_range=(0, 100)),
        DataField("light_lux", XiaomiReading.light_lux, lambda r: r.light_lux, hard_range=(0, 1_000_000), soft_range=(0, 200_000)),
        DataField("conductivity_us_cm", XiaomiReading.conductivity_us_cm, lambda r: r.conductivity_us_cm, hard_range=(0, 100_000), soft_range=(0, 10_000)),
    ),
)
