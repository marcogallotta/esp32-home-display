from datetime import datetime
from math import ceil
from typing import Any
from uuid import UUID

from psycopg.errors import UniqueViolation
from sqlalchemy import and_, extract, func, or_, select
from sqlalchemy.dialects.postgresql import insert
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session
from sqlalchemy.sql import literal_column

from common import (
    normalize_timestamp_to_utc,
    validate_mac_address,
    validate_query_timestamp,
    warn_soft_ranges,
)
from errors import BadRequestError, ServerMisconfiguredError, UnauthorizedError
from models import Sensor
from sensor_spec import DataField, SensorSpec


def verify_api_key(expected_api_key: str | None, provided_api_key: str | None):
    if not expected_api_key:
        raise ServerMisconfiguredError("server misconfigured")
    if provided_api_key != expected_api_key:
        raise UnauthorizedError("unauthorized")


def get_sensor_by_mac(db: Session, mac: str) -> Sensor | None:
    return db.execute(select(Sensor).where(Sensor.mac == mac)).scalar_one_or_none()


def get_sensor_by_id(db: Session, sensor_id: UUID) -> Sensor | None:
    return db.execute(select(Sensor).where(Sensor.id == sensor_id)).scalar_one_or_none()


def list_sensors(db: Session) -> list[Sensor]:
    return db.execute(select(Sensor).order_by(Sensor.name)).scalars().all()


def ensure_sensor(db: Session, mac: str, name: str | None, sensor_type: int) -> Sensor:
    sensor = get_sensor_by_mac(db, mac)

    if sensor is None:
        if name is None:
            raise BadRequestError("name required for unknown sensor")
        sensor = Sensor(mac=mac, name=name, type=sensor_type)
        db.add(sensor)
        db.flush()
        return sensor

    if sensor.type != sensor_type:
        raise BadRequestError("sensor type does not match existing sensor")

    if name is not None and sensor.name != name:
        raise BadRequestError("sensor name does not match existing sensor")

    return sensor


def is_expected_unique_conflict(exc: IntegrityError, constraint_name: str) -> bool:
    orig = exc.orig
    return (
        isinstance(orig, UniqueViolation)
        and getattr(orig.diag, "constraint_name", None) == constraint_name
    )


def classify_existing_reading(
    existing_values: dict[str, Any] | None,
    incoming: Any,
    data_fields: tuple[DataField, ...],
) -> dict[str, Any]:
    if existing_values is None:
        return {"status": "ok", "result": "created"}

    warnings = []
    merged = False

    for field in data_fields:
        existing_value = existing_values[field.name]
        incoming_value = getattr(incoming, field.name)

        if incoming_value is None:
            continue

        if existing_value is None:
            merged = True
            continue

        if existing_value != incoming_value:
            warnings.append(
                {
                    "code": "conflicting_field_ignored",
                    "field": field.name,
                    "existing": existing_value,
                    "incoming": incoming_value,
                }
            )

    if warnings and merged:
        return {"status": "ok", "result": "merged_with_conflict", "warnings": warnings}
    if warnings:
        return {"status": "ok", "result": "conflict", "warnings": warnings}
    if merged:
        return {"status": "ok", "result": "merged"}
    return {"status": "ok", "result": "duplicate"}


def ingest_reading(
    db: Session,
    reading: Any,
    sensor: SensorSpec,
):
    reading.mac = validate_mac_address(reading.mac)
    reading.timestamp = normalize_timestamp_to_utc(reading.timestamp)

    warn_soft_ranges(reading, sensor.soft_ranges)
    sensor_row = ensure_sensor(db, reading.mac, reading.name, sensor.db_sensor_type)

    existing = db.execute(
        select(sensor.reading_model).where(
            sensor.reading_model.mac == reading.mac,
            sensor.reading_model.timestamp == reading.timestamp,
        )
    ).scalar_one_or_none()

    existing_values = None
    if existing is not None:
        existing_values = {
            field.name: getattr(existing, field.name)
            for field in sensor.data_fields
        }

    values = reading.model_dump(exclude={"name"})
    values["sensor_id"] = sensor_row.id

    table = sensor.reading_model.__table__
    insert_stmt = insert(table).values(**values)
    excluded = insert_stmt.excluded

    merge_needed_checks = []
    for field in sensor.data_fields:
        col = field.column
        excluded_col = getattr(excluded, field.name)
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
                field.name: func.coalesce(field.column, getattr(excluded, field.name))
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
        return {"status": "ok", "result": "created"}

    return classify_existing_reading(existing_values, reading, sensor.data_fields)


