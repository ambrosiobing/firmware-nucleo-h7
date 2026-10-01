"""P12's two gates, proven on synthetic input, including the chapter's criterion.

THE ACCEPTANCE CRITERION THIS FILE EXISTS FOR: a deliberate five percent charge
regression turns the build red with nobody at the bench. That is asserted directly
below, and it is the whole reason the gate could be written before the hardware.

The rest of the file pins the failures that matter more than the happy path. A gate
is only worth having if it fails, and the ways it must fail are specific:

  - a missing measurement is a failure, not a pass. A gate that skips when it cannot
    find its input reports green on the day the build stops producing the thing it
    checks.
  - a missing phase is a failure. Losing a phase looks like an improvement: fewer
    phases, less total charge, everything green.
  - a target with no committed budget is a failure. A new binary nobody is watching
    the size of is how the margin disappears.
  - a ledger with no build identity cannot gate anything, because a capture with no
    build identity is not evidence.

No hardware, no board, no instrument.
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "P12"))

from gates import (CHARGE_TOLERANCE_FRACTION, GateFailure,  # noqa: E402
                   check_charge, check_sizes)

BASELINE = {"phases": {"wake": 100.0, "sense": 500.0, "compute": 100.0,
                       "send": 600.0, "sleep": 20.0}}
BASELINE_TOTAL = sum(BASELINE["phases"].values())

BUDGETS = {"p01-first-light": {"flash": 16384, "static_ram": 12288}}


def ledger(**overrides) -> dict:
    phases = dict(BASELINE["phases"])
    phases.update(overrides)
    return {"build": "v0.1-3-gdeadbee", "phases": phases,
            "total_uc": sum(phases.values())}


# ------------------------------- the criterion the chapter states outright

def test_a_five_percent_charge_regression_turns_the_build_red():
    """The chapter's acceptance criterion, asserted rather than described.

    One phase grows by six percent, which is past the five percent tolerance, and
    the gate must fail and name the phase. Nobody is at the bench; this runs on a
    laptop with no instrument attached.
    """
    bad = ledger(send=600.0 * 1.06)
    with pytest.raises(GateFailure) as exc:
        check_charge(bad, BASELINE)
    assert "send" in str(exc.value)
    assert "tolerance" in str(exc.value)


def test_a_regression_just_inside_the_tolerance_passes():
    """The other half of the claim. A gate that failed everything would also pass
    the test above, so the boundary is pinned from both sides."""
    ok = ledger(send=600.0 * (1.0 + CHARGE_TOLERANCE_FRACTION * 0.9))
    lines = check_charge(ok, BASELINE)
    assert any("send" in line for line in lines)


def test_an_improvement_is_not_a_failure():
    """Using less charge than the baseline is the point of the work, not a defect."""
    better = ledger(send=400.0)
    check_charge(better, BASELINE)


# ------------------------------------------- the failures that matter most

def test_a_missing_phase_is_a_failure_and_not_an_improvement():
    """Losing a phase looks like an improvement: fewer phases, less total, green.

    This is the single most dangerous way for an energy gate to be wrong, because
    the direction of the error flatters the work.
    """
    phases = dict(BASELINE["phases"])
    del phases["send"]
    bad = {"build": "v0.1", "phases": phases, "total_uc": sum(phases.values())}
    with pytest.raises(GateFailure, match="send"):
        check_charge(bad, BASELINE)


def test_phases_that_do_not_sum_to_the_total_are_refused():
    """A ledger whose parts do not reconcile has lost or double counted a phase, and
    no verdict on that run is worth anything."""
    bad = ledger()
    bad["total_uc"] = BASELINE_TOTAL * 2
    with pytest.raises(GateFailure, match="missing or double counted"):
        check_charge(bad, BASELINE)


def test_a_ledger_with_no_build_identity_cannot_gate_anything():
    bad = ledger()
    del bad["build"]
    with pytest.raises(GateFailure, match="not evidence"):
        check_charge(bad, BASELINE)


def test_a_phase_measured_but_not_in_the_baseline_is_a_failure():
    """Otherwise the gate watches less than the run does."""
    bad = ledger(radio=900.0)
    with pytest.raises(GateFailure, match="radio"):
        check_charge(bad, BASELINE)


def test_an_empty_ledger_is_a_failure_not_a_pass():
    with pytest.raises(GateFailure):
        check_charge({"build": "v0.1", "phases": {}, "total_uc": 0.0}, BASELINE)


# ------------------------------------------------------------ the size gate

def test_a_build_within_its_budget_passes():
    lines = check_sizes({"p01-first-light": {"flash": 9000, "static_ram": 4000}}, BUDGETS)
    assert any("p01-first-light" in line for line in lines)
    assert any("margin" in line for line in lines)


def test_flash_over_budget_fails_and_says_by_how_much():
    with pytest.raises(GateFailure) as exc:
        check_sizes({"p01-first-light": {"flash": 20000, "static_ram": 4000}}, BUDGETS)
    assert "over the committed 16384 by 3616" in str(exc.value)


def test_static_ram_over_budget_fails():
    with pytest.raises(GateFailure, match="static_ram"):
        check_sizes({"p01-first-light": {"flash": 9000, "static_ram": 99999}}, BUDGETS)


def test_a_target_with_no_committed_budget_is_a_failure():
    """A new binary nobody is watching the size of is how the margin disappears."""
    with pytest.raises(GateFailure, match="no committed budget"):
        check_sizes({"p99-new-thing": {"flash": 100, "static_ram": 100}}, BUDGETS)


def test_no_sizes_reported_at_all_is_a_failure_not_a_pass():
    """A build failure wearing a passing gate's clothes."""
    with pytest.raises(GateFailure, match="no sizes were reported"):
        check_sizes({}, BUDGETS)


