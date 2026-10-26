"""P18's acceptance arithmetic, in C and Python, against an oracle this file
recomputes in exact rational arithmetic every time it runs.

**Why the oracle is trustworthy.** Every case marked exact is re-derived here
with `fractions.Fraction`, which is standard library and which neither
implementation uses, and the cases are chosen so that every intermediate is
representable in double, in single and in Q15. No exact case passes on a
tolerance, so a disagreement is an implementation being wrong rather than a
rounding argument. The design case is the opposite by nature and says so: its
numbers are measurements with stated tolerances.

**The design coefficients are checked without a design library.** A Butterworth
low pass has a closed-form magnitude, and the bilinear transform with prewarping
maps it onto the digital frequency axis exactly, so

    |H(f)| = 1 / sqrt(1 + (tan(pi f / fs) / tan(pi fc / fs)) ** (2 * order))

can be compared against the committed sections at any frequency. That check needs
only the standard library, which matters because the runner installs pytest and
nothing else. SciPy produced the coefficients on win11 aquamarine; this is what
makes them checkable afterwards by somebody who does not have it.

**The finding is a table rather than a sentence.** Chapter 18 writes its Q15
tolerance of 55 dB before the first measurement, which is the right order. What
it does not state is how the passband gain is divided between the sections, and
that choice moves the same filter on the same vector from 15.02 dB to 55.24 dB.
The chapter's own export, step 3, is the 15.02 dB case. The one scaling that
clears the tolerance does so with sixteen saturated samples, which the chapter's
acceptance script has nowhere to report.
"""
from __future__ import annotations

import ctypes
import json
import math
import sys
from fractions import Fraction
from pathlib import Path

import pytest

from conftest import BUILD, require_artefact, shared_library_name

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "projects" / "P18-transforms-filters"
sys.path.insert(0, str(PROJECT / "python"))

from dspaccept import (ERRORS, Q15_MAX_POST_SHIFT,  # noqa: E402
                       accept_metrics as py_metrics,
                       biquad_df1_q15 as py_q15,
                       biquad_df2t as py_cascade,
                       chapter_export_rows, chapter_snr_db, equal_shares,
                       q15_coeffs as py_q15_coeffs,
                       q15_post_shift as py_post_shift, redistribute_gain)

ORACLE = json.loads((PROJECT / "vectors.json").read_text(encoding="utf-8"))
assert ORACLE["schema"] == 1, "the oracle's schema moved"
PER = 5


class CAccept(ctypes.Structure):
    """Must match accept_t in c/dspaccept.h, field for field."""

    _fields_ = [("rel_rms", ctypes.c_double), ("snr_db", ctypes.c_double),
                ("exact", ctypes.c_int)]


_LIB = None


def c_lib():
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("dspaccept")
        require_artefact(path)
        d = ctypes.CDLL(str(path))
        dbl = ctypes.POINTER(ctypes.c_double)
        flt = ctypes.POINTER(ctypes.c_float)
        i16 = ctypes.POINTER(ctypes.c_int16)
        d.biquad_df2t_f64.restype = ctypes.c_int
        d.biquad_df2t_f64.argtypes = [dbl, ctypes.c_uint32, dbl, dbl,
                                      ctypes.c_uint32]
        d.biquad_df2t_f32.restype = ctypes.c_int
        d.biquad_df2t_f32.argtypes = [flt, ctypes.c_uint32, flt, flt,
                                      ctypes.c_uint32]
        d.q15_post_shift.restype = ctypes.c_int
        d.q15_post_shift.argtypes = [dbl, ctypes.c_uint32,
                                     ctypes.POINTER(ctypes.c_int32)]
        d.q15_coeffs.restype = ctypes.c_int
        d.q15_coeffs.argtypes = [dbl, ctypes.c_uint32, ctypes.c_int32, i16]
        d.biquad_df1_q15.restype = ctypes.c_int
        d.biquad_df1_q15.argtypes = [i16, ctypes.c_uint32, ctypes.c_int32, i16,
                                     i16, ctypes.c_uint32,
                                     ctypes.POINTER(ctypes.c_uint32)]
        d.accept_metrics.restype = ctypes.c_int
        d.accept_metrics.argtypes = [dbl, dbl, ctypes.c_uint32,
                                     ctypes.POINTER(CAccept)]
        _LIB = d
    return _LIB


def arr(ctype, values):
    n = max(len(values), 1)
    return (ctype * n)(*values) if values else (ctype * 1)()


