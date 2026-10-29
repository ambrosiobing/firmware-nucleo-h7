"""P10's per-phase charge, in three languages, against an oracle in fractions.

**Why the oracle is trustworthy.** Every case was recomputed in exact rational
arithmetic with `fractions.Fraction`, which neither implementation uses, and every
current is a sum of powers of two with dt at 2^-10 seconds, so no case needs a
tolerance to pass. A disagreement here is an implementation being wrong rather
than a rounding argument.

**The residual is the acceptance test and it is expected to be non-zero** when
runs are rejected, because their charge is in the total and not in the split. One
pair of cases makes that visible: the same trace at a floor of two has a residual
of two thirds, and at a floor of one has a residual of zero.

**One test here is about the chapter rather than about this code.** Step 7's
decoder filters runs by comparing a difference of two products of dt against dt,
and that comparison is decided by floating-point rounding: on one ten sample trace
it keeps two single-sample runs and rejects three others. The sketch is carried in
the Python module, unused, so the demonstration runs against the real thing rather
than against a paraphrase of it.
"""
from __future__ import annotations

import ctypes
import json
import random
import sys
from pathlib import Path

import pytest

from conftest import (BUILD, PHASE_RUST_FILTER, require_artefact,
                      run_filter, shared_library_name)

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "projects" / "P10-energy-phases"
sys.path.insert(0, str(PROJECT / "python"))

from phaseledger import (ERRORS, chapter_sketch_intervals,  # noqa: E402
                         ledger as py_ledger)

ORACLE = json.loads((PROJECT / "vectors.json").read_text(encoding="utf-8"))
assert ORACLE["schema"] == 1, "the oracle's schema moved"

PHASE_CODES = 8


class CLedger(ctypes.Structure):
    """Must match phase_ledger_t in c/phaseledger.h, field for field."""

    _fields_ = [
        ("per_code", ctypes.c_double * PHASE_CODES),
        ("total", ctypes.c_double),
        ("split", ctypes.c_double),
        ("residual", ctypes.c_double),
        ("runs", ctypes.c_uint32),
        ("rejected", ctypes.c_uint32),
        ("rejected_samples", ctypes.c_uint32),
    ]


_LIB = None


def c_lib():
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("phaseledger")
        require_artefact(path)
        d = ctypes.CDLL(str(path))
        d.phase_ledger.restype = ctypes.c_int
        d.phase_ledger.argtypes = [
            ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_uint32, ctypes.c_double, ctypes.c_uint32,
            ctypes.POINTER(CLedger),
        ]
        _LIB = d
    return _LIB


def c_run(current, codes, dt, floor):
    lib = c_lib()
    n = len(codes)
    carr = (ctypes.c_double * max(n, 1))(*current) if n else (ctypes.c_double * 1)()
    karr = (ctypes.c_uint8 * max(n, 1))(*codes) if n else (ctypes.c_uint8 * 1)()
    out = CLedger()
    rc = lib.phase_ledger(carr, karr, n, dt, floor, ctypes.byref(out))
    return rc, out


def dt_of(case):
    return case.get("dt_s", ORACLE["dt_s"])


@pytest.mark.parametrize("case", ORACLE["cases"], ids=lambda c: c["why"][:46])
def test_the_python_matches_the_oracle(case):
    rc, got = py_ledger(case["current_a"], case["codes"], dt_of(case),
                        case["min_samples"])
    if "error" in case:
        assert rc == ERRORS[case["error"]], case["why"]
        assert got is None
        return
    assert rc == 0, case["why"]
    for code, charge in case["per_code"].items():
        assert got["per_code"][int(code)] == charge, case["why"]
    assert got["total"] == case["total"], case["why"]
    assert got["residual"] == pytest.approx(case["residual"], abs=1e-15), case["why"]
    assert (got["runs"], got["rejected"], got["rejected_samples"]) == (
        case["runs"], case["rejected"], case["rejected_samples"]), case["why"]


@pytest.mark.parametrize("case", ORACLE["cases"], ids=lambda c: c["why"][:46])
def test_the_c_matches_the_oracle(case):
    rc, got = c_run(case["current_a"], case["codes"], dt_of(case),
                    case["min_samples"])
    if "error" in case:
        assert rc == ERRORS[case["error"]], case["why"]
        return
    assert rc == 0, case["why"]
    for code, charge in case["per_code"].items():
        assert got.per_code[int(code)] == charge, case["why"]
    assert got.total == case["total"], case["why"]
    assert got.residual == pytest.approx(case["residual"], abs=1e-15), case["why"]
    assert (got.runs, got.rejected, got.rejected_samples) == (
        case["runs"], case["rejected"], case["rejected_samples"]), case["why"]


