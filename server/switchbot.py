from datetime import datetime

from pydantic import BaseModel, model_validator

from common import check_hard_ranges
from models import SWITCHBOT_TYPE, SwitchbotReading
from sensor_spec import DataField, SensorSpec


class ReadingIn(BaseModel):
    mac: str
    name: str | None = None
    timestamp: datetime
    temperature_c: float
    humidity_pct: float

    @model_validator(mode="after")
    def validate_fields(self):
        if self.name == "":
            raise ValueError("name must not be empty")

        check_hard_ranges(self, SENSOR.hard_ranges)
        return self


class ReadingOut(BaseModel):
    timestamp: datetime
    temperature_c: float
    humidity_pct: float


SENSOR = SensorSpec[ReadingIn, ReadingOut, SwitchbotReading](
    db_sensor_type=SWITCHBOT_TYPE,
    reading_model=SwitchbotReading,
    reading_out=ReadingOut,
    unique_constraint_name="switchbot_readings_mac_timestamp_uniq",
    data_fields=(
        DataField("temperature_c", SwitchbotReading.temperature_c),
        DataField("humidity_pct", SwitchbotReading.humidity_pct),
    ),
    hard_ranges={
        "temperature_c": (-40.0, 125.0),
        "humidity_pct": (0.0, 100.0),
    },
    soft_ranges={
        "temperature_c": (-20.0, 60.0),
    },
)
