"""P17's timebase arithmetic, in C and Python, against one oracle.

**Which criteria this serves.** Chapter 17's criterion 1 compares four
implementations of one bus read and needs the peripheral and the shield. Three of
its other criteria are arithmetic on counter values and are here: the
instrument's own cost "measured and subtracted, and the subtraction visible in
the source rather than folded into a constant", "the first run and the median of
the remainder reported separately", and "the timebase substitution demonstrated
on all three peripherals, with the wrap period of each stated".

**Everything is compared exactly.** Counter values and tick counts are integers,
and the wrap period is reported as an integer of nanoseconds so that two
implementations can compare it without a tolerance.

**The chapter's three wrap periods are correct**, which is worth asserting
because most of what gets checked in this volume is not. Step 8 states about
15.3 seconds, about 71.6 minutes and about 60 milliseconds; recomputed they are
15.339 s, 71.583 min and 59.919 ms, and the test derives them rather than
quoting them.

**Two findings, both run against the chapter's printed code.**

* Step 8's timebase struct carries a start function, a tick reader, a frequency
  and a width in bits, and the comment beside its third instance says "counts
  down". There is no field for the direction, so an elapsed interval computed
  through that abstraction is wrong for one of the three peripherals it exists
  to make interchangeable: a hundred ticks of a 24-bit down counter reads as
  16777116, which is 59.918 ms rather than 357 ns.
* Step 6 subtracts the instrument's calibrated cost with `(b - a) -
  cyc_overhead` on a `uint32_t`. An interval below that cost wraps to a number
  near 2 to the 32nd, and at the core clock that is reported as 15.339 seconds,
  which is exactly one wrap of the cycle counter and therefore looks like a
  plausible duration rather than like an error.
"""
from __future__ import annotations

import ctypes
import json
import sys
from pathlib import Path

import pytest

from conftest import BUILD, require_artefact, shared_library_name

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "projects" / "P17-hal-against-registers"
sys.path.insert(0, str(PROJECT / "python"))

from tbmath import (ERRORS, TB_COUNTS_DOWN, TB_COUNTS_UP,  # noqa: E402
                    chapter_measure_run, chapter_timebase_elapsed,
                    elapsed as py_elapsed, report as py_report,
                    subtract_overhead as py_subtract, wrap_ns as py_wrap)

ORACLE = json.loads((PROJECT / "vectors.json").read_text(encoding="utf-8"))
assert ORACLE["schema"] == 1, "the oracle's schema moved"
CORE_HZ = ORACLE["core_hz"]


class CReport(ctypes.Structure):
    """Must match tb_report_t in c/tbmath.h, field for field."""

    _fields_ = [("first", ctypes.c_uint32), ("median", ctypes.c_uint32),
                ("smallest", ctypes.c_uint32), ("largest", ctypes.c_uint32),
                ("count", ctypes.c_uint32)]


_LIB = None


def c_lib():
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("tbmath")
        require_artefact(path)
        d = ctypes.CDLL(str(path))
        u32p = ctypes.POINTER(ctypes.c_uint32)
        d.tb_elapsed.restype = ctypes.c_int
        d.tb_elapsed.argtypes = [ctypes.c_uint32, ctypes.c_uint32,
                                 ctypes.c_uint32, ctypes.c_int, u32p]
        d.tb_wrap_ns.restype = ctypes.c_int
        d.tb_wrap_ns.argtypes = [ctypes.c_double, ctypes.c_uint32,
                                 ctypes.POINTER(ctypes.c_uint64)]
        d.tb_subtract_overhead.restype = ctypes.c_int
        d.tb_subtract_overhead.argtypes = [ctypes.c_uint32, ctypes.c_uint32,
                                           u32p]
        d.tb_report.restype = ctypes.c_int
        d.tb_report.argtypes = [u32p, ctypes.c_uint32,
                                ctypes.POINTER(CReport)]
        _LIB = d
    return _LIB


def c_elapsed(start, end, width, down):
    out = ctypes.c_uint32(0xDEADBEEF)
    rc = c_lib().tb_elapsed(start, end, width, down, ctypes.byref(out))
    return rc, (out.value if rc == 0 else None)


def c_wrap(hz, width):
    out = ctypes.c_uint64(0)
    rc = c_lib().tb_wrap_ns(hz, width, ctypes.byref(out))
    return rc, (out.value if rc == 0 else None)


