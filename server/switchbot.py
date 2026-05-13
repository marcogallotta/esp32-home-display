from datetime import datetime
from typing import Any
from uuid import UUID

from pydantic import BaseModel, Field, model_validator

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

        check_hard_ranges(self, SENSOR.data_fields)
        return self


class ReadingOut(BaseModel):
    timestamp: datetime
    temperature_c: float
    humidity_pct: float


class SensorIn(BaseModel):
    mac: str
    name: str | None = None

    @model_validator(mode="after")
    def validate_fields(self):
        if self.name == "":
            raise ValueError("name must not be empty")
        return self


class SensorsIn(BaseModel):
    sensors: list[SensorIn]

    @model_validator(mode="after")
    def validate_fields(self):
        if not self.sensors:
            raise ValueError("sensors must not be empty")
        return self


class SyncIntervalOut(BaseModel):
    start: datetime
    end: datetime


class SensorOut(BaseModel):
    mac: str
    sensor_id: UUID
    first_timestamp: datetime | None
    latest_timestamp: datetime | None
    sync_intervals: list[SyncIntervalOut]
    sync_intervals_capped: bool = False


class SyncWarningOut(BaseModel):
    code: str
    message: str


class SensorsOut(BaseModel):
    sensors: list[SensorOut]
    warnings: list[SyncWarningOut] = Field(default_factory=list)


class BulkIn(BaseModel):
    sensor_id: UUID
    readings: list[Any]

    @model_validator(mode="after")
    def validate_fields(self):
        if not self.readings:
            raise ValueError("readings must not be empty")
        return self


class BulkErrorOut(BaseModel):
    index: int
    code: str
    message: str


class BulkOut(BaseModel):
    status: str
    received: int
    created: int
    duplicate: int
    conflict: int
    invalid: int
    errors: list[BulkErrorOut]


SENSOR = SensorSpec[ReadingIn, ReadingOut, SwitchbotReading](
    db_sensor_type=SWITCHBOT_TYPE,
    reading_model=SwitchbotReading,
    reading_out=ReadingOut,
    unique_constraint_name="switchbot_readings_mac_timestamp_uniq",
    data_fields=(
        DataField("temperature_c", SwitchbotReading.temperature_c, hard_range=(-40.0, 125.0), soft_range=(-20.0, 60.0)),
        DataField("humidity_pct", SwitchbotReading.humidity_pct, hard_range=(0.0, 100.0)),
    ),
)
