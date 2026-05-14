import math
import threading
import time
from collections.abc import Callable
from dataclasses import dataclass
from typing import Protocol

from fastapi import HTTPException, Request


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


class TokenBucketLimiter:
    def __init__(
        self,
        store: MapStore,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self._store = store
        self._clock = clock
        self._lock = threading.Lock()

    def consume(self, key: str, capacity: float, rate: float) -> float | None:
        """Returns None if allowed, or seconds to wait if denied."""
        now = self._clock()
        with self._lock:
            state = self._store.get(key)
            if state is None:
                tokens = capacity
            else:
                assert isinstance(state, TokenBucketState)
                elapsed = now - state.updated_at
                tokens = min(capacity, state.tokens + elapsed * rate)

            if tokens >= 1.0:
                self._store.set(key, TokenBucketState(tokens=tokens - 1.0, updated_at=now))
                return None

            self._store.set(key, TokenBucketState(tokens=tokens, updated_at=now))
            return (1.0 - tokens) / rate


_limiter = TokenBucketLimiter(MemoryMapStore())


def make_rate_limiter(
    base_key: str,
    limit: int,
    period: int,
    *,
    burst: bool = False,
    per_ip: bool = False,
    limiter: TokenBucketLimiter | None = None,
) -> Callable:
    capacity = float(limit) if burst else 1.0
    rate = limit / period
    _lim = limiter or _limiter

    async def dependency(request: Request) -> None:
        host = request.client.host if request.client else "unknown"
        key = f"{base_key}:{host}" if per_ip else base_key
        retry_after = _lim.consume(key, capacity, rate)
        if retry_after is not None:
            raise HTTPException(
                status_code=429,
                headers={"Retry-After": str(math.ceil(retry_after))},
            )

    return dependency
