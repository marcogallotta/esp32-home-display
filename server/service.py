from datetime import datetime
from typing import Any, Callable

from fastapi import HTTPException
from sqlalchemy import and_, or_, select
from sqlalchemy.dialects.postgresql import insert
from sqlalchemy.orm import Session
from sqlalchemy.sql import func, literal_column

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
        db.flush()
        return

    if sensor.type != sensor_type:
        raise HTTPException(status_code=400, detail="sensor type does not match existing sensor")

    if name is not None and sensor.name != name:
        raise HTTPException(status_code=400, detail="sensor name does not match existing sensor")


def classify_noop(existing: Any, incoming: Any, compare_fields: list[str]) -> dict[str, Any]:
    warnings = []

    for field in compare_fields:
        existing_value = getattr(existing, field)
        incoming_value = getattr(incoming, field)

        if existing_value is None or incoming_value is None:
            continue

        if existing_value != incoming_value:
            warnings.append(
                {
                    "code": "conflicting_field_ignored",
                    "field": field,
                    "existing": existing_value,
                    "incoming": incoming_value,
                }
            )

    if warnings:
        return {
            "status": "ok",
            "result": "conflict",
            "warnings": warnings,
        }

    return {
        "status": "ok",
        "result": "duplicate",
    }


def ingest_reading(
    db: Session,
    reading: Any,
    sensor_type: int,
    maybe_warn: Callable[[Any], None],
    reading_model: Any,
    compare_fields: list[str],
):
    reading.mac = validate_mac_address(reading.mac)
    reading.timestamp = normalize_timestamp_to_utc(reading.timestamp)

    maybe_warn(reading)
    ensure_sensor(db, reading.mac, reading.name, sensor_type)

    values = reading.model_dump(exclude={"name"})

    table = reading_model.__table__
    insert_stmt = insert(table).values(**values)
    excluded = insert_stmt.excluded

    compatibility_checks = []
    merge_needed_checks = []

    for field in compare_fields:
        col = getattr(table.c, field)
        excluded_col = getattr(excluded, field)

        compatibility_checks.append(
            or_(
                col.is_(None),
                excluded_col.is_(None),
                col == excluded_col,
            )
        )

        merge_needed_checks.append(
            and_(
                col.is_(None),
                excluded_col.is_not(None),
            )
        )

    upsert_stmt = (
        insert_stmt.on_conflict_do_update(
            index_elements=[table.c.mac, table.c.timestamp],
            set_={
                field: func.coalesce(getattr(table.c, field), getattr(excluded, field))
                for field in compare_fields
            },
            where=and_(
                *compatibility_checks,
                or_(*merge_needed_checks),
            ),
        )
        .returning(literal_column("xmax = 0").label("inserted"))
    )

    row = db.execute(upsert_stmt).first()
    db.commit()

    if row is not None:
        return {
            "status": "ok",
            "result": "created" if row.inserted else "merged",
        }

    existing = db.execute(
        select(reading_model).where(
            reading_model.mac == reading.mac,
            reading_model.timestamp == reading.timestamp,
        )
    ).scalar_one()

    return classify_noop(existing, reading, compare_fields)


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
