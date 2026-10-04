"""P06's four witnesses against one set of synthetic captures.

**Which half of P06 this is.** The three acquisition back ends refuse at run time
rather than guessing a converter, timer or transfer engine setting RM0455
governs. The witness needs none of that, and it is the piece that decides whether
a clean square wave at a plausible wrong rate is reported as a pass. That is this
project's whole subject: a clean square wave at the wrong rate is
indistinguishable from a right one without an external witness.

**The oracle is synthetic captures whose answer is known by construction**,
written to files because a capture is thousands of doubles. The one that matters
most is the second: a clean 1100 Hz wave, which every implementation must fail on
the rate criterion while passing the jitter one. A witness that called that a
pass would be worthless, and no amount of agreement between four implementations
would save it.

**Why this comparison is numeric and not exact.** The other four parity tests
compare bytes, row indices or integers, and demand equality. This one compares
doubles to a relative tolerance of 1e-12, and the reason is worth stating rather
than hiding. Double arithmetic is deterministic, but the contraction of a
multiply and an add into one fused instruction is not the same across four
toolchains: gcc may contract where rustc does not, and the Cortex-M7's floating
point unit has a fused multiply-add where a host may not. The C and C++ are
compiled with `-ffp-contract=off` to remove the question where it can be removed.
The pass criteria are 0.1 percent, so 1e-12 is nine orders of magnitude tighter
than anything the measurement claims, and any disagreement at that scale is an
arithmetic difference rather than a difference of opinion about the rate.

The booleans and the counts are compared exactly. A verdict that differed between
implementations would be a real disagreement whatever the arithmetic did.

C is the reference, as in the other parity tests, because `rate.c` is the one
that compiles for the target.
"""
from __future__ import annotations

import ctypes
import math
import shutil
import sys
import tempfile
from pathlib import Path

import pytest

from conftest import (
    BUILD,
    RATE_CPP_FILTER,
    RATE_RUST_FILTER,
    assert_not_vacuous,
    run_filter,
    shared_library_name,
)

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from firmkit import rate as py_rate  # noqa: E402

LANGUAGES = ("C", "C++", "Python", "Rust")

# Nine orders of magnitude tighter than the 0.1 percent the measurement claims.
RELATIVE_TOLERANCE = 1e-12

FS = 100_000.0          # the MCC 118's rate, which is the witness this volume has
NOMINAL = 1000.0

# The fields compared as doubles, and the ones compared exactly.
DOUBLE_FIELDS = (
    "duration_s", "resolution_s", "count_rate_hz", "fit_period_s", "fit_rate_hz",
    "fit_rate_se_hz", "interval_mean_s", "interval_sd_s", "interval_worst_s",
    "interval_worst_dev_s",
)
EXACT_FIELDS = (
    "edges", "missing_edges", "routes_agree", "rate_within_tolerance",
    "jitter_within_limit", "worst_interval_within_limit", "no_missing_edges",
    "pass", "limited_by_instrument",
)


class CResult(ctypes.Structure):
    """Must match rate_result_t in projects/P06-timer-sampling/c/rate.h, in order."""

    _fields_ = [
        ("sample_rate_hz", ctypes.c_double),
        ("duration_s", ctypes.c_double),
        ("edges", ctypes.c_size_t),
        ("nominal_hz", ctypes.c_double),
        ("resolution_s", ctypes.c_double),
        ("count_rate_hz", ctypes.c_double),
        ("fit_period_s", ctypes.c_double),
        ("fit_rate_hz", ctypes.c_double),
        ("fit_rate_se_hz", ctypes.c_double),
        ("interval_mean_s", ctypes.c_double),
        ("interval_sd_s", ctypes.c_double),
        ("interval_worst_s", ctypes.c_double),
        ("interval_worst_dev_s", ctypes.c_double),
        ("missing_edges", ctypes.c_size_t),
        ("routes_agree", ctypes.c_bool),
        ("rate_within_tolerance", ctypes.c_bool),
        ("jitter_within_limit", ctypes.c_bool),
        ("worst_interval_within_limit", ctypes.c_bool),
        ("no_missing_edges", ctypes.c_bool),
        ("pass", ctypes.c_bool),
        ("limited_by_instrument", ctypes.c_bool),
    ]


_LIB = None