def c_subtract(raw, overhead):
    out = ctypes.c_uint32(0xDEADBEEF)
    rc = c_lib().tb_subtract_overhead(raw, overhead, ctypes.byref(out))
    return rc, (out.value if rc == 0 else None)


def c_report(samples):
    n = len(samples)
    arr = (ctypes.c_uint32 * max(n, 1))(*samples) if samples \
        else (ctypes.c_uint32 * 1)()
    out = CReport()
    rc = c_lib().tb_report(arr, n, ctypes.byref(out))
    return rc, out


@pytest.mark.parametrize("case", ORACLE["elapsed"], ids=lambda c: c["why"][:46])
def test_both_implementations_measure_the_same_interval(case):
    args = (case["start"], case["end"], case["width_bits"],
            case["counts_down"])
    rc, got = py_elapsed(*args)
    assert rc == 0 and got == case["ticks"], case["why"]
    rc, got = c_elapsed(*args)
    assert rc == 0 and got == case["ticks"], case["why"]


@pytest.mark.parametrize("case", ORACLE["elapsed_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_readings(case):
    args = (case["start"], case["end"], case["width_bits"],
            case["counts_down"])
    rc, got = py_elapsed(*args)
    assert rc == ERRORS[case["error"]] and got is None, case["why"]
    rc, got = c_elapsed(*args)
    assert rc == ERRORS[case["error"]] and got is None, case["why"]


@pytest.mark.parametrize("case", ORACLE["wraps"], ids=lambda c: c["why"][:46])
def test_both_implementations_agree_on_the_wrap_period(case):
    rc, got = py_wrap(case["hz"], case["width_bits"])
    assert rc == 0 and got == case["ns"], case["why"]
    rc, got = c_wrap(case["hz"], case["width_bits"])
    assert rc == 0 and got == case["ns"], case["why"]


@pytest.mark.parametrize("case", ORACLE["wrap_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_wrap_questions(case):
    rc, got = py_wrap(case["hz"], case["width_bits"])
    assert rc == ERRORS[case["error"]] and got is None, case["why"]
    rc, got = c_wrap(case["hz"], case["width_bits"])
    assert rc == ERRORS[case["error"]] and got is None, case["why"]


@pytest.mark.parametrize("case", ORACLE["subtractions"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_subtract_the_instrument_alike(case):
    rc, got = py_subtract(case["raw"], case["overhead"])
    assert rc == 0 and got == case["ticks"], case["why"]
    rc, got = c_subtract(case["raw"], case["overhead"])
    assert rc == 0 and got == case["ticks"], case["why"]


@pytest.mark.parametrize("case", ORACLE["subtraction_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_an_interval_below_the_instrument(case):
    rc, got = py_subtract(case["raw"], case["overhead"])
    assert rc == ERRORS[case["error"]] and got is None, case["why"]
    rc, got = c_subtract(case["raw"], case["overhead"])
    assert rc == ERRORS[case["error"]] and got is None, case["why"]


@pytest.mark.parametrize("case", ORACLE["reports"], ids=lambda c: c["why"][:46])
def test_both_implementations_keep_the_first_run_apart(case):
    rc, got = py_report(case["samples"])
    assert rc == 0, case["why"]
    for key in ("first", "median", "smallest", "largest", "count"):
        assert got[key] == case[key], (key, case["why"])
    rc, out = c_report(case["samples"])
    assert rc == 0, case["why"]
    for key in ("first", "median", "smallest", "largest", "count"):
        assert getattr(out, key) == case[key], (key, case["why"])


@pytest.mark.parametrize("case", ORACLE["report_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_reports(case):
    rc, got = py_report(case["samples"])
    assert rc == ERRORS[case["error"]] and got is None, case["why"]
    rc, out = c_report(case["samples"])
    assert rc == ERRORS[case["error"]], case["why"]


# ---------------------------------------------------------------------------
# The findings, and the one confirmation. Through the twin, so they run on the
# authoring laptop too.

def test_the_chapters_three_wrap_periods_are_right():
    """A confirmation rather than a finding, and worth asserting as one.

    Step 8 states three wrap periods in round words. Recomputed from the width
    and the frequency they are 15.339 s, 71.583 min and 59.919 ms, which is what
    "about 15.3 seconds", "about 71.6 minutes" and "about 60 milliseconds" mean.
    """
    seconds = {}
    for case in ORACLE["wraps"]:
        rc, ns = py_wrap(case["hz"], case["width_bits"])
        assert rc == 0
        assert ns == case["ns"]
        seconds[case["why"][:9]] = ns / 1.0e9

    cyc = next(v for k, v in seconds.items() if k.startswith("the cycle"))
    timer = next(v for k, v in seconds.items() if k.startswith("a general"))
    systick = next(v for k, v in seconds.items() if k.startswith("SysTick"))
    assert round(cyc, 1) == 15.3, cyc
    assert round(timer / 60.0, 1) == 71.6, timer
    assert round(systick * 1000.0, 0) == 60.0, systick
    print("\n  step 8's wrap periods, recomputed: {:.3f} s, {:.3f} min and "
          "{:.3f} ms, against about 15.3 seconds, about 71.6 minutes and about "
          "60 milliseconds".format(cyc, timer / 60.0, systick * 1000.0))


def test_the_timebase_struct_has_no_field_for_the_direction():
    """Finding one, on one pair of readings of a 24-bit down counter.

    Step 8's struct carries a width and a frequency and no direction, and its
    own comment says the third peripheral counts down. The only subtraction
    available through that abstraction turns a hundred ticks into nearly a whole
    wrap.
    """
    case = next(c for c in ORACLE["elapsed"] if "a hundred ticks" in c["why"])
    rc, known = py_elapsed(case["start"], case["end"], case["width_bits"],
                           TB_COUNTS_DOWN)
    assert rc == 0 and known == 100

    through_struct = chapter_timebase_elapsed(case["start"], case["end"],
                                              case["width_bits"])
    assert through_struct != known
    assert through_struct == 16777116, through_struct
    ns_known = known / CORE_HZ * 1e9
    ms_struct = through_struct / CORE_HZ * 1e3
    assert ms_struct > 50.0 and ns_known < 1000.0
    print("\n  one pair of readings of a 24-bit down counter: {} ticks, which "
          "is {:.1f} ns, read through step 8's struct as {} ticks, which is "
          "{:.3f} ms".format(known, ns_known, through_struct, ms_struct))


def test_the_instrument_subtraction_reports_one_whole_wrap_instead_of_refusing():
    """Finding two, and the coincidence that makes it worse.

    An interval below the calibrated instrument cost wraps to a number near 2 to
    the 32nd. At the core clock that is 15.339 seconds, which is exactly one
    wrap of the cycle counter, so the bogus figure is the same size as a real
    duration this timebase can produce and does not look like an error at all.
    """
    case = ORACLE["subtraction_refusals"][0]
    rc, got = py_subtract(case["raw"], case["overhead"])
    assert rc == ERRORS["underflow"] and got is None

    printed = chapter_measure_run(0, case["raw"], case["overhead"])
    assert printed > 4_000_000_000, printed
    seconds = printed / CORE_HZ

    rc, wrap = py_wrap(CORE_HZ, 32)
    assert rc == 0
    # The two agree to better than a part in a million, which is the point: the
    # wrong answer is indistinguishable in size from one whole wrap.
    assert abs(seconds - wrap / 1e9) / (wrap / 1e9) < 1e-6
    print("\n  an interval of {} ticks against a calibrated cost of {}: step 6 "
          "as printed reports {} ticks, which is {:.3f} s, and one whole wrap "
          "of the same counter is {:.3f} s"
          .format(case["raw"], case["overhead"], printed, seconds,
                  wrap / 1e9))


def test_a_first_run_is_not_assumed_to_be_the_slowest():
    """The control for the report, which is why the two are kept apart.

    Step 6 says to report the first run and the median of the remainder
    separately and calls the difference a cache effect. It does not say the
    first is larger, and this case has a first run faster than every warm one,
    so nothing here orders them.
    """
    case = next(c for c in ORACLE["reports"] if "slower than the cold" in c["why"])
    rc, got = py_report(case["samples"])
    assert rc == 0
    assert got["first"] < got["median"], (
        "this case no longer has a first run faster than the median, so it no "
        "longer demonstrates that nothing orders them")
    print("\n  a first run of {} against a median of {} over {} warm runs: the "
          "two are reported apart and neither is assumed larger"
          .format(got["first"], got["median"], got["count"]))
