from datetime import datetime, timedelta, timezone
import logging
from math import ceil
from typing import Any
from uuid import UUID

from psycopg.errors import UniqueViolation
from pydantic import ValidationError
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


logger = logging.getLogger(__name__)

SYNC_RETENTION_DAYS = 68
SYNC_TIMESTAMPS_MAX = 20_000


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


def reconcile_sensor_name(sensor: Sensor, name: str | None):
    if name is None or sensor.name == name:
        return

    logger.warning("sensor name changed from %s → %s", sensor.name, name)
    sensor.name = name


def get_or_create_sensor_for_sync(
    db: Session,
    *,
    mac: str,
    name: str | None,
    sensor_type: int,
) -> Sensor:
    mac = validate_mac_address(mac)
    sensor = get_sensor_by_mac(db, mac)

    if sensor is None:
        insert_stmt = (
            insert(Sensor)
            .values(mac=mac, name=name or mac, type=sensor_type)
            .on_conflict_do_nothing(index_elements=[Sensor.mac])
        )
        db.execute(insert_stmt)
        sensor = get_sensor_by_mac(db, mac)

    if sensor is None:
        raise BadRequestError("sensor could not be resolved")

    if sensor.type != sensor_type:
        raise BadRequestError("sensor type does not match existing sensor")

    reconcile_sensor_name(sensor, name)
    return sensor


def get_sensor_timestamp_bounds(
    db: Session, sensor_id: UUID, sensor: SensorSpec
) -> tuple[datetime | None, datetime | None]:
    row = db.execute(
        select(
            func.min(sensor.reading_model.timestamp),
            func.max(sensor.reading_model.timestamp),
        ).where(sensor.reading_model.sensor_id == sensor_id)
    ).one()
    return row[0], row[1]


def get_sensor_timestamps(
    db: Session,
    sensor_id: UUID,
    sensor: SensorSpec,
    *,
    after: datetime | None = None,
    limit: int | None = None,
) -> list[datetime]:
    stmt = (
        select(sensor.reading_model.timestamp)
        .where(sensor.reading_model.sensor_id == sensor_id)
        .order_by(sensor.reading_model.timestamp.desc())
    )
    if after is not None:
        stmt = stmt.where(sensor.reading_model.timestamp >= after)
    if limit is not None:
        stmt = stmt.limit(limit)
    return sorted(db.execute(stmt).scalars())


def build_sync_intervals(
    timestamps: list[datetime],
    *,
    gap_threshold: timedelta,
    max_intervals: int,
) -> tuple[list[dict[str, datetime]], bool]:
    intervals: list[dict[str, datetime]] = []

    for current, next_timestamp in zip(timestamps, timestamps[1:]):
        if next_timestamp - current < gap_threshold:
            continue
        intervals.append({"start": current, "end": next_timestamp})

    if len(intervals) > max_intervals:
        return (intervals[-max_intervals:] if max_intervals > 0 else []), True
    return intervals, False


def get_sensor_sync_state(
    db: Session,
    *,
    sensor_row: Sensor,
    sensor: SensorSpec,
    gap_threshold: timedelta,
    max_intervals: int,
) -> dict[str, Any]:
    now = datetime.now(timezone.utc)
    retention_cutoff = now - timedelta(days=SYNC_RETENTION_DAYS)

    first_ts, latest_ts = get_sensor_timestamp_bounds(db, sensor_row.id, sensor)
    timestamps = get_sensor_timestamps(
        db,
        sensor_row.id,
        sensor,
        after=retention_cutoff,
        limit=SYNC_TIMESTAMPS_MAX,
    )
    intervals, capped = build_sync_intervals(
        timestamps,
        gap_threshold=gap_threshold,
        max_intervals=max_intervals,
    )

    return {
        "mac": sensor_row.mac,
        "sensor_id": sensor_row.id,
        "first_timestamp": first_ts,
        "latest_timestamp": latest_ts,
        "sync_intervals": intervals,
        "sync_intervals_capped": capped,
    }


