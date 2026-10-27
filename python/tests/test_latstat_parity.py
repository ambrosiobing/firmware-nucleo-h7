"""P20's latency histogram, percentiles and budget verdict, in C and Python.

**This file is what decides chapter 20 without a kernel.** The chapter's third
and fourth acceptance criteria are that a million release events are recorded
and reported as a median, a 99.9th percentile and a maximum with the overflow
count shown, and that all three meet table 20.4's budgets. The figures come
from the board. The arithmetic that turns them into a verdict does not, and it
is here.

**Everything is compared exactly.** All of it is integer, so there is no
tolerance anywhere in this file.

**Runs are stored as cycle counts with a repeat**, so a thousand releases are
two numbers rather than a thousand, and the two largest histograms are stored
as bins directly, because four thousand million samples cannot be taken one at
a time in a test.

**The findings are about the chapter's wording and not about either
implementation.** A percentile read from a histogram is a bin one microsecond
wide, table 20.4's rows are whole microseconds, and the division that produced
the samples truncates, so a bin sitting on its budget decides nothing and the
error at the boundary is always a pass. The two readings the chapter's wording
invites are carried unused in the twin and a test runs both.
"""
from __future__ import annotations

import ctypes
import json
import sys
from pathlib import Path

import pytest

from conftest import BUILD, require_artefact, shared_library_name

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "projects" / "P20-rtos-task-set"
sys.path.insert(0, str(PROJECT / "python"))

from latstat import (ERRORS, LAT_BINS, LAT_FAIL,  # noqa: E402
                     LAT_PASS, LAT_UNDECIDED, UINT32_MAX,
                     chapter_two_way_verdict, chapter_used_from_kernel,
                     new_hist, record as py_record, report as py_report,
                     stack as py_stack, verdict as py_verdict)

ORACLE = json.loads((PROJECT / "vectors.json").read_text(encoding="utf-8"))
assert ORACLE["schema"] == 1, "the oracle's schema moved"
assert ORACLE["bins"] == LAT_BINS, "the bin count moved"
BUDGET = ORACLE["budget"]
CPU = ORACLE["cycles_per_us"]


class CHist(ctypes.Structure):
    """Must match lat_hist_t in c/latstat.h, field for field."""

    _fields_ = [("bins", ctypes.c_uint32 * LAT_BINS),
                ("over", ctypes.c_uint32),
                ("max_us", ctypes.c_uint32),
                ("count", ctypes.c_uint32)]


class CReport(ctypes.Structure):
    """Must match lat_report_t."""

    _fields_ = [("median_us", ctypes.c_uint32),
                ("p999_us", ctypes.c_uint32),
                ("median_resolved", ctypes.c_uint8),
                ("p999_resolved", ctypes.c_uint8),
                ("max_us", ctypes.c_uint32),
                ("over", ctypes.c_uint32),
                ("count", ctypes.c_uint32)]


class CBudget(ctypes.Structure):
    """Must match lat_budget_t."""

    _fields_ = [("median_us", ctypes.c_uint32),
                ("p999_us", ctypes.c_uint32),
                ("max_us", ctypes.c_uint32)]


class CVerdict(ctypes.Structure):
    """Must match lat_verdict_t."""

    _fields_ = [("median", ctypes.c_uint8), ("p999", ctypes.c_uint8),
                ("max", ctypes.c_uint8)]


class CStack(ctypes.Structure):
    """Must match lat_stack_t."""

    _fields_ = [("used", ctypes.c_uint32), ("permil", ctypes.c_uint32),
                ("within", ctypes.c_uint8)]


_LIB = None


def c_lib():
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("latstat")
        require_artefact(path)
        d = ctypes.CDLL(str(path))
        d.lat_record.restype = ctypes.c_int
        d.lat_record.argtypes = [ctypes.POINTER(CHist), ctypes.c_uint32,
                                 ctypes.c_uint32]
        d.lat_report.restype = ctypes.c_int
        d.lat_report.argtypes = [ctypes.POINTER(CHist),
                                 ctypes.POINTER(CReport)]
        d.lat_verdict.restype = ctypes.c_int
        d.lat_verdict.argtypes = [ctypes.POINTER(CReport),
                                  ctypes.POINTER(CBudget),
                                  ctypes.POINTER(CVerdict)]
        d.lat_stack.restype = ctypes.c_int
        d.lat_stack.argtypes = [ctypes.c_uint32, ctypes.c_uint32,
                                ctypes.POINTER(CStack)]
        _LIB = d
    return _LIB


def c_hist_from(spec):
    """A histogram built field by field, which is how the oracle stores the two
    that cannot be recorded one sample at a time."""
    h = CHist()
    for key, value in spec["bins"].items():
        h.bins[int(key)] = value
    h.over = spec["over"]
    h.max_us = spec["max_us"]
    h.count = spec["count"]
    return h


