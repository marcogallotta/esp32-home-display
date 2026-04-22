from datetime import datetime
from typing import Any

from psycopg.errors import UniqueViolation
from sqlalchemy import and_, or_, select
from sqlalchemy.dialects.postgresql import insert
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session
from sqlalchemy.sql import func, literal_column

from common import (
    normalize_timestamp_to_utc,
    validate_mac_address,
    validate_query_timestamp,
    warn_soft_ranges,
)
from errors import BadRequestError, ServerMisconfiguredError, UnauthorizedError
from models import Sensor
from sensor_spec import SensorSpec


def verify_api_key(expected_api_key: str | None, provided_api_key: str | None):
    if not expected_api_key:
        raise ServerMisconfiguredError("server misconfigured")
    if provided_api_key != expected_api_key:
        raise UnauthorizedError("unauthorized")


def ensure_sensor(db: Session, mac: str, name: str | None, sensor_type: int):
    sensor = db.get(Sensor, mac)

    if sensor is None:
        if name is None:
            raise BadRequestError("name required for unknown sensor")
        db.add(Sensor(mac=mac, name=name, type=sensor_type))
        db.flush()
        return

    if sensor.type != sensor_type:
        raise BadRequestError("sensor type does not match existing sensor")

    if name is not None and sensor.name != name:
        raise BadRequestError("sensor name does not match existing sensor")


def is_expected_unique_conflict(exc: IntegrityError, constraint_name: str) -> bool:
    orig = exc.orig
    return (
        isinstance(orig, UniqueViolation)
        and getattr(orig.diag, "constraint_name", None) == constraint_name
    )


def classify_existing_reading(
    existing_values: dict[str, Any] | None,
    incoming: Any,
    data_fields: list[str],
) -> dict[str, Any]:
    if existing_values is None:
        return {
            "status": "ok",
            "result": "created",
        }

    warnings = []
    merged = False

    for field in data_fields:
        existing_value = existing_values[field]
        incoming_value = getattr(incoming, field)

        if incoming_value is None:
            continue

        if existing_value is None:
            merged = True
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

    if warnings and merged:
        return {
            "status": "ok",
            "result": "merged_with_conflict",
            "warnings": warnings,
        }

    if warnings:
        return {
            "status": "ok",
            "result": "conflict",
            "warnings": warnings,
        }

    if merged:
        return {
            "status": "ok",
            "result": "merged",
        }

    return {
        "status": "ok",
        "result": "duplicate",
    }


def ingest_reading(
    db: Session,
    reading: Any,
    sensor: SensorSpec,
):
    reading.mac = validate_mac_address(reading.mac)
    reading.timestamp = normalize_timestamp_to_utc(reading.timestamp)

    warn_soft_ranges(reading, sensor.soft_ranges)
    ensure_sensor(db, reading.mac, reading.name, sensor.db_sensor_type)

    existing = db.execute(
        select(sensor.reading_model).where(
            sensor.reading_model.mac == reading.mac,
            sensor.reading_model.timestamp == reading.timestamp,
        )
    ).scalar_one_or_none()

    existing_values = None
    if existing is not None:
        existing_values = {
            field: getattr(existing, field)
            for field in sensor.data_fields
        }

    values = reading.model_dump(exclude={"name"})

    table = sensor.reading_model.__table__
    insert_stmt = insert(table).values(**values)
    excluded = insert_stmt.excluded

    merge_needed_checks = []

    for field in sensor.data_fields:
        col = getattr(table.c, field)
        excluded_col = getattr(excluded, field)
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
                for field in sensor.data_fields
            },
            where=or_(*merge_needed_checks),
        )
        .returning(literal_column("xmax = 0").label("inserted"))
    )

    try:
        row = db.execute(upsert_stmt).first()
        db.commit()
    except IntegrityError as exc:
        db.rollback()
        if not is_expected_unique_conflict(exc, sensor.unique_constraint_name):
            raise
        row = None

    if existing_values is None and row is not None and row.inserted:
        return {
            "status": "ok",
            "result": "created",
        }

    return classify_existing_reading(existing_values, reading, sensor.data_fields)


def fetch_readings(
    db: Session,
    mac: str,
    limit: int,
    before: datetime | None,
    after: datetime | None,
    reading_out: Any,
    sensor: SensorSpec,
):
    mac = validate_mac_address(mac)
    before = validate_query_timestamp("before", before)
    after = validate_query_timestamp("after", after)

    if before is not None and after is not None and after > before:
        raise BadRequestError("after must be <= before")

    sensor_row = db.get(Sensor, mac)
    if sensor_row is None:
        return []

    selected_fields = ["timestamp", *sensor.data_fields]
    columns = [getattr(sensor.reading_model, field) for field in selected_fields]

    stmt = (
        select(*columns)
        .where(sensor.reading_model.mac == mac)
        .order_by(sensor.reading_model.timestamp.desc())
        .limit(limit)
    )

    if before is not None:
        stmt = stmt.where(sensor.reading_model.timestamp <= before)

    if after is not None:
        stmt = stmt.where(sensor.reading_model.timestamp >= after)

    rows = db.execute(stmt).all()

    return [
        reading_out(**{field: getattr(row, field) for field in selected_fields})
        for row in rows
    ]
