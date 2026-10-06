"""The frequency counter's arithmetic, on a host, against a table written by hand.

WHY THIS EXISTS. c/instr/freqcount.c dereferences LPTIM1 and a GPIO port, so
nothing on a host could call it, so until Monday 5 October 2026 the only test its
divide could have had was to flash the board and believe the number. The
arithmetic moved to c/instr/freqmath.c, which reads no register, and this drives
that file.

The split is the same one c/clock/clocktree.c got on Sunday 4 October 2026 and
for the same stated reason: an arithmetic error that only a flash can find is an
arithmetic error nobody will find.

WHAT IS AT RISK AND THEREFORE WHAT IS TESTED. Four things, and the `why` on each
case below says which one it is for:

  - multiply before divide, because the wrong order still produces a plausible
    number and is three digits low at the frequency P06 cares about
  - the 64-bit intermediate, because a 32-bit one wraps into a frequency that is
    not merely wrong but unrelated
  - two refusals at two different thresholds, the sampling ceiling and the
    narrower wrap-poll limit, where either comparison on the wrong side of its
    boundary turns a refusal into a wrong answer
  - the modular counter difference, which is the only correct behaviour for a
    gate that spans a composed wrap

THE TABLE IS THE ORACLE AND IT IS HAND WRITTEN. Every expected value here was
computed by hand from the definition, not recorded from a run. Where a figure is
not obvious the `why` shows the arithmetic, so a reader can disagree with the
table rather than only with the code.
"""
from __future__ import annotations

import ctypes

import pytest

from conftest import BUILD, shared_library_name

LIBRARY = BUILD / shared_library_name("freqmath")

ARR = 0xFFFF          # LPTIM1's full 16 bits, as freqcount.c configures it
TICK = 256            # the sub-second tick rate this board reports
USABLE = 0x10000 * 256  # 16777216 Hz, one full counter per tick


@pytest.fixture(scope="module")
def lib():
    if not LIBRARY.exists():
        pytest.skip("{} is missing. Build it with:\n    "
                    "python python/tools/build_host.py".format(LIBRARY.name))
    h = ctypes.CDLL(str(LIBRARY))
    h.freqmath_compose.restype = ctypes.c_uint32
    h.freqmath_compose.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
    h.freqmath_edges.restype = ctypes.c_uint32
    h.freqmath_edges.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
    h.freqmath_usable_max_hz.restype = ctypes.c_uint32
    h.freqmath_usable_max_hz.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
    h.freqmath_mhz.restype = ctypes.c_uint64
    h.freqmath_mhz.argtypes = [ctypes.c_uint32] * 5
    h.freqmath_ticks_for_ms.restype = ctypes.c_uint32
    h.freqmath_ticks_for_ms.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
    h.freqmath_resolution_mhz.restype = ctypes.c_uint64
    h.freqmath_resolution_mhz.argtypes = [ctypes.c_uint32]
    return h


# --------------------------------------------------------- the frequency itself

