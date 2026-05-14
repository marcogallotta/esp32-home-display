from app.api_limits import MemoryMapStore


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
