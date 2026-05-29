import logging
from datetime import datetime
from pathlib import Path
from typing import Annotated, Any, Literal
from uuid import UUID

from fastapi import APIRouter, Depends, FastAPI, Header, HTTPException, Query, Request
from fastapi.exception_handlers import request_validation_exception_handler
from fastapi.exceptions import RequestValidationError
from fastapi.responses import FileResponse, JSONResponse, RedirectResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel
from sqlalchemy.orm import Session
from starlette.middleware.sessions import SessionMiddleware

from . import openmeteo, predict
from . import switchbot as sb
from . import xiaomi as xm
from .common import (
    BULK_ERROR_DETAIL_LIMIT,
    READINGS_DEFAULT_LIMIT,
    READINGS_MAX_LIMIT,
)
from .api_limits import MemoryMapStore, TokenBucketLimiter, make_rate_limiter
from .config import Config
from .errors import BadRequestError, ServerMisconfiguredError, UnauthorizedError
from .models import SWITCHBOT_TYPE, XIAOMI_TYPE
from .service import (
    bulk_result_counts,
    fetch_latest_readings,
    fetch_readings,
    get_or_create_sensors_with_sync_state,
    get_sensor_by_id,
    ingest_reading,
    list_sensors,
    verify_api_key,
)

logger = logging.getLogger(__name__)

_STATIC_DIR = Path(__file__).parent.parent / "static"


class IngestResponse(BaseModel):
    result: Literal["created", "duplicate", "merged", "conflict", "merged_with_conflict"]
    warnings: list[dict[str, Any]]


class SensorOut(BaseModel):
    id: UUID
    mac: str
    name: str
    type: str


class LatestReadingOut(BaseModel):
    mac: str
    sensor_id: UUID
    latest_timestamp: datetime
    reading: dict[str, Any]


class LatestSensorsOut(BaseModel):
    sensors: list[LatestReadingOut]


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
        expected_api_key=request.app.state.config.api_key,
        provided_api_key=x_api_key,
    )


def require_session(request: Request):
    if not request.session.get("authenticated"):
        raise UnauthorizedError("unauthorized")


def require_session_or_api_key(request: Request) -> str:
    supplied_api_key = request.headers.get("x-api-key")
    if supplied_api_key is not None:
        verify_api_key(
            expected_api_key=request.app.state.config.api_key,
            provided_api_key=supplied_api_key,
        )
        return "api_key"
    require_session(request)
    return "session"


def get_db(request: Request):
    session_factory = request.app.state.session_factory
    db = session_factory()
    try:
        yield db
    finally:
        db.close()