MHZ_CASES = [
    dict(name="one kilohertz over a one second gate",
         why="1000 edges in 256 ticks of 256 Hz is one second, so 1000 Hz, which "
             "is 1000000 millihertz. Dividing edges by ticks first gives 3 and "
             "then 768000 millihertz, which is the multiply-before-divide case "
             "and is 23 per cent low rather than obviously broken.",
         edges=1000, tick_hz=TICK, ticks=256, ceiling=0, arr=ARR,
         expect=1_000_000),

    dict(name="the same rate over a quarter second gate",
         why="250 edges in 64 ticks is also 1000 Hz. Two different gates giving "
             "the same answer is what makes this a rate rather than a count.",
         edges=250, tick_hz=TICK, ticks=64, ceiling=0, arr=ARR,
         expect=1_000_000),

    dict(name="an edge count that does not divide the tick count",
         why="1 edge in 256 ticks is 1 Hz exactly, 1000 millihertz, and is the "
             "instrument's own resolution over a one second gate. The point of "
             "the case is that the integer divide happens last: 1 times 256 "
             "times 1000 over 256 is exact, where 1 over 256 first is zero.",
         edges=1, tick_hz=TICK, ticks=256, ceiling=0, arr=ARR,
         expect=1_000),

    dict(name="a fractional frequency truncates and does not round",
         why="1000 edges in 257 ticks of 256 Hz is 1000 over 1.00390625 seconds, "
             "so 996.108 Hz. In millihertz that is 1000*256*1000/257 = "
             "996108.949, which truncates to 996108. Truncation rather than "
             "rounding is the choice and it is recorded here so a change to "
             "rounding shows up as a failure. The first version of this row said "
             "995136186, three orders of magnitude out, and the double entry "
             "check against the definition is what caught it.",
         edges=1000, tick_hz=TICK, ticks=257, ceiling=0, arr=ARR,
         expect=996_108),

    dict(name="the full counter in one tick is at the usable maximum",
         why="65536 edges in 1 tick is 65536*256 = 16777216 Hz, which is exactly "
             "freqmath_usable_max_hz, and the comparison is >= so it refuses. "
             "This is the boundary case and the reason the comparison is not >.",
         edges=0x10000, tick_hz=TICK, ticks=1, ceiling=0, arr=ARR,
         expect=0),

    dict(name="one edge below the usable maximum is allowed",
         why="65535 edges in 1 tick is 16776960 Hz, 256 Hz below the limit, so it "
             "passes. Together with the case above this pins the boundary to the "
             "right side rather than to within a counter's width of it.",
         edges=0xFFFF, tick_hz=TICK, ticks=1, ceiling=0, arr=ARR,
         expect=16_776_960_000),

    dict(name="the sampling ceiling refuses before the usable maximum does",
         why="1000 edges in 1 tick is 256000 Hz. With a ceiling of 256000 the "
             "comparison is >= so it refuses, even though the usable maximum is "
             "sixty times higher. Two thresholds, and the lower one has to win.",
         edges=1000, tick_hz=TICK, ticks=1, ceiling=256_000, arr=ARR,
         expect=0),

    dict(name="one hertz below that ceiling is allowed",
         why="999 edges in 1 tick is 255744 Hz, under a ceiling of 256000, so it "
             "passes. The pair fixes the ceiling comparison the same way.",
         edges=999, tick_hz=TICK, ticks=1, ceiling=256_000, arr=ARR,
         expect=255_744_000),

    dict(name="a zero ceiling means the caller does not know one",
         why="freqcount_ceiling_hz returns 0 when the clock tree could not be "
             "decoded. That must not be read as a ceiling of zero hertz, which "
             "would refuse everything. Same edges as the case above, no ceiling, "
             "so it passes.",
         edges=999, tick_hz=TICK, ticks=1, ceiling=0, arr=ARR,
         expect=255_744_000),

    dict(name="no edges at all is zero and not a refusal",
         why="An idle input gives 0 edges and 0 millihertz, which is the correct "
             "frequency of a line that is not changing. It is indistinguishable "
             "from a refusal and freqmath.h says so; this case records that the "
             "value is 0 rather than something else.",
         edges=0, tick_hz=TICK, ticks=256, ceiling=0, arr=ARR,
         expect=0),

    dict(name="a gate of no known length refuses",
         why="ticks of 0 would divide by zero. It refuses rather than returning "
             "anything, because a frequency over an unknown interval is not a "
             "frequency.",
         edges=1000, tick_hz=TICK, ticks=0, ceiling=0, arr=ARR,
         expect=0),

    dict(name="a tick rate of zero refuses for the same reason",
         why="lseref reports ck_apre_hz as 0 when it could not compute it from "
             "PRER. That has to refuse and not multiply by zero, which would "
             "report 0 millihertz and look like an idle line.",
         edges=1000, tick_hz=0, ticks=256, ceiling=0, arr=ARR,
         expect=0),

    dict(name="the intermediate exceeds 32 bits and must not wrap",
         why="65535 edges times 256 times 1000 is 16776960000, which is about "
             "3.9 times 2^32. In 32-bit arithmetic that wraps to 1517174784 and "
             "then divides, giving a frequency unrelated to the input. This is "
             "the same numbers as the allowed boundary case above, kept as its "
             "own entry because what it is for is different: that one is about "
             "the limit and this one is about the width of the multiply.",
         edges=0xFFFF, tick_hz=TICK, ticks=1, ceiling=0, arr=ARR,
         expect=16_776_960_000),
]


@pytest.mark.parametrize("case", MHZ_CASES, ids=[c["name"] for c in MHZ_CASES])
def test_the_frequency_is_what_the_table_says(lib, case):
    got = lib.freqmath_mhz(case["edges"], case["tick_hz"], case["ticks"],
                           case["ceiling"], case["arr"])
    assert got == case["expect"], case["why"]


# ----------------------------------------------------------- the counter itself

