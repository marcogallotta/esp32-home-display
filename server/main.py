from datetime import datetime
from typing import Annotated

from fastapi import Depends, FastAPI, HTTPException, Query
from sqlalchemy import select
from sqlalchemy.orm import Session

import switchbot as sb
import xiaomi as xm
from common import (
    READINGS_DEFAULT_LIMIT,
    READINGS_MAX_LIMIT,
    normalize_timestamp_to_utc,
    validate_mac_address,
    validate_query_timestamp,
)
from models import SessionLocal, Sensor


app = FastAPI()


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def ensure_sensor(db: Session, mac: str, name: str | None, sensor_type: int):
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


@app.get("/health")
def health():
    return {"ok": True}


@app.post("/switchbot/reading")
def create_switchbot_reading(reading: sb.ReadingIn, db: Session = Depends(get_db)):
    reading.mac = validate_mac_address(reading.mac)
    reading.timestamp = normalize_timestamp_to_utc(reading.timestamp)

    sb.maybe_warn(reading)
    ensure_sensor(db, reading.mac, reading.name, sb.SENSOR_TYPE_DB)
    db.add(sb.build_row(reading))
    db.commit()
    return {"ok": True}


@app.post("/xiaomi/reading")
def create_xiaomi_reading(reading: xm.ReadingIn, db: Session = Depends(get_db)):
    reading.mac = validate_mac_address(reading.mac)
    reading.timestamp = normalize_timestamp_to_utc(reading.timestamp)

    xm.maybe_warn(reading)
    ensure_sensor(db, reading.mac, reading.name, xm.SENSOR_TYPE_DB)
    db.add(xm.build_row(reading))
    db.commit()
    return {"ok": True}


@app.get("/switchbot/readings", response_model=list[sb.ReadingOut])
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
            sb.READING_MODEL.timestamp,
            sb.READING_MODEL.temperature_c,
            sb.READING_MODEL.humidity_pct,
        )
        .where(sb.READING_MODEL.mac == mac)
        .order_by(sb.READING_MODEL.timestamp.desc())
        .limit(limit)
    )

    if before is not None:
        stmt = stmt.where(sb.READING_MODEL.timestamp <= before)

    if after is not None:
        stmt = stmt.where(sb.READING_MODEL.timestamp >= after)

    rows = db.execute(stmt).all()

    return [
        sb.ReadingOut(
            timestamp=row.timestamp,
            temperature_c=row.temperature_c,
            humidity_pct=row.humidity_pct,
        )
        for row in rows
    ]


@app.get("/xiaomi/readings", response_model=list[xm.ReadingOut])
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
            xm.READING_MODEL.timestamp,
            xm.READING_MODEL.temperature_c,
            xm.READING_MODEL.moisture_pct,
            xm.READING_MODEL.light_lux,
            xm.READING_MODEL.conductivity_us_cm,
        )
        .where(xm.READING_MODEL.mac == mac)
        .order_by(xm.READING_MODEL.timestamp.desc())
        .limit(limit)
    )

    if before is not None:
        stmt = stmt.where(xm.READING_MODEL.timestamp <= before)

    if after is not None:
        stmt = stmt.where(xm.READING_MODEL.timestamp >= after)

    rows = db.execute(stmt).all()

    return [
        xm.ReadingOut(
            timestamp=row.timestamp,
            temperature_c=row.temperature_c,
            moisture_pct=row.moisture_pct,
            light_lux=row.light_lux,
            conductivity_us_cm=row.conductivity_us_cm,
        )
        for row in rows
    ]