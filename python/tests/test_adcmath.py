"""c/instr/adcmath.c against a hand-written table, and that table against itself.

WHAT IS AT RISK IN THIS ARITHMETIC, which is why it is worth a file:

  - THE HALVING COMES FIRST. ST halves the ADC clock and then compares it with
    6.25, 12.5 and 25 MHz. Comparing the unhalved clock selects one setting too
    high at every boundary, which works at one clock and not at the next.
  - THE BOUNDARIES ARE "AT OR BELOW". A clock of exactly 12.5 MHz after halving
    takes setting 1, not 2. Four thresholds, each with a case on both sides.
  - THE DIVISOR SET IS NOT A RANGE. PRESC expresses the even values 2 to 12 and
    then 16, 32, 64, 128 and 256. A range check accepts 14 and 24, which the
    field cannot encode, and produces a clock nothing generates.
  - 0 IS A LEGAL BOOST SETTING, so it cannot also mean refused. Every function
    here reports success separately from its value, which is the opposite of
    freqmath_mhz, where 0 millihertz is a refusal because no reading is 0.

THE TABLE IS THE ORACLE AND IT IS HAND WRITTEN. Every expected value was worked
out from the rule in the `why`, not recorded from a run, and then recomputed a
second way in the tests that read no C. Double entry has found three errors in
this repository's hand tables.
"""
import ctypes

import pytest

# The shared helper rather than a path of this file's own, which is how the
# sibling tests do it and which this file got wrong first: it hard-coded
# "adcmath.so" and the build names it libadcmath.so on anything but Windows, so
# every ctypes case skipped on the one laptop that can build the library. The
# double entry still ran and passed, which is exactly why a skip is worse than a
# failure: 24 passed looked like success.
from conftest import BUILD, shared_library_name

LIBRARY = BUILD / shared_library_name("adcmath")


@pytest.fixture(scope="module")
def lib():
    if not LIBRARY.exists():
        pytest.skip("{} is missing. Build it with:\n    "
                    "python python/tools/build_host.py".format(LIBRARY.name))
    h = ctypes.CDLL(str(LIBRARY))
    h.adcmath_boost.restype = ctypes.c_bool
    h.adcmath_boost.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
    h.adcmath_clock_hz.restype = ctypes.c_bool
    h.adcmath_clock_hz.argtypes = [ctypes.c_uint32, ctypes.c_uint32,
                                   ctypes.POINTER(ctypes.c_uint32)]
    h.adcmath_pcsel_bit.restype = ctypes.c_bool
    h.adcmath_pcsel_bit.argtypes = [ctypes.c_uint32,
                                    ctypes.POINTER(ctypes.c_uint32)]
    return h


# ------------------------------------------------------------------- boost

BOOST_CASES = [
    dict(clk=12_500_000, ok=True, boost=0,
         why="halved is 6.25 MHz exactly, which is AT the first threshold, and "
             "the rule is at-or-below, so setting 0. Comparing the unhalved "
             "12.5 MHz would have given setting 1"),
    dict(clk=12_500_002, ok=True, boost=1,
         why="halved is 6 250 001, one hertz past the threshold, so setting 1. "
             "This case and the one above are the pair that pins the boundary"),
    dict(clk=25_000_000, ok=True, boost=1,
         why="halved is 12.5 MHz exactly, at the second threshold, so still 1"),
    dict(clk=25_000_002, ok=True, boost=2,
         why="halved is 12 500 001, so setting 2"),
    dict(clk=50_000_000, ok=True, boost=2,
         why="halved is 25 MHz exactly, at the third threshold, so still 2"),
    dict(clk=50_000_002, ok=True, boost=3,
         why="halved is 25 000 001, past the last threshold, so setting 3"),
    dict(clk=1_000_000, ok=True, boost=0,
         why="a slow ADC clock wants no boost at all, and 0 is a SETTING here "
             "rather than a refusal, which is why success is reported "
             "separately from the value"),
    dict(clk=0, ok=False, boost=None,
         why="a clock of zero is what a caller gets from a clock tree it could "
             "not decode, and there is no boost setting for it"),
    dict(clk=100_000_001, ok=False, boost=None,
         why="past this file's own 100 MHz ceiling, which ST's function does "
             "not have: it would select 3 and ask nothing. A caller this far "
             "out has an arithmetic error upstream"),
]


@pytest.mark.parametrize("case", BOOST_CASES, ids=lambda c: "clk%d" % c["clk"])
def test_boost(lib, case):
    out = ctypes.c_uint32(0xFFFFFFFF)
    got = lib.adcmath_boost(case["clk"], ctypes.byref(out))
    assert got == case["ok"], case["why"]
    if case["ok"]:
        assert out.value == case["boost"], case["why"]


@pytest.mark.parametrize("case", BOOST_CASES, ids=lambda c: "clk%d" % c["clk"])
def test_the_boost_table_survives_a_second_derivation(case):
    """Double entry: the thresholds as a list walked in order, reading no C.

    The implementation is a chain of else-if comparisons, which is ST's shape.
    This counts how many thresholds the halved clock exceeds, which is the same
    answer by a different route, so a mistake in either the chain's order or the
    table's expected values shows up as a disagreement.
    """
    if not case["ok"]:
        return
    freq = case["clk"] // 2
    derived = sum(1 for t in (6_250_000, 12_500_000, 25_000_000) if freq > t)
    assert derived == case["boost"], (
        "clk {}: halved {}, the table says {} and counting thresholds "
        "exceeded gives {}".format(case["clk"], freq, case["boost"], derived))


