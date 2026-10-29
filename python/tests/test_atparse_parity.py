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

from conftest import (AT_CPP_FILTER, AT_RUST_FILTER, BUILD, BUILD_HOST_CMD,
                      require_artefact, run_filter, shared_library_name)

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


# ---------------------------------------------------------------------------
# The Rust, driven as a filter over the same oracle. One process for the whole
# run, which is conftest's rule.
#
# Three verbs, one per piece of the chapter that needs no module:
#
#     C <n_urcs> <urc field>*n <line field>   -> class=<name>
#     D <now_ms> <deadline_ms>                -> due=1 | due=0
#     B <attempt> <base_ms> <cap_ms> <rand>   -> delay=NNN | REFUSED backoff
#
# A FIELD IS A LENGTH AND THEN HEX, AND THE REASON IS THE ORACLE RATHER THAN A
# TASTE FOR HEX. Nine of the seventeen classify cases contain a space, four
# contain a comma or a double quote, and one is the EMPTY line, which is a class
# of its own here. TEN OF THE SEVENTEEN CANNOT CROSS A WHITESPACE-SEPARATED
# PROTOCOL AT ALL and only seven can, and a
# filter that cannot carry a case cannot test it: an oracle case a signature
# could not express has already cost this volume two red commits, and the answer
# then was to change the signature rather than to exempt the case. The length
# travels with the hex because a whitespace split cannot produce an empty token.
#
# THE REFUSAL IS A WORD AND NOT 0xFFFFFFFF. The C returns AT_BACKOFF_REFUSED,
# which is a legal delay of about forty-nine days, so a protocol carrying it as
# a number would make the refusal indistinguishable from an answer. The Rust
# returns Option<u32> and the filter prints a word.

NEWLINE = chr(10)
REFUSED = "REFUSED backoff"


def hex_field(text):
    """One length-and-hex field, which is empty as a length of zero alone."""
    raw = text.encode()
    if not raw:
        return "0"
    return "{} {}".format(len(raw), raw.hex().upper())


def classify_request(case, urcs):
    parts = ["C", str(len(urcs))]
    parts += [hex_field(u) for u in urcs]
    parts.append(hex_field(case["line"]))
    return " ".join(parts)


def due_request(case):
    return "D {} {}".format(case["now"], case["deadline"])


def backoff_request(case):
    return "B {} {} {} {}".format(case["attempt"], case["base_ms"],
                                  case["cap_ms"], case["rand"])


def all_requests(urcs):
    """Every request, in the order the answers are read back.

    Built where a test with no cargo can reach them, because a filter test
    SKIPS where there is no cargo and a skipped body never runs at all.
    """
    lines = [classify_request(c, urcs) for c in ORACLE["classify"]]
    lines += [due_request(c) for c in ORACLE["due"]]
    lines += [backoff_request(c) for c in ORACLE["backoff"]]
    return lines


def decode_field(tokens, at):
    """Read one length-and-hex field back, returning (text, next index).

    The inverse of hex_field, written here so the test can assert what the
    request SAYS rather than only that it is well formed. P10's version of this
    file checked the shape of every request and still sent the wrong interval
    to two cases, on Thursday 29 October 2026, because a wrong value is as well
    formed as a right one.
    """
    want = int(tokens[at])
    if want == 0:
        return "", at + 1
    raw = bytes.fromhex(tokens[at + 1])
    assert len(raw) == want, (want, tokens[at + 1])
    return raw.decode(), at + 2