def test_a_build_that_omits_a_field_is_a_failure():
    with pytest.raises(GateFailure, match="reported no static_ram"):
        check_sizes({"p01-first-light": {"flash": 9000}}, BUDGETS)


# --------------------------------------------- the committed files are usable

def test_the_committed_budgets_and_baseline_load_and_reconcile():
    """The files in projects/P12-energy-regression are real files the gate reads, so
    a typo in either would be found here rather than on the day the gate first runs
    for real."""
    import json
    root = Path(__file__).resolve().parent.parent.parent / "projects" / "P12-energy-regression"

    budgets = json.loads((root / "budgets.json").read_text(encoding="utf-8"))
    targets = [k for k in budgets if not k.startswith("_")]
    assert targets, "no targets have budgets"
    for t in targets:
        assert "flash" in budgets[t] and "static_ram" in budgets[t], t

    base = json.loads((root / "baseline.json").read_text(encoding="utf-8"))
    assert abs(sum(base["phases"].values()) - base["total_uc"]) < 0.01, (
        "the committed baseline's phases do not sum to its own total")

    # And the gate accepts a run that matches the committed baseline exactly.
    run = {"build": "v0.0-placeholder", "phases": dict(base["phases"]),
           "total_uc": base["total_uc"]}
    check_charge(run, base)


def test_the_committed_baseline_says_it_is_not_a_measurement():
    """Every number in it is a placeholder, and the file must say so, because a
    placeholder that reads like a measurement is the worst artefact in this
    repository."""
    import json
    root = Path(__file__).resolve().parent.parent.parent / "projects" / "P12-energy-regression"
    base = json.loads((root / "baseline.json").read_text(encoding="utf-8"))
    note = base.get("_note", "").lower()
    assert "placeholder" in note, "the baseline must say its numbers are placeholders"
    # Either wording; pinning one exact phrase would make this test brittle about
    # prose rather than about the claim.
    assert ("not a measurement" in note or "none is a measurement" in note), (
        "the baseline must say its numbers are not measurements")
