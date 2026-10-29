"""P14's record decoder, in C and Python, against one oracle.

**What is exact and what is not, said once.** Header fields and counts are
integers and are compared exactly. The sensitivity table's entries are powers of
two, so a conversion that multiplies or divides by one of them is exact in binary
and is also compared exactly. Altitudes raise a ratio to a fractional power,
which is a library call in both languages and need not agree in the last bit
between gcc's and CPython's, so those are compared to a relative 1e-12 and
nothing tighter. That is nine orders of magnitude tighter than anything this
project claims about a pressure sensor.

**The sensitivity table is a stated table and not a datasheet claim.** Neither
sensor's datasheet is on this bench, and chapter 14's own argument for putting
scale codes in the record applies one level down: a sensitivity compiled into a
decoder is the constant step 9 exists to remove. So the table is an argument, an
undefined code is refused, and every finding below is about the RATIO between two
entries rather than about either value.

**The findings, each run against the chapter's own printed form.**

* Step 6 selects the narrow barometer scale for the quiet state, "where the
  finer sensitivity is what gives altitude any resolution", and the wide scale
  inside the event window. Altitude resolution is the pressure resolution times
  the slope of step 9's relation, so it degrades by exactly the ratio of the two
  sensitivities during the event the recorder exists to capture.
* Step 9's relation needs a reference pressure and the record of step 7 does not
  carry one, although it carries both acceleration scales and the barometer
  range for the stated reason that a record should be readable years later. A
  reference wrong by ten hectopascals, which is ordinary weather, is about
  84 metres of altitude that is not there.
* Step 10's printed figure omits the address and register overhead its own
  comment names. That is 0.26 per cent and changes no conclusion; it is here
  because the sentence and the sum disagree.
"""
from __future__ import annotations

import ctypes
import json
import sys
from pathlib import Path

import pytest

from conftest import (BUILD, REC_RUST_FILTER, require_artefact, run_filter,
                      shared_library_name)

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "projects" / "P14-iks5a1-dual-range"
sys.path.insert(0, str(PROJECT / "python"))

from recdecode import (ERRORS, REC_BARO_NARROW, REC_BARO_WIDE,  # noqa: E402
                       REC_FS_CODES, altitude_m as py_altitude,
                       altitude_resolution_m as py_resolution,
                       chapter_altitude, chapter_drain_ms,
                       counts_to_hpa as py_hpa, counts_to_mg as py_mg,
                       drain_ms as py_drain, parse_header as py_header)

ORACLE = json.loads((PROJECT / "vectors.json").read_text(encoding="utf-8"))
assert ORACLE["schema"] == 1, "the oracle's schema moved"
TABLE = ORACLE["table"]
P0 = ORACLE["p0_hpa"]
BARO_CODES = 2


class CHdr(ctypes.Structure):
    """Must match rec_hdr_t in c/recdecode.h, field for field."""

    _fields_ = [("tick_event", ctypes.c_uint32), ("n_pre", ctypes.c_uint16),
                ("n_post", ctypes.c_uint16), ("n_baro", ctypes.c_uint16),
                ("lo_fs_g", ctypes.c_uint8), ("hi_fs_g", ctypes.c_uint8),
                ("baro_range", ctypes.c_uint8), ("flags", ctypes.c_uint8)]


class CTable(ctypes.Structure):
    """Must match rec_table_t."""

    _fields_ = [("lo_mg_per_lsb", ctypes.c_double * REC_FS_CODES),
                ("hi_mg_per_lsb", ctypes.c_double * REC_FS_CODES),
                ("baro_lsb_per_hpa", ctypes.c_double * BARO_CODES),
                ("lo_valid", ctypes.c_uint8), ("hi_valid", ctypes.c_uint8),
                ("baro_valid", ctypes.c_uint8)]


_LIB = None


