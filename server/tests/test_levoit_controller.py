from datetime import datetime, timedelta, timezone

import pytest

from app.levoit_controller import (
    LevoitControlDecision,
    absolute_humidity,
    compute_decision,
    target_relative_humidity,
)

_NOW = datetime(2026, 4, 21, 12, 0, 0, tzinfo=timezone.utc)
_FRESH_TS = "2026-04-21T11:55:00Z"   # 5 minutes ago — within 900s default
_STALE_TS = "2026-04-21T09:00:00Z"   # 3 hours ago


def _decide(**overrides):
    defaults = dict(
        timestamp=_FRESH_TS,
        temperature_c=20.0,
        humidity_pct=50.0,
        now=_NOW,
        target_absolute_humidity=8.0,
        minimum_humidity=40,
        maximum_humidity=60,
        reading_max_age_seconds=900,
        minimum_command_interval_seconds=300,
        humidity_change_threshold=2.0,
    )
    defaults.update(overrides)
    return compute_decision(**defaults)


# --- math helpers ---

def test_absolute_humidity_reasonable_value():
    # 20°C, 50% RH → ~8.6 g/m³
    ah = absolute_humidity(20.0, 50.0)
    assert 8.0 < ah < 9.5


def test_target_relative_humidity_roundtrip():
    # Computing target RH for a target AH should round-trip back to that AH.
    temp = 20.0
    target_ah = 8.0
    rh = target_relative_humidity(temp, target_ah)
    ah_back = absolute_humidity(temp, rh)
    assert abs(ah_back - target_ah) < 0.01


# --- compute target RH from target AH ---

def test_computes_commanded_humidity_from_target_ah_and_temperature():
    # At 20°C, target_AH=8 g/m³ → ~46% RH
    d = _decide(target_absolute_humidity=8.0, temperature_c=20.0)
    assert d.action == "set"
    assert d.commanded_humidity is not None
    assert 44 <= d.commanded_humidity <= 48


# --- clamping ---

def test_clamps_below_minimum_humidity():
    # Very low target AH → ideal RH will be below minimum_humidity=40
    d = _decide(target_absolute_humidity=1.0, minimum_humidity=40)
    assert d.action == "set"
    assert d.commanded_humidity == 40


def test_clamps_above_maximum_humidity():
    # Very high target AH → ideal RH will be above maximum_humidity=60
    d = _decide(target_absolute_humidity=20.0, maximum_humidity=60)
    assert d.action == "set"
    assert d.commanded_humidity == 60


def test_ideal_humidity_not_clamped_in_output():
    # ideal_humidity in the decision is the pre-clamp value
    d = _decide(target_absolute_humidity=1.0, minimum_humidity=40, humidity_change_threshold=0.0)
    assert d.action == "set"
    assert d.ideal_humidity is not None
    assert d.ideal_humidity < 40.0
    assert d.commanded_humidity == 40


# --- stale reading ---

def test_skips_stale_reading():
    d = _decide(timestamp=_STALE_TS, reading_max_age_seconds=900)
    assert d.action == "skip"
    assert "too old" in d.reason
    assert d.age_seconds is not None
    assert d.age_seconds > 900


def test_fresh_reading_is_not_stale():
    d = _decide(timestamp=_FRESH_TS, reading_max_age_seconds=900)
    assert d.action == "set"


# --- missing / invalid timestamp ---

def test_skips_missing_timestamp():
    d = _decide(timestamp=None)
    assert d.action == "skip"
    assert "timestamp" in d.reason


def test_skips_invalid_timestamp():
    d = _decide(timestamp="not-a-date")
    assert d.action == "skip"
    assert "invalid" in d.reason


# --- missing temperature ---

def test_skips_missing_temperature():
    d = _decide(temperature_c=None)
    assert d.action == "skip"
    assert "temperature" in d.reason


# --- missing humidity ---

def test_missing_humidity_does_not_block_set_decision():
    d = _decide(humidity_pct=None)
    assert d.action == "set"
    assert d.commanded_humidity is not None


def test_missing_humidity_yields_none_current_absolute_humidity():
    d = _decide(humidity_pct=None)
    assert d.current_absolute_humidity is None


# --- current_absolute_humidity populated when humidity present ---

