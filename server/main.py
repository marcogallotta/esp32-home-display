import re
from datetime import datetime, timedelta, timezone
from enum import Enum
import logging
from typing import Annotated, Optional
import os

from fastapi import APIRouter, Depends, FastAPI, Header, HTTPException, Query
from pydantic import BaseModel, model_validator
from sqlalchemy import select
from sqlalchemy.orm import Session

from models import (
    SessionLocal,
    Sensor,
    SwitchbotReading,
    XiaomiReading,
    SWITCHBOT_TYPE,
    XIAOMI_TYPE,
)

app = FastAPI()
logger = logging.getLogger(__name__)
API_KEY = os.getenv("API_KEY")
def require_api_key(x_api_key: str = Header(...)):
    if x_api_key != API_KEY:
        raise HTTPException(status_code=401)
protected = APIRouter(dependencies=[Depends(require_api_key)])

HARD_TEMPERATURE_C_MIN = -40.0
HARD_TEMPERATURE_C_MAX = 125.0
HARD_HUMIDITY_PCT_MIN = 0.0
HARD_HUMIDITY_PCT_MAX = 100.0
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

MAC_ADDRESS_RE = re.compile(r"^(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$")

READINGS_DEFAULT_LIMIT = 10
READINGS_MAX_LIMIT = 100

QUERY_TIMESTAMP_MIN = datetime(2000, 1, 1, tzinfo=timezone.utc)
QUERY_TIMESTAMP_MAX_SKEW = timedelta(days=7)


class SensorType(str, Enum):
    switchbot = "switchbot"
    xiaomi = "xiaomi"


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def check_range(name: str, value, min_value, max_value):
    if value is None:
        return
    if value < min_value or value > max_value:
        raise ValueError(f"{name} must be in [{min_value}, {max_value}]")


def warn_if_suspicious(metric: str, value, mac: str):
    logger.warning("Suspicious %s=%s for sensor %s", metric, value, mac)


class SwitchbotReadingIn(BaseModel):
    mac: str
    name: Optional[str] = None
    type: SensorType
    timestamp: datetime
    temperature_c: float
    humidity_pct: float

    @model_validator(mode="after")
    def validate_fields(self):
        if self.type != SensorType.switchbot:
            raise ValueError("type must be 'switchbot'")

        check_range("temperature_c", self.temperature_c, HARD_TEMPERATURE_C_MIN, HARD_TEMPERATURE_C_MAX)
        check_range("humidity_pct", self.humidity_pct, HARD_HUMIDITY_PCT_MIN, HARD_HUMIDITY_PCT_MAX)
        return self


class XiaomiReadingIn(BaseModel):
    mac: str
    name: Optional[str] = None
    type: SensorType
    timestamp: datetime
    temperature_c: Optional[float] = None
    moisture_pct: Optional[int] = None
    light_lux: Optional[int] = None
    conductivity_us_cm: Optional[int] = None

    @model_validator(mode="after")
    def validate_fields(self):
        if self.type != SensorType.xiaomi:
            raise ValueError("type must be 'xiaomi'")

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
        check_range("conductivity_us_cm", self.conductivity_us_cm, HARD_CONDUCTIVITY_US_CM_MIN, HARD_CONDUCTIVITY_US_CM_MAX)
        return self


def sensor_type_to_db(sensor_type: SensorType) -> int:
    if sensor_type == SensorType.switchbot:
        return SWITCHBOT_TYPE
    return XIAOMI_TYPE


def ensure_sensor(db: Session, mac: str, name: Optional[str], sensor_type: int):
    sensor = db.get(Sensor, mac)

    if sensor is None:
        if name is None:
            raise HTTPException(status_code=400, detail="name required for unknown sensor")
        db.add(Sensor(mac=mac, name=name, type=sensor_type))
        return

    if sensor.type != sensor_type:
        raise HTTPException(status_code=400, detail="sensor type does not match existing sensor")

    if name is not None and sensor.name != name:
        raise HTTPException(status_code=400, detail="sensor name does not match existing sensor")


def maybe_warn_switchbot(reading: SwitchbotReadingIn):
    if reading.temperature_c < SOFT_TEMPERATURE_C_MIN or reading.temperature_c > SOFT_TEMPERATURE_C_MAX:
        warn_if_suspicious("temperature_c", reading.temperature_c, reading.mac)


def maybe_warn_xiaomi(reading: XiaomiReadingIn):
    if reading.temperature_c is not None:
        if reading.temperature_c < SOFT_TEMPERATURE_C_MIN or reading.temperature_c > SOFT_TEMPERATURE_C_MAX:
            warn_if_suspicious("temperature_c", reading.temperature_c, reading.mac)

    if reading.light_lux is not None and reading.light_lux > SOFT_LIGHT_LUX_MAX:
        warn_if_suspicious("light_lux", reading.light_lux, reading.mac)

    if reading.conductivity_us_cm is not None and reading.conductivity_us_cm > SOFT_CONDUCTIVITY_US_CM_MAX:
        warn_if_suspicious("conductivity_us_cm", reading.conductivity_us_cm, reading.mac)


@app.get("/health")
def health():
    return {"ok": True}

