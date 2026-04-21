from datetime import datetime

from pydantic import BaseModel, model_validator

from common import check_range, warn_if_suspicious
from models import XIAOMI_TYPE, XiaomiReading


HARD_TEMPERATURE_C_MIN = -40.0
HARD_TEMPERATURE_C_MAX = 125.0
HARD_MOISTURE_PCT_MIN = 0
HARD_MOISTURE_PCT_MAX = 100
HARD_LIGHT_LUX_MIN = 0
HARD_LIGHT_LUX_MAX = 1_000_000
HARD_CONDUCTIVITY_US_CM_MIN = 0
HARD_CONDUCTIVITY_US_CM_MAX = 100_000

SOFT_TEMPERATURE_C_MIN = -20.0
SOFT_TEMPERATURE_C_MAX = 60.0
SOFT_LIGHT_LUX_MAX = 200_000
SOFT_CONDUCTIVITY_US_CM_MAX = 10_000


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
        if (
            self.temperature_c is None
            and self.moisture_pct is None
            and self.light_lux is None
            and self.conductivity_us_cm is None
        ):
            raise ValueError("at least one reading field must be non-null")

        check_range("temperature_c", self.temperature_c, HARD_TEMPERATURE_C_MIN, HARD_TEMPERATURE_C_MAX)
        check_range("moisture_pct", self.moisture_pct, HARD_MOISTURE_PCT_MIN, HARD_MOISTURE_PCT_MAX)
        check_range("light_lux", self.light_lux, HARD_LIGHT_LUX_MIN, HARD_LIGHT_LUX_MAX)
        check_range(
            "conductivity_us_cm",
            self.conductivity_us_cm,
            HARD_CONDUCTIVITY_US_CM_MIN,
            HARD_CONDUCTIVITY_US_CM_MAX,
        )
        return self


class ReadingOut(BaseModel):
    timestamp: datetime
    temperature_c: float | None
    moisture_pct: int | None
    light_lux: int | None
    conductivity_us_cm: int | None


def maybe_warn(reading: ReadingIn):
    if reading.temperature_c is not None:
        if reading.temperature_c < SOFT_TEMPERATURE_C_MIN or reading.temperature_c > SOFT_TEMPERATURE_C_MAX:
            warn_if_suspicious("temperature_c", reading.temperature_c, reading.mac)

    if reading.light_lux is not None and reading.light_lux > SOFT_LIGHT_LUX_MAX:
        warn_if_suspicious("light_lux", reading.light_lux, reading.mac)

    if reading.conductivity_us_cm is not None and reading.conductivity_us_cm > SOFT_CONDUCTIVITY_US_CM_MAX:
        warn_if_suspicious("conductivity_us_cm", reading.conductivity_us_cm, reading.mac)


def build_row(reading: ReadingIn) -> XiaomiReading:
    return XiaomiReading(
        mac=reading.mac,
        timestamp=reading.timestamp,
        temperature_c=reading.temperature_c,
        moisture_pct=reading.moisture_pct,
        light_lux=reading.light_lux,
        conductivity_us_cm=reading.conductivity_us_cm,
    )


SENSOR_TYPE_DB = XIAOMI_TYPE
READING_MODEL = XiaomiReading
