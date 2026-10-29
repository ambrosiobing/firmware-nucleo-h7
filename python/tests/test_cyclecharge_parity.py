"""P07's cycle segmentation and battery arithmetic, in four languages.

**Chapter 7 asked for this file in these words.** Step 9: "Test the analysis on a
synthetic trace. The segmentation is the fragile part, so it is tested on the
host with a trace whose answer is known by construction, in continuous
integration, with no instrument attached."

**Everything here is exact.** Every current in the oracle is a power of two and
the sample rate is 1024 Hz, so each charge is a sum of powers of two divided by a
power of two and no case passes on a tolerance. The traces are stored as a recipe
and built here by `build`, which is the same ten lines the generator used, so the
oracle stays small and checkable rather than being thousands of numbers.

**Three findings, all against code chapter 7 prints.**

* Step 9's own test asserts nine cycles. Its trace tiles ten marker pulses and
  begins while the marker is already high, so the first cycle has no rising edge
  in front of it: ten pulses bound eight complete cycles. The chapter's printed
  code returns eight and this implementation returns eight, so the defect is in
  the assertion rather than in a disagreement between implementations, and a test
  whose expected value is wrong is the one thing a reader checks the code
  against.
* The printed window begins at the rise index rather than one past it, so every
  cycle carries one sample belonging to the cycle before. On the flat trace step
  9 uses this is invisible, which is why that test passes on the charge. One
  sample of 4096 microamps at index 23, still inside the first cycle, is
  attributed to the second by the chapter's window and to the first here.
* Step 10's lifetime takes cycles per day as a free assumption while the trace
  already fixes the cycle period, and nothing checks that the product is a day.
  A one second cycle at a hundred cycles a day covers 100 seconds of 86400, and
  at two microamps of sleep the printed form gives 73636 days where costing the
  rest of the day gives 4033.
"""
from __future__ import annotations

import ctypes
import json
import sys
from pathlib import Path

import pytest

from conftest import (BUILD, BUILD_HOST_CMD, CYC_CPP_FILTER,
                      CYC_RUST_FILTER, require_artefact, run_filter,
                      shared_library_name)

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "projects" / "P07-stop-mode"
sys.path.insert(0, str(PROJECT / "python"))

from cyclecharge import (CYC_MAX_CYCLES, ERRORS,  # noqa: E402
                         chapter_lifetime_days, chapter_per_cycle_charge,
                         lifetime as py_lifetime,
                         per_cycle_charge as py_charge, report as py_report)

ORACLE = json.loads((PROJECT / "vectors.json").read_text(encoding="utf-8"))
assert ORACLE["schema"] == 1, "the oracle's schema moved"


def build(spec):
    """The trace builder, the same ten lines the oracle was generated with."""
    period, high = spec["period"], spec["high"]
    current, digital = [], []
    for pulse in range(spec["pulses"]):
        level = spec["current_per_cycle"][pulse
                                          % len(spec["current_per_cycle"])]
        for k in range(period):
            current.append(float(level))
            digital.append(1 if k < high else 0)
    if spec.get("lead_low"):
        current = ([float(spec["current_per_cycle"][0])] * spec["lead_low"]
                   + current)
        digital = [0] * spec["lead_low"] + digital
    for key, value in spec.get("override", {}).items():
        current[int(key)] = float(value)
    return current, digital


class CReport(ctypes.Structure):
    """Must match cyc_report_t in c/cyclecharge.h, field for field."""

    _fields_ = [("cycles", ctypes.c_uint32), ("median_uc", ctypes.c_double),
                ("min_uc", ctypes.c_double), ("max_uc", ctypes.c_double),
                ("spread_pct", ctypes.c_double),
                ("energy_median_uj", ctypes.c_double)]


class CBattery(ctypes.Structure):
    """Must match cyc_battery_t."""

    _fields_ = [("energy_per_cycle_uj", ctypes.c_double),
                ("cycles_per_day", ctypes.c_double),
                ("cycle_period_s", ctypes.c_double),
                ("sleep_ua", ctypes.c_double),
                ("sleep_volts", ctypes.c_double),
                ("cell_mah", ctypes.c_double),
                ("cell_volts", ctypes.c_double)]


