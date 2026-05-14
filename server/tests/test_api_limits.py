import pytest

from app.api_limits import MemoryMapStore, TokenBucketLimiter, TokenBucketState


# --- MapStore ---

def test_get_missing_returns_none():
    store = MemoryMapStore()
    assert store.get("x") is None


def test_set_then_get_returns_value():
    store = MemoryMapStore()
    store.set("x", 42)
    assert store.get("x") == 42


def test_set_overwrites_existing():
    store = MemoryMapStore()
    store.set("x", 1)
    store.set("x", 2)
    assert store.get("x") == 2


def test_delete_removes_value():
    store = MemoryMapStore()
    store.set("x", 99)
    store.delete("x")
    assert store.get("x") is None


def test_delete_missing_key_is_safe():
    store = MemoryMapStore()
    store.delete("nonexistent")


def test_store_and_retrieve_token_bucket_state():
    store = MemoryMapStore()
    state = TokenBucketState(tokens=9.5, updated_at=1_000_000.0)
    store.set("bucket:esp32:write", state)
    result = store.get("bucket:esp32:write")
    assert result == state


# --- TokenBucketLimiter ---

def _limiter(t_ref):
    return TokenBucketLimiter(MemoryMapStore(), clock=lambda: t_ref[0])


def test_first_request_allowed():
    lim = _limiter([0.0])
    assert lim.consume("k", capacity=5.0, rate=1.0) is None


def test_bucket_drains_and_denies():
    lim = _limiter([0.0])
    for _ in range(5):
        assert lim.consume("k", capacity=5.0, rate=1.0) is None
    assert lim.consume("k", capacity=5.0, rate=1.0) is not None


def test_denied_returns_positive_retry_after():
    lim = _limiter([0.0])
    lim.consume("k", capacity=1.0, rate=1.0)  # drain
    retry = lim.consume("k", capacity=1.0, rate=1.0)
    assert retry is not None
    assert retry > 0.0


def test_retry_after_is_accurate():
    lim = _limiter([0.0])
    lim.consume("k", capacity=1.0, rate=1.0)  # drain to 0 tokens
    retry = lim.consume("k", capacity=1.0, rate=1.0)
    assert retry == pytest.approx(1.0)


def test_tokens_refill_over_time():
    t = [0.0]
    lim = _limiter(t)
    lim.consume("k", capacity=1.0, rate=1.0)  # drain
    assert lim.consume("k", capacity=1.0, rate=1.0) is not None  # denied
    t[0] = 1.0
    assert lim.consume("k", capacity=1.0, rate=1.0) is None  # refilled


def test_burst_false_caps_at_one_token():
    t = [0.0]
    lim = _limiter(t)
    assert lim.consume("k", capacity=1.0, rate=10.0) is None   # allowed
    assert lim.consume("k", capacity=1.0, rate=10.0) is not None  # denied
    t[0] = 0.5  # +5 tokens at rate=10, but capacity=1
    assert lim.consume("k", capacity=1.0, rate=10.0) is None   # 1 token available
    assert lim.consume("k", capacity=1.0, rate=10.0) is not None  # denied again


def test_burst_true_allows_full_capacity():
    lim = _limiter([0.0])
    for _ in range(10):
        assert lim.consume("k", capacity=10.0, rate=10 / 60) is None
    assert lim.consume("k", capacity=10.0, rate=10 / 60) is not None


def test_keys_are_independent():
    lim = _limiter([0.0])
    lim.consume("a", capacity=1.0, rate=1.0)  # drain a
    assert lim.consume("b", capacity=1.0, rate=1.0) is None  # b unaffected