def choose_bucket_seconds(window_seconds: float, max_points: int) -> int:
    return max(1, ceil(window_seconds / max_points))


def rows_to_models(rows, reading_out: Any, selected_fields: list[str]):
    return [
        reading_out(**{field: row._mapping[field] for field in selected_fields})
        for row in rows
    ]


def fetch_raw_readings(
    db: Session,
    mac: str,
    limit: int,
    before: datetime | None,
    after: datetime | None,
    sensor: SensorSpec,
):
    data_field_names = [field.name for field in sensor.data_fields]
    selected_fields = ["timestamp", *data_field_names]
    columns = [getattr(sensor.reading_model, field).label(field) for field in selected_fields]

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
    return rows_to_models(rows, sensor.reading_out, selected_fields)


def fetch_window_readings(
    db: Session,
    mac: str,
    start_ts: datetime,
    end_ts: datetime,
    max_points: int | None,
    sensor: SensorSpec,
):
    data_field_names = [field.name for field in sensor.data_fields]
    selected_fields = ["timestamp", *data_field_names]
    columns = [getattr(sensor.reading_model, field).label(field) for field in selected_fields]

    base_where = (
        sensor.reading_model.mac == mac,
        sensor.reading_model.timestamp >= start_ts,
        sensor.reading_model.timestamp <= end_ts,
    )

    if max_points is None:
        stmt = (
            select(*columns)
            .where(*base_where)
            .order_by(sensor.reading_model.timestamp.desc())
        )
        rows = db.execute(stmt).all()
        return rows_to_models(rows, sensor.reading_out, selected_fields)

    window_seconds = max(1.0, (end_ts - start_ts).total_seconds())
    bucket_seconds = choose_bucket_seconds(window_seconds, max_points)

    timestamp_col = sensor.reading_model.timestamp
    bucket_expr = func.floor(extract("epoch", timestamp_col) / bucket_seconds) * bucket_seconds
    row_number = func.row_number().over(
        partition_by=bucket_expr,
        order_by=timestamp_col.desc(),
    ).label("rn")

    subquery = (
        select(*columns, row_number)
        .where(*base_where)
        .subquery()
    )

    stmt = (
        select(*[subquery.c[field] for field in selected_fields])
        .where(subquery.c.rn == 1)
        .order_by(subquery.c.timestamp.desc())
    )

    rows = db.execute(stmt).all()
    return rows_to_models(rows, sensor.reading_out, selected_fields)


def fetch_readings(
    db: Session,
    mac: str,
    limit: int,
    before: datetime | None,
    after: datetime | None,
    start_ts: datetime | None,
    end_ts: datetime | None,
    max_points: int | None,
    sensor: SensorSpec,
):
    mac = validate_mac_address(mac)
    before = validate_query_timestamp("before", before)
    after = validate_query_timestamp("after", after)
    start_ts = validate_query_timestamp("start_ts", start_ts)
    end_ts = validate_query_timestamp("end_ts", end_ts)

    if before is not None and after is not None and after > before:
        raise BadRequestError("after must be <= before")

    if (start_ts is None) != (end_ts is None):
        raise BadRequestError("start_ts and end_ts must be provided together")

    if max_points is not None and start_ts is None:
        raise BadRequestError("max_points requires start_ts and end_ts")

    if start_ts is not None and end_ts is not None and start_ts > end_ts:
        raise BadRequestError("start_ts must be <= end_ts")

    if start_ts is not None and (before is not None or after is not None):
        raise BadRequestError("start_ts/end_ts cannot be combined with before/after")

    if get_sensor_by_mac(db, mac) is None:
        return []

    if start_ts is not None and end_ts is not None:
        return fetch_window_readings(
            db=db,
            mac=mac,
            start_ts=start_ts,
            end_ts=end_ts,
            max_points=max_points,
            sensor=sensor,
        )

    return fetch_raw_readings(
        db=db,
        mac=mac,
        limit=limit,
        before=before,
        after=after,
        sensor=sensor,
    )