def py_hist_from(spec):
    h = new_hist()
    for key, value in spec["bins"].items():
        h["bins"][int(key)] = value
    h["over"] = spec["over"]
    h["max_us"] = spec["max_us"]
    h["count"] = spec["count"]
    return h


def c_record_run(case):
    h = CHist()
    lib = c_lib()
    for cycles, repeat in case["samples"]:
        for _ in range(repeat):
            rc = lib.lat_record(ctypes.byref(h), cycles,
                                case["cycles_per_us"])
            assert rc == 0, case["why"]
    return h


def py_record_run(case):
    h = new_hist()
    for cycles, repeat in case["samples"]:
        for _ in range(repeat):
            rc = py_record(h, cycles, case["cycles_per_us"])
            assert rc == 0, case["why"]
    return h


REPORT_FIELDS = ("median_us", "p999_us", "median_resolved", "p999_resolved",
                 "max_us", "over", "count")
VERDICT_FIELDS = ("median", "p999", "max")


def check_c_report_and_verdict(hist, case):
    lib = c_lib()
    rep = CReport()
    assert lib.lat_report(ctypes.byref(hist), ctypes.byref(rep)) == 0, \
        case["why"]
    for key in REPORT_FIELDS:
        assert getattr(rep, key) == case["report"][key], (key, case["why"])

    budget = CBudget(BUDGET["median_us"], BUDGET["p999_us"],
                     BUDGET["max_us"])
    ver = CVerdict()
    assert lib.lat_verdict(ctypes.byref(rep), ctypes.byref(budget),
                           ctypes.byref(ver)) == 0, case["why"]
    for key in VERDICT_FIELDS:
        assert getattr(ver, key) == case["verdict"][key], (key, case["why"])


def check_py_report_and_verdict(hist, case):
    rc, rep = py_report(hist)
    assert rc == 0, case["why"]
    for key in REPORT_FIELDS:
        assert rep[key] == case["report"][key], (key, case["why"])
    rc, ver = py_verdict(rep, BUDGET)
    assert rc == 0, case["why"]
    for key in VERDICT_FIELDS:
        assert ver[key] == case["verdict"][key], (key, case["why"])


@pytest.mark.parametrize("case", ORACLE["runs"], ids=lambda c: c["why"][:46])
def test_the_python_records_and_judges_every_run_as_the_oracle_says(case):
    check_py_report_and_verdict(py_record_run(case), case)


@pytest.mark.parametrize("case", ORACLE["runs"], ids=lambda c: c["why"][:46])
def test_the_c_records_and_judges_every_run_as_the_oracle_says(case):
    check_c_report_and_verdict(c_record_run(case), case)


@pytest.mark.parametrize("case", ORACLE["built"], ids=lambda c: c["why"][:46])
def test_the_python_judges_every_built_histogram_as_the_oracle_says(case):
    check_py_report_and_verdict(py_hist_from(case["spec"]), case)


@pytest.mark.parametrize("case", ORACLE["built"], ids=lambda c: c["why"][:46])
def test_the_c_judges_every_built_histogram_as_the_oracle_says(case):
    check_c_report_and_verdict(c_hist_from(case["spec"]), case)