def create_app(config: Config, engine, session_factory) -> FastAPI:
    app = FastAPI()

    rl = config.rate_limits
    limiter = TokenBucketLimiter(MemoryMapStore())
    esp32_read_limit = make_rate_limiter(
        "esp32_app:read",
        limit=rl.esp32_app.read.limit,
        period=rl.esp32_app.read.period,
        burst=rl.esp32_app.burst,
        limiter=limiter,
    )
    esp32_live_write_limit = make_rate_limiter(
        "esp32_app:live_write",
        limit=rl.esp32_app.live_write.limit,
        period=rl.esp32_app.live_write.period,
        burst=rl.esp32_app.burst,
        limiter=limiter,
    )
    esp32_bulk_write_limit = make_rate_limiter(
        "esp32_app:bulk_write",
        limit=rl.esp32_app.bulk_write.limit,
        period=rl.esp32_app.bulk_write.period,
        burst=rl.esp32_app.burst,
        limiter=limiter,
    )
    frontend_limit = make_rate_limiter(
        "frontend",
        limit=rl.frontend.limit,
        period=rl.frontend.period,
        burst=True,
        per_ip=True,
        limiter=limiter,
    )
    login_limit = make_rate_limiter(
        "login",
        limit=rl.login.limit,
        period=rl.login.period,
        burst=True,
        per_ip=True,
        limiter=limiter,
    )

    async def sensor_read_limit(
        request: Request,
        auth_mode: str = Depends(require_session_or_api_key),
    ) -> None:
        if auth_mode == "api_key":
            await esp32_read_limit(request)
        else:
            await frontend_limit(request)

    app.state.config = config
    app.state.engine = engine
    app.state.session_factory = session_factory

    app.add_middleware(
        SessionMiddleware,
        secret_key=config.session_secret,
        https_only=config.session_secure,
    )

    app.mount("/static", StaticFiles(directory=_STATIC_DIR), name="static")

    @app.exception_handler(BadRequestError)
    def handle_bad_request(_: Request, exc: BadRequestError):
        return JSONResponse(status_code=400, content={"detail": str(exc)})

    @app.exception_handler(UnauthorizedError)
    def handle_unauthorized(_: Request, exc: UnauthorizedError):
        return JSONResponse(status_code=401, content={"detail": str(exc)})

    @app.exception_handler(ServerMisconfiguredError)
    def handle_server_misconfigured(_: Request, exc: ServerMisconfiguredError):
        return JSONResponse(status_code=500, content={"detail": str(exc)})

    @app.exception_handler(RequestValidationError)
    async def handle_validation_error(request: Request, exc: RequestValidationError):
        if request.url.path.endswith("/switchbot/bulk"):
            try:
                body = await request.json()
            except Exception:
                body = {}
            readings = body.get("readings") if isinstance(body, dict) else None
            timestamps = [
                r.get("timestamp")
                for r in (readings or [])
                if isinstance(r, dict) and "timestamp" in r
            ]
            logger.warning(
                "switchbot/bulk rejected: detail=%s sensor_id=%s readings=%s first_ts=%s last_ts=%s",
                exc.errors(),
                body.get("sensor_id") if isinstance(body, dict) else None,
                len(readings) if readings is not None else None,
                timestamps[0] if timestamps else None,
                timestamps[-1] if timestamps else None,
            )
        return await request_validation_exception_handler(request, exc)

    @app.get("/health")
    def health():
        return {"ok": True}

    @app.get("/login")
    def login_page():
        return FileResponse(_STATIC_DIR / "login.html")

    @app.post("/login", dependencies=[Depends(login_limit)])
    async def login(request: Request):
        form = await request.form()
        password = str(form.get("password", ""))
        expected = request.app.state.config.dashboard_password
        if not expected or password != expected:
            raise UnauthorizedError("unauthorized")
        request.session["authenticated"] = True
        return RedirectResponse(url="/static/overview.html", status_code=303)

    @app.post("/logout")
    def logout(request: Request):
        request.session.clear()
        return RedirectResponse(url="/login", status_code=303)

    device = APIRouter(dependencies=[Depends(require_api_key)])
    dashboard = APIRouter(dependencies=[Depends(require_session), Depends(frontend_limit)])
    sensor_router = APIRouter(dependencies=[Depends(sensor_read_limit)])

    @sensor_router.get("/sensors", response_model=list[SensorOut])
    def get_sensors(db: Session = Depends(get_db)):
        return [
            SensorOut(
                id=sensor.id,
                mac=sensor.mac,
                name=sensor.name,
                type=SENSOR_TYPE_NAMES[sensor.type],
            )
            for sensor in list_sensors(db)
        ]

    @sensor_router.get("/sensors/latest", response_model=LatestSensorsOut)
    def get_sensors_latest(
        sensor_id: Annotated[UUID | None, Query()] = None,
        db: Session = Depends(get_db),
    ):
        sensor_ids = [sensor_id] if sensor_id is not None else None
        readings = fetch_latest_readings(db, SENSOR_SPECS, sensor_ids=sensor_ids)
        return LatestSensorsOut(sensors=readings)

    @sensor_router.get("/sensors/{sensor_id}/readings")
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
            expected_type=sensor_row.type,
        )

    @device.post("/switchbot/sensors", response_model=sb.SensorsOut, dependencies=[Depends(esp32_read_limit)])
    def create_switchbot_sensors(
        payload: sb.SensorsIn,
        request: Request,
        db: Session = Depends(get_db),
    ):
        return get_or_create_sensors_with_sync_state(
            db=db,
            requested_sensors=payload.sensors,
            sensor=sb.SENSOR,
            gap_threshold_minutes=request.app.state.config.switchbot_sync_gap_threshold_minutes,
            max_intervals_per_sensor=request.app.state.config.switchbot_sync_max_intervals_per_sensor,
            max_intervals_total=request.app.state.config.switchbot_sync_max_intervals_total,
        )

    @device.post("/switchbot/bulk", response_model=sb.BulkOut, dependencies=[Depends(esp32_bulk_write_limit)])
    def create_switchbot_bulk(
        payload: sb.BulkIn,
        request: Request,
        db: Session = Depends(get_db),
    ):
        max_readings = request.app.state.config.switchbot_bulk_max_readings
        readings = payload.readings
        timestamps = [
            r.timestamp
            for r in readings
            if hasattr(r, "timestamp")
        ]

        def _log_rejection(detail: str):
            logger.warning(
                "switchbot/bulk rejected: detail=%s sensor_id=%s readings=%s first_ts=%s last_ts=%s",
                detail,
                payload.sensor_id,
                len(readings),
                timestamps[0] if timestamps else None,
                timestamps[-1] if timestamps else None,
            )

        if len(readings) > max_readings:
            _log_rejection(f"readings must contain at most {max_readings} items")
            raise HTTPException(
                status_code=422,
                detail=f"readings must contain at most {max_readings} items",
            )

        sensor_row = get_sensor_by_id(db, payload.sensor_id)
        if sensor_row is None:
            _log_rejection("unknown sensor_id")
            raise HTTPException(status_code=422, detail="unknown sensor_id")

        if sensor_row.type != SWITCHBOT_TYPE:
            _log_rejection("sensor_id is not a SwitchBot sensor")
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

    @device.post("/switchbot/reading", response_model=IngestResponse, dependencies=[Depends(esp32_live_write_limit)])
    def create_switchbot_reading(reading: sb.ReadingIn, db: Session = Depends(get_db)):
        return ingest_reading(db=db, reading=reading, sensor=sb.SENSOR)

    @device.post("/xiaomi/reading", response_model=IngestResponse, dependencies=[Depends(esp32_live_write_limit)])
    def create_xiaomi_reading(reading: xm.ReadingIn, db: Session = Depends(get_db)):
        return ingest_reading(db=db, reading=reading, sensor=xm.SENSOR)

    app.include_router(device)
    app.include_router(dashboard)
    app.include_router(sensor_router)
    app.include_router(openmeteo.router, dependencies=[Depends(require_session), Depends(frontend_limit)])
    app.include_router(predict.router, dependencies=[Depends(require_session), Depends(frontend_limit)])
    return app
