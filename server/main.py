from datetime import datetime
from typing import Annotated

from fastapi import APIRouter, Depends, FastAPI, Header, Query, Request
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
from sqlalchemy.orm import Session

import switchbot as sb
import xiaomi as xm
from common import READINGS_DEFAULT_LIMIT, READINGS_MAX_LIMIT
from config import load_config
from db import build_engine, build_session_factory
from errors import BadRequestError, ServerMisconfiguredError, UnauthorizedError
from service import fetch_readings, ingest_reading, verify_api_key


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

    @protected.post("/switchbot/reading")
    def create_switchbot_reading(reading: sb.ReadingIn, db: Session = Depends(get_db)):
        return ingest_reading(
            db=db,
            reading=reading,
            sensor=sb.SENSOR,
        )

    @protected.post("/xiaomi/reading")
    def create_xiaomi_reading(reading: xm.ReadingIn, db: Session = Depends(get_db)):
        return ingest_reading(
            db=db,
            reading=reading,
            sensor=xm.SENSOR,
        )

    @protected.get("/switchbot/readings", response_model=list[sb.ReadingOut])
    def get_switchbot_readings(
        mac: str,
        limit: Annotated[int, Query(ge=0, le=READINGS_MAX_LIMIT)] = READINGS_DEFAULT_LIMIT,
        before: datetime | None = None,
        after: datetime | None = None,
        db: Session = Depends(get_db),
    ):
        return fetch_readings(
            db=db,
            mac=mac,
            limit=limit,
            before=before,
            after=after,
            sensor=sb.SENSOR,
        )

    @protected.get("/xiaomi/readings", response_model=list[xm.ReadingOut])
    def get_xiaomi_readings(
        mac: str,
        limit: Annotated[int, Query(ge=0, le=READINGS_MAX_LIMIT)] = READINGS_DEFAULT_LIMIT,
        before: datetime | None = None,
        after: datetime | None = None,
        db: Session = Depends(get_db),
    ):
        return fetch_readings(
            db=db,
            mac=mac,
            limit=limit,
            before=before,
            after=after,
            sensor=xm.SENSOR,
        )

    app.include_router(protected)
    return app


app = create_app(load_config())