def c_cascade(coeffs, nstage, data, single):
    lib = c_lib()
    n = len(data)
    if single:
        c = arr(ctypes.c_float, [float(v) for v in coeffs])
        i = arr(ctypes.c_float, [float(v) for v in data])
        o = (ctypes.c_float * max(n, 1))()
        rc = lib.biquad_df2t_f32(c, nstage, i, o, n)
    else:
        c = arr(ctypes.c_double, [float(v) for v in coeffs])
        i = arr(ctypes.c_double, [float(v) for v in data])
        o = (ctypes.c_double * max(n, 1))()
        rc = lib.biquad_df2t_f64(c, nstage, i, o, n)
    return rc, list(o)[:n]


def c_post_shift(coeffs, nstage):
    out = ctypes.c_int32(-99)
    rc = c_lib().q15_post_shift(arr(ctypes.c_double, coeffs), nstage,
                                ctypes.byref(out))
    return rc, (out.value if rc == 0 else None)


def c_q15_coeffs(coeffs, nstage, post_shift):
    out = (ctypes.c_int16 * max(len(coeffs), 1))()
    rc = c_lib().q15_coeffs(arr(ctypes.c_double, coeffs), nstage, post_shift,
                            out)
    return rc, (list(out)[:nstage * PER] if rc == 0 else None)


def c_q15_run(coeffs_q15, nstage, post_shift, data_q15):
    n = len(data_q15)
    out = (ctypes.c_int16 * max(n, 1))()
    sat = ctypes.c_uint32(0)
    rc = c_lib().biquad_df1_q15(arr(ctypes.c_int16, coeffs_q15), nstage,
                                post_shift, arr(ctypes.c_int16, data_q15), out,
                                n, ctypes.byref(sat))
    return rc, (list(out)[:n] if rc == 0 else None), sat.value


def c_accept(got, ref):
    out = CAccept()
    rc = c_lib().accept_metrics(arr(ctypes.c_double, got),
                               arr(ctypes.c_double, ref), len(got),
                               ctypes.byref(out))
    return rc, out


# ---------------------------------------------------------------------------
# The oracle, recomputed rather than trusted.

def frac_cascade(coeffs, nstage, data_q15):
    """The cascade in exact rationals. Used by nothing but this file."""
    out = [Fraction(q, 32768) for q in data_q15]
    for s in range(nstage):
        b0, b1, b2, a1, a2 = (Fraction(c) for c in coeffs[s * PER:(s + 1) * PER])
        d1 = Fraction(0)
        d2 = Fraction(0)
        for i, x in enumerate(out):
            y = b0 * x + d1
            d1 = b1 * x + a1 * y + d2
            d2 = b2 * x + a2 * y
            out[i] = y
    return out


EXACT = [c for c in ORACLE["cases"] if c["exact"]]


def test_the_oracles_exact_cases_are_recomputed_in_rational_arithmetic():
    """Every exact case, re-derived here from its coefficients and its input.

    This is what makes the word exact in the oracle a checked property rather
    than a label: all three committed outputs must equal the rational result
    with no tolerance anywhere, in both formats and in Q15.
    """
    assert len(EXACT) >= 6, "the exact cases have gone missing"
    for case in EXACT:
        want = frac_cascade(case["coeffs"], case["nstage"], case["in_q15"])
        for got, name in ((case["out_f64"], "f64"), (case["out_f32"], "f32")):
            assert [Fraction(v) for v in got] == want, (name, case["why"])
        assert [Fraction(v, 32768) for v in case["out_q15"]] == want, case["why"]
        assert case["saturations"] == 0, case["why"]


@pytest.mark.parametrize("case", ORACLE["cases"], ids=lambda c: c["why"][:46])
def test_the_python_matches_the_oracle(case):
    for single, key in ((False, "out_f64"), (True, "out_f32")):
        rc, got = py_cascade(case["coeffs"], case["nstage"],
                             [q / 32768.0 for q in case["in_q15"]],
                             single=single)
        assert rc == 0, case["why"]
        assert got == case[key], (key, case["why"])
    rc, qc = py_q15_coeffs(case["coeffs"], case["nstage"], case["post_shift"])
    assert (rc, qc) == (0, case["coeffs_q15"]), case["why"]
    rc, got, sat = py_q15(qc, case["nstage"], case["post_shift"],
                          case["in_q15"])
    assert rc == 0, case["why"]
    assert got == case["out_q15"], case["why"]
    assert sat == case["saturations"], case["why"]