# ------------------------------------------------------------------- clock

CLOCK_CASES = [
    dict(src=280_000_000, div=1, ok=True, out=280_000_000,
         why="synchronous mode with CKMODE at divide by one, which is the only "
             "divisor the two fields share"),
    dict(src=140_000_000, div=4, ok=True, out=35_000_000,
         why="divide by four, which CKMODE can express and PRESC also can"),
    dict(src=120_000_000, div=12, ok=True, out=10_000_000,
         why="twelve is the largest of the even asynchronous divisors"),
    dict(src=256_000_000, div=256, ok=True, out=1_000_000,
         why="and 256 is the largest PRESC expresses at all"),
    dict(src=140_000_000, div=14, ok=False, out=None,
         why="fourteen sits between 12 and 16 and PRESC cannot encode it. A "
             "range check would accept it and the resulting clock would be a "
             "number nothing on the part produces"),
    dict(src=140_000_000, div=24, ok=False, out=None,
         why="nor 24, for the same reason, which is why the divisors are a list "
             "and not a range"),
    dict(src=140_000_000, div=3, ok=False, out=None,
         why="and three is not in either field's set"),
    dict(src=140_000_000, div=0, ok=False, out=None,
         why="dividing by nothing"),
    dict(src=0, div=1, ok=False, out=None,
         why="no source clock, which is what an undecoded clock tree gives"),
]


@pytest.mark.parametrize("case", CLOCK_CASES,
                         ids=lambda c: "src%ddiv%d" % (c["src"], c["div"]))
def test_clock_hz(lib, case):
    out = ctypes.c_uint32(0xFFFFFFFF)
    got = lib.adcmath_clock_hz(case["src"], case["div"], ctypes.byref(out))
    assert got == case["ok"], case["why"]
    if case["ok"]:
        assert out.value == case["out"], case["why"]


@pytest.mark.parametrize("case", CLOCK_CASES,
                         ids=lambda c: "src%ddiv%d" % (c["src"], c["div"]))
def test_the_clock_table_survives_a_second_derivation(case):
    """The expressible set written out as literals, against the C's switch."""
    expressible = {1, 2, 4, 6, 8, 10, 12, 16, 32, 64, 128, 256}
    should = case["src"] != 0 and case["div"] in expressible
    assert should == case["ok"], (
        "src {} div {}: the table says ok={} and the expressible set says "
        "{}".format(case["src"], case["div"], case["ok"], should))
    if should:
        assert case["src"] // case["div"] == case["out"], "the division itself"


# ------------------------------------------------------------------- pcsel

PCSEL_CASES = [
    dict(ch=0, ok=True, bit=1,
         why="channel 0 is bit 0"),
    dict(ch=1, ok=True, bit=2,
         why="and the bit is the channel number, not an index into a table"),
    dict(ch=19, ok=True, bit=1 << 19,
         why="nineteen is the highest this file accepts, and it is a "
             "conservative choice pending a datasheet rather than a sourced "
             "limit"),
    dict(ch=20, ok=False, bit=None,
         why="one past that, refused rather than accepted on the chance that "
             "the limit is wrong in the permissive direction"),
    dict(ch=36, ok=False, bit=None,
         why="THE CASE THIS FUNCTION EXISTS FOR. ST's expression masks with "
             "0x1F, so 36 would become channel 4 and the converter would "
             "preselect the wrong input with nothing in any register to show "
             "it. Refusing is the only behaviour that surfaces the mistake"),
]


@pytest.mark.parametrize("case", PCSEL_CASES, ids=lambda c: "ch%d" % c["ch"])
def test_pcsel_bit(lib, case):
    out = ctypes.c_uint32(0xFFFFFFFF)
    got = lib.adcmath_pcsel_bit(case["ch"], ctypes.byref(out))
    assert got == case["ok"], case["why"]
    if case["ok"]:
        assert out.value == case["bit"], case["why"]


@pytest.mark.parametrize("case", PCSEL_CASES, ids=lambda c: "ch%d" % c["ch"])
def test_the_pcsel_table_survives_a_second_derivation(case):
    """Double entry for PCSEL, added after a control showed it was missing.

    The boost and clock tables each had a second derivation; this one did not,
    so its expected values were checked only by the ctypes test, which skips on
    the laptop with no compiler. A deliberate mutation of `ch=1, bit=2` to
    `bit=4` passed there, which is the gap a control exists to find. The bit is
    the channel number itself, so two over the range check is the whole rule and
    it is recomputed here without reading any C.
    """
    expected_ok = case["ch"] <= 19
    assert expected_ok == case["ok"], (
        "ch {}: the table says ok={} and the range rule says {}".format(
            case["ch"], case["ok"], expected_ok))
    if expected_ok:
        assert 2 ** case["ch"] == case["bit"], (
            "ch {}: the table says bit {} and two to that power is {}".format(
                case["ch"], case["bit"], 2 ** case["ch"]))


def test_the_mask_would_have_hidden_the_out_of_range_channel():
    """The reason the range check is not redundant, stated as arithmetic.

    This reads no C. It shows that ST's mask maps 36 onto 4, so a function
    relying on the mask alone would select a real channel for an impossible
    request, which is the failure the refusal prevents.
    """
    assert (36 & 0x1F) == 4
    assert (32 & 0x1F) == 0, "and 32 would look like channel 0"
    assert (20 & 0x1F) == 20, "while 20 masks to itself, so the mask alone " \
                              "cannot be the check"