def c_lib():
    """The C, which is the reference, so a test without it skips and does not fail.

    `rate.c` is new as of Saturday 3 October 2026 and win11 aquamarine cannot
    build it, because compiling there is forbidden. A skip naming the command is
    the honest result, as in P03's parity test and for the same reason.
    """
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("rate")
        if not path.exists():
            pytest.skip(
                "{} is missing, and this laptop does not compile. In WSL on "
                "bing@JPTOUPM678:\n    python3 python/tools/build_host.py".format(path.name)
            )
        d = ctypes.CDLL(str(path))
        d.rate_analyse.restype = ctypes.c_int
        d.rate_analyse.argtypes = [
            ctypes.POINTER(ctypes.c_double), ctypes.c_size_t,
            ctypes.c_double, ctypes.c_double, ctypes.POINTER(CResult),
        ]
        d.rate_status_name.restype = ctypes.c_char_p
        d.rate_status_name.argtypes = [ctypes.c_int]
        _LIB = d
    return _LIB


# ------------------------------------------------------- the stimulus

def render(rises, half_period, fs, n, low=0.0, high=3.3):
    """A square wave from a list of rising-edge times.

    Built from the edges rather than from a phase function, because that is what
    lets a capture carry a dropped edge or a doubled one exactly where the test
    intends rather than wherever rounding puts it.
    """
    out = [low] * n
    for r in rises:
        first = max(0, math.ceil(r * fs))
        last = min(n, math.ceil((r + half_period) * fs))
        for i in range(first, last):
            out[i] = high
    return out


def captures():
    """Every capture, with the reason it is here and what it must produce."""
    seconds = 0.1
    n = int(FS * seconds)
    period = 1.0 / NOMINAL
    ideal = [k * period for k in range(int(seconds * NOMINAL))]

    out = []

    out.append(("a clean kilohertz square wave",
                render(ideal, period / 2, FS, n)))

    # The case the project exists for. Every implementation must fail this on
    # the rate and pass it on the jitter: it is clean, and it is wrong.
    wrong_period = 1.0 / 1100.0
    wrong = [k * wrong_period for k in range(int(seconds * 1100))]
    out.append(("a clean wave at 1100 Hz, which must fail on the rate alone",
                render(wrong, wrong_period / 2, FS, n)))

    # Jitter well inside criterion 2, from a deterministic sequence rather than
    # a random one so a failure can be reproduced by reading the test.
    jittered = [k * period + 2e-6 * math.sin(k * 1.7) for k in range(len(ideal))]
    out.append(("jitter of two microseconds, inside the ten the criterion allows",
                render(jittered, period / 2, FS, n)))

    # Jitter outside criterion 2, which must fail on jitter and still pass the
    # rate, because the mean period is unchanged.
    coarse = [k * period + 30e-6 * math.sin(k * 1.7) for k in range(len(ideal))]
    out.append(("jitter of thirty microseconds, outside the criterion",
                render(coarse, period / 2, FS, n)))

    # One edge missing, which shows as an interval near twice nominal.
    dropped = [t for k, t in enumerate(ideal) if k != 40]
    out.append(("one dropped edge, an interval near twice nominal",
                render(dropped, period / 2, FS, n)))

    # One extra edge, which shows as two intervals near a half.
    doubled = sorted(ideal + [40 * period + period / 2])
    out.append(("one doubled edge, two intervals near a half",
                render(doubled, period / 4, FS, n)))

    # An offset and an attenuation, so the thresholds taken from the observed
    # swing are exercised rather than assumed rail voltages.
    out.append(("offset and attenuated, so the thresholds come from the swing",
                render(ideal, period / 2, FS, n, low=1.0, high=1.5)))

    # Refusals. A flat recording has no edges to find; a fragment has too few.
    out.append(("a flat recording, refused rather than called perfect",
                [1.65] * n))
    out.append(("a fragment, too few edges to measure",
                render(ideal[:2], period / 2, FS, 200)))

    return out


@pytest.fixture(scope="module")
def captures_dir():
    """A directory for the capture files, made with tempfile rather than with
    pytest's tmp_path fixture.

    tmp_path keeps a `pytest-current` symlink beside its directories, and
    creating or reading that symlink raises PermissionError on win11 aquamarine,
    which does not have the privilege Windows wants for a symlink. The captures
    need a scratch directory and nothing else, so tempfile gives one with no
    symlink involved. This is an environment fact about one laptop rather than a
    preference, and it is written down so the fixture is not helpfully changed
    back.
    """
    path = Path(tempfile.mkdtemp(prefix="p06-captures-"))
    yield path
    shutil.rmtree(path, ignore_errors=True)


