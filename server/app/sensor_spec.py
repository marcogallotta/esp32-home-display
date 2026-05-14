from dataclasses import dataclass
from datetime import datetime
from typing import Any, Callable, Protocol, TypeVar, Generic

from sqlalchemy.orm.attributes import InstrumentedAttribute


Range = tuple[int | float, int | float]


class ReadingLike(Protocol):
    mac: str
    name: str | None
    timestamp: datetime

    def model_dump(self, *, exclude: set[str] | None = None) -> dict:
        ...


@dataclass(frozen=True)
class DataField:
    name: str
    column: InstrumentedAttribute
    getter: Callable[[Any], Any]
    hard_range: Range | None = None
    soft_range: Range | None = None


ReadingT = TypeVar("ReadingT", bound=ReadingLike)
ReadingOutT = TypeVar("ReadingOutT")
ReadingModelT = TypeVar("ReadingModelT")


@dataclass(frozen=True)
class SensorSpec(Generic[ReadingT, ReadingOutT, ReadingModelT]):
    db_sensor_type: int
    reading_model: type[ReadingModelT]
    reading_out: type[ReadingOutT]
    unique_constraint_name: str
    data_fields: tuple[DataField, ...]