class CBatteryOut(ctypes.Structure):
    """Must match cyc_battery_out_t."""

    _fields_ = [("cell_j", ctypes.c_double), ("covered_s", ctypes.c_double),
                ("uncovered_s", ctypes.c_double),
                ("cycle_j_per_day", ctypes.c_double),
                ("uncovered_j_per_day", ctypes.c_double),
                ("days", ctypes.c_double),
                ("days_chapter", ctypes.c_double),
                ("covers_the_day", ctypes.c_int)]


_LIB = None


def c_lib():
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("cyclecharge")
        require_artefact(path)
        d = ctypes.CDLL(str(path))
        d.cyc_per_cycle_charge.restype = ctypes.c_int
        d.cyc_per_cycle_charge.argtypes = [
            ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_uint32, ctypes.c_double, ctypes.c_uint8,
            ctypes.POINTER(ctypes.c_double), ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32)]
        d.cyc_report.restype = ctypes.c_int
        d.cyc_report.argtypes = [ctypes.POINTER(ctypes.c_double),
                                 ctypes.c_uint32, ctypes.c_double,
                                 ctypes.c_uint32, ctypes.POINTER(CReport)]
        d.cyc_lifetime.restype = ctypes.c_int
        d.cyc_lifetime.argtypes = [ctypes.POINTER(CBattery),
                                   ctypes.POINTER(CBatteryOut)]
        _LIB = d
    return _LIB


def c_charge(current, digital, fs, bit, max_out=CYC_MAX_CYCLES):
    n = len(current)
    cur = (ctypes.c_double * max(n, 1))(*current) if n \
        else (ctypes.c_double * 1)()
    dig = (ctypes.c_uint8 * max(n, 1))(*digital) if n \
        else (ctypes.c_uint8 * 1)()
    out = (ctypes.c_double * max(max_out, 1))()
    count = ctypes.c_uint32(0)
    rc = c_lib().cyc_per_cycle_charge(cur, dig, n, fs, bit, out, max_out,
                                      ctypes.byref(count))
    return rc, list(out)[:count.value]


def c_report(charges, volts, floor):
    n = len(charges)
    arr = (ctypes.c_double * max(n, 1))(*charges) if n \
        else (ctypes.c_double * 1)()
    out = CReport()
    rc = c_lib().cyc_report(arr, n, volts, floor, ctypes.byref(out))
    return rc, out


def c_lifetime(spec):
    arg = CBattery(spec["energy_per_cycle_uj"], spec["cycles_per_day"],
                   spec["cycle_period_s"], spec["sleep_ua"],
                   spec["sleep_volts"], spec["cell_mah"], spec["cell_volts"])
    out = CBatteryOut()
    rc = c_lib().cyc_lifetime(ctypes.byref(arg), ctypes.byref(out))
    return rc, out


@pytest.mark.parametrize("case", ORACLE["traces"], ids=lambda c: c["why"][:46])
def test_both_implementations_segment_every_trace_alike(case):
    current, digital = build(case["spec"])
    rc, got = py_charge(current, digital, case["fs_hz"], case["cycle_bit"])
    assert rc == 0, case["why"]
    assert got == case["charges_uc"], case["why"]
    assert len(got) == case["count"], case["why"]
    rc, got = c_charge(current, digital, case["fs_hz"], case["cycle_bit"])
    assert rc == 0, case["why"]
    assert got == case["charges_uc"], case["why"]