def c_lib():
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("recdecode")
        require_artefact(path)
        d = ctypes.CDLL(str(path))
        dbl = ctypes.POINTER(ctypes.c_double)
        d.rec_parse_header.restype = ctypes.c_int
        d.rec_parse_header.argtypes = [ctypes.POINTER(ctypes.c_uint8),
                                       ctypes.c_uint32, ctypes.POINTER(CHdr)]
        d.rec_counts_to_mg.restype = ctypes.c_int
        d.rec_counts_to_mg.argtypes = [ctypes.POINTER(CHdr),
                                       ctypes.POINTER(CTable), ctypes.c_int16,
                                       ctypes.c_int, dbl]
        d.rec_counts_to_hpa.restype = ctypes.c_int
        d.rec_counts_to_hpa.argtypes = [ctypes.POINTER(CHdr),
                                        ctypes.POINTER(CTable), ctypes.c_int32,
                                        dbl]
        d.rec_altitude_m.restype = ctypes.c_int
        d.rec_altitude_m.argtypes = [ctypes.c_double, ctypes.c_double, dbl]
        d.rec_altitude_resolution_m.restype = ctypes.c_int
        d.rec_altitude_resolution_m.argtypes = [ctypes.c_double,
                                                ctypes.c_double,
                                                ctypes.c_double, dbl]
        d.rec_drain_ms.restype = ctypes.c_int
        d.rec_drain_ms.argtypes = [ctypes.c_uint32, ctypes.c_uint32,
                                   ctypes.c_uint32, ctypes.c_double, dbl]
        _LIB = d
    return _LIB


def c_table():
    return CTable((ctypes.c_double * REC_FS_CODES)(*TABLE["lo_mg_per_lsb"]),
                  (ctypes.c_double * REC_FS_CODES)(*TABLE["hi_mg_per_lsb"]),
                  (ctypes.c_double * BARO_CODES)(*TABLE["baro_lsb_per_hpa"]),
                  TABLE["lo_valid"], TABLE["hi_valid"], TABLE["baro_valid"])


def c_header(buf):
    arr = (ctypes.c_uint8 * max(len(buf), 1))(*buf) if buf \
        else (ctypes.c_uint8 * 1)()
    out = CHdr()
    rc = c_lib().rec_parse_header(arr, len(buf), ctypes.byref(out))
    return rc, out


def c_double_call(fn, *args):
    out = ctypes.c_double(float("nan"))
    rc = fn(*args, ctypes.byref(out))
    return rc, out.value


@pytest.mark.parametrize("case", ORACLE["headers"], ids=lambda c: c["why"][:46])
def test_both_implementations_parse_or_refuse_every_header(case):
    rc_p, hdr_p = py_header(case["bytes"])
    rc_c, hdr_c = c_header(case["bytes"])
    if "error" in case:
        assert rc_p == ERRORS[case["error"]], case["why"]
        assert rc_c == ERRORS[case["error"]], case["why"]
        return
    assert (rc_p, rc_c) == (0, 0), case["why"]
    for key, value in case["hdr"].items():
        assert hdr_p[key] == value, (key, case["why"])
        assert getattr(hdr_c, key) == value, (key, case["why"])


