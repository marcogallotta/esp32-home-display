from datetime import datetime
from typing import Annotated

from fastapi import Depends, FastAPI, Query
from sqlalchemy.orm import Session

import switchbot as sb
import xiaomi as xm
from common import READINGS_DEFAULT_LIMIT, READINGS_MAX_LIMIT
from models import SessionLocal
from service import fetch_readings, ingest_reading


app = FastAPI()


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


@app.get("/health")
def health():
    return {"ok": True}


@app.post("/switchbot/reading")
def create_switchbot_reading(reading: sb.ReadingIn, db: Session = Depends(get_db)):
    return ingest_reading(
        db=db,
        reading=reading,
        sensor_type=sb.SENSOR_TYPE_DB,
        maybe_warn=sb.maybe_warn,
        build_row=sb.build_row,
        reading_model=sb.READING_MODEL,
        compare_fields=["temperature_c", "humidity_pct"],
    )


@app.post("/xiaomi/reading")
def create_xiaomi_reading(reading: xm.ReadingIn, db: Session = Depends(get_db)):
    return ingest_reading(
        db=db,
        reading=reading,
        sensor_type=xm.SENSOR_TYPE_DB,
        maybe_warn=xm.maybe_warn,
        build_row=xm.build_row,
        reading_model=xm.READING_MODEL,
        compare_fields=[
            "temperature_c",
            "moisture_pct",
            "light_lux",
            "conductivity_us_cm",
        ],
    )


@app.get("/switchbot/readings", response_model=list[sb.ReadingOut])
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


@app.get("/xiaomi/readings", response_model=list[xm.ReadingOut])
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