def test_current_absolute_humidity_computed_when_humidity_present():
    d = _decide(temperature_c=20.0, humidity_pct=50.0)
    assert d.current_absolute_humidity is not None
    assert 8.0 < d.current_absolute_humidity < 9.5


# --- command interval ---

def test_skips_when_command_interval_too_recent():
    recent = _NOW - timedelta(seconds=100)
    d = _decide(last_command_time=recent, minimum_command_interval_seconds=300)
    assert d.action == "skip"
    assert "too recent" in d.reason


def test_does_not_skip_when_command_interval_expired():
    old = _NOW - timedelta(seconds=400)
    d = _decide(last_command_time=old, minimum_command_interval_seconds=300)
    assert d.action == "set"


def test_does_not_skip_when_no_last_command_time():
    d = _decide(last_command_time=None)
    assert d.action == "set"


# --- humidity_change_threshold vs last commanded ---

def test_skips_when_change_within_threshold_vs_last_command():
    # Use a fixed target to get a known commanded value, then set last_commanded
    # so the difference is below the threshold.
    d_baseline = _decide(humidity_change_threshold=0.0)
    commanded = d_baseline.commanded_humidity
    assert commanded is not None

    d = _decide(
        last_commanded_humidity=commanded,  # same → diff = 0 < threshold=2
        humidity_change_threshold=2.0,
    )
    assert d.action == "skip"
    assert "last command" in d.reason


def test_does_not_skip_when_change_meets_threshold_vs_last_command():
    d_baseline = _decide(humidity_change_threshold=0.0)
    commanded = d_baseline.commanded_humidity
    assert commanded is not None

    # diff == threshold (2) → NOT skipped (condition is strict <)
    d = _decide(
        last_commanded_humidity=commanded - 2,
        humidity_change_threshold=2.0,
    )
    assert d.action == "set"


# --- humidity_change_threshold vs device target ---

def test_skips_when_change_within_threshold_vs_device_target():
    d_baseline = _decide(humidity_change_threshold=0.0)
    commanded = d_baseline.commanded_humidity
    assert commanded is not None

    d = _decide(
        current_device_target_humidity=commanded,
        humidity_change_threshold=2.0,
    )
    assert d.action == "skip"
    assert "device target" in d.reason


def test_does_not_skip_when_change_meets_threshold_vs_device_target():
    d_baseline = _decide(humidity_change_threshold=0.0)
    commanded = d_baseline.commanded_humidity
    assert commanded is not None

    d = _decide(
        current_device_target_humidity=commanded - 2,
        humidity_change_threshold=2.0,
    )
    assert d.action == "set"


# --- full set decision ---

def test_returns_set_decision_when_all_valid():
    d = _decide()
    assert d.action == "set"
    assert d.commanded_humidity is not None
    assert d.age_seconds is not None
    assert d.ideal_humidity is not None
    assert d.current_absolute_humidity is not None
    assert d.reason == "ok"


# --- decision fields populated on skip ---

def test_skip_fields_populated_after_computing_ideal():
    # A threshold-skip fires after ideal is computed, so all fields should be set.
    d_baseline = _decide(humidity_change_threshold=0.0)
    commanded = d_baseline.commanded_humidity
    assert commanded is not None

    d = _decide(
        last_commanded_humidity=commanded,
        humidity_change_threshold=2.0,
    )
    assert d.action == "skip"
    assert d.ideal_humidity is not None
    assert d.age_seconds is not None
    assert d.current_absolute_humidity is not None
    assert d.commanded_humidity == commanded


def test_command_interval_skip_preserves_commanded_humidity():
    recent = _NOW - timedelta(seconds=100)
    d_baseline = _decide(humidity_change_threshold=0.0)
    commanded = d_baseline.commanded_humidity

    d = _decide(last_command_time=recent, minimum_command_interval_seconds=300)
    assert d.action == "skip"
    assert d.commanded_humidity == commanded


def test_early_skips_have_no_commanded_humidity():
    # Skips before computation (no timestamp, stale, no temperature) → commanded=None.
    assert _decide(timestamp=None).commanded_humidity is None
    assert _decide(timestamp=_STALE_TS).commanded_humidity is None
    assert _decide(temperature_c=None).commanded_humidity is None
