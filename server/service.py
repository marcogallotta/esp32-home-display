from datetime import datetime
from typing import Any, Callable

from fastapi import HTTPException
from sqlalchemy import select
from sqlalchemy.exc import IntegrityError
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


def compare_existing_and_incoming(existing: Any, incoming: Any, compare_fields: list[str]) -> str:
    for field in compare_fields:
        existing_value = getattr(existing, field)
        incoming_value = getattr(incoming, field)

        if existing_value is None or incoming_value is None:
            continue

        if existing_value != incoming_value:
            return "conflict"

    return "duplicate"


def ingest_reading(
    db: Session,
    reading: Any,
    sensor_type: int,
    maybe_warn: Callable[[Any], None],
    build_row: Callable[[Any], Any],
    reading_model: Any,
    compare_fields: list[str],
):
    reading.mac = validate_mac_address(reading.mac)
    reading.timestamp = normalize_timestamp_to_utc(reading.timestamp)

    maybe_warn(reading)
    ensure_sensor(db, reading.mac, reading.name, sensor_type)

    row = build_row(reading)
    db.add(row)

    try:
        db.commit()
        return {"status": "ok", "result": "created"}
    except IntegrityError as exc:
        db.rollback()

        existing = db.execute(
            select(reading_model).where(
                reading_model.mac == reading.mac,
                reading_model.timestamp == reading.timestamp,
            )
        ).scalar_one()

        result = compare_existing_and_incoming(existing, reading, compare_fields)

        return {
            "status": "ok",
            "result": result,
        }


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