def test_the_composed_counter_puts_the_wraps_above_sixteen_bits(lib):
    assert lib.freqmath_compose(0, 0) == 0
    assert lib.freqmath_compose(0, 0xFFFF) == 0xFFFF
    assert lib.freqmath_compose(1, 0) == 0x10000
    assert lib.freqmath_compose(1, 0x1234) == 0x11234
    assert lib.freqmath_compose(0xFFFF, 0xFFFF) == 0xFFFFFFFF


def test_the_counter_is_masked_rather_than_trusted(lib):
    """A counter wider than 16 bits must not corrupt the wrap count.

    LPTIM1's is 16 bits, so this cannot happen from the hardware. It can happen
    from a caller, and silently moving somebody's count into the wrap field is
    the kind of error that would read as a frequency 65536 times too high.
    """
    assert lib.freqmath_compose(0, 0x1_0000) == 0
    assert lib.freqmath_compose(2, 0xF_0001) == 0x20001


def test_the_edge_difference_is_modular(lib):
    """The one case this function exists for.

    A gate that starts near the top of the composed counter and ends after it
    wraps must give the real difference, not a huge one. 10 edges across the
    wrap: before 0xFFFFFFFB, after 0x00000005.
    """
    assert lib.freqmath_edges(100, 150) == 50
    assert lib.freqmath_edges(0xFFFFFFFB, 0x00000005) == 10
    assert lib.freqmath_edges(0, 0) == 0
    assert lib.freqmath_edges(0, 0xFFFFFFFF) == 0xFFFFFFFF


# ------------------------------------------------------------- the two limits

def test_the_usable_maximum_is_the_counter_width_times_the_tick_rate(lib):
    assert lib.freqmath_usable_max_hz(ARR, TICK) == USABLE == 16_777_216
    assert lib.freqmath_usable_max_hz(0xFF, TICK) == 256 * 256
    assert lib.freqmath_usable_max_hz(ARR, 1) == 0x10000
    assert lib.freqmath_usable_max_hz(0, TICK) == 0, "no autoreload, no limit"
    assert lib.freqmath_usable_max_hz(ARR, 0) == 0, "no tick rate, no limit"


def test_the_usable_maximum_saturates_rather_than_wrapping(lib):
    """An autoreload of 0xFFFFFFFF times any rate leaves 32 bits.

    That is a caller's mistake rather than a configuration this board can hold,
    and saturating says so where wrapping would produce a small number and
    therefore a refusal of everything.
    """
    assert lib.freqmath_usable_max_hz(0xFFFFFFFF, 2) == 0xFFFFFFFF


# -------------------------------------------------------- the gate conversion

def test_a_gate_in_milliseconds_becomes_ticks(lib):
    assert lib.freqmath_ticks_for_ms(1000, TICK) == 256
    assert lib.freqmath_ticks_for_ms(500, TICK) == 128
    assert lib.freqmath_ticks_for_ms(100, TICK) == 25, "25.6 truncates to 25"
    assert lib.freqmath_ticks_for_ms(0, TICK) == 0, "no gate asked for"
    assert lib.freqmath_ticks_for_ms(1000, 0) == 0, "no rate to convert with"


def test_a_gate_shorter_than_one_tick_still_gets_one(lib):
    """Rounding down to zero ticks would divide by zero in the frequency.

    One tick is longer than the caller asked for, and the measurement reports the
    tick count it used, so the caller can see that rather than being misled.
    """
    assert lib.freqmath_ticks_for_ms(1, TICK) == 1
    assert lib.freqmath_ticks_for_ms(3, TICK) == 1, "0.768 ticks becomes 1"
    assert lib.freqmath_ticks_for_ms(4, TICK) == 1, "1.024 ticks truncates to 1"


def test_the_resolution_is_one_edge_in_the_gate(lib):
    assert lib.freqmath_resolution_mhz(1000) == 1000, "1 Hz over a one second gate"
    assert lib.freqmath_resolution_mhz(100) == 10000, "10 Hz over 100 ms"
    assert lib.freqmath_resolution_mhz(0) == 0


def test_the_resolution_meets_the_method_documents_tolerance_at_one_kilohertz(lib):
    """P06's acceptance criterion, asserted rather than described.

    The method document allows 0.1 per cent at 1 kHz, which is 1 Hz. A one second
    gate resolves exactly 1 Hz, so the instrument is at the tolerance and not
    inside it, and that is why P06's chapter says this checks the witness rather
    than replacing it.
    """
    resolution_hz = lib.freqmath_resolution_mhz(1000) / 1000.0
    assert resolution_hz == 1.0
    assert resolution_hz / 1000.0 == pytest.approx(0.001)


# ------------------------------------- the accounting freqcount.c actually uses

