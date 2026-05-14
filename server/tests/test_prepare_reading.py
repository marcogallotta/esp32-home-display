from datetime import datetime, timedelta, timezone

from app import switchbot
from app import xiaomi
from app.service import prepare_reading


TZ_PLUS2 = timezone(timedelta(hours=2))
TS_OFFSET = datetime(2026, 4, 21, 20, 0, 0, tzinfo=TZ_PLUS2)
TS_UTC = datetime(2026, 4, 21, 18, 0, 0, tzinfo=timezone.utc)


def _switchbot(**kwargs):
    defaults = dict(mac="aa:bb:cc:dd:ee:ff", name="test", timestamp=TS_OFFSET, temperature_c=21.5, humidity_pct=48.0)
    return switchbot.ReadingIn(**{**defaults, **kwargs})


def _xiaomi(**kwargs):
    defaults = dict(mac="aa:bb:cc:dd:ee:ff", name="test", timestamp=TS_OFFSET, temperature_c=21.5)
    return xiaomi.ReadingIn(**{**defaults, **kwargs})


def test_prepare_reading_returns_new_object():
    reading = _switchbot()
    result = prepare_reading(reading, switchbot.SENSOR)
    assert result is not reading


def test_prepare_reading_does_not_mutate_mac():
    reading = _switchbot(mac="aa:bb:cc:dd:ee:ff")
    prepare_reading(reading, switchbot.SENSOR)
    assert reading.mac == "aa:bb:cc:dd:ee:ff"


def test_prepare_reading_normalizes_mac_in_copy():
    reading = _switchbot(mac="aa:bb:cc:dd:ee:ff")
    result = prepare_reading(reading, switchbot.SENSOR)
    assert result.mac == "AA:BB:CC:DD:EE:FF"


def test_prepare_reading_does_not_mutate_timestamp():
    reading = _switchbot(timestamp=TS_OFFSET)
    prepare_reading(reading, switchbot.SENSOR)
    assert reading.timestamp == TS_OFFSET
    assert reading.timestamp.tzinfo == TZ_PLUS2


def test_prepare_reading_normalizes_timestamp_to_utc_in_copy():
    reading = _switchbot(timestamp=TS_OFFSET)
    result = prepare_reading(reading, switchbot.SENSOR)
    assert result.timestamp == TS_UTC
    assert result.timestamp.tzinfo == timezone.utc


def test_prepare_reading_xiaomi_does_not_mutate_mac():
    reading = _xiaomi(mac="aa:bb:cc:dd:ee:ff")
    prepare_reading(reading, xiaomi.SENSOR)
    assert reading.mac == "aa:bb:cc:dd:ee:ff"


def test_prepare_reading_xiaomi_does_not_mutate_timestamp():
    reading = _xiaomi(timestamp=TS_OFFSET)
    prepare_reading(reading, xiaomi.SENSOR)
    assert reading.timestamp.tzinfo == TZ_PLUS2
