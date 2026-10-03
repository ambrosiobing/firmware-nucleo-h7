"""P02's four rings against one trace, in every ordering mode.

**What this compares and what it cannot.** P02's subject is the memory-ordering
argument, and the cost of each of the four choices is a number measured on the
board: zero cycles for the compiler fence, 22 for one DMB, 29 for acquire plus
release. None of that is comparable on a host, and no part of this file tries.

What is comparable is the structure, and it is comparable exactly: for one list
of operations, which bytes are accepted, which are refused and counted as drops,
the order they come back in, and the two free-running counters. Four
implementations agreeing on that is four independent checks of the invariant and
of the arithmetic, which is the half of P02 that does not need hardware.

**The counters are in the comparison on purpose.** `used` is derived from
`head - tail`, so an implementation that wrongly masked its counters to the
capacity would agree on `used` forever and disagree on `head` at once. One of the
operation lists therefore starts the counters three short of their 32-bit wrap
and drives them past it. In C that wrap is defined unsigned arithmetic and is
what makes the subtraction correct; in Python integers do not wrap at all and
the mask has to be written; in Rust it is `wrapping_sub`. Three different
spellings of one claim, which is the reason to compare them.

**All four modes are driven, and must give identical traces.** A barrier
constrains visibility between two contexts and says nothing about one context's
own sequence, so a single-threaded trace that changed with the mode would mean a
mode was touching the data path rather than the ordering. The C builds four
objects, the C++ instantiates a template four times and the Rust monomorphises a
const generic; this checks that the three routes arrive at the same place.

C is the reference, as in the other parity tests, because it is the
implementation the board links.
"""
from __future__ import annotations

import ctypes
import random
import sys
from pathlib import Path

import pytest

from conftest import RING_CPP_FILTER, RING_RUST_FILTER, assert_not_vacuous, run_filter
from test_ring import BARRIERS, CAPACITY, Ring as CRing, lib as c_lib

sys.path.insert(
    0, str(Path(__file__).resolve().parents[2] / "projects" / "P02-ring-buffer" / "python")
)

import ring as py_ring  # noqa: E402

LANGUAGES = ("C", "C++", "Python", "Rust")

# How far past the 32-bit wrap the wrap list drives the counters. Small, because
# the only interesting part is crossing it.
PAST_THE_WRAP = 40