# The composition at freqcount.c's call site, since Tuesday 6 October 2026:
# wraps counted from the OPENING sample, so the opening residue composes with a
# wrap count of zero and the closing one with the count the gate accumulated.
# Expected values entered by hand, with the arithmetic written out, and then
# recomputed independently in the test below.
GATE_EDGES = [
    dict(name="no wrap at all, a plain difference",
         why="A megahertz over a twelve tick gate is 46875 edges and the "
             "counter started at zero, so no wrap and the difference is the "
             "count.",
         wraps=0, before=0, after=46_875, edges=46_875),
    dict(name="an idle input over any gate",
         why="Same count at both ends and no wrap is zero edges, which is the "
             "correct reading of a line with nothing on it rather than a "
             "failure.",
         wraps=0, before=60_000, after=60_000, edges=0),
    dict(name="exactly one wrap, back to the same residue",
         why="One match and the same 16-bit value at both ends is 65536 edges, "
             "not zero. The old two-sample scheme got this case right, which is "
             "why it looked plausible.",
         wraps=1, before=60_000, after=60_000, edges=65_536),
    dict(name="THE BORROW: one wrap and a smaller closing residue",
         why="65536 + 1000 - 60000 = 6536. The subtraction borrows one 65536 "
             "out of the wrap field on its own, which is the whole reason the "
             "composition is 32-bit and modular. A signed difference with an "
             "absolute value would give 59000 here.",
         wraps=1, before=60_000, after=1_000, edges=6_536),
    dict(name="one wrap and a larger closing residue",
         why="65536 + 60000 - 1000 = 124536. No borrow, so this is the case "
             "that would still pass if the borrow were broken, and it is here "
             "to sit beside the one above.",
         wraps=1, before=1_000, after=60_000, edges=124_536),
    dict(name="the real borrow at a megahertz over twelve ticks",
         why="46875 edges from a start of 50000 ends at 96875, which is one "
             "wrap and a residue of 31339. 65536 + 31339 - 50000 = 46875, the "
             "same answer as the no-wrap case above from a different pair of "
             "residues. This is the case the board runs.",
         wraps=1, before=50_000, after=31_339, edges=46_875),
    dict(name="fifteen wraps, which the old scheme reported as one",
         why="15 * 65536 = 983040 edges. The two-sample scheme returned 65536 "
             "for this gate, short by fourteen wraps, and that is the defect "
             "this accounting replaces.",
         wraps=15, before=0, after=0, edges=983_040),
    dict(name="three wraps across the top of the counter",
         why="3 * 65536 + 0 - 65535 = 131073. Three matches but only two full "
             "counters of edges plus one, because the gate opened one count "
             "below a wrap and closed on one.",
         wraps=3, before=65_535, after=0, edges=131_073),
    dict(name="a one second gate at the stated usable maximum",
         why="256 wraps is 16777216 edges, which is 65536 times 256 and is the "
             "figure freqmath_usable_max_hz states for a 256 Hz tick. The "
             "residues cancel, so this is the limit case entered as a count "
             "rather than as a rate.",
         wraps=256, before=12_345, after=12_345, edges=16_777_216),
]


@pytest.mark.parametrize("case", GATE_EDGES, ids=lambda c: c["name"])
def test_the_gate_edges_are_composed_the_way_freqcount_composes_them(lib, case):
    """The exact call freqcount.c makes, so the test and the target agree.

    freqcount.c computes:

        freqmath_edges(freqmath_compose(0, before), freqmath_compose(wraps, after))

    and nothing else, so that is what is driven here. A test that recomputed the
    edges in Python and compared would be testing Python.
    """
    got = lib.freqmath_edges(lib.freqmath_compose(0, case["before"]),
                             lib.freqmath_compose(case["wraps"], case["after"]))
    assert got == case["edges"], case["why"]


@pytest.mark.parametrize("case", GATE_EDGES, ids=lambda c: c["name"])
def test_the_hand_written_gate_edges_survive_a_second_derivation(case):
    """Double entry, which has found three errors in this repository's tables.

    The expected column above was written by hand from the arithmetic in each
    `why`. This recomputes it from the definition instead, wraps times the
    counter width plus the difference of the residues, and reads no C at all. A
    disagreement means the table is wrong, which is the failure a table checked
    only against the implementation cannot have.
    """
    derived = case["wraps"] * 0x10000 + case["after"] - case["before"]
    assert derived == case["edges"], (
        "{}: the table says {} and the definition gives {}".format(
            case["name"], case["edges"], derived))
    assert derived >= 0, "a gate cannot contain a negative number of edges"
