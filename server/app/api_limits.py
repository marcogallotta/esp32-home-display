import threading
from dataclasses import dataclass
from typing import Protocol


@dataclass
class TokenBucketState:
    tokens: float
    updated_at: float


class MapStore(Protocol):
    def get(self, key: str) -> object | None: ...
    def set(self, key: str, value: object) -> None: ...
    def delete(self, key: str) -> None: ...


class MemoryMapStore:
    def __init__(self) -> None:
        self._data: dict[str, object] = {}
        self._lock = threading.Lock()

    def get(self, key: str) -> object | None:
        with self._lock:
            return self._data.get(key)

    def set(self, key: str, value: object) -> None:
        with self._lock:
            self._data[key] = value

    def delete(self, key: str) -> None:
        with self._lock:
            self._data.pop(key, None)