def test_every_request_says_what_its_case_says_without_running_anything():
    """Runs everywhere, including where no filter exists, on purpose."""
    urcs = ORACLE["urcs"]
    lines = all_requests(urcs)
    assert len(lines) == (len(ORACLE["classify"]) + len(ORACLE["due"])
                          + len(ORACLE["backoff"]))

    empties = 0
    for case, line in zip(ORACLE["classify"], lines):
        tokens = line.split()
        assert tokens[0] == "C", case["why"]
        assert int(tokens[1]) == len(urcs), case["why"]
        at = 2
        for prefix in urcs:
            got, at = decode_field(tokens, at)
            assert got == prefix, case["why"]
        got, at = decode_field(tokens, at)
        # The line itself, back out of the hex and compared with the case.
        assert got == case["line"], case["why"]
        assert at == len(tokens), case["why"]
        if case["line"] == "":
            empties += 1
    assert empties == 1, (
        "the oracle's empty line case is the one a whitespace protocol cannot "
        "carry, and {} of them were found rather than one".format(empties))

    at = len(ORACLE["classify"])
    for case in ORACLE["due"]:
        tokens = lines[at].split()
        assert tokens[0] == "D", case["why"]
        assert int(tokens[1]) == case["now"], case["why"]
        assert int(tokens[2]) == case["deadline"], case["why"]
        at += 1
    for case in ORACLE["backoff"]:
        tokens = lines[at].split()
        assert tokens[0] == "B", case["why"]
        assert int(tokens[1]) == case["attempt"], case["why"]
        assert int(tokens[2]) == case["base_ms"], case["why"]
        assert int(tokens[3]) == case["cap_ms"], case["why"]
        assert int(tokens[4]) == case["rand"], case["why"]
        at += 1
    assert at == len(lines)

    print(NEWLINE + "  {} requests built with no filter and no compiler, every "
          "line read back out of its hex and compared with the case, the empty "
          "line included".format(len(lines)))


def check_the_filter_against_the_oracle(path, command, label):
    """One process, the three sections in the oracle's order.

    One function for both filters, because the C++ and the Rust speak one
    protocol by design and a second copy of this loop would be two places for
    a comparison to weaken.
    """
    require_artefact(path, command)
    urcs = ORACLE["urcs"]
    lines = all_requests(urcs)
    answers = run_filter(path, lines)
    assert len(answers) == len(lines)

    at = 0
    for case in ORACLE["classify"]:
        want = "class={}".format(case["class"])
        assert answers[at] == want, case["why"]
        at += 1
    for case in ORACLE["due"]:
        want = "due={}".format(1 if case["due"] else 0)
        assert answers[at] == want, case["why"]
        at += 1
    refusals = 0
    for case in ORACLE["backoff"]:
        if case["value"] == py.BACKOFF_REFUSED:
            assert answers[at] == REFUSED, case["why"]
            refusals += 1
        else:
            assert answers[at] == "delay={}".format(case["value"]), case["why"]
        at += 1
    assert at == len(answers)
    assert refusals == 3, (
        "three of the backoff cases are refusals, a base of zero, a cap below "
        "the base and a cap above half the range, and {} were seen"
        .format(refusals))
    print(NEWLINE + "  the {} answered all {} cases, {} classify, {} "
          "deadline and {} backoff of which {} refused"
          .format(label, len(answers), len(ORACLE["classify"]),
                  len(ORACLE["due"]), len(ORACLE["backoff"]), refusals))


def test_the_rust_filter_answers_every_case_as_the_oracle_says():
    check_the_filter_against_the_oracle(
        AT_RUST_FILTER, "cargo build --release --workspace", "Rust")


def test_the_cpp_filter_answers_every_case_as_the_oracle_says():
    check_the_filter_against_the_oracle(AT_CPP_FILTER, BUILD_HOST_CMD, "C++")


def check_an_empty_table_is_an_engine_with_no_table(path, command, label):
    """The same lines with no prefixes, where every unsolicited line is plain.

    The C reaches this through a NULL table and neither of the other two can:
    a Rust slice is never null and the C++ takes a sized vector, so an empty
    table is where that behaviour lives in both. All three have to agree, which
    is what this compares.
    """
    require_artefact(path, command)
    lines = [classify_request(c, []) for c in ORACLE["classify"]]
    answers = run_filter(path, lines)
    assert len(answers) == len(lines)
    moved = 0
    for case, answer in zip(ORACLE["classify"], answers):
        expected = case["class"]
        if expected == "urc":
            expected = "intermediate"
            moved += 1
        assert answer == "class={}".format(expected), case["why"]
    assert moved == sum(1 for c in ORACLE["classify"] if c["class"] == "urc")
    assert moved > 0, (
        "no case in the oracle is unsolicited, so this test compares nothing")
    print(NEWLINE + "  with no table the {} moves {} unsolicited lines to "
          "intermediate and leaves every other class where it was"
          .format(label, moved))


def test_the_rust_filter_reads_an_empty_table_as_an_engine_with_no_table():
    check_an_empty_table_is_an_engine_with_no_table(
        AT_RUST_FILTER, "cargo build --release --workspace", "Rust")


def test_the_cpp_filter_reads_an_empty_table_as_an_engine_with_no_table():
    check_an_empty_table_is_an_engine_with_no_table(AT_CPP_FILTER,
                                                    BUILD_HOST_CMD, "C++")