@pytest.mark.parametrize("case", ORACLE["conversions"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_convert_counts_to_mg_exactly(case):
    rc_p, hdr_p = py_header(case["bytes"])
    rc_c, hdr_c = c_header(case["bytes"])
    assert (rc_p, rc_c) == (0, 0), case["why"]
    rc, mg = py_mg(hdr_p, TABLE, case["counts"], case["fine"])
    assert rc == 0 and mg == case["mg"], case["why"]
    table = c_table()
    rc, mg = c_double_call(c_lib().rec_counts_to_mg, ctypes.byref(hdr_c),
                           ctypes.byref(table), case["counts"], case["fine"])
    assert rc == 0 and mg == case["mg"], case["why"]


@pytest.mark.parametrize("case", ORACLE["pressures"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_convert_counts_to_hectopascals_exactly(case):
    rc_p, hdr_p = py_header(case["bytes"])
    rc_c, hdr_c = c_header(case["bytes"])
    assert (rc_p, rc_c) == (0, 0), case["why"]
    rc, hpa = py_hpa(hdr_p, TABLE, case["counts"])
    assert rc == 0 and hpa == case["hpa"], case["why"]
    table = c_table()
    rc, hpa = c_double_call(c_lib().rec_counts_to_hpa, ctypes.byref(hdr_c),
                            ctypes.byref(table), case["counts"])
    assert rc == 0 and hpa == case["hpa"], case["why"]


@pytest.mark.parametrize("case", ORACLE["conversion_refusals"],
                         ids=lambda c: c["why"][:46])
def test_a_code_the_table_does_not_define_is_refused_by_both(case):
    rc_p, hdr_p = py_header(case["bytes"])
    rc_c, hdr_c = c_header(case["bytes"])
    assert (rc_p, rc_c) == (0, 0), "the header itself should parse"
    rc, mg = py_mg(hdr_p, TABLE, 1, 1)
    assert rc == ERRORS[case["error"]] and mg is None, case["why"]
    table = c_table()
    rc, _ = c_double_call(c_lib().rec_counts_to_mg, ctypes.byref(hdr_c),
                          ctypes.byref(table), 1, 1)
    assert rc == ERRORS[case["error"]], case["why"]


@pytest.mark.parametrize("case", ORACLE["altitudes"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_agree_on_the_barometric_relation(case):
    rc, m = py_altitude(case["p_hpa"], case["p0_hpa"])
    assert rc == 0
    assert m == pytest.approx(case["m"], rel=1e-12), case["why"]
    rc, m = c_double_call(c_lib().rec_altitude_m, case["p_hpa"],
                          case["p0_hpa"])
    assert rc == 0
    assert m == pytest.approx(case["m"], rel=1e-12), case["why"]


@pytest.mark.parametrize("case", ORACLE["altitude_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_an_impossible_pressure(case):
    rc, m = py_altitude(case["p_hpa"], case["p0_hpa"])
    assert rc == ERRORS[case["error"]] and m is None, case["why"]
    rc, _ = c_double_call(c_lib().rec_altitude_m, case["p_hpa"],
                          case["p0_hpa"])
    assert rc == ERRORS[case["error"]], case["why"]


@pytest.mark.parametrize("case", ORACLE["resolutions"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_agree_on_the_altitude_resolution(case):
    rc, m = py_resolution(case["lsb_per_hpa"], case["p_hpa"], case["p0_hpa"])
    assert rc == 0
    assert m == pytest.approx(case["m"], rel=1e-12), case["why"]
    rc, m = c_double_call(c_lib().rec_altitude_resolution_m,
                          case["lsb_per_hpa"], case["p_hpa"], case["p0_hpa"])
    assert rc == 0
    assert m == pytest.approx(case["m"], rel=1e-12), case["why"]


@pytest.mark.parametrize("case", ORACLE["drains"], ids=lambda c: c["why"][:46])
def test_both_implementations_price_the_transaction_alike(case):
    rc, ms = py_drain(case["samples"], case["bytes_per_sample"],
                      case["overhead_bytes"], case["bus_hz"])
    assert rc == 0 and ms == case["ms"], case["why"]
    rc, ms = c_double_call(c_lib().rec_drain_ms, case["samples"],
                           case["bytes_per_sample"], case["overhead_bytes"],
                           case["bus_hz"])
    assert rc == 0 and ms == case["ms"], case["why"]


@pytest.mark.parametrize("case", ORACLE["drain_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_transactions(case):
    rc, ms = py_drain(case["samples"], case["bytes_per_sample"],
                      case["overhead_bytes"], case["bus_hz"])
    assert rc == ERRORS[case["error"]] and ms is None, case["why"]
    rc, _ = c_double_call(c_lib().rec_drain_ms, case["samples"],
                          case["bytes_per_sample"], case["overhead_bytes"],
                          case["bus_hz"])
    assert rc == ERRORS[case["error"]], case["why"]


# ---------------------------------------------------------------------------
# The findings. Driven through the twin so they run on the authoring laptop
# too, where the whole C half skips; the C agrees on every number above.

def test_the_event_window_records_altitude_at_the_worse_resolution():
    """Finding one, as the two numbers side by side.

    Step 6 picks the narrow scale for the quiet state because the finer
    sensitivity is what gives altitude any resolution, and the wide scale inside
    the event window. The resolution therefore degrades by the ratio of the two
    sensitivities exactly when the event happens.
    """
    narrow = TABLE["baro_lsb_per_hpa"][REC_BARO_NARROW]
    wide = TABLE["baro_lsb_per_hpa"][REC_BARO_WIDE]
    assert narrow > wide, (
        "the narrow range is not the finer one in this table, which would make "
        "this finding about a different pair of numbers")
    rc_n, res_n = py_resolution(narrow, P0, P0)
    rc_w, res_w = py_resolution(wide, P0, P0)
    assert (rc_n, rc_w) == (0, 0)
    ratio = res_w / res_n
    assert ratio == pytest.approx(narrow / wide, rel=1e-12), (
        "the resolution does not scale with the sensitivity, so this finding "
        "rests on something other than the arithmetic")
    assert ratio > 1.0
    print("\n  altitude resolution at {:.2f} hPa: narrow range {:.4f} mm per "
          "count, wide range {:.4f} mm per count, {:.1f} times worse, and the "
          "wide range is the one step 6 selects inside the event window"
          .format(P0, res_n * 1000.0, res_w * 1000.0, ratio))


def test_the_slope_of_the_relation_is_what_sets_that_resolution():
    """And the slope itself, so the number above is traceable.

    About 8.3 metres per hectopascal near the reference. Checked against a
    difference quotient as well as the analytic form, because the two agreeing
    is what says the derivative was taken correctly.
    """
    rc, res = py_resolution(1.0, P0, P0)
    assert rc == 0
    step = 1e-6
    rc_a, a = py_altitude(P0 - step, P0)
    rc_b, b = py_altitude(P0 + step, P0)
    assert (rc_a, rc_b) == (0, 0)
    numeric = (a - b) / (2.0 * step)
    assert res == pytest.approx(numeric, rel=1e-6), (res, numeric)
    print("\n  the relation's slope at the reference is {:.4f} m per hPa, "
          "analytic, and {:.4f} by difference quotient".format(res, numeric))


def test_a_reference_wrong_by_ordinary_weather_is_tens_of_metres():
    """Finding two, as the number a compiled sea-level constant would carry.

    The record of step 7 carries both acceleration scales and the barometer
    range, for the stated reason that a record should be readable years later,
    and does not carry the reference pressure step 9's relation needs.
    """
    case = next(c for c in ORACLE["altitudes"] if "THE WEATHER" in c["why"])
    rc, m = py_altitude(case["p_hpa"], case["p0_hpa"])
    assert rc == 0
    assert abs(m) > 50.0, (
        "ten hectopascals of reference error is no longer tens of metres, so "
        "this finding is wrong: {:.1f} m".format(m))
    # And the same pressure against its own reference is zero, which is the
    # control: the error is in the reference and not in the relation.
    rc, zero = py_altitude(case["p_hpa"], case["p_hpa"])
    assert rc == 0 and zero == 0.0
    assert chapter_altitude(case["p_hpa"], case["p0_hpa"]) == pytest.approx(
        m, rel=1e-12), "the sketch no longer reproduces the relation"
    print("\n  one pressure of {:.2f} hPa reads as {:.1f} m against a reference "
          "ten hectopascals away, and as exactly 0 m against its own"
          .format(case["p_hpa"], m))


def test_step_tens_printed_figure_omits_the_overhead_its_comment_names():
    """Finding three, and it is small enough to say so.

    The printed sum is reproduced exactly, then the two bytes the comment names
    are added. A quarter of one per cent, which changes no conclusion.
    """
    printed = next(c for c in ORACLE["drains"] if "PRINTED FIGURE" in c["why"])
    withover = next(c for c in ORACLE["drains"]
                    if "its own comment" in c["why"])
    rc, ms = py_drain(printed["samples"], printed["bytes_per_sample"], 0,
                      printed["bus_hz"])
    assert rc == 0
    assert ms == pytest.approx(chapter_drain_ms(), rel=0, abs=0), (
        "the sketch and this implementation no longer agree on the printed sum")
    assert round(ms, 1) == 17.3, (
        "the printed sum is no longer 17.3 ms: {:.3f}".format(ms))
    delta = (withover["ms"] - ms) / ms * 100.0
    assert 0.0 < delta < 1.0, delta
    print("\n  step 10 as printed {:.3f} ms; with the address and register "
          "bytes its comment names {:.3f} ms, {:+.2f} per cent"
          .format(ms, withover["ms"], delta))


# ---------------------------------------------------------------------------
# The Rust, driven as a filter over the same oracle. One process for the whole
# run, which is conftest's rule.
#
# Six verbs, one per function the oracle exercises:
#
#     H <14 bytes>
#     M <14 bytes> <table> <counts> <fine>
#     P <14 bytes> <table> <counts>
#     A <p_hpa> <p0_hpa>
#     R <lsb_per_hpa> <p_hpa> <p0_hpa>
#     D <samples> <bytes_per_sample> <overhead> <bus_hz>
#
# THE TABLE TRAVELS IN EVERY REQUEST THAT NEEDS IT, fifteen tokens: five fine
# sensitivities, five coarse, two barometer, then the three valid masks. The
# filter holds nothing between lines, which is the point rather than frugality:
# a filter that remembered a table from an earlier request could answer one that
# never named a table, and what this project tests is that a decoder uses the
# record's own codes against a table it was GIVEN.
#
# THE CRATE IS std AND NOT no_std, which is the one structural difference from
# its siblings. Step 9's relation raises a ratio to a fractional power and
# f64::powf is in std, so the cross step for the board's target does not name
# this crate and the guard that compares that step's list against every no_std
# crate does not expect it to.

NEWLINE = chr(10)


def table_tokens():
    """The oracle's table as the fifteen tokens the filter reads."""
    out = [repr(float(v)) for v in TABLE["lo_mg_per_lsb"]]
    out += [repr(float(v)) for v in TABLE["hi_mg_per_lsb"]]
    out += [repr(float(v)) for v in TABLE["baro_lsb_per_hpa"]]
    out += [str(TABLE["lo_valid"]), str(TABLE["hi_valid"]),
            str(TABLE["baro_valid"])]
    return out


def header_request(case):
    return " ".join(["H"] + [str(b) for b in case["bytes"]])


def mg_request(case, counts, fine):
    parts = ["M"] + [str(b) for b in case["bytes"]] + table_tokens()
    parts += [str(counts), "1" if fine else "0"]
    return " ".join(parts)


def hpa_request(case, counts):
    parts = ["P"] + [str(b) for b in case["bytes"]] + table_tokens()
    parts += [str(counts)]
    return " ".join(parts)


def all_requests():
    """Every request, in the order the answers are read back.

    Built where a test with no cargo can reach them, because the filter test
    SKIPS where there is no cargo and a skipped body never runs at all.
    """
    lines = [header_request(c) for c in ORACLE["headers"]]
    lines += [mg_request(c, c["counts"], c["fine"])
              for c in ORACLE["conversions"]]
    lines += [hpa_request(c, c["counts"]) for c in ORACLE["pressures"]]
    # The refusal cases carry no counts: the refusal is about the code in the
    # record and the table's valid bit, so any count reaches it.
    lines += [mg_request(c, 1000, True) for c in ORACLE["conversion_refusals"]]
    lines += ["A {!r} {!r}".format(float(c["p_hpa"]), float(c["p0_hpa"]))
              for c in ORACLE["altitudes"] + ORACLE["altitude_refusals"]]
    lines += ["R {!r} {!r} {!r}".format(float(c["lsb_per_hpa"]),
                                        float(c["p_hpa"]),
                                        float(c["p0_hpa"]))
              for c in ORACLE["resolutions"]]
    lines += ["D {} {} {} {!r}".format(c["samples"], c["bytes_per_sample"],
                                       c["overhead_bytes"],
                                       float(c["bus_hz"]))
              for c in ORACLE["drains"] + ORACLE["drain_refusals"]]
    return lines


def fields_of(answer):
    out = {}
    for token in answer.split():
        key, value = token.split("=", 1)
        out[key] = value
    return out


def test_every_request_says_what_its_case_says_without_running_anything():
    """Runs everywhere, including where no filter exists, on purpose.

    It reads each request back apart and compares it with the case rather than
    checking that it is well formed, which is the distinction P10's version of
    this file got wrong on Thursday 29 October 2026: a wrong value is as well
    formed as a right one.
    """
    lines = all_requests()
    at = 0
    for case in ORACLE["headers"]:
        t = lines[at].split()
        assert t[0] == "H", case["why"]
        assert [int(x) for x in t[1:]] == case["bytes"], case["why"]
        at += 1

    def check_table(tokens):
        want = table_tokens()
        assert len(tokens) == len(want), "the table is fifteen tokens"
        for got, expect in zip(tokens[:12], want[:12]):
            assert float(got) == float(expect)
        for got, expect in zip(tokens[12:], want[12:]):
            assert int(got) == int(expect)

    for case in ORACLE["conversions"]:
        t = lines[at].split()
        assert t[0] == "M", case["why"]
        assert [int(x) for x in t[1:15]] == case["bytes"], case["why"]
        check_table(t[15:30])
        assert int(t[30]) == case["counts"], case["why"]
        assert int(t[31]) == (1 if case["fine"] else 0), case["why"]
        assert len(t) == 32, case["why"]
        at += 1

    for case in ORACLE["pressures"]:
        t = lines[at].split()
        assert t[0] == "P", case["why"]
        assert [int(x) for x in t[1:15]] == case["bytes"], case["why"]
        check_table(t[15:30])
        assert int(t[30]) == case["counts"], case["why"]
        at += 1

    for case in ORACLE["conversion_refusals"]:
        t = lines[at].split()
        assert t[0] == "M", case["why"]
        assert [int(x) for x in t[1:15]] == case["bytes"], case["why"]
        at += 1

    for case in ORACLE["altitudes"] + ORACLE["altitude_refusals"]:
        t = lines[at].split()
        assert t[0] == "A", case["why"]
        assert float(t[1]) == case["p_hpa"], case["why"]
        assert float(t[2]) == case["p0_hpa"], case["why"]
        at += 1

    for case in ORACLE["resolutions"]:
        t = lines[at].split()
        assert t[0] == "R", case["why"]
        assert float(t[1]) == case["lsb_per_hpa"], case["why"]
        assert float(t[2]) == case["p_hpa"], case["why"]
        assert float(t[3]) == case["p0_hpa"], case["why"]
        at += 1

    for case in ORACLE["drains"] + ORACLE["drain_refusals"]:
        t = lines[at].split()
        assert t[0] == "D", case["why"]
        assert int(t[1]) == case["samples"], case["why"]
        assert int(t[2]) == case["bytes_per_sample"], case["why"]
        assert int(t[3]) == case["overhead_bytes"], case["why"]
        assert float(t[4]) == case["bus_hz"], case["why"]
        at += 1

    assert at == len(lines)
    print(NEWLINE + "  {} requests built with no filter and no compiler, every "
          "field read back and compared with its case".format(len(lines)))


def test_the_rust_filter_answers_every_case_as_the_oracle_says():
    """One process, the nine sections in the oracle's order."""
    require_artefact(REC_RUST_FILTER, "cargo build --release --workspace")
    lines = all_requests()
    answers = run_filter(REC_RUST_FILTER, lines)
    assert len(answers) == len(lines)

    at = 0
    refused = 0
    for case in ORACLE["headers"]:
        if "error" in case:
            assert answers[at] == "REFUSED {}".format(case["error"]), case["why"]
            refused += 1
        else:
            got = fields_of(answers[at])
            hdr = case["hdr"]
            assert int(got["tick"]) == hdr["tick_event"], case["why"]
            assert int(got["pre"]) == hdr["n_pre"], case["why"]
            assert int(got["post"]) == hdr["n_post"], case["why"]
            assert int(got["baro"]) == hdr["n_baro"], case["why"]
            assert int(got["lo"]) == hdr["lo_fs_g"], case["why"]
            assert int(got["hi"]) == hdr["hi_fs_g"], case["why"]
            assert int(got["range"]) == hdr["baro_range"], case["why"]
            assert int(got["flags"]) == hdr["flags"], case["why"]
        at += 1

    # The conversions are EXACT, because the oracle's table is powers of two.
    for case in ORACLE["conversions"]:
        got = fields_of(answers[at])
        assert float(got["mg"]) == case["mg"], case["why"]
        at += 1
    for case in ORACLE["pressures"]:
        got = fields_of(answers[at])
        assert float(got["hpa"]) == case["hpa"], case["why"]
        at += 1
    for case in ORACLE["conversion_refusals"]:
        assert answers[at] == "REFUSED {}".format(case["error"]), case["why"]
        refused += 1
        at += 1

    # The altitudes raise a ratio to a fractional power, which is a library
    # call in every language, so they are compared to a relative 1e-12 and
    # nothing tighter. The oracle says so in its own note.
    for case in ORACLE["altitudes"]:
        got = fields_of(answers[at])
        if case["m"] == 0.0:
            assert float(got["m"]) == 0.0, case["why"]
        else:
            assert float(got["m"]) == pytest.approx(case["m"],
                                                    rel=1e-12), case["why"]
        at += 1
    for case in ORACLE["altitude_refusals"]:
        assert answers[at] == "REFUSED {}".format(case["error"]), case["why"]
        refused += 1
        at += 1
    for case in ORACLE["resolutions"]:
        got = fields_of(answers[at])
        assert float(got["m"]) == pytest.approx(case["m"],
                                                rel=1e-12), case["why"]
        at += 1
    for case in ORACLE["drains"]:
        got = fields_of(answers[at])
        assert float(got["ms"]) == pytest.approx(case["ms"],
                                                 rel=1e-12), case["why"]
        at += 1
    for case in ORACLE["drain_refusals"]:
        assert answers[at] == "REFUSED {}".format(case["error"]), case["why"]
        refused += 1
        at += 1

    assert at == len(answers)
    expected_refusals = (sum(1 for c in ORACLE["headers"] if "error" in c)
                         + len(ORACLE["conversion_refusals"])
                         + len(ORACLE["altitude_refusals"])
                         + len(ORACLE["drain_refusals"]))
    assert refused == expected_refusals, (refused, expected_refusals)
    print(NEWLINE + "  the Rust answered all {} requests across six verbs, {} "
          "of them refusals, with the conversions compared exactly and the "
          "altitudes to a relative 1e-12".format(len(answers), refused))


def test_the_rust_carries_finding_one_through_the_filter():
    """Not a parity check: the event window's cost, through the filter.

    Finding one is that step 6 selects the WIDE barometer scale inside an event
    window, where altitude resolution is worst, and it does so during the event
    the recorder exists to capture. The ratio is asserted here against the
    filter's own answers rather than described.
    """
    require_artefact(REC_RUST_FILTER, "cargo build --release --workspace")
    narrow = float(TABLE["baro_lsb_per_hpa"][0])
    wide = float(TABLE["baro_lsb_per_hpa"][1])
    lines = ["R {!r} {!r} {!r}".format(narrow, P0, P0),
             "R {!r} {!r} {!r}".format(wide, P0, P0)]
    answers = run_filter(REC_RUST_FILTER, lines)
    fine = float(fields_of(answers[0])["m"])
    coarse = float(fields_of(answers[1])["m"])
    assert coarse > fine, "the wide range resolves altitude less finely"
    # Exactly the ratio of the two sensitivities, because the pressure is the
    # same in both calls and the table is powers of two.
    assert coarse / fine == pytest.approx(narrow / wide, rel=1e-12)
    print(NEWLINE + "  finding one through the filter: {:.4f} m per bit on the "
          "narrow range against {:.4f} m on the wide, a ratio of {:.1f}"
          .format(fine, coarse, coarse / fine))
