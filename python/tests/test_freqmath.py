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
