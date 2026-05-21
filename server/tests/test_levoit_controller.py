import pytest

from app.levoit_controller import (
    LevoitControlDecision,
    absolute_humidity,
    compute_decision,
    target_relative_humidity,
)


def _decide(**overrides):
    defaults = dict(
        temperature_c=20.0,
        humidity_pct=50.0,
        target_absolute_humidity=8.0,
        minimum_humidity=40,
        maximum_humidity=60,
        humidity_change_threshold=1.0,
    )
    defaults.update(overrides)
    return compute_decision(**defaults)


# --- math helpers ---

def test_absolute_humidity_reasonable_value():
    # 20°C, 50% RH → ~8.6 g/m³
    ah = absolute_humidity(20.0, 50.0)
    assert 8.0 < ah < 9.5


def test_target_relative_humidity_roundtrip():
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
    d = _decide(target_absolute_humidity=1.0, minimum_humidity=40)
    assert d.action == "set"
    assert d.commanded_humidity == 40


def test_clamps_above_maximum_humidity():
    d = _decide(target_absolute_humidity=20.0, maximum_humidity=60)
    assert d.action == "set"
    assert d.commanded_humidity == 60


def test_ideal_humidity_not_clamped_in_output():
    d = _decide(target_absolute_humidity=1.0, minimum_humidity=40, humidity_change_threshold=0.0)
    assert d.action == "set"
    assert d.ideal_humidity is not None
    assert d.ideal_humidity < 40.0
    assert d.commanded_humidity == 40


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


# --- humidity_change_threshold vs last commanded ---

def test_skips_when_change_within_threshold_vs_last_command():
    d_baseline = _decide(humidity_change_threshold=0.0)
    commanded = d_baseline.commanded_humidity
    assert commanded is not None

    d = _decide(last_commanded_humidity=commanded, humidity_change_threshold=1.0)
    assert d.action == "skip"
    assert "threshold" in d.reason


def test_does_not_skip_when_change_meets_threshold_vs_last_command():
    d_baseline = _decide(humidity_change_threshold=0.0)
    commanded = d_baseline.commanded_humidity
    assert commanded is not None

    # diff == threshold → NOT skipped (condition is strict <)
    d = _decide(last_commanded_humidity=commanded - 1, humidity_change_threshold=1.0)
    assert d.action == "set"


def test_first_run_no_last_commanded_always_sets():
    d = _decide(last_commanded_humidity=None)
    assert d.action == "set"


# --- full set decision ---

def test_returns_set_decision_when_all_valid():
    d = _decide()
    assert d.action == "set"
    assert d.commanded_humidity is not None
    assert d.ideal_humidity is not None
    assert d.current_absolute_humidity is not None
    assert d.reason == "ok"


# --- decision fields populated on skip ---

def test_skip_fields_populated_after_computing_ideal():
    d_baseline = _decide(humidity_change_threshold=0.0)
    commanded = d_baseline.commanded_humidity
    assert commanded is not None

    d = _decide(last_commanded_humidity=commanded, humidity_change_threshold=1.0)
    assert d.action == "skip"
    assert d.ideal_humidity is not None
    assert d.current_absolute_humidity is not None
    assert d.commanded_humidity == commanded


def test_early_skip_has_no_commanded_humidity():
    assert _decide(temperature_c=None).commanded_humidity is None