@pytest.mark.parametrize("case", ORACLE["trace_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_traces(case):
    n = case["n"]
    current = [128.0] * n
    digital = [0] * n
    rc, _ = py_charge(current, digital, case["fs_hz"], case["cycle_bit"])
    assert rc == ERRORS[case["error"]], case["why"]
    rc, _ = c_charge(current, digital, case["fs_hz"], case["cycle_bit"])
    assert rc == ERRORS[case["error"]], case["why"]


def test_both_implementations_refuse_more_cycles_than_the_array_holds():
    """The bound, reached rather than described.

    A trace with more complete cycles than the caller's array can hold is
    refused, because writing past the end of it is the one failure a bounded
    analysis must not have.
    """
    spec = {"period": 4, "high": 1, "pulses": 12, "current_per_cycle": [128.0],
            "lead_low": 2}
    current, digital = build(spec)
    rc, _ = py_charge(current, digital, 1024.0, 0, max_out=4)
    assert rc == ERRORS["room"]
    rc, _ = c_charge(current, digital, 1024.0, 0, max_out=4)
    assert rc == ERRORS["room"]


@pytest.mark.parametrize("case", ORACLE["reports"], ids=lambda c: c["why"][:46])
def test_both_implementations_report_the_same_median_and_spread(case):
    rc, got = py_report(case["charges_uc"], case["volts"], case["min_cycles"])
    assert rc == 0, case["why"]
    for key in ("cycles", "median_uc", "min_uc", "max_uc", "spread_pct",
                "energy_median_uj"):
        assert got[key] == case[key], (key, case["why"])
    rc, out = c_report(case["charges_uc"], case["volts"], case["min_cycles"])
    assert rc == 0, case["why"]
    for key in ("cycles", "median_uc", "min_uc", "max_uc", "spread_pct",
                "energy_median_uj"):
        assert getattr(out, key) == case[key], (key, case["why"])


@pytest.mark.parametrize("case", ORACLE["report_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_reports(case):
    rc, out = py_report(case["charges_uc"], case["volts"], case["min_cycles"])
    assert rc == ERRORS[case["error"]] and out is None, case["why"]
    rc, _ = c_report(case["charges_uc"], case["volts"], case["min_cycles"])
    assert rc == ERRORS[case["error"]], case["why"]


@pytest.mark.parametrize("case", ORACLE["lifetimes"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_cost_the_day_alike(case):
    rc, got = py_lifetime(case["spec"])
    assert rc == 0, case["why"]
    for key in ("cell_j", "covered_s", "uncovered_s", "cycle_j_per_day",
                "uncovered_j_per_day", "days", "days_chapter",
                "covers_the_day"):
        assert got[key] == case[key], (key, case["why"])
    rc, out = c_lifetime(case["spec"])
    assert rc == 0, case["why"]
    for key in ("cell_j", "covered_s", "uncovered_s", "cycle_j_per_day",
                "uncovered_j_per_day", "days", "days_chapter",
                "covers_the_day"):
        assert getattr(out, key) == case[key], (key, case["why"])


@pytest.mark.parametrize("case", ORACLE["lifetime_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_lifetimes(case):
    rc, out = py_lifetime(case["spec"])
    assert rc == ERRORS[case["error"]] and out is None, case["why"]
    rc, _ = c_lifetime(case["spec"])
    assert rc == ERRORS[case["error"]], case["why"]


# ---------------------------------------------------------------------------
# The findings, each run against the chapter's printed code. Driven through the
# twin so they run on the authoring laptop too, where the whole C half skips;
# the C agrees on every oracle case above.

def test_step_nines_own_test_asserts_a_count_its_own_code_does_not_produce():
    """Finding one, by running step 9's construction through step 8's code.

    Ten marker pulses, the trace beginning high. The assertion in the chapter is
    nine with the comment "nine complete cycles in ten". Its own code returns
    eight, and so does this implementation, which is what says eight is right.
    """
    case = next(c for c in ORACLE["traces"] if "STEP 9 CONSTRUCTION" in c["why"])
    current, digital = build(case["spec"])
    pulses = case["spec"]["pulses"]
    theirs = chapter_per_cycle_charge(current, digital, case["fs_hz"])
    rc, mine = py_charge(current, digital, case["fs_hz"], 0)
    assert rc == 0
    assert pulses == 10, "the construction is no longer the chapter's"
    assert len(theirs) == len(mine) == 8, (len(theirs), len(mine))
    assert len(mine) != 9, (
        "the count is now nine, so step 9's assertion is right and this finding "
        "is wrong")
    print("\n  step 9's trace holds {} marker pulses and asserts {} cycles; its "
          "own code returns {} and this implementation returns {}"
          .format(pulses, 9, len(theirs), len(mine)))


def test_the_printed_window_takes_one_sample_from_the_previous_cycle():
    """Finding two, by the one sample that lands in a different cycle.

    Index 23 is the last low sample before a rise, so the current drawn there
    belongs to the cycle that was still running. The chapter's window starts at
    the rise index, which is 23, so it carries that sample into the next cycle.
    """
    case = next(c for c in ORACLE["traces"] if "INDEX 23" in c["why"])
    current, digital = build(case["spec"])
    theirs = chapter_per_cycle_charge(current, digital, case["fs_hz"])
    rc, mine = py_charge(current, digital, case["fs_hz"], 0)
    assert rc == 0 and mine == case["charges_uc"]

    big = 4096.0 / case["fs_hz"]
    theirs_at = [i for i, v in enumerate(theirs) if v > 3.0]
    mine_at = [i for i, v in enumerate(mine) if v > 3.0]
    assert len(theirs_at) == len(mine_at) == 1, (theirs_at, mine_at)
    assert theirs_at[0] == mine_at[0] + 1, (
        "the two windows now attribute the boundary sample to the same cycle, "
        "so this finding is wrong: {} and {}".format(theirs_at, mine_at))
    # And the flat trace the chapter's own test uses cannot see any of it.
    flat = next(c for c in ORACLE["traces"] if "eleven marker pulses" in c["why"])
    fc, fd = build(flat["spec"])
    assert chapter_per_cycle_charge(fc, fd, flat["fs_hz"]) == flat["charges_uc"], (
        "the two windows disagree on a flat trace, which would mean step 9's "
        "test could have caught this")
    print("\n  the 4096 microamp sample at index 23 is cycle {} for the "
          "chapter's window and cycle {} here, a difference of {:.4f} "
          "microcoulombs; on a flat trace the two windows agree exactly, which "
          "is why step 9's test passes"
          .format(theirs_at[0], mine_at[0], big))


def test_step_tens_lifetime_leaves_most_of_the_day_free():
    """Finding three, as the factor it is worth.

    The printed form divides the cell by the cycles alone. Where the cycles do
    not fill the day, the rest costs nothing in that sum.
    """
    rows = []
    for case in ORACLE["lifetimes"]:
        if case["spec"]["sleep_ua"] == 0.0:
            continue
        rc, got = py_lifetime(case["spec"])
        assert rc == 0
        printed = chapter_lifetime_days(case["spec"]["energy_per_cycle_uj"],
                                        case["spec"]["cycles_per_day"],
                                        case["spec"]["cell_mah"],
                                        case["spec"]["cell_volts"])
        assert printed == pytest.approx(got["days_chapter"], rel=1e-12), (
            "the sketch no longer reproduces the printed form")
        rows.append((case["spec"]["cycles_per_day"], got["covered_s"],
                     printed, got["days"]))

    full = [r for r in rows if r[1] == 86400.0]
    assert full and full[0][2] == pytest.approx(full[0][3], rel=1e-12), (
        "the two forms disagree even when the cycles fill the day, which would "
        "make this a different finding")
    partial = [r for r in rows if r[1] < 86400.0]
    assert partial, "no case leaves part of the day uncovered"
    worst = max(partial, key=lambda r: r[2] / r[3])
    assert worst[2] / worst[3] > 5.0, (
        "the uncovered day is no longer worth a factor of five, so this finding "
        "has changed: {:.1f}".format(worst[2] / worst[3]))

    print("\n  cycles/day   covered s   step 10 says   costing the rest   factor")
    for cpd, covered, printed, days in rows:
        print("  {:9.0f}   {:9.0f}   {:10.1f} d   {:14.1f} d   {:6.1f}"
              .format(cpd, covered, printed, days, printed / days))


def test_the_sleep_current_is_the_assumption_that_makes_the_printed_form_right():
    """The control for finding three.

    With no sleep current assumed, the uncovered day genuinely costs nothing and
    the two forms agree. That is what keeps the finding about the missing
    assumption rather than about the arithmetic.
    """
    case = next(c for c in ORACLE["lifetimes"]
                if c["spec"]["sleep_ua"] == 0.0)
    rc, got = py_lifetime(case["spec"])
    assert rc == 0
    assert got["uncovered_s"] > 0.0
    assert got["days"] == pytest.approx(got["days_chapter"], rel=1e-12), (
        "the two forms differ with no sleep current, so something other than "
        "the sleep energy is moving them apart")
    print("\n  control: with no sleep current assumed the two forms agree at "
          "{:.1f} days, even with {:.0f} seconds of the day uncovered"
          .format(got["days"], got["uncovered_s"]))


# ---------------------------------------------------------------------------
# The Rust, driven as a filter over the same oracle. One process for the whole
# run, which is conftest's rule.
#
# Three verbs, one per function:
#
#     C <fs_hz> <cycle_bit> <max_out> <n> <current digital>*n
#     R <volts> <min_cycles> <count> <count charges>
#     L <energy_uj> <cycles_per_day> <period_s> <sleep_ua> <sleep_v> <mah> <cell_v>
#
# EVERY FLOATING POINT VALUE CROSSES THE PROTOCOL EXACTLY, which matters here
# more than in any other filter in this volume: this file compares f64 with ==
# and no tolerance, so a value losing one bit on the wire would fail a
# comparison the arithmetic passed. Python writes with repr and reads with
# float, Rust parses with str::parse and prints with {:?}, and both of those
# pairs are shortest-round-trip guarantees rather than conveniences.

NEWLINE = chr(10)

LIFETIME_FIELDS = ("energy_per_cycle_uj", "cycles_per_day", "cycle_period_s",
                   "sleep_ua", "sleep_volts", "cell_mah", "cell_volts")

# The answer names each field without the unit suffix the structs carry, so one
# map keeps the two spellings from drifting apart.
REPORT_FROM_ANSWER = {"cycles": "cycles", "median": "median_uc",
                      "min": "min_uc", "max": "max_uc",
                      "spread": "spread_pct", "energy": "energy_median_uj"}
LIFETIME_FROM_ANSWER = {"cell_j": "cell_j", "covered": "covered_s",
                        "uncovered": "uncovered_s",
                        "cycle_j": "cycle_j_per_day",
                        "uncovered_j": "uncovered_j_per_day",
                        "days": "days", "days_chapter": "days_chapter",
                        "covers": "covers_the_day"}


def charge_request(current, digital, fs_hz, cycle_bit):
    body = " ".join("{!r} {}".format(c, d) for c, d in zip(current, digital))
    return "C {!r} {} {} {} {}".format(fs_hz, cycle_bit, CYC_MAX_CYCLES,
                                       len(digital), body).strip()


def report_request(charges, volts, min_cycles):
    body = " ".join(repr(float(c)) for c in charges)
    return "R {!r} {} {} {}".format(volts, min_cycles, len(charges),
                                    body).strip()


def lifetime_request(spec):
    return "L " + " ".join(repr(float(spec[k])) for k in LIFETIME_FIELDS)


def fields_of(answer):
    out = {}
    for token in answer.split():
        key, value = token.split("=", 1)
        out[key] = value
    return out


def all_requests():
    """Every request line, built where a test with no cargo can reach them.

    Separated from the test below because that test SKIPS on win11 aquamarine,
    where there is no cargo, and a skipped body is never executed at all. On
    Thursday 29 October 2026 P04's version of this file referred to a module
    level name that file does not define, and the skip hid it completely until
    the laptop with a compiler ran it. Anything checkable without the artefact
    is checked without it.
    """
    lines = []
    for case in ORACLE["traces"]:
        current, digital = build(case["spec"])
        lines.append(charge_request(current, digital, case["fs_hz"],
                                    case["cycle_bit"]))
    for case in ORACLE["trace_refusals"]:
        lines.append(charge_request([0.0] * case["n"], [0] * case["n"],
                                    case["fs_hz"], case["cycle_bit"]))
    for case in ORACLE["reports"] + ORACLE["report_refusals"]:
        lines.append(report_request(case["charges_uc"], case["volts"],
                                    case["min_cycles"]))
    for case in ORACLE["lifetimes"] + ORACLE["lifetime_refusals"]:
        lines.append(lifetime_request(case["spec"]))
    return lines


def test_the_requests_round_trip_every_float_without_running_anything():
    """Runs everywhere, including where no filter exists, on purpose.

    It also checks the one property the protocol must have and the arithmetic
    cannot supply: every float written into a request reads back as the same
    value. A tolerance anywhere in this file would hide that, and there is
    none.
    """
    lines = all_requests()
    expected = (len(ORACLE["traces"]) + len(ORACLE["trace_refusals"])
                + len(ORACLE["reports"]) + len(ORACLE["report_refusals"])
                + len(ORACLE["lifetimes"]) + len(ORACLE["lifetime_refusals"]))
    assert len(lines) == expected

    floats = 0
    for line in lines:
        parts = line.split()
        assert parts[0] in ("C", "R", "L"), line[:40]
        for token in parts[1:]:
            if "." in token or "e" in token or "E" in token:
                assert float(repr(float(token))) == float(token), token
                floats += 1
    assert floats > 100, (
        "only {} floats appear in the requests, which is fewer than this "
        "oracle holds, so the builder has stopped emitting them".format(floats))
    print(NEWLINE + "  {} requests built with no filter and no compiler, and "
          "{} floating point tokens each read back as the value written"
          .format(len(lines), floats))


# The C++ and the Rust speak one protocol, so the test below runs over both and
# the language is in the test id, which is what names the one that disagreed.
FILTERS = [(CYC_CPP_FILTER, "cpp", BUILD_HOST_CMD),
           (CYC_RUST_FILTER, "rust", "cargo build --release --workspace")]
FILTER_ARGS = [(path, command) for path, _, command in FILTERS]
FILTER_IDS = [name for _, name, _ in FILTERS]


@pytest.mark.parametrize("path,command", FILTER_ARGS, ids=FILTER_IDS)
def test_each_filter_answers_every_case_as_the_oracle_says(path, command):
    """One process, every case of all six kinds, in the oracle's order."""
    require_artefact(path, command)
    lines = []
    for case in ORACLE["traces"]:
        current, digital = build(case["spec"])
        lines.append(charge_request(current, digital, case["fs_hz"],
                                    case["cycle_bit"]))
    for case in ORACLE["trace_refusals"]:
        lines.append(charge_request([0.0] * case["n"], [0] * case["n"],
                                    case["fs_hz"], case["cycle_bit"]))
    for case in ORACLE["reports"] + ORACLE["report_refusals"]:
        lines.append(report_request(case["charges_uc"], case["volts"],
                                    case["min_cycles"]))
    for case in ORACLE["lifetimes"] + ORACLE["lifetime_refusals"]:
        lines.append(lifetime_request(case["spec"]))

    answers = run_filter(path, lines)
    assert len(answers) == len(lines)

    at = 0
    for case in ORACLE["traces"]:
        got = fields_of(answers[at])
        assert int(got["count"]) == case["count"], case["why"]
        charges = [float(v) for v in got["uc"].split(",")] if got["uc"] else []
        assert charges == case["charges_uc"], case["why"]
        at += 1
    for case in ORACLE["trace_refusals"]:
        assert answers[at] == "REFUSED {}".format(case["error"]), case["why"]
        at += 1
    for case in ORACLE["reports"]:
        got = fields_of(answers[at])
        for name, field in REPORT_FROM_ANSWER.items():
            want = case[field]
            value = int(got[name]) if field == "cycles" else float(got[name])
            assert value == want, (field, case["why"])
        at += 1
    for case in ORACLE["report_refusals"]:
        assert answers[at] == "REFUSED {}".format(case["error"]), case["why"]
        at += 1
    for case in ORACLE["lifetimes"]:
        got = fields_of(answers[at])
        for name, field in LIFETIME_FROM_ANSWER.items():
            want = case[field]
            if field == "covers_the_day":
                value = int(got[name])
            else:
                value = float(got[name])
            assert value == want, (field, case["why"])
        at += 1
    for case in ORACLE["lifetime_refusals"]:
        assert answers[at] == "REFUSED {}".format(case["error"]), case["why"]
        at += 1
    assert at == len(lines)

    refusals = (len(ORACLE["trace_refusals"]) + len(ORACLE["report_refusals"])
                + len(ORACLE["lifetime_refusals"]))
    print(NEWLINE + "  the filter answered all {} requests exactly, {} traces, "
          "{} reports, {} lifetimes and {} refusals, every float compared "
          "with no tolerance"
          .format(len(lines), len(ORACLE["traces"]), len(ORACLE["reports"]),
                  len(ORACLE["lifetimes"]), refusals))