@pytest.mark.parametrize("case", ORACLE["cases"], ids=lambda c: c["why"][:46])
def test_the_c_matches_the_oracle(case):
    rc, got = c_cascade(case["coeffs"], case["nstage"],
                        [q / 32768.0 for q in case["in_q15"]], single=False)
    assert rc == 0, case["why"]
    assert got == case["out_f64"], case["why"]
    rc, got = c_cascade(case["coeffs"], case["nstage"],
                        [q / 32768.0 for q in case["in_q15"]], single=True)
    assert rc == 0, case["why"]
    assert got == case["out_f32"], case["why"]
    rc, qc = c_q15_coeffs(case["coeffs"], case["nstage"], case["post_shift"])
    assert (rc, qc) == (0, case["coeffs_q15"]), case["why"]
    rc, got, sat = c_q15_run(qc, case["nstage"], case["post_shift"],
                             case["in_q15"])
    assert rc == 0, case["why"]
    assert got == case["out_q15"], case["why"]
    assert sat == case["saturations"], case["why"]


@pytest.mark.parametrize("case", ORACLE["metrics"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_judge_the_metric_cases_alike(case):
    rc_p, p = py_metrics(case["got"], case["ref"])
    rc_c, c = c_accept(case["got"], case["ref"])
    if "error" in case:
        assert rc_p == ERRORS[case["error"]], case["why"]
        assert rc_c == ERRORS[case["error"]], case["why"]
        return
    assert (rc_p, rc_c) == (0, 0), case["why"]
    assert p["exact"] == case["exact"] == c.exact, case["why"]
    assert p["rel_rms"] == case["rel_rms"] == c.rel_rms, case["why"]
    if case["exact"]:
        assert math.isinf(p["snr_db"]) and math.isinf(c.snr_db), case["why"]
    else:
        assert p["snr_db"] == pytest.approx(case["snr_db"], abs=1e-12)
        assert c.snr_db == pytest.approx(case["snr_db"], abs=1e-12)


@pytest.mark.parametrize("case", ORACLE["refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_things(case):
    nstage = case["nstage"] if "nstage" in case else 1
    if "shift_error" in case:
        rc_p, _ = py_post_shift(case["coeffs"], nstage)
        rc_c, _ = c_post_shift(case["coeffs"], nstage)
        assert rc_p == rc_c == ERRORS[case["shift_error"]], case["why"]
        return
    rc_p, _ = py_q15_coeffs(case["coeffs"], nstage, case["post_shift"])
    rc_c, _ = c_q15_coeffs(case["coeffs"], nstage, case["post_shift"])
    if case.get("fits"):
        assert rc_p == rc_c == 0, case["why"]
        return
    assert rc_p == rc_c == ERRORS[case["error"]], case["why"]


# ---------------------------------------------------------------------------
# The design, and the two acceptance criteria it is judged by.

D = ORACLE["design"]


def design_input():
    """The vector, from the one line of the oracle that describes it."""
    return [sum(a * math.sin(2.0 * math.pi * f * i / D["fs_hz"])
                for f, a in zip(D["tone_hz"], D["tone_amp"]))
            for i in range(D["n"])]


def to_q15(data):
    return [max(-32768, min(32767, int(math.floor(v * 32768.0 + 0.5))))
            for v in data]


def test_the_committed_coefficients_are_the_butterworth_the_chapter_designs():
    """Checked against the closed form, with no design library on the runner.

    A Butterworth magnitude is 1 / sqrt(1 + (w / wc) ** 2n) in the analogue
    prototype, and the bilinear transform with prewarping maps it exactly onto
    tan(pi f / fs) / tan(pi fc / fs). So the committed sections can be verified
    by anybody holding only the order, the two frequencies and this formula.
    """
    nstage, fs, fc, order = D["nstage"], D["fs_hz"], D["fc_hz"], D["order"]
    assert nstage * 2 == order, "the sections do not add up to the order"
    worst = 0.0
    for hz in (0.0, 1.0, 5.0, 10.0, 20.0, 40.0, 80.0, 150.0, 250.0, 400.0,
               499.0):
        w = 2.0 * math.pi * hz / fs
        z = complex(math.cos(w), math.sin(w))
        h = complex(1.0, 0.0)
        for s in range(nstage):
            b0, b1, b2, a1, a2 = D["coeffs"][s * PER:(s + 1) * PER]
            # a1 and a2 are stored negated, so the denominator puts them back.
            num = b0 + b1 / z + b2 / (z * z)
            den = 1.0 - a1 / z - a2 / (z * z)
            h *= num / den
        want = 1.0 / math.sqrt(
            1.0 + (math.tan(math.pi * hz / fs)
                   / math.tan(math.pi * fc / fs)) ** (2 * order))
        assert abs(abs(h) - want) <= 1e-9 * max(want, 1e-12), hz
        worst = max(worst, abs(abs(h) - want) / max(want, 1e-300))
    # And the three points that make it a low pass at this corner rather than
    # any other filter that happens to fit a formula.
    assert worst < 1e-9
    print("\n  the committed sections match the closed-form Butterworth to "
          "{:.1e} relative, at eleven frequencies".format(worst))


def test_the_float32_path_meets_the_chapters_tolerance_on_the_shipped_vector():
    """The float criterion, which passes, measured rather than assumed."""
    x = design_input()
    rc, ref = py_cascade(D["coeffs"], D["nstage"], x)
    assert rc == 0
    rc, got = py_cascade(D["coeffs"], D["nstage"], x, single=True)
    assert rc == 0
    rc, met = py_metrics(got, ref)
    assert rc == 0
    assert met["rel_rms"] <= D["tol_f32_rel_rms"], met["rel_rms"]
    print("\n  float32 against the double reference: rel RMS {:.3e}, "
          "tolerance {:.0e}, pass".format(met["rel_rms"],
                                          D["tol_f32_rel_rms"]))


def q15_measure(rows, x_q15, ref):
    rc, shift = py_post_shift(rows, D["nstage"])
    assert rc == 0, "no post shift in range fits this scaling"
    rc, qc = py_q15_coeffs(rows, D["nstage"], shift)
    assert rc == 0
    rc, y, sat = py_q15(qc, D["nstage"], shift, x_q15)
    assert rc == 0
    rc, met = py_metrics([v / 32768.0 for v in y], ref)
    assert rc == 0
    return shift, met["snr_db"], sat


def test_the_q15_verdict_is_decided_by_a_scaling_the_chapter_never_states():
    """The finding, as the table that produced it.

    One filter, one vector, one tolerance, and the only thing changing is how
    the passband gain is divided between the two sections. The chapter's own
    export is the first row.
    """
    x = design_input()
    xq = to_q15(x)
    rc, ref = py_cascade(D["coeffs"], D["nstage"], x)
    assert rc == 0
    tol = D["tol_q15_snr_db"]

    rows = []
    shift, snr, sat = q15_measure(D["coeffs"], xq, ref)
    rows.append(("chapter export, step 3", shift, snr, sat))
    for ratio in (1.0, 1.5, 2.0, 2.5):
        rc, scaled = redistribute_gain(D["coeffs"], D["nstage"],
                                       equal_shares(D["coeffs"], D["nstage"],
                                                    ratio))
        assert rc == 0, ratio
        shift, snr, sat = q15_measure(scaled, xq, ref)
        rows.append(("gain split, ratio {:.1f}".format(ratio), shift, snr, sat))

    print("\n  one filter, one vector, tolerance {:.0f} dB:".format(tol))
    for name, shift, snr, sat in rows:
        print("    {:<26s} post shift {}  {:7.2f} dB  {:4d} saturations  {}"
              .format(name, shift, snr, sat,
                      "pass" if snr >= tol else "fail"))

    snrs = [r[2] for r in rows]
    assert min(snrs) < tol <= max(snrs), (
        "the sweep no longer spans the tolerance, so this finding is wrong: "
        "{}".format(["{:.2f}".format(s) for s in snrs]))
    assert rows[0][2] < tol, (
        "the chapter's own export now passes its own tolerance, so this "
        "finding is wrong: {:.2f} dB".format(rows[0][2]))
    # The one scaling that clears the tolerance does it with saturated samples,
    # and the chapter's acceptance script has no field for them.
    passing = [r for r in rows if r[2] >= tol]
    assert passing and all(r[3] > 0 for r in passing), (
        "a scaling now clears the tolerance with no saturation, which would "
        "make the saturation count beside it unnecessary")
    print("    the only scaling that clears {:.0f} dB does so with {} "
          "saturated samples".format(tol, passing[0][3]))


def test_the_harness_can_tell_a_quantised_path_from_a_clean_one():
    """The control. Without it, every number above could be the harness.

    The double path against itself is exact, and the same comparison against a
    single-precision path is not, so the metric is measuring the format and not
    reporting a constant.
    """
    x = design_input()
    rc, ref = py_cascade(D["coeffs"], D["nstage"], x)
    assert rc == 0
    rc, met = py_metrics(ref, ref)
    assert rc == 0 and met["exact"] == 1 and math.isinf(met["snr_db"])
    rc, single = py_cascade(D["coeffs"], D["nstage"], x, single=True)
    assert rc == 0
    rc, met = py_metrics(single, ref)
    assert rc == 0 and met["exact"] == 0
    assert 100.0 < met["snr_db"] < 200.0, met["snr_db"]
    print("\n  control: the reference against itself is exact; against float32 "
          "it is {:.1f} dB".format(met["snr_db"]))


def test_the_chapters_metric_divides_by_zero_where_this_one_reports_exact():
    """The second finding, demonstrated against the sketch itself.

    Step 9's snr_db has no test for an error energy of zero, and every exact
    case in this oracle has one. In the chapter's NumPy that is an infinity and
    a warning; in plain Python it raises, which is how it was found.
    """
    case = EXACT[0]
    ref = [q / 32768.0 for q in case["in_q15"]]
    with pytest.raises(ZeroDivisionError):
        chapter_snr_db(ref, ref)
    rc, met = py_metrics(ref, ref)
    assert rc == 0 and met["exact"] == 1
    print("\n  the chapter's metric raises ZeroDivisionError on two identical "
          "blocks; accept_metrics reports exact")


def test_the_chapters_export_is_carried_unchanged_and_is_the_worst_row():
    """The export sketch is kept as printed, so the table above compares the
    real thing rather than a paraphrase of it."""
    sos = [[D["coeffs"][s * PER + 0], D["coeffs"][s * PER + 1],
            D["coeffs"][s * PER + 2], 1.0, -D["coeffs"][s * PER + 3],
            -D["coeffs"][s * PER + 4]] for s in range(D["nstage"])]
    rebuilt = [v for row in chapter_export_rows(sos) for v in row]
    assert rebuilt == D["coeffs"], (
        "the export sketch no longer reproduces the committed coefficients")


def test_the_two_implementations_agree_on_the_design_case():
    """The C and the Python over the real design, not only over the oracle."""
    x = design_input()
    rc_p, ref_p = py_cascade(D["coeffs"], D["nstage"], x)
    rc_c, ref_c = c_cascade(D["coeffs"], D["nstage"], x, single=False)
    assert (rc_p, rc_c) == (0, 0)
    assert ref_p == ref_c, "the double cascades disagree"
    rc_p, f32_p = py_cascade(D["coeffs"], D["nstage"], x, single=True)
    rc_c, f32_c = c_cascade(D["coeffs"], D["nstage"], x, single=True)
    assert (rc_p, rc_c) == (0, 0)
    assert f32_p == f32_c, "the single cascades disagree"

    xq = to_q15(x)
    rc_p, shift_p = py_post_shift(D["coeffs"], D["nstage"])
    rc_c, shift_c = c_post_shift(D["coeffs"], D["nstage"])
    assert (rc_p, rc_c) == (0, 0) and shift_p == shift_c
    rc_p, qc_p = py_q15_coeffs(D["coeffs"], D["nstage"], shift_p)
    rc_c, qc_c = c_q15_coeffs(D["coeffs"], D["nstage"], shift_c)
    assert (rc_p, rc_c) == (0, 0) and qc_p == qc_c
    rc_p, y_p, sat_p = py_q15(qc_p, D["nstage"], shift_p, xq)
    rc_c, y_c, sat_c = c_q15_run(qc_c, D["nstage"], shift_c, xq)
    assert (rc_p, rc_c) == (0, 0)
    assert y_p == y_c and sat_p == sat_c, "the Q15 cascades disagree"

    rc_p, met_p = py_metrics([v / 32768.0 for v in y_p], ref_p)
    rc_c, met_c = c_accept([v / 32768.0 for v in y_c], ref_c)
    assert (rc_p, rc_c) == (0, 0)
    assert met_p["snr_db"] == met_c.snr_db
    assert met_p["rel_rms"] == met_c.rel_rms


def test_the_post_shift_bound_is_the_one_the_header_states():
    """A design nobody scaled is refused rather than shifted into range."""
    rc, _ = py_post_shift([2.0 ** (Q15_MAX_POST_SHIFT + 1), 0.0, 0.0, 0.0, 0.0],
                          1)
    assert rc == ERRORS["range"]
    rc, _ = c_post_shift([2.0 ** (Q15_MAX_POST_SHIFT + 1), 0.0, 0.0, 0.0, 0.0],
                         1)
    assert rc == ERRORS["range"]