def write_capture(captures_dir, name, samples):
    """One value per line, with a header, so the loader's header rule is used."""
    path = captures_dir / (name.replace(" ", "_").replace(",", "")[:40] + ".csv")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("volts\n")
        for v in samples:
            fh.write("{:.17g}\n".format(v))
    return path


# ------------------------------------------------------- the four routes

def via_c(cases):
    d = c_lib()
    out = []
    for _, samples in cases:
        buf = (ctypes.c_double * len(samples))(*samples)
        r = CResult()
        rc = d.rate_analyse(buf, len(samples), FS, NOMINAL, ctypes.byref(r))
        if rc != 0:
            out.append({"refused": d.rate_status_name(rc).decode(), "edges": r.edges})
            continue
        row = {f: getattr(r, f) for f in DOUBLE_FIELDS}
        row.update({f: (int(getattr(r, f)) if not isinstance(getattr(r, f), bool)
                        else getattr(r, f)) for f in EXACT_FIELDS})
        out.append(row)
    return out


def via_python(cases):
    out = []
    for _, samples in cases:
        r = py_rate.analyse(samples, FS, NOMINAL)
        if "error" in r:
            # The Python reports its refusal as a message rather than a code.
            # Mapped here rather than changed there, because the message is what
            # a reader of the report sees and the code is what a caller checks.
            name = ("no_swing" if r["edges"] == 0 and "three" in r["error"]
                    else "too_few_edges")
            out.append({"refused": name, "edges": r["edges"]})
            continue
        row = {f: r[f] for f in DOUBLE_FIELDS}
        row["edges"] = r["edges"]
        row["missing_edges"] = r["missing_edges"]
        row["routes_agree"] = r["routes_agree"]
        for f in ("rate_within_tolerance", "jitter_within_limit",
                  "worst_interval_within_limit", "no_missing_edges"):
            row[f] = r["checks"][f]
        row["pass"] = r["pass"]
        row["limited_by_instrument"] = "note" in r
        out.append(row)
    return out


def via_filter(path, cases, captures_dir):
    lines = []
    for name, samples in cases:
        capture = write_capture(captures_dir, name, samples)
        lines.append("A {} {:.17g} {:.17g}".format(capture, FS, NOMINAL))
    answers = run_filter(path, lines)
    out = []
    for answer in answers:
        if answer.startswith("REFUSED"):
            parts = answer.split()
            edges = 0
            for p in parts:
                if p.startswith("edges="):
                    edges = int(p.split("=")[1])
            out.append({"refused": parts[1], "edges": edges})
            continue
        fields = dict(p.split("=", 1) for p in answer.split())
        row = {f: float(fields[f]) for f in DOUBLE_FIELDS}
        row["edges"] = int(fields["edges"])
        row["missing_edges"] = int(fields["missing_edges"])
        for f in EXACT_FIELDS:
            if f in ("edges", "missing_edges"):
                continue
            row[f] = fields[f] == "1"
        out.append(row)
    return out


def available(cases, captures_dir):
    """The implementations that exist on this laptop, each over the same cases.

    **A defect worth leaving a note about, because of where it hid.** Until
    Sunday 4 October 2026 the call below passed `tmp_path`, a name left over from
    the rename to `captures_dir` and bound nowhere in this module. On win11
    aquamarine the branch never ran: neither filter is built there, so
    `path.exists()` is false for both, and this test reported that C and Python
    agree and passed. The NameError appeared the first time a laptop had all four
    implementations, which is the only time the four-language comparison runs at
    all.

    So the pattern that makes this suite honest on a laptop with no compiler, a
    missing implementation skips with the reason printed, is also the pattern that
    kept an error in the comparison path out of sight. `assert_not_vacuous` did
    not catch it either, because it skips on Windows for the same reason. The
    lesson is recorded rather than designed away: on this laptop a parity test
    proves the Python and the C agree, and nothing more, whatever its green tick
    suggests.
    """
    present = {"C": via_c(cases), "Python": via_python(cases)}
    for name, path in (("C++", RATE_CPP_FILTER), ("Rust", RATE_RUST_FILTER)):
        if path.exists():
            present[name] = via_filter(path, cases, captures_dir)
    return present


def close(a, b):
    if isinstance(a, float) and math.isnan(a):
        return isinstance(b, float) and math.isnan(b)
    if a == b:
        return True
    scale = max(abs(a), abs(b))
    return abs(a - b) <= RELATIVE_TOLERANCE * scale