def test_the_two_implementations_agree_on_every_case():
    for case in ORACLE["cases"]:
        rc_c, c_out = c_run(case["current_a"], case["codes"], dt_of(case),
                            case["min_samples"])
        rc_p, p_out = py_ledger(case["current_a"], case["codes"], dt_of(case),
                                case["min_samples"])
        assert rc_c == rc_p, case["why"]
        if rc_c != 0:
            continue
        assert list(c_out.per_code) == p_out["per_code"], case["why"]
        assert c_out.total == p_out["total"], case["why"]
        assert c_out.split == p_out["split"], case["why"]
        assert c_out.residual == pytest.approx(p_out["residual"], abs=1e-15)


def test_the_residual_is_exactly_zero_when_nothing_is_rejected_and_the_values_are_exact():
    """The reconciliation, over a long trace rather than a case.

    Two thousand samples whose currents are all powers of two, so every partial
    sum is exact in binary and the two routes to the total must agree to the last
    bit. This is the strongest form of the chapter's acceptance test, and it is
    available only because the values were chosen to be exact.
    """
    random.seed(10)
    current = [2.0 ** random.randint(-6, 2) for _ in range(2000)]
    codes = []
    while len(codes) < 2000:
        codes += [random.choice([0, 1, 2, 3, 4])] * random.randint(3, 40)
    codes = [int(c) for c in codes[:2000]]
    rc, out = c_run(current, codes, ORACLE["dt_s"], 1)
    assert rc == 0
    assert out.rejected == 0
    assert out.total == out.split, "the two routes to the total disagree"
    assert out.residual == 0.0


def test_an_inexact_trace_leaves_a_rounding_residual_and_not_a_zero():
    """The honest version of the test above, and why it is a separate test.

    With currents that are not exact in binary, the two routes sum the same
    numbers in different groupings, and floating-point addition is not
    associative, so the residual is tiny rather than zero. Asserting zero here
    would be asserting something false about arithmetic, and a reader comparing
    the two tests sees which property belongs to the ledger and which to the
    machine.
    """
    random.seed(11)
    current = [random.uniform(0.001, 0.2) for _ in range(2000)]
    codes = []
    while len(codes) < 2000:
        codes += [random.choice([0, 1, 2, 3, 4])] * random.randint(3, 40)
    codes = [int(c) for c in codes[:2000]]
    rc, out = c_run(current, codes, ORACLE["dt_s"], 1)
    assert rc == 0
    assert out.rejected == 0
    assert out.residual < 1e-12, out.residual
    print("\n  2 000 inexact samples, nothing rejected, residual {:.3e}"
          .format(out.residual))


def test_the_chapters_sketch_rejects_by_rounding_and_this_is_why_runs_are_counted():
    """The finding, demonstrated against the sketch itself rather than described.

    chapter_sketch_intervals is step 7 as printed. On this trace it keeps two
    single-sample runs and rejects three others, all of them one sample long,
    because the differences of its dt products land either side of dt in the last
    bits. The implementations here count samples as integers instead.
    """
    dt = 1e-5
    codes = [0, 1, 2, 2, 3, 0, 0, 0, 4, 7]
    built = []
    cur, start = codes[0], 0
    for i in range(1, len(codes)):
        if codes[i] != cur:
            built.append((cur, start, i)); start, cur = i, codes[i]
    built.append((cur, start, len(codes)))
    singles = [b for b in built if b[2] - b[1] == 1]
    kept = chapter_sketch_intervals(codes, dt)

    assert len(built) == 7 and len(singles) == 5
    assert len(kept) < len(built), (
        "the sketch's filter rejected nothing, so this finding is wrong")
    kept_singles = len(kept) - (len(built) - len(singles))
    assert 0 < kept_singles < len(singles), (
        "the sketch kept {} of {} single-sample runs, so it is not inconsistent "
        "and the finding is wrong".format(kept_singles, len(singles)))

    rc, out = c_run([1.0] * len(codes), codes, dt, 2)
    assert rc == 0
    assert out.rejected == len(singles), (
        "counting samples rejected {} where {} runs are one sample long"
        .format(out.rejected, len(singles)))
    print("\n  the sketch keeps {} of {} single-sample runs and rejects {};"
          " counting samples rejects all {}"
          .format(kept_singles, len(singles), len(singles) - kept_singles,
                  out.rejected))