def get_or_create_sensors_with_sync_state(
    db: Session,
    requested_sensors: list[Any],
    sensor: SensorSpec,
    *,
    gap_threshold_minutes: int,
    max_intervals_per_sensor: int,
    max_intervals_total: int,
) -> dict[str, Any]:
    resolved = []
    warnings = []
    remaining_total = max_intervals_total
    any_capped = False

    gap_threshold = timedelta(minutes=gap_threshold_minutes)
    per_sensor_limit = max_intervals_per_sensor

    for requested in requested_sensors:
        sensor_row = get_or_create_sensor_for_sync(
            db=db,
            mac=requested.mac,
            name=requested.name,
            sensor_type=sensor.db_sensor_type,
        )

        allowed_for_sensor = min(per_sensor_limit, remaining_total)
        state = get_sensor_sync_state(
            db=db,
            sensor_row=sensor_row,
            sensor=sensor,
            gap_threshold=gap_threshold,
            max_intervals=allowed_for_sensor,
        )
        remaining_total -= len(state["sync_intervals"])

        if state["sync_intervals_capped"]:
            any_capped = True
            logger.warning(
                "switchbot_sync_intervals_capped sensor_id=%s mac=%s allowed=%s",
                sensor_row.id,
                sensor_row.mac,
                allowed_for_sensor,
            )

        resolved.append(state)

    if any_capped:
        warnings.append(
            {
                "code": "sync_intervals_capped",
                "message": (
                    "sync intervals were capped; client should upload returned "
                    "intervals and request /switchbot/sensors again"
                ),
            }
        )

    db.commit()
    return {"sensors": resolved, "warnings": warnings}


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

    reconcile_sensor_name(sensor, name)
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
        return {"result": "created", "warnings": []}

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
        return {"result": "merged_with_conflict", "warnings": warnings}
    if warnings:
        return {"result": "conflict", "warnings": warnings}
    if merged:
        return {"result": "merged", "warnings": []}
    return {"result": "duplicate", "warnings": []}


def prepare_reading(reading: Any, sensor: SensorSpec) -> Any:
    mac = validate_mac_address(reading.mac)
    timestamp = normalize_timestamp_to_utc(reading.timestamp)
    normalized = reading.model_copy(update={"mac": mac, "timestamp": timestamp})
    warn_soft_ranges(normalized, sensor.data_fields)
    return normalized


def get_existing_values(
    db: Session,
    reading: Any,
    sensor: SensorSpec,
) -> dict[str, Any] | None:
    existing = db.execute(
        select(sensor.reading_model).where(
            sensor.reading_model.mac == reading.mac,
            sensor.reading_model.timestamp == reading.timestamp,
        )
    ).scalar_one_or_none()

    if existing is None:
        return None

    return {
        field.name: getattr(existing, field.name)
        for field in sensor.data_fields
    }


def execute_reading_upsert(
    db: Session,
    reading: Any,
    sensor_row: Sensor,
    sensor: SensorSpec,
    *,
    commit: bool,
):
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
        if commit:
            db.commit()
        return row
    except IntegrityError as exc:
        if not commit:
            raise
        db.rollback()
        if not is_expected_unique_conflict(exc, sensor.unique_constraint_name):
            raise
        return None


def ingest_reading(
    db: Session,
    reading: Any,
    sensor: SensorSpec,
    *,
    commit: bool = True,
):
    reading = prepare_reading(reading, sensor)
    sensor_row = ensure_sensor(db, reading.mac, reading.name, sensor.db_sensor_type)
    existing_values = get_existing_values(db, reading, sensor)
    row = execute_reading_upsert(db, reading, sensor_row, sensor, commit=commit)

    if existing_values is None and row is not None and row.inserted:
        return {"result": "created", "warnings": []}

    return classify_existing_reading(existing_values, reading, sensor.data_fields)



