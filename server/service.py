from datetime import datetime
from typing import Any, Callable

from fastapi import HTTPException
from sqlalchemy import select
from sqlalchemy.orm import Session

from common import (
    normalize_timestamp_to_utc,
    validate_mac_address,
    validate_query_timestamp,
)
from models import Sensor


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


def ingest_reading(
    db: Session,
    reading: Any,
    sensor_type: int,
    maybe_warn: Callable[[Any], None],
    build_row: Callable[[Any], Any],
):
    reading.mac = validate_mac_address(reading.mac)
    reading.timestamp = normalize_timestamp_to_utc(reading.timestamp)

    maybe_warn(reading)
    ensure_sensor(db, reading.mac, reading.name, sensor_type)

    db.add(build_row(reading))
    db.commit()

    return {"ok": True}


def fetch_readings(
    db: Session,
    mac: str,
    limit: int,
    before: datetime | None,
    after: datetime | None,
    reading_model: Any,
    out_model: Any,
    selected_fields: list[str],
):
    mac = validate_mac_address(mac)
    before = validate_query_timestamp("before", before)
    after = validate_query_timestamp("after", after)

    if before is not None and after is not None and after > before:
        raise HTTPException(status_code=400, detail="after must be <= before")

    sensor = db.get(Sensor, mac)
    if sensor is None:
        return []

    columns = [getattr(reading_model, field) for field in selected_fields]

    stmt = (
        select(*columns)
        .where(reading_model.mac == mac)
        .order_by(reading_model.timestamp.desc())
        .limit(limit)
    )

    if before is not None:
        stmt = stmt.where(reading_model.timestamp <= before)

    if after is not None:
        stmt = stmt.where(reading_model.timestamp >= after)

    rows = db.execute(stmt).all()

    return [
        out_model(**{field: getattr(row, field) for field in selected_fields})
        for row in rows
    ]
