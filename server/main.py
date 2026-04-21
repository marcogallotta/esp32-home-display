from datetime import datetime
from typing import Annotated

from fastapi import APIRouter, Depends, FastAPI, Header, HTTPException, Query, Request
from sqlalchemy.orm import Session

import switchbot as sb
import xiaomi as xm
from common import READINGS_DEFAULT_LIMIT, READINGS_MAX_LIMIT
from config import load_config
from db import build_engine, build_session_factory
from service import fetch_readings, ingest_reading


def require_api_key(request: Request, x_api_key: str | None = Header(default=None)):
    api_key = request.app.state.config.get("api_key")
    print(f"{api_key=}")
    if not api_key:
        raise HTTPException(status_code=500, detail="server misconfigured")
    if x_api_key != api_key:
        raise HTTPException(status_code=401, detail="unauthorized")


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

    protected = APIRouter(dependencies=[Depends(require_api_key)])

    @app.get("/health")
    def health():
        return {"ok": True}

    @protected.post("/switchbot/reading")
    def create_switchbot_reading(reading: sb.ReadingIn, db: Session = Depends(get_db)):
        return ingest_reading(
            db=db,
            reading=reading,
            sensor_type=sb.SENSOR_TYPE_DB,
            maybe_warn=sb.maybe_warn,
            reading_model=sb.READING_MODEL,
            compare_fields=["temperature_c", "humidity_pct"],
        )

    @protected.post("/xiaomi/reading")
    def create_xiaomi_reading(reading: xm.ReadingIn, db: Session = Depends(get_db)):
        return ingest_reading(
            db=db,
            reading=reading,
            sensor_type=xm.SENSOR_TYPE_DB,
            maybe_warn=xm.maybe_warn,
            reading_model=xm.READING_MODEL,
            compare_fields=[
                "temperature_c",
                "moisture_pct",
                "light_lux",
                "conductivity_us_cm",
            ],
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
            reading_model=sb.READING_MODEL,
            out_model=sb.ReadingOut,
            selected_fields=["timestamp", "temperature_c", "humidity_pct"],
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
            reading_model=xm.READING_MODEL,
            out_model=xm.ReadingOut,
            selected_fields=[
                "timestamp",
                "temperature_c",
                "moisture_pct",
                "light_lux",
                "conductivity_us_cm",
            ],
        )

    app.include_router(protected)
    return app


app = create_app(load_config())