def add_bulk_error(
    errors: list[dict[str, Any]],
    *,
    error_limit: int,
    index: int,
    code: str,
    message: str,
):
    if len(errors) >= error_limit:
        return
    errors.append({"index": index, "code": code, "message": message})


def validation_error_message(exc: ValidationError) -> str:
    errors = exc.errors()
    if not errors:
        return "invalid reading"

    first = errors[0]
    loc = first.get("loc", ())
    message = first.get("msg", "invalid reading")

    if loc:
        return f"{'.'.join(str(part) for part in loc)}: {message}"
    return message


def bulk_result_counts(
    db: Session,
    *,
    sensor_row: Sensor,
    raw_readings: list[Any],
    reading_model: Any,
    sensor: SensorSpec,
    error_limit: int,
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "status": "ok",
        "received": len(raw_readings),
        "succeeded": 0,
        "created": 0,
        "duplicate": 0,
        "conflict": 0,
        "invalid": 0,
        "errors": [],
        "failed": [],
    }

    for index, raw in enumerate(raw_readings):
        if not isinstance(raw, dict):
            result["invalid"] += 1
            reason = "reading must be an object"
            add_bulk_error(
                result["errors"],
                error_limit=error_limit,
                index=index,
                code="invalid_reading",
                message=reason,
            )
            result["failed"].append({"index": index, "reason": reason})
            continue

        savepoint = db.begin_nested()
        try:
            reading = reading_model(**{**raw, "mac": sensor_row.mac, "name": None})
            row_result = ingest_reading(db=db, reading=reading, sensor=sensor, commit=False)
            savepoint.commit()
        except ValidationError as exc:
            savepoint.rollback()
            result["invalid"] += 1
            reason = validation_error_message(exc)
            add_bulk_error(
                result["errors"],
                error_limit=error_limit,
                index=index,
                code="invalid_reading",
                message=reason,
            )
            result["failed"].append({"index": index, "reason": reason})
            continue
        except BadRequestError as exc:
            savepoint.rollback()
            result["invalid"] += 1
            reason = str(exc)
            add_bulk_error(
                result["errors"],
                error_limit=error_limit,
                index=index,
                code="invalid_reading",
                message=reason,
            )
            result["failed"].append({"index": index, "reason": reason})
            continue
        except Exception as exc:
            savepoint.rollback()
            logger.error("bulk ingest db error at index %d: %s", index, exc)
            result["invalid"] += 1
            reason = "internal error"
            add_bulk_error(
                result["errors"],
                error_limit=error_limit,
                index=index,
                code="db_error",
                message=reason,
            )
            result["failed"].append({"index": index, "reason": reason})
            continue

        row_status = row_result.get("result")
        if row_status == "created":
            result["created"] += 1
            result["succeeded"] += 1
        elif row_status == "duplicate":
            result["duplicate"] += 1
            result["succeeded"] += 1
        elif row_status == "merged":
            result["created"] += 1
            result["succeeded"] += 1
        elif row_status in {"conflict", "merged_with_conflict"}:
            result["conflict"] += 1
            result["succeeded"] += 1
            add_bulk_error(
                result["errors"],
                error_limit=error_limit,
                index=index,
                code="conflict",
                message="conflicting reading ignored",
            )
        else:
            result["invalid"] += 1
            reason = f"unexpected ingest result: {row_status}"
            add_bulk_error(
                result["errors"],
                error_limit=error_limit,
                index=index,
                code="unexpected_result",
                message=reason,
            )
            result["failed"].append({"index": index, "reason": reason})

    db.commit()
    return result


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
    expected_type: int,
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

    sensor_row = get_sensor_by_mac(db, mac)
    if sensor_row is None:
        return []
    if sensor_row.type != expected_type:
        raise BadRequestError("sensor type mismatch")

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
