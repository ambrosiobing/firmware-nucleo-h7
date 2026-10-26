"""P11's classifier, deadline test and backoff, in C and Python against one oracle.

**The piece that cannot be tested on hardware.** Chapter 11's step 4 writes the
deadline comparison as a signed difference and says the obvious form "is correct
for forty-nine days and then is not". A board cannot demonstrate that without
waiting forty-nine days; a host sets the counter to whatever it likes. Five of the
nine deadline cases sit at or across the wrap, and two of them pin the exact
millisecond where the technique's own bound gives out, which is a property of the
method rather than a bug in it.

**What was checked before either implementation was allowed to agree.** Every
deadline case against a modular distance rather than a signed cast, and every
backoff case against the chapter's formula written out longhand in Python's
unbounded integers. **That check found an error in the oracle**: a case written as
not due was due, because the deadline sat a shade under half the wrap period away
rather than a shade over. The pair that replaced it pins both sides of the flip.

**One difference from the chapter's sketch, with its reason.** The sketch's
backoff calls at_rand_small() inside itself, which makes the result unprovable: a
test can assert a band and never a value. Here the draw is a parameter, so the
function is pure and the oracle states exact values. The engine passes whatever
source it has.
"""
from __future__ import annotations

import ctypes
import json
import sys
from pathlib import Path

import pytest

from conftest import BUILD, require_artefact, shared_library_name

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "projects" / "P11-at-engine"
sys.path.insert(0, str(PROJECT / "python"))

import atparse as py  # noqa: E402

ORACLE = json.loads((PROJECT / "vectors.json").read_text(encoding="utf-8"))
assert ORACLE["schema"] == 1, "the oracle's schema moved"

_LIB = None


def c_lib():
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("atparse")
        require_artefact(path)
        d = ctypes.CDLL(str(path))
        d.at_classify.restype = ctypes.c_int
        d.at_classify.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p)]
        d.at_due.restype = ctypes.c_bool
        d.at_due.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
        d.at_backoff_ms.restype = ctypes.c_uint32
        d.at_backoff_ms.argtypes = [ctypes.c_uint8, ctypes.c_uint32,
                                    ctypes.c_uint32, ctypes.c_uint32]
        _LIB = d
    return _LIB


def c_urc_table():
    """The NULL terminated array of prefixes the C classifier takes."""
    items = [s.encode() for s in ORACLE["urcs"]]
    arr = (ctypes.c_char_p * (len(items) + 1))()
    for i, s in enumerate(items):
        arr[i] = s
    arr[len(items)] = None
    return arr


# ------------------------------------------------------------- the classifier

@pytest.mark.parametrize("case", ORACLE["classify"],
                         ids=lambda c: (c["line"] or "<empty>")[:24])
def test_the_python_classifier_matches_the_oracle(case):
    got = py.classify(case["line"], ORACLE["urcs"])
    assert py.class_name(got) == case["class"], case["why"]


@pytest.mark.parametrize("case", ORACLE["classify"],
                         ids=lambda c: (c["line"] or "<empty>")[:24])
def test_the_c_classifier_matches_the_oracle(case):
    got = c_lib().at_classify(case["line"].encode(), c_urc_table())
    assert py.class_name(got) == case["class"], case["why"]


def test_an_empty_urc_table_makes_every_urc_an_intermediate_line():
    """Passing no table is how an engine says it has none yet, and is not an error.

    The lines that would have been unsolicited become intermediate, which is what
    an engine with an empty table does, and the final result codes are unaffected
    because they are not in the table in the first place.
    """
    lib = c_lib()
    empty = (ctypes.c_char_p * 1)()
    empty[0] = None
    for case in ORACLE["classify"]:
        line = case["line"].encode()
        with_none = lib.at_classify(line, None)
        with_empty = lib.at_classify(line, empty)
        assert with_none == with_empty, case["line"]
        expected = case["class"]
        if expected == "urc":
            expected = "intermediate"
        assert py.class_name(with_none) == expected, case["why"]
        assert py.class_name(py.classify(case["line"], None)) == expected


# ---------------------------------------------------------- the deadline test

