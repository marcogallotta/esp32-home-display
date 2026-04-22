from datetime import datetime

from pydantic import BaseModel, model_validator

from common import check_range, warn_if_suspicious
from models import SWITCHBOT_TYPE, SwitchbotReading
from sensor_spec import SensorSpec


HARD_TEMPERATURE_C_MIN = -40.0
HARD_TEMPERATURE_C_MAX = 125.0
HARD_HUMIDITY_PCT_MIN = 0.0
HARD_HUMIDITY_PCT_MAX = 100.0

SOFT_TEMPERATURE_C_MIN = -20.0
SOFT_TEMPERATURE_C_MAX = 60.0


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

        check_range("temperature_c", self.temperature_c, HARD_TEMPERATURE_C_MIN, HARD_TEMPERATURE_C_MAX)
        check_range("humidity_pct", self.humidity_pct, HARD_HUMIDITY_PCT_MIN, HARD_HUMIDITY_PCT_MAX)
        return self


class ReadingOut(BaseModel):
    timestamp: datetime
    temperature_c: float
    humidity_pct: float


def maybe_warn(reading: ReadingIn):
    if reading.temperature_c < SOFT_TEMPERATURE_C_MIN or reading.temperature_c > SOFT_TEMPERATURE_C_MAX:
        warn_if_suspicious("temperature_c", reading.temperature_c, reading.mac)


SENSOR = SensorSpec(
    db_sensor_type=SWITCHBOT_TYPE,
    reading_model=SwitchbotReading,
    data_fields=["temperature_c", "humidity_pct"],
    maybe_warn=maybe_warn,
)