# ---------------------------------------------------------------------------
# The Rust, driven as a filter over the same oracle. One process for the whole
# run, which is conftest's rule.
#
# One verb, because this project is one function:
#
#     L <dt_s> <min_samples> <n> <current code>*n
#
# The answer always carries all eight per-code charges, zeros included, because
# codes 5 and 6 are unused in the chapter and a trace containing one is a
# finding the report has to be able to show.

NEWLINE = chr(10)


def ledger_request(case):
    body = " ".join("{!r} {}".format(float(a), c)
                    for a, c in zip(case["current_a"], case["codes"]))
    return "L {!r} {} {} {}".format(ORACLE["dt_s"], case["min_samples"],
                                    len(case["codes"]), body).strip()


def all_ledger_requests():
    """Every request line, built where a test with no cargo can reach them.

    Separated from the filter test because that test SKIPS where there is no
    cargo and a skipped body never runs at all, which hid a plain NameError in
    P04's version of this file on Thursday 29 October 2026 until the laptop
    with a compiler found it.
    """
    return [ledger_request(c) for c in ORACLE["cases"]]


def fields_of(answer):
    out = {}
    for token in answer.split():
        key, value = token.split("=", 1)
        out[key] = value
    return out


def test_the_requests_round_trip_every_float_without_running_anything():
    """Runs everywhere, including where no filter exists, on purpose."""
    lines = all_ledger_requests()
    assert len(lines) == len(ORACLE["cases"])
    floats = 0
    for line, case in zip(lines, ORACLE["cases"]):
        parts = line.split()
        assert parts[0] == "L", case["why"]
        assert int(parts[3]) == len(case["codes"]), case["why"]
        for token in parts[1:]:
            if "." in token or "e" in token or "E" in token:
                assert float(repr(float(token))) == float(token), token
                floats += 1
    assert floats >= len(ORACLE["cases"]), (
        "only {} floating point tokens appear across {} requests, which is "
        "fewer than one each, so the builder has stopped emitting them"
        .format(floats, len(ORACLE["cases"])))
    print(NEWLINE + "  {} requests built with no filter and no compiler, and "
          "{} floating point tokens each read back as the value written"
          .format(len(lines), floats))


def test_the_rust_filter_answers_every_case_as_the_oracle_says():
    """One process, every case in the oracle's order, refusals included."""
    require_artefact(PHASE_RUST_FILTER, "cargo build --release --workspace")
    lines = all_ledger_requests()
    answers = run_filter(PHASE_RUST_FILTER, lines)
    assert len(answers) == len(lines)

    refusals = 0
    for case, answer in zip(ORACLE["cases"], answers):
        if "error" in case:
            assert answer == "REFUSED {}".format(case["error"]), case["why"]
            refusals += 1
            continue
        got = fields_of(answer)
        # total and the per-code charges exactly, which is what this oracle is
        # built from powers of two for.
        assert float(got["total"]) == case["total"], case["why"]
        assert int(got["runs"]) == case["runs"], case["why"]
        assert int(got["rejected"]) == case["rejected"], case["why"]
        want_samples = case["rejected_samples"]
        assert int(got["rejected_samples"]) == want_samples, case["why"]
        for code in range(PHASE_CODES):
            want = case["per_code"].get(str(code), 0.0)
            assert float(got["per{}".format(code)]) == want, (code,
                                                              case["why"])
        # The residual is the one figure the C and the Python compare with a
        # tolerance, because it is a quotient rather than a sum of powers of
        # two, so the Rust is held to the same bound rather than a tighter one.
        assert float(got["residual"]) == pytest.approx(case["residual"],
                                                       abs=1e-15), case["why"]
    assert refusals == sum(1 for c in ORACLE["cases"] if "error" in c)
    print(NEWLINE + "  the Rust answered all {} cases, {} of them refusals, "
          "with total and all eight per-code charges compared exactly"
          .format(len(lines), refusals))
