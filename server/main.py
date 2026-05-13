from datetime import datetime
from typing import Annotated
from uuid import UUID

from fastapi import APIRouter, Depends, FastAPI, Header, HTTPException, Query, Request
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel
from sqlalchemy.orm import Session

import switchbot as sb
import xiaomi as xm
from common import (
    BULK_ERROR_DETAIL_LIMIT,
    READINGS_DEFAULT_LIMIT,
    READINGS_MAX_LIMIT,
    SWITCHBOT_BULK_DEFAULT_MAX_READINGS,
    SWITCHBOT_SYNC_DEFAULT_GAP_THRESHOLD_MINUTES,
    SWITCHBOT_SYNC_DEFAULT_MAX_INTERVALS_PER_SENSOR,
    SWITCHBOT_SYNC_DEFAULT_MAX_INTERVALS_TOTAL,
)
from config import load_config
from db import build_engine, build_session_factory
from errors import BadRequestError, ServerMisconfiguredError, UnauthorizedError
from models import SWITCHBOT_TYPE, XIAOMI_TYPE
from service import (
    bulk_result_counts,
    fetch_readings,
    get_or_create_sensors_with_sync_state,
    get_sensor_by_id,
    ingest_reading,
    list_sensors,
    verify_api_key,
)


class SensorOut(BaseModel):
    id: UUID
    name: str
    type: str


SENSOR_SPECS = {
    SWITCHBOT_TYPE: sb.SENSOR,
    XIAOMI_TYPE: xm.SENSOR,
}

SENSOR_TYPE_NAMES = {
    SWITCHBOT_TYPE: "switchbot",
    XIAOMI_TYPE: "xiaomi",
}


def require_api_key(request: Request, x_api_key: str | None = Header(default=None)):
    verify_api_key(
        expected_api_key=request.app.state.config.get("api_key"),
        provided_api_key=x_api_key,
    )


def get_db(request: Request):
    session_factory = request.app.state.session_factory
    db = session_factory()
    try:
        yield db
    finally:
        db.close()


def create_app(config: dict) -> FastAPI:
    app = FastAPI()

    engine = build_engine(config)
    session_factory = build_session_factory(engine)

    app.state.config = config
    app.state.engine = engine
    app.state.session_factory = session_factory

    app.mount("/static", StaticFiles(directory="static"), name="static")

    @app.exception_handler(BadRequestError)
    def handle_bad_request(_: Request, exc: BadRequestError):
        return JSONResponse(status_code=400, content={"detail": str(exc)})

    @app.exception_handler(UnauthorizedError)
    def handle_unauthorized(_: Request, exc: UnauthorizedError):
        return JSONResponse(status_code=401, content={"detail": str(exc)})

    @app.exception_handler(ServerMisconfiguredError)
    def handle_server_misconfigured(_: Request, exc: ServerMisconfiguredError):
        return JSONResponse(status_code=500, content={"detail": str(exc)})

    protected = APIRouter(dependencies=[Depends(require_api_key)])

    @app.get("/health")
    def health():
        return {"ok": True}

    @protected.get("/sensors", response_model=list[SensorOut])
    def get_sensors(db: Session = Depends(get_db)):
        return [
            SensorOut(
                id=sensor.id,
                name=sensor.name,
                type=SENSOR_TYPE_NAMES[sensor.type],
            )
            for sensor in list_sensors(db)
        ]

    @protected.get("/sensors/{sensor_id}/readings")
    def get_sensor_readings(
        sensor_id: UUID,
        limit: Annotated[int, Query(ge=0, le=READINGS_MAX_LIMIT)] = READINGS_DEFAULT_LIMIT,
        before: datetime | None = None,
        after: datetime | None = None,
        start_ts: datetime | None = None,
        end_ts: datetime | None = None,
        max_points: Annotated[int | None, Query(gt=0, le=5000)] = None,
        db: Session = Depends(get_db),
    ):
        sensor_row = get_sensor_by_id(db, sensor_id)
        if sensor_row is None:
            return []

        return fetch_readings(
            db=db,
            mac=sensor_row.mac,
            limit=limit,
            before=before,
            after=after,
            start_ts=start_ts,
            end_ts=end_ts,
            max_points=max_points,
            sensor=SENSOR_SPECS[sensor_row.type],
        )

    @protected.post("/switchbot/sensors", response_model=sb.SensorsOut)
    def create_switchbot_sensors(
        payload: sb.SensorsIn,
        request: Request,
        db: Session = Depends(get_db),
    ):
        return get_or_create_sensors_with_sync_state(
            db=db,
            requested_sensors=payload.sensors,
            sensor=sb.SENSOR,
            gap_threshold_minutes=request.app.state.config.get(
                "switchbot_sync_gap_threshold_minutes",
                SWITCHBOT_SYNC_DEFAULT_GAP_THRESHOLD_MINUTES,
            ),
            max_intervals_per_sensor=int(
                request.app.state.config.get(
                    "switchbot_sync_max_intervals_per_sensor",
                    SWITCHBOT_SYNC_DEFAULT_MAX_INTERVALS_PER_SENSOR,
                )
            ),
            max_intervals_total=int(
                request.app.state.config.get(
                    "switchbot_sync_max_intervals_total",
                    SWITCHBOT_SYNC_DEFAULT_MAX_INTERVALS_TOTAL,
                )
            ),
        )

    @protected.post("/switchbot/bulk", response_model=sb.BulkOut)
    def create_switchbot_bulk(
        payload: sb.BulkIn,
        request: Request,
        db: Session = Depends(get_db),
    ):
        max_readings = int(
            request.app.state.config.get(
                "switchbot_bulk_max_readings",
                SWITCHBOT_BULK_DEFAULT_MAX_READINGS,
            )
        )
        if len(payload.readings) > max_readings:
            raise HTTPException(
                status_code=422,
                detail=f"readings must contain at most {max_readings} items",
            )

        sensor_row = get_sensor_by_id(db, payload.sensor_id)
        if sensor_row is None:
            raise HTTPException(status_code=422, detail="unknown sensor_id")

        if sensor_row.type != SWITCHBOT_TYPE:
            raise HTTPException(
                status_code=422,
                detail="sensor_id is not a SwitchBot sensor",
            )

        return bulk_result_counts(
            db=db,
            sensor_row=sensor_row,
            raw_readings=payload.readings,
            reading_model=sb.ReadingIn,
            sensor=sb.SENSOR,
            error_limit=BULK_ERROR_DETAIL_LIMIT,
        )

    @protected.post("/switchbot/reading")
    def create_switchbot_reading(reading: sb.ReadingIn, db: Session = Depends(get_db)):
        return ingest_reading(db=db, reading=reading, sensor=sb.SENSOR)

    @protected.post("/xiaomi/reading")
    def create_xiaomi_reading(reading: xm.ReadingIn, db: Session = Depends(get_db)):
        return ingest_reading(db=db, reading=reading, sensor=xm.SENSOR)

    app.include_router(protected)
    return app


app = create_app(load_config())