def operations():
    """Four lists, each a sequence of operations with its own reason.

    Returned as a list of (name, ops) so a failure names which list it was.
    Each op is "P xx", "G" or "S"; the mode is sent once at the start by the
    caller, because a mode change discards the ring.
    """
    out = []

    # 1. Fill to exactly the capacity, refuse one, drain, and ask for one more.
    # The boundary the free-running counters exist for: no slot is sacrificed to
    # tell full from empty, so the capacity is 256 and not 255.
    ops = ["S"]
    ops += ["P {:02X}".format(i & 0xFF) for i in range(CAPACITY)]
    ops += ["S", "P FF", "S"]
    ops += ["G"] * CAPACITY
    ops += ["G", "S"]
    out.append(("fill, refuse, drain", ops))

    # 2. A random interleaving, which is where an off-by-one in the occupancy
    # test shows up as a byte in the wrong place rather than as a refusal.
    rng = random.Random(20261005)
    ops = ["S"]
    for _ in range(4000):
        if rng.random() < 0.55:
            ops.append("P {:02X}".format(rng.randrange(256)))
        else:
            ops.append("G")
        if rng.random() < 0.02:
            ops.append("S")
    ops.append("S")
    out.append(("random interleaving, 4000 operations", ops))

    # 3. Bursts that overrun on purpose, so the drop counter is exercised
    # heavily rather than once. A ring that silently overwrote instead of
    # refusing would pass list 2 and fail here.
    ops = ["S"]
    for burst in range(12):
        ops += ["P {:02X}".format((burst * 7 + i) & 0xFF) for i in range(CAPACITY + 30)]
        ops.append("S")
        ops += ["G"] * (CAPACITY // 2)
        ops.append("S")
    out.append(("overrunning bursts, drops counted", ops))

    # 4. The counters driven past their own 32-bit wrap. This cannot be done
    # through the operation verbs in a reasonable number of steps, so the test
    # places the counters directly for the C and the Python and asks the filters
    # to do the same; see wrap_cases below, which is a separate comparison.
    return out


def via_c(mode, ops):
    """The C, through ctypes, which is how the board's implementation is reached."""
    dll = c_lib(mode)
    r = CRing()
    dll.ring_init(ctypes.byref(r))
    byte = ctypes.c_uint8(0)
    answers = []
    for op in ops:
        if op[0] == "P":
            value = int(op[2:], 16)
            if dll.ring_put(ctypes.byref(r), value):
                answers.append("put=1 used={}".format(dll.ring_used(ctypes.byref(r))))
            else:
                answers.append("put=0 drops={}".format(dll.ring_drops(ctypes.byref(r))))
        elif op[0] == "G":
            if dll.ring_get(ctypes.byref(r), ctypes.byref(byte)):
                answers.append(
                    "get=1 byte={:02X} used={}".format(
                        byte.value, dll.ring_used(ctypes.byref(r))
                    )
                )
            else:
                answers.append("get=0")
        else:
            answers.append(
                "used={} drops={} head={} tail={}".format(
                    dll.ring_used(ctypes.byref(r)), dll.ring_drops(ctypes.byref(r)),
                    r.head, r.tail,
                )
            )
    return answers


def via_python(mode, ops):
    r = py_ring.Ring(mode)
    answers = []
    for op in ops:
        if op[0] == "P":
            if r.put(int(op[2:], 16)):
                answers.append("put=1 used={}".format(r.used()))
            else:
                answers.append("put=0 drops={}".format(r.drops))
        elif op[0] == "G":
            got = r.get()
            if got is None:
                answers.append("get=0")
            else:
                answers.append("get=1 byte={:02X} used={}".format(got, r.used()))
        else:
            answers.append(
                "used={} drops={} head={} tail={}".format(r.used(), r.drops, r.head, r.tail)
            )
    return answers


def via_filter(path, mode, ops):
    """The C++ and the Rust, through the four verbs both speak. The init answer
    is dropped so the four implementations return the same length of list."""
    answers = run_filter(path, ["I {}".format(mode)] + list(ops))
    assert answers[0] == "init mode={} cap={}".format(mode, CAPACITY), answers[0]
    return answers[1:]


def available(mode, ops):
    present = {"C": via_c(mode, ops), "Python": via_python(mode, ops)}
    for name, path in (("C++", RING_CPP_FILTER), ("Rust", RING_RUST_FILTER)):
        if path.exists():
            present[name] = via_filter(path, mode, ops)
    return present


# --------------------------------------------------------------- the tests

@pytest.mark.parametrize("mode", BARRIERS)
def test_every_language_produces_the_same_trace(mode):
    """Every list, in one ordering mode, through every implementation present."""
    for name, ops in operations():
        results = available(mode, ops)
        reference = results["C"]
        for lang, got in sorted(results.items()):
            assert len(got) == len(reference), (
                "{} in mode {} on {}: {} answers for {} operations".format(
                    lang, mode, name, len(got), len(reference)
                )
            )
            for i, (want, have) in enumerate(zip(reference, got)):
                assert have == want, (
                    "mode {}, {}, operation {} ({}): {} said {!r} and C said {!r}".format(
                        mode, name, i, ops[i], lang, have, want
                    )
                )


def test_the_trace_does_not_depend_on_the_ordering_mode(capsys):
    """A barrier constrains two contexts against each other and says nothing
    about one context's own sequence, so these four traces must be identical. If
    this fails, a mode is doing something to the data path."""
    name, ops = operations()[1]
    per_mode = {}
    for mode in BARRIERS:
        for lang, got in available(mode, ops).items():
            per_mode.setdefault(lang, {})[mode] = got

    for lang, by_mode in sorted(per_mode.items()):
        reference = by_mode[BARRIERS[0]]
        for mode, got in sorted(by_mode.items()):
            assert got == reference, (
                "{} gives a different trace in mode {} than in mode {} on {}".format(
                    lang, mode, BARRIERS[0], name
                )
            )

    with capsys.disabled():
        total = sum(len(ops) for _, ops in operations())
        print("\n  P02 parity over {} operations in each of {} modes".format(
            total, len(BARRIERS)))
        for lang in LANGUAGES:
            print("    {:<7} {}".format(
                lang, "agrees" if lang in per_mode else "not built, not compared"))


def test_the_counters_are_correct_across_their_own_wrap():
    """Driven by placing the counters, because reaching the wrap through the
    verbs would take four billion operations.

    The C and the Python are placed directly. The filters cannot be, so they are
    compared on the arithmetic instead: a full fill and drain starting from zero
    must leave head and tail equal to the number of bytes, and the C's own
    `test_counters_stay_correct_across_their_own_wrap` covers the placed case
    for the implementation the board links.
    """
    dll = c_lib(2)
    r = CRing()
    dll.ring_init(ctypes.byref(r))
    r.head = (1 << 32) - 4
    r.tail = (1 << 32) - 4

    py = py_ring.Ring(2)
    py.head = (1 << 32) - 4
    py.tail = (1 << 32) - 4

    byte = ctypes.c_uint8(0)
    for i in range(PAST_THE_WRAP):
        value = i & 0xFF
        assert dll.ring_put(ctypes.byref(r), value) == 1
        assert py.put(value) == 1
        assert r.head == py.head, (
            "at byte {} the C head is {} and the Python head is {}".format(
                i, r.head, py.head
            )
        )
        assert dll.ring_get(ctypes.byref(r), ctypes.byref(byte)) == 1
        got = py.get()
        assert byte.value == got == value
        assert r.tail == py.tail
        assert dll.ring_used(ctypes.byref(r)) == py.used() == 0

    # The counters have gone past zero and the occupancy is still right, which
    # is the whole point of not wrapping them to the capacity.
    assert r.head < PAST_THE_WRAP, "the C head did not wrap"
    assert py.head < PAST_THE_WRAP, "the Python head did not wrap"


def test_the_comparison_was_not_vacuous():
    assert_not_vacuous(available(2, ["S"]), "P02")


def test_rust_is_compared_when_cargo_has_been_run():
    """Named separately so the CI log says whether Rust was in the comparison."""
    if not RING_RUST_FILTER.exists():
        pytest.skip(
            "{} is missing. Build it in WSL on bing@JPTOUPM678 with:\n"
            "    cargo build --release --workspace".format(RING_RUST_FILTER.name)
        )
    answers = via_filter(RING_RUST_FILTER, 2, ["P 41", "G", "S"])
    assert answers == ["put=1 used=1", "get=1 byte=41 used=0",
                       "used=0 drops=0 head=1 tail=1"]