def validate_mac_address(mac: str) -> str:
    if not MAC_ADDRESS_RE.fullmatch(mac):
        raise HTTPException(status_code=400, detail="invalid mac format")
    return mac.upper()


def validate_query_timestamp(name: str, value: datetime | None) -> datetime | None:
    if value is None:
        return None

    if value.tzinfo is None:
        raise HTTPException(status_code=400, detail=f"{name} must include timezone")

    now = datetime.now(timezone.utc)
    if value < QUERY_TIMESTAMP_MIN:
        raise HTTPException(status_code=400, detail=f"{name} is too old")
    if value > now + QUERY_TIMESTAMP_MAX_SKEW:
        raise HTTPException(status_code=400, detail=f"{name} is too far in the future")

    return value


class SwitchbotReadingOut(BaseModel):
    timestamp: datetime
    temperature_c: float
    humidity_pct: float


@protected.get("/switchbot/readings", response_model=list[SwitchbotReadingOut])
def get_switchbot_readings(
    mac: str,
    limit: Annotated[int, Query(ge=0, le=READINGS_MAX_LIMIT)] = READINGS_DEFAULT_LIMIT,
    before: datetime | None = None,
    after: datetime | None = None,
    db: Session = Depends(get_db),
):
    mac = validate_mac_address(mac)
    before = validate_query_timestamp("before", before)
    after = validate_query_timestamp("after", after)

    if before is not None and after is not None and after > before:
        raise HTTPException(status_code=400, detail="after must be <= before")

    sensor = db.get(Sensor, mac)
    if sensor is None:
        return []

    stmt = (
        select(
            SwitchbotReading.timestamp,
            SwitchbotReading.temperature_c,
            SwitchbotReading.humidity_pct,
        )
        .where(SwitchbotReading.mac == mac)
        .order_by(SwitchbotReading.timestamp.desc())
        .limit(limit)
    )

    if before is not None:
        stmt = stmt.where(SwitchbotReading.timestamp <= before)

    if after is not None:
        stmt = stmt.where(SwitchbotReading.timestamp >= after)

    rows = db.execute(stmt).all()

    return [
        SwitchbotReadingOut(
            timestamp=row.timestamp,
            temperature_c=row.temperature_c,
            humidity_pct=row.humidity_pct,
        )
        for row in rows
    ]


class XiaomiReadingOut(BaseModel):
    timestamp: datetime
    temperature_c: Optional[float]
    moisture_pct: Optional[int]
    light_lux: Optional[int]
    conductivity_us_cm: Optional[int]


@protected.get("/xiaomi/readings", response_model=list[XiaomiReadingOut])
def get_xiaomi_readings(
    mac: str,
    limit: Annotated[int, Query(ge=0, le=READINGS_MAX_LIMIT)] = READINGS_DEFAULT_LIMIT,
    before: datetime | None = None,
    after: datetime | None = None,
    db: Session = Depends(get_db),
):
    mac = validate_mac_address(mac)
    before = validate_query_timestamp("before", before)
    after = validate_query_timestamp("after", after)

    if before is not None and after is not None and after > before:
        raise HTTPException(status_code=400, detail="after must be <= before")

    sensor = db.get(Sensor, mac)
    if sensor is None:
        return []

    stmt = (
        select(
            XiaomiReading.timestamp,
            XiaomiReading.temperature_c,
            XiaomiReading.moisture_pct,
            XiaomiReading.light_lux,
            XiaomiReading.conductivity_us_cm,
        )
        .where(XiaomiReading.mac == mac)
        .order_by(XiaomiReading.timestamp.desc())
        .limit(limit)
    )

    if before is not None:
        stmt = stmt.where(XiaomiReading.timestamp <= before)

    if after is not None:
        stmt = stmt.where(XiaomiReading.timestamp >= after)

    rows = db.execute(stmt).all()

    return [
        XiaomiReadingOut(
            timestamp=row.timestamp,
            temperature_c=row.temperature_c,
            moisture_pct=row.moisture_pct,
            light_lux=row.light_lux,
            conductivity_us_cm=row.conductivity_us_cm,
        )
        for row in rows
    ]


@protected.post("/switchbot/reading")
def create_switchbot_reading(reading: SwitchbotReadingIn, db: Session = Depends(get_db)):
    maybe_warn_switchbot(reading)
    ensure_sensor(db, reading.mac, reading.name, sensor_type_to_db(reading.type))

    db.add(
        SwitchbotReading(
            mac=reading.mac,
            timestamp=reading.timestamp,
            temperature_c=reading.temperature_c,
            humidity_pct=reading.humidity_pct,
        )
    )
    db.commit()
    return {"ok": True}


@protected.post("/xiaomi/reading")
def create_xiaomi_reading(reading: XiaomiReadingIn, db: Session = Depends(get_db)):
    maybe_warn_xiaomi(reading)
    ensure_sensor(db, reading.mac, reading.name, sensor_type_to_db(reading.type))

    db.add(
        XiaomiReading(
            mac=reading.mac,
            timestamp=reading.timestamp,
            temperature_c=reading.temperature_c,
            moisture_pct=reading.moisture_pct,
            light_lux=reading.light_lux,
            conductivity_us_cm=reading.conductivity_us_cm,
        )
    )
    db.commit()
    return {"ok": True}

app.include_router(protected)