# --------------------------------------------------------------- the tests

def test_every_language_analyses_every_capture_the_same_way(captures_dir, capsys):
    cases = captures()
    results = available(cases, captures_dir)
    reference = results["C"]

    for lang, got in sorted(results.items()):
        for i, (want, have) in enumerate(zip(reference, got)):
            label = cases[i][0]
            assert ("refused" in want) == ("refused" in have), (
                "{}: {} {} and C {}".format(
                    label, lang,
                    "refused" if "refused" in have else "accepted",
                    "refused" if "refused" in want else "accepted")
            )
            if "refused" in want:
                assert have["refused"] == want["refused"], (
                    "{}: {} refused with {} and C with {}".format(
                        label, lang, have["refused"], want["refused"])
                )
                continue
            for f in EXACT_FIELDS:
                assert have[f] == want[f], "{}: {} says {}={} and C says {}".format(
                    label, lang, f, have[f], want[f])
            for f in DOUBLE_FIELDS:
                assert close(have[f], want[f]), (
                    "{}: {} says {}={!r} and C says {!r}, a relative difference "
                    "of {:.3e}".format(
                        label, lang, f, have[f], want[f],
                        abs(have[f] - want[f]) / max(abs(have[f]), abs(want[f]), 1e-300))
                )

    with capsys.disabled():
        print("\n  P06 parity over {} captures, {} fields each".format(
            len(cases), len(DOUBLE_FIELDS) + len(EXACT_FIELDS)))
        for lang in LANGUAGES:
            print("    {:<7} {}".format(
                lang, "agrees" if lang in results else "not built, not compared"))


def test_a_clean_wave_at_the_wrong_rate_fails_on_the_rate_in_every_language(captures_dir):
    """The case P06 exists for, asserted by name rather than left to the general
    comparison. A witness that called this a pass would be worthless, and four
    implementations agreeing on a wrong answer would still be wrong."""
    case = [c for c in captures() if "1100" in c[0]]
    assert len(case) == 1
    for lang, got in sorted(available(case, captures_dir).items()):
        row = got[0]
        assert "refused" not in row, "{} refused a clean wave".format(lang)
        assert not row["rate_within_tolerance"], (
            "{} called 1100 Hz within 0.1 percent of 1000".format(lang))
        assert row["jitter_within_limit"], (
            "{} blamed jitter for a rate error".format(lang))
        assert row["routes_agree"], (
            "{}: both routes should agree on the wrong rate".format(lang))
        assert not row["pass"]
        assert abs(row["fit_rate_hz"] - 1100.0) < 1.0, (
            "{} measured {} and not 1100".format(lang, row["fit_rate_hz"]))


def test_the_captures_exercise_every_criterion_and_both_refusals():
    """Checked in Python, so it runs on every laptop and not only where a
    compiler exists. A capture list where everything passes would compare four
    implementations of the easy case."""
    rows = via_python(captures())
    assert any("refused" in r and r["refused"] == "no_swing" for r in rows), \
        "no capture is refused for having no swing"
    assert any("refused" in r and r["refused"] == "too_few_edges" for r in rows), \
        "no capture is refused for having too few edges"

    accepted = [r for r in rows if "refused" not in r]
    assert any(r["pass"] for r in accepted), "no capture passes"
    assert any(not r["rate_within_tolerance"] for r in accepted), \
        "no capture fails on the rate"
    assert any(not r["jitter_within_limit"] for r in accepted), \
        "no capture fails on jitter"
    assert any(r["missing_edges"] > 0 for r in accepted), \
        "no capture has a missing edge"


def test_the_comparison_was_not_vacuous(captures_dir):
    assert_not_vacuous(available(captures()[:1], captures_dir), "P06")


def test_rust_is_compared_when_cargo_has_been_run(captures_dir):
    """Named separately so the CI log says whether Rust was in the comparison."""
    if not RATE_RUST_FILTER.exists():
        pytest.skip(
            "{} is missing. Build it in WSL on bing@JPTOUPM678 with:\n"
            "    cargo build --release --workspace".format(RATE_RUST_FILTER.name)
        )
    case = [c for c in captures() if c[0].startswith("a clean kilohertz")]
    got = via_filter(RATE_RUST_FILTER, case, captures_dir)[0]
    assert got["pass"], "a clean kilohertz wave must pass every criterion"
    assert abs(got["fit_rate_hz"] - 1000.0) < 1.0