@pytest.mark.parametrize("case", ORACLE["record_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_samples(case):
    h = new_hist()
    h["count"] = case["count_before"]
    rc = py_record(h, case["cycles"], case["cycles_per_us"])
    assert rc == ERRORS[case["error"]], case["why"]

    ch = CHist()
    ch.count = case["count_before"]
    rc = c_lib().lat_record(ctypes.byref(ch), case["cycles"],
                            case["cycles_per_us"])
    assert rc == ERRORS[case["error"]], case["why"]


@pytest.mark.parametrize("case", ORACLE["report_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_histograms(case):
    rc, rep = py_report(py_hist_from(case["spec"]))
    assert rc == ERRORS[case["error"]] and rep is None, case["why"]

    out = CReport()
    hist = c_hist_from(case["spec"])
    rc = c_lib().lat_report(ctypes.byref(hist), ctypes.byref(out))
    assert rc == ERRORS[case["error"]], case["why"]


@pytest.mark.parametrize("case", ORACLE["stacks"], ids=lambda c: c["why"][:46])
def test_both_implementations_compute_the_same_stack_figures(case):
    rc, got = py_stack(case["budget"], case["free"])
    assert rc == 0, case["why"]
    assert got == {"used": case["used"], "permil": case["permil"],
                   "within": case["within"]}, case["why"]

    out = CStack()
    rc = c_lib().lat_stack(case["budget"], case["free"], ctypes.byref(out))
    assert rc == 0, case["why"]
    assert (out.used, out.permil, out.within) == \
        (case["used"], case["permil"], case["within"]), case["why"]


@pytest.mark.parametrize("case", ORACLE["stack_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_stack_figures(case):
    rc, got = py_stack(case["budget"], case["free"])
    assert rc == ERRORS[case["error"]] and got is None, case["why"]

    out = CStack()
    rc = c_lib().lat_stack(case["budget"], case["free"], ctypes.byref(out))
    assert rc == ERRORS[case["error"]], case["why"]


# ---------------------------------------------------------------------------
# The findings. Through the twin, so they run on the authoring laptop too.

def test_a_percentile_from_a_histogram_is_a_bin_and_not_a_value():
    """Finding one, against the reading criterion 4's wording invites.

    Bin 100 holds every sample whose truncated microsecond value is 100, so the
    true values in it run from 100 up to just under 101. Table 20.4's median
    budget is 100. A two-way comparison answers pass; this project answers
    undecided, because part of that bin does not meet the budget.
    """
    case = next(c for c in ORACLE["runs"] if "CANNOT BE DECIDED" in c["why"])
    rc, rep = py_report(py_record_run(case))
    assert rc == 0
    rc, ver = py_verdict(rep, BUDGET)
    assert rc == 0

    pinned = ver["median"]
    permitted = chapter_two_way_verdict(rep["median_us"],
                                        BUDGET["median_us"])
    assert pinned == LAT_UNDECIDED
    assert permitted == LAT_PASS
    assert pinned != permitted, (
        "the two readings now agree on this run, so it no longer demonstrates "
        "the finding and a different run is needed")

    # The control: one microsecond lower and the same two readings agree, so
    # the disagreement is about the boundary and not about the comparison.
    below = dict(rep)
    below["median_us"] = BUDGET["median_us"] - 1
    rc, ver_below = py_verdict(below, BUDGET)
    assert rc == 0 and ver_below["median"] == LAT_PASS
    assert chapter_two_way_verdict(below["median_us"],
                                   BUDGET["median_us"]) == LAT_PASS
    print("\n  a median in bin {} against a budget of {}: the pinned verdict "
          "is undecided and the two-way reading says pass; one microsecond "
          "lower and both say pass"
          .format(rep["median_us"], BUDGET["median_us"]))


def test_the_truncation_makes_two_different_runs_report_the_same_figures():
    """Finding two, and it is why undecided is not a missing feature.

    One run releases at exactly 100 microseconds and the other at 100.9, which
    is 28252 cycles at 280 per microsecond. The division truncates, so both
    report a median of 100 and are indistinguishable afterwards. Every sample
    in the second run exceeds the 100 microsecond budget, and a two-way
    comparison passes it.
    """
    on_budget = next(c for c in ORACLE["runs"]
                     if "CANNOT BE DECIDED" in c["why"])
    above = next(c for c in ORACLE["runs"]
                 if "INDISTINGUISHABLE" in c["why"])
    rc, a = py_report(py_record_run(on_budget))
    assert rc == 0
    rc, b = py_report(py_record_run(above))
    assert rc == 0
    assert a == b, "the two runs no longer report the same figures"

    cycles = above["samples"][0][0]
    assert cycles // CPU == 100 and cycles % CPU == 252
    # 252 of 280 is nine tenths, stated as integers rather than as a decimal.
    assert 252 * 10 == CPU * 9
    assert chapter_two_way_verdict(b["median_us"],
                                   BUDGET["median_us"]) == LAT_PASS
    print("\n  {} cycles at {} per microsecond is 100 and nine tenths, "
          "recorded as {}, so a run whose every sample exceeds the budget "
          "reports the same median as one that sits on it"
          .format(cycles, CPU, b["median_us"]))


def test_an_unresolved_percentile_is_not_the_top_bin():
    """Finding three, against the run whose rank falls inside the overflow.

    The chapter keeps the overflow count so the tail can be trusted and then
    reads its percentiles from the histogram. Two releases of 2000 microseconds
    in a thousand put rank 999 past every bin, so there is no bin to report.
    The top bin, 1023, would be a wrong answer nothing downstream could detect,
    and it would also pass nothing: the verdict is a failure either way, which
    is why the flag matters rather than the number.
    """
    case = next(c for c in ORACLE["runs"] if "UNRESOLVED" in c["why"])
    rc, rep = py_report(py_record_run(case))
    assert rc == 0
    assert rep["p999_resolved"] == 0 and rep["over"] == 2
    assert rep["median_resolved"] == 1, "the median is still answerable"
    rc, ver = py_verdict(rep, BUDGET)
    assert rc == 0 and ver["p999"] == LAT_FAIL

    # And the control: one fewer overflow sample and the rank lands in a bin,
    # so the flag is about the rank and not about the overflow being non-zero.
    resolved = next(c for c in ORACLE["runs"]
                    if "still answerable" in c["why"])
    rc, other = py_report(py_record_run(resolved))
    assert rc == 0 and other["over"] == 1 and other["p999_resolved"] == 1
    print("\n  with 2 of 1000 samples over the top bin the 99.9th percentile's "
          "rank of 999 is unresolved; with 1 of 1000 it resolves to bin {}, "
          "and in both runs the maximum row fails"
          .format(other["p999_us"]))


def test_the_rank_product_needs_more_than_32_bits():
    """Finding four, against a count the 32-bit product cannot take.

    The 99.9th percentile's rank is the ceiling of 999 times the count over
    1000. The chapter's million events make 999 million, which fits in 32 bits.
    This counter holds four thousand million, where the product is 3996000000000
    and a 32-bit multiply wraps to 1680414720, giving a rank that lands in the
    first populated bin instead of the right one.
    """
    case = next(c for c in ORACLE["built"] if "32-BIT PRODUCT" in c["why"])
    count = case["spec"]["count"]
    assert count == 4000000000

    wrapped = (999 * count) % (2 ** 32)
    assert wrapped == 1680414720
    # The wrapped rank falls inside the first bin's own share, so a 32-bit
    # implementation would report that bin and pass the budget row that the
    # correct rank fails.
    assert wrapped < case["spec"]["bins"]["50"]
    assert case["report"]["p999_us"] == 900
    assert case["verdict"]["p999"] == LAT_FAIL
    assert chapter_two_way_verdict(50, BUDGET["p999_us"]) == LAT_PASS

    rc, rep = py_report(py_hist_from(case["spec"]))
    assert rc == 0 and rep["p999_us"] == 900
    print("\n  at a count of {} the 99.9th percentile's rank is {} and lands "
          "in bin {}; a 32-bit product wraps to {} and would report bin 50, "
          "which passes a budget the right answer fails"
          .format(count, -(-(999 * count) // 1000), rep["p999_us"], wrapped))


def test_the_kernel_reports_free_stack_and_the_budget_table_wants_used():
    """Finding five, against the reading criterion 6's wording invites.

    uxTaskGetStackHighWaterMark returns the smallest free space a task has ever
    had. Criterion 6 asks that no task has used more than eighty percent of its
    budget. On the oracle's boundary case the task has used 819 bytes of 1024,
    which is exactly eighty percent and allowed; reading the kernel's figure as
    the used figure reports 205 bytes, which is twenty percent, and the
    direction of that error hides the problem rather than inventing one.
    """
    case = next(c for c in ORACLE["stacks"] if "exactly eighty" in c["why"])
    rc, got = py_stack(case["budget"], case["free"])
    assert rc == 0
    assert got["used"] == 819 and got["permil"] == 799 and got["within"] == 1

    misread = chapter_used_from_kernel(case["free"])
    assert misread == 205
    assert misread * 1000 // case["budget"] == 200
    assert got["used"] > misread, (
        "the kernel's figure is no longer the smaller of the two on this case")

    # The rule is exact at the boundary: one byte more used and it refuses.
    worse = next(c for c in ORACLE["stacks"] if "one byte past" in c["why"])
    rc, got2 = py_stack(worse["budget"], worse["free"])
    assert rc == 0 and got2["used"] == 820 and got2["within"] == 0
    print("\n  819 of 1024 bytes used is exactly eighty percent and allowed, "
          "820 is refused, and reading the kernel's free figure as used "
          "reports {} bytes, which is 200 per thousand instead of {}"
          .format(misread, got["permil"]))


def test_the_counter_refuses_rather_than_wrapping():
    """A counter at its maximum, which is the one refusal the chapter's code has
    no room for.

    Step 5's `hist_n++` wraps silently at four thousand million and change, and
    every rank computed afterwards is a fraction of the wrapped number. The
    refusal here is before the sample, so the histogram and the count never
    disagree.
    """
    h = new_hist()
    h["count"] = UINT32_MAX
    assert py_record(h, 1000, CPU) == ERRORS["full"]
    assert h["count"] == UINT32_MAX, "the refused sample changed the histogram"
    assert sum(h["bins"]) == 0 and h["over"] == 0
    # One below the maximum is accepted, so the refusal is about the boundary
    # and not about the counter being large.
    h["count"] = UINT32_MAX - 1
    assert py_record(h, 1000, CPU) == 0
    assert h["count"] == UINT32_MAX
    print("\n  a counter at {} refuses the next sample and leaves the "
          "histogram untouched; one below it accepts".format(UINT32_MAX))