@pytest.mark.parametrize("case", ORACLE["due"], ids=lambda c: c["why"][:40])
def test_both_implementations_match_the_oracle_on_the_deadline(case):
    assert py.due(case["now"], case["deadline"]) is case["due"], case["why"]
    got = c_lib().at_due(case["now"], case["deadline"])
    assert bool(got) is case["due"], case["why"]


def test_the_naive_comparison_would_fail_the_cases_the_signed_one_passes():
    """The falsifier for the whole technique, so the wrap cases are not decoration.

    If `now >= deadline` passed every oracle case too, the signed difference
    would be ceremony. It does not: the wrap cases are exactly where the two
    disagree, and this counts them rather than asserting there are some.
    """
    disagreements = [c for c in ORACLE["due"]
                     if (c["now"] >= c["deadline"]) != c["due"]]
    assert len(disagreements) >= 2, (
        "the oracle does not contain a case where the naive comparison differs, "
        "so it is not testing the wrap at all")
    print("\n  {} of {} cases where `now >= deadline` gives the wrong answer"
          .format(len(disagreements), len(ORACLE["due"])))


def test_the_deadline_test_is_exact_across_a_walk_through_the_wrap():
    """Every millisecond from just before the wrap to just after it.

    A deadline is set, the counter is walked across the boundary, and the answer
    must flip exactly once, at the deadline, with no second flip from the wrap
    itself. Two thousand steps rather than a sample.
    """
    lib = c_lib()
    deadline = 0xFFFFFFF0
    flips = 0
    previous = lib.at_due((deadline - 1000) & 0xFFFFFFFF, deadline)
    for step in range(-999, 1001):
        now = (deadline + step) & 0xFFFFFFFF
        got = bool(lib.at_due(now, deadline))
        assert got == py.due(now, deadline), (now, deadline)
        if got != previous:
            flips += 1
            assert step == 0, "the answer flipped at an offset of {}".format(step)
        previous = got
    assert flips == 1, "the answer flipped {} times across the wrap".format(flips)
    print("  2 000 steps across the wrap, exactly one flip, at the deadline")


# ----------------------------------------------------------------- the backoff

@pytest.mark.parametrize("case", ORACLE["backoff"], ids=lambda c: c["why"][:40])
def test_both_implementations_match_the_oracle_on_the_backoff(case):
    args = (case["attempt"], case["base_ms"], case["cap_ms"], case["rand"])
    assert py.backoff_ms(*args) == case["value"], case["why"]
    assert c_lib().at_backoff_ms(*args) == case["value"], case["why"]


def test_the_jitter_window_is_a_quarter_wide_and_sits_an_eighth_low():
    """The shape of the scatter, over every draw rather than the two in the oracle.

    For a fixed interval the returned value must cover exactly the band from
    seven eighths of it to one and an eighth minus one, with no gaps, as the draw
    runs across the span. This walks all 8 000 draws for a 32 000 ms interval and
    checks the set of outcomes is exactly that contiguous band.
    """
    lib = c_lib()
    base = cap = 32000
    seen = {lib.at_backoff_ms(0, base, cap, r) for r in range(cap // 4)}
    low = cap - cap // 8
    high = low + cap // 4 - 1
    assert min(seen) == low and max(seen) == high
    assert len(seen) == high - low + 1, "the band has gaps in it"
    assert high > cap, (
        "the band does not exceed the cap, so the note about the cap bounding "
        "the doubling rather than the result is wrong")
    print("\n  interval {} ms, jitter band {} to {} ms, cap exceeded by {}"
          .format(cap, low, high, high - cap))


def test_the_delay_never_decreases_as_the_attempt_rises():
    """Doubling with a cap is monotonic, which is the point of a backoff.

    With the draw fixed at zero so the comparison is of the interval and not of
    the scatter, every attempt from zero to forty must give at least what the one
    before it gave, and the sequence must reach the cap and stay there.
    """
    lib = c_lib()
    values = [lib.at_backoff_ms(a, 1000, 32000, 0) for a in range(41)]
    for a in range(1, len(values)):
        assert values[a] >= values[a - 1], (a, values[a - 1], values[a])
    assert values[-1] == values[10], "the sequence had not settled at the cap by 10"
    assert values[0] < values[-1], "the backoff never grew at all"
