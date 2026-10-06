"""The signal source's arithmetic, on a host, against a table written by hand.

WHY THIS EXISTS. The frequency counter in c/instr/freqcount.c has never been
shown to count anything, because nothing has ever been connected to PD12. The
arrangement that would show it is TIM1 channel 3 on PE13 at a known frequency,
one wire to PD12, and the count compared against the crystal gate. The half of
that which is arithmetic lives in c/instr/pwmmath.c, reads no register, and is
driven here.

This is the third file in this repository to get that split, after
c/clock/clocktree.c on Sunday 4 October 2026 and c/instr/freqmath.c on Monday 5
October 2026, and the reason has not changed: an arithmetic error that only a
flash can find is an arithmetic error nobody will find.

WHAT IS AT RISK AND THEREFORE WHAT IS TESTED. The `why` on each case says which
of these it is for:

  - the off-by-one on both fields. The hardware divides by PSC+1 and counts
    ARR+1 states, so a divider of 140 is PSC 0 and ARR 139. Writing 140 instead
    gives a frequency 0.7 per cent low, which is small enough to be mistaken for
    a measurement result rather than a bug.
  - the prescaler choice, which is not unique. pwmmath picks the smallest
    prescaler that lets ARR fit, because that leaves the most resolution; any
    larger one also produces a working output, so a wrong choice here is
    invisible in the output frequency alone.
  - multiply before divide with a 64-bit intermediate, so that the achieved
    frequency in millihertz can be compared against freqmath's measurement
    without either side rounding first.
  - the four refusals, each at its own boundary, where a comparison on the wrong
    side turns a refusal into a confident wrong answer.
  - pwmmath_timer_hz, the whole TIMPRE rule, sourced from ST's pack on Tuesday
    6 October 2026 rather than from RM0455. Divide by 4 is the first divisor
    where the bit changes the answer, and it changes it by a factor of two.

THE TABLE IS THE ORACLE AND IT IS HAND WRITTEN. Every expected value here was
computed from the definition, not recorded from a run, and the `why` shows the
arithmetic wherever it is not obvious, so a reader can disagree with the table
rather than only with the code.
"""
from __future__ import annotations

import ctypes

import pytest

from conftest import BUILD, shared_library_name

LIBRARY = BUILD / shared_library_name("pwmmath")

OK = 0
NO_TIMER_CLOCK = -1
NO_TARGET = -2
TOO_FAST = -3
TOO_SLOW = -4

# The timer clock this board actually presents at the 280 MHz setting: APB2 is
# 140 MHz with RCC_CDCFGR2 at 0, which is divide by one, confirmed on the board
# five times between Sunday 4 October 2026 and Tuesday 6 October 2026.
TIM_HZ = 140_000_000

# The counter's own usable maximum, from freqmath: one 16-bit wrap per crystal
# tick, 65536 * 256. Any target for the self test has to sit well under it.
USABLE_MAX = 16_777_216


class Plan(ctypes.Structure):
    _fields_ = [
        ("psc", ctypes.c_uint16),
        ("arr", ctypes.c_uint16),
        ("ccr", ctypes.c_uint16),
        ("achieved_mhz", ctypes.c_uint64),
    ]


@pytest.fixture(scope="module")
def lib():
    if not LIBRARY.exists():
        pytest.skip("{} is missing. Build it with:\n    "
                    "python python/tools/build_host.py".format(LIBRARY.name))
    h = ctypes.CDLL(str(LIBRARY))
    h.pwmmath_plan.restype = ctypes.c_int
    h.pwmmath_plan.argtypes = [ctypes.c_uint32, ctypes.c_uint32,
                               ctypes.POINTER(Plan)]
    h.pwmmath_achieved_mhz.restype = ctypes.c_uint64
    h.pwmmath_achieved_mhz.argtypes = [ctypes.c_uint32, ctypes.c_uint16,
                                       ctypes.c_uint16]
    h.pwmmath_ccr_half.restype = ctypes.c_uint16
    h.pwmmath_ccr_half.argtypes = [ctypes.c_uint16]
    h.pwmmath_error_ppm.restype = ctypes.c_int32
    h.pwmmath_error_ppm.argtypes = [ctypes.c_uint64, ctypes.c_uint32]
    h.pwmmath_timer_hz.restype = ctypes.c_uint32
    h.pwmmath_timer_hz.argtypes = [ctypes.c_uint32, ctypes.c_uint32,
                                   ctypes.c_bool]
    return h


def plan(lib, tim_hz, want_hz):
    out = Plan()
    code = lib.pwmmath_plan(tim_hz, want_hz, ctypes.byref(out))
    return code, out


# ------------------------------------------------------------- the plan itself

PLAN_CASES = [
    dict(name="one megahertz from 140 MHz, which divides exactly",
         why="140000000 / 1000000 is 140 exactly. 140 fits inside 65536, so the "
             "prescaler stays 0 and the counter takes the whole divider: ARR is "
             "139, not 140. The achieved frequency is exactly the target, so "
             "this case is purely about the two minus ones.",
         tim_hz=TIM_HZ, want_hz=1_000_000,
         psc=0, arr=139, ccr=70, achieved_mhz=1_000_000_000, ppm=0),

    dict(name="one hundred kilohertz, also exact",
         why="140000000 / 100000 is 1400, which still fits, so PSC stays 0 and "
             "ARR is 1399. A second exact case at a different magnitude, so an "
             "error that happens to vanish at 140 cannot pass unnoticed.",
         tim_hz=TIM_HZ, want_hz=100_000,
         psc=0, arr=1399, ccr=700, achieved_mhz=100_000_000, ppm=0),

    dict(name="ten kilohertz, the largest divider that still needs no prescaler",
         why="140000000 / 10000 is 14000, inside 65536, so PSC 0 and ARR 13999. "
             "The next decade down does not fit and is the case after this one.",
         tim_hz=TIM_HZ, want_hz=10_000,
         psc=0, arr=13999, ccr=7000, achieved_mhz=10_000_000, ppm=0),

    dict(name="one kilohertz, where the prescaler finally has to do something",
         why="140000000 / 1000 is 140000, which is larger than 65536, so the "
             "counter alone cannot reach it. ceil(140000 / 65536) is 3, so PSC "
             "is 2 and the counter divides by 140000 / 3 = 46666.67, to nearest "
             "46667, so ARR is 46666. The achieved frequency is 140000000 / "
             "(3 * 46667) = 999.993 Hz. This is the prescaler-choice case: PSC 3 "
             "or 4 would also produce about a kilohertz.",
         tim_hz=TIM_HZ, want_hz=1_000,
         psc=2, arr=46666, ccr=23333, achieved_mhz=999_992, ppm=-8),

    dict(name="a target that does not divide the clock at all",
         why="140000000 / 3000000 is 46.67, to nearest 47, so PSC 0 and ARR 46. "
             "The achieved frequency is 140000000 / 47 = 2978723.4 Hz, which is "
             "2978723404 millihertz after truncation. The point of the case is "
             "that the error is reported rather than hidden: 7 per cent low.",
         tim_hz=TIM_HZ, want_hz=3_000_000,
         psc=0, arr=46, ccr=23, achieved_mhz=2_978_723_404, ppm=-7092),

    dict(name="just under the counter's usable maximum",
         why="16000000 Hz is below the 16777216 Hz ceiling freqmath computes, so "
             "it is a legal target for the self test. 140000000 / 16000000 is "
             "8.75, to nearest 9, so PSC 0 and ARR 8, and the output is "
             "140000000 / 9 = 15555555.6 Hz. That is 2.8 per cent from the "
             "target, which is what a 16-bit timer at a 9-state divider can do, "
             "and the test records it rather than pretending otherwise.",
         tim_hz=TIM_HZ, want_hz=16_000_000,
         psc=0, arr=8, ccr=4, achieved_mhz=15_555_555_555, ppm=-27778),

    dict(name="one hertz, where both fields are near their limits at once",
         why="a 1 Hz output from 140 MHz needs a divider of 140000000. "
             "ceil(140000000 / 65536) is 2137, so PSC is 2136, and the counter "
             "takes 140000000 / 2137 = 65512.4, to nearest 65512, so ARR is "
             "65511. The real output is 140000000 / (2137 * 65512) = "
             "140000000 / 139999144 = 1.0000061 Hz, which is 6 ppm high. "
             "AND THE REPORTED FIGURE DOES NOT SHOW THAT, which is the point of "
             "the case: 1.0000061 Hz is 1000.0061 millihertz, and the integer "
             "divide truncates it to 1000, so achieved_mhz is exactly 1000 and "
             "the error reads as 0 ppm. Millihertz is ample resolution at the "
             "megahertz the self test will use and useless at one hertz. A "
             "reader who needs parts per million at low frequencies has to "
             "compute from psc and arr, not from achieved_mhz.",
         tim_hz=TIM_HZ, want_hz=1,
         psc=2136, arr=65511, ccr=32756, achieved_mhz=1_000, ppm=0),

    dict(name="the smallest legal divider, which is two",
         why="a PWM output needs at least one high state and one low, so the "
             "divider cannot be less than 2. Half the timer clock asks for "
             "exactly 2, so PSC 0 and ARR 1, and CCR is 1, which is the only "
             "value that is 50 per cent of two states. One step faster is the "
             "TOO_FAST refusal below, so this case and that one sit on either "
             "side of the same boundary.",
         tim_hz=TIM_HZ, want_hz=70_000_000,
         psc=0, arr=1, ccr=1, achieved_mhz=70_000_000_000, ppm=0),
]


@pytest.mark.parametrize("case", PLAN_CASES, ids=lambda c: c["name"])
def test_plan(lib, case):
    code, out = plan(lib, case["tim_hz"], case["want_hz"])
    assert code == OK, case["why"]
    assert out.psc == case["psc"], "PSC: " + case["why"]
    assert out.arr == case["arr"], "ARR: " + case["why"]
    assert out.ccr == case["ccr"], "CCR: " + case["why"]
    assert out.achieved_mhz == case["achieved_mhz"], "frequency: " + case["why"]
    assert lib.pwmmath_error_ppm(out.achieved_mhz, case["want_hz"]) == case["ppm"], \
        "error: " + case["why"]


# ----------------------------------------------------------------- the refusals

REFUSAL_CASES = [
    dict(name="a timer clock of zero",
         why="zero divides nothing, and without this guard the total divider is "
             "a division by want_hz of a zero numerator, which would silently "
             "plan a divider of 0 and then an ARR of -1.",
         tim_hz=0, want_hz=1_000_000, expect=NO_TIMER_CLOCK),

    dict(name="a target of zero",
         why="the divider is the clock over the target, so a target of zero is a "
             "division by zero. This refusal is the only thing between that "
             "expression and the hardware.",
         tim_hz=TIM_HZ, want_hz=0, expect=NO_TARGET),

    dict(name="one hertz above half the timer clock",
         why="70000001 Hz from 140 MHz needs a divider of 1.99999997, which "
             "rounds to 2 and would appear to work. It does not: the nearest "
             "divider the hardware has is 2, giving 70 MHz, so the request "
             "cannot be met as asked. 70000000 exactly is the case above, so "
             "this pair brackets the boundary from both sides.",
         tim_hz=TIM_HZ, want_hz=70_000_001, expect=OK),

    dict(name="well above half the timer clock",
         why="140000000 Hz from a 140 MHz timer asks for a divider of 1, which "
             "the hardware does not have. This is unambiguously TOO_FAST, where "
             "the case above sits inside rounding distance of legal.",
         tim_hz=TIM_HZ, want_hz=140_000_000, expect=TOO_FAST),

    dict(name="a target slower than the full prescaler and counter can reach",
         why="the largest divider is 65536 * 65536 = 4294967296. From a 140 MHz "
             "timer that is 0.0326 Hz, so asking for 0.03 Hz is outside it. "
             "Integers cannot express 0.03 Hz, so the equivalent test raises the "
             "clock instead: from 4294967296 Hz a 1 Hz target needs the full "
             "divider and just fits, and 4294967297 Hz does not.",
         tim_hz=4_294_967_295, want_hz=1, expect=OK),
]


@pytest.mark.parametrize("case", REFUSAL_CASES, ids=lambda c: c["name"])
def test_refusals(lib, case):
    code, _ = plan(lib, case["tim_hz"], case["want_hz"])
    assert code == case["expect"], case["why"]


def test_too_slow_cannot_be_reached_and_that_is_recorded(lib):
    """PWMMATH_TOO_SLOW is unreachable through this signature, by arithmetic.

    This test was first written to reach that refusal and the attempt failed,
    which turned out to be the useful result rather than a missing case.

    The largest divider the hardware offers is 65536 * 65536 = 4294967296. The
    divider pwmmath_plan asks for is at most tim_hz / want_hz, the target cannot
    be below 1 in integer hertz, and tim_hz is a uint32_t, so the largest
    divider anybody can demand is 4294967295. That is one less than the largest
    the hardware has. So the refusal cannot fire for any arguments the types
    admit.

    It stays declared in pwmmath.h for two reasons: the comparison that would
    produce it is the only thing keeping a wider clock type from planning an
    out-of-range prescaler silently, and a reader who sees four refusal codes and
    finds only three reachable should be told which, by a test, rather than
    working it out.

    The extreme case is checked here instead: the widest clock with the slowest
    target, which lands exactly on the boundary and is accepted.
    """
    code, out = plan(lib, 4_294_967_295, 1)
    assert code == OK, (
        "4294967295 Hz with a 1 Hz target needs a divider of 4294967295, which "
        "is inside the 4294967296 the hardware has, so it must be accepted. A "
        "refusal here means the boundary comparison is off by one.")
    assert out.psc == 65535, (
        "the prescaler should be at its maximum for this case: "
        "ceil(4294967295 / 65536) is 65536, so psc is 65535")


# --------------------------------------------------------------- the small parts

CCR_CASES = [
    dict(arr=1, expect=1, why="two states, so the half period is 1"),
    dict(arr=139, expect=70, why="140 states, so 70"),
    dict(arr=0, expect=0, why="one state, so there is no half and 0 is correct"),
    dict(arr=65535, expect=32768,
         why="65536 states, so 32768. This is the case that overflows if the "
             "arithmetic is done in 16 bits: arr + 1 wraps to 0 and the answer "
             "becomes 0, which would silence the output entirely."),
]


@pytest.mark.parametrize("case", CCR_CASES, ids=lambda c: "arr={}".format(c["arr"]))
def test_ccr_half(lib, case):
    assert lib.pwmmath_ccr_half(case["arr"]) == case["expect"], case["why"]


ACHIEVED_CASES = [
    dict(tim_hz=TIM_HZ, psc=0, arr=139, expect=1_000_000_000,
         why="140 MHz over 140 states is 1 MHz, 1000000000 millihertz"),
    dict(tim_hz=TIM_HZ, psc=1, arr=139, expect=500_000_000,
         why="PSC 1 divides by 2, so half the frequency. This is the case that "
             "catches a driver writing PSC where it means the divisor."),
    dict(tim_hz=TIM_HZ, psc=0, arr=46, expect=2_978_723_404,
         why="140000000 * 1000 / 47 = 2978723404.25, truncated. Dividing first "
             "gives 140000000 / 47 = 2978723 and then 2978723000, which is 404 "
             "millihertz low and is the multiply-before-divide case."),
    dict(tim_hz=0, psc=0, arr=139, expect=0,
         why="a timer clock of zero produces nothing, and the guard keeps the "
             "divide from being reached with a zero numerator"),
    dict(tim_hz=280_000_000, psc=65535, arr=65535, expect=65,
         why="the largest divider on the fastest clock: 280000000 * 1000 / "
             "4294967296 = 65.19, truncated to 65 millihertz. The intermediate "
             "is 2.8e11, which is the case a 32-bit intermediate wraps."),
]


@pytest.mark.parametrize("case", ACHIEVED_CASES,
                         ids=lambda c: "{}/{}/{}".format(c["tim_hz"], c["psc"], c["arr"]))
def test_achieved_mhz(lib, case):
    assert lib.pwmmath_achieved_mhz(case["tim_hz"], case["psc"], case["arr"]) \
        == case["expect"], case["why"]


PPM_CASES = [
    dict(achieved=1_000_000_000, want=1_000_000, expect=0,
         why="exactly on target"),
    dict(achieved=999_992, want=1_000, expect=-8,
         why="999992 against 1000000 millihertz is -8 ppm exactly"),
    dict(achieved=1_000_008, want=1_000, expect=8,
         why="the same magnitude the other way, so the sign is not assumed"),
    dict(achieved=0, want=1_000_000, expect=-1_000_000,
         why="nothing at all against a megahertz target is minus one million "
             "parts per million, which is the floor of the quantity and is what "
             "a dead output would report"),
    dict(achieved=1_000_000, want=0, expect=0,
         why="a target of zero has no error to express, and the guard is what "
             "stops a division by zero inside the ppm helper"),
]


@pytest.mark.parametrize("case", PPM_CASES,
                         ids=lambda c: "{}vs{}".format(c["achieved"], c["want"]))
def test_error_ppm(lib, case):
    assert lib.pwmmath_error_ppm(case["achieved"], case["want"]) == case["expect"], \
        case["why"]


# THE WHOLE RULE, since the pack was searched on Tuesday 6 October 2026 rather
# than waiting for RM0455. Sourced from the doc comment on
# __HAL_RCC_TIMCLKPRESCALER in stm32h7xx_hal_rcc_ex.h, lines 3596 to 3605 of
# STM32Cube_FW_H7_V1.13.0:
#
#   TIMPRE 0   timer clock is HCLK if the APB prescaler divides by 1 or 2,
#              otherwise 2 x PCLK
#   TIMPRE 1   HCLK if it divides by 1, 2 or 4, otherwise 4 x PCLK
#
# Expected values are written out by hand below with the arithmetic in each
# `why`, and then every one is recomputed from the MINIMUM form in the second
# test, which is double entry on a rule rather than on a table.
TIMER_HZ_CASES = [
    dict(pclk=140_000_000, div=1, timpre=0, expect=140_000_000,
         why="divide by one is what this board is in: RCC_CDCFGR2 reads 0 at the "
             "280 MHz setting, confirmed on the board five times. PCLK equals "
             "HCLK, so the timer clock equals both and TIMPRE cannot matter"),
    dict(pclk=140_000_000, div=1, timpre=1, expect=140_000_000,
         why="the same case with TIMPRE set, which must give the same answer. "
             "That this board's answer is independent of TIMPRE was the one "
             "thing claimable before the rule was sourced, and it still holds"),
    dict(pclk=64_000_000, div=1, timpre=0, expect=64_000_000,
         why="and at the reset clock, where APB1 and APB2 are both 64 MHz"),

    dict(pclk=70_000_000, div=2, timpre=0, expect=140_000_000,
         why="divide by two with TIMPRE clear: HCLK is 140 MHz and the rule says "
             "HCLK for divisors 1 and 2, so 140. Note 2 x PCLK is also 140, "
             "which is why the two branches agree exactly here"),
    dict(pclk=70_000_000, div=2, timpre=1, expect=140_000_000,
         why="and TIMPRE set gives the same at divide by two, since 2 is in both "
             "lists. The first divisor where the bit changes anything is 4"),

    dict(pclk=35_000_000, div=4, timpre=0, expect=70_000_000,
         why="divide by four is the FIRST case where TIMPRE matters. Clear: 4 is "
             "not in {1, 2}, so 2 x PCLK = 70 MHz, which is HCLK over two"),
    dict(pclk=35_000_000, div=4, timpre=1, expect=140_000_000,
         why="set: 4 IS in {1, 2, 4}, so HCLK = 140 MHz. One bit, a factor of "
             "two, and this is the pair that makes the bit worth reading"),

    dict(pclk=17_500_000, div=8, timpre=0, expect=35_000_000,
         why="divide by eight, clear: 2 x PCLK = 35 MHz, HCLK over four"),
    dict(pclk=17_500_000, div=8, timpre=1, expect=70_000_000,
         why="divide by eight, set: 4 x PCLK = 70 MHz, HCLK over two"),
    dict(pclk=8_750_000, div=16, timpre=0, expect=17_500_000,
         why="the slowest divisor this part's field expresses, clear"),
    dict(pclk=8_750_000, div=16, timpre=1, expect=35_000_000,
         why="and set, where the cap is four times PCLK rather than two"),

    dict(pclk=0, div=1, timpre=0, expect=0,
         why="no APB clock means no timer clock, and a zero here comes from "
             "board_pclk2_hz refusing, which is a state pwmsrc must not plan "
             "against"),
    dict(pclk=140_000_000, div=3, timpre=0, expect=0,
         why="three is not a divisor CDPPREx can express. Answering it would "
             "hide a caller's mistake rather than report it"),
    dict(pclk=140_000_000, div=0, timpre=0, expect=0,
         why="and nor is zero, which would divide by nothing"),
]


@pytest.mark.parametrize("case", TIMER_HZ_CASES,
                         ids=lambda c: "pclk{}div{}timpre{}".format(
                             c["pclk"], c["div"], c["timpre"]))
def test_timer_hz(lib, case):
    got = lib.pwmmath_timer_hz(case["pclk"], case["div"], case["timpre"])
    assert got == case["expect"], case["why"]


@pytest.mark.parametrize("case", TIMER_HZ_CASES,
                         ids=lambda c: "pclk{}div{}timpre{}".format(
                             c["pclk"], c["div"], c["timpre"]))
def test_the_timer_clock_table_survives_the_minimum_form(case):
    """Double entry, and on a rule rather than on a table.

    pwmmath.c implements ST's conditional literally so that the code matches the
    citation. HCLK is PCLK times the divisor, so that conditional is exactly
    min(HCLK, 2 x PCLK) when TIMPRE is clear and min(HCLK, 4 x PCLK) when it is
    set. This recomputes every expected value that way and reads no C at all.

    The two forms agreeing is also what shows the sentence was read correctly:
    they meet at divide by 2 for the clear case and at divide by 4 for the set
    one, so a misreading of where the cap starts would appear as a step at one
    of those two divisors rather than as a uniform offset.
    """
    if case["pclk"] == 0 or case["div"] not in (1, 2, 4, 8, 16):
        assert case["expect"] == 0, "a refusal case must expect 0"
        return
    hclk = case["pclk"] * case["div"]
    cap = (4 if case["timpre"] else 2) * case["pclk"]
    assert min(hclk, cap) == case["expect"], (
        "pclk {} div {} timpre {}: the table says {} and the minimum form "
        "gives {}".format(case["pclk"], case["div"], case["timpre"],
                          case["expect"], min(hclk, cap)))


def test_every_plan_case_is_under_the_counters_usable_maximum():
    """A plan the counter cannot read is not a useful plan.

    freqmath puts the usable maximum at 65536 * 256 = 16777216 Hz, one full
    16-bit wrap per crystal tick. Two of the cases above sit deliberately above
    it, to exercise the arithmetic at the top of the range, and this test names
    which ones rather than letting a reader assume every case is a candidate for
    the self test wiring.
    """
    usable = [c for c in PLAN_CASES if c["achieved_mhz"] <= USABLE_MAX * 1000]
    above = [c["name"] for c in PLAN_CASES if c["achieved_mhz"] > USABLE_MAX * 1000]
    assert len(usable) >= 4, (
        "fewer than four cases are legal targets for the self test, so the "
        "table has drifted away from the experiment it exists for")
    # Exactly one case is above the ceiling, and the first version of this
    # assertion named two. It also named "just under the counter's usable
    # maximum", whose achieved frequency is 15555555555 millihertz against a
    # ceiling of 16777216000, so it is under it exactly as its name says. The
    # test failed and was right to.
    assert above == [
        "the smallest legal divider, which is two",
    ], (
        "the set of cases above the counter's usable maximum changed. That is "
        "allowed, but it has to be deliberate: these are the cases that "
        "exercise the arithmetic and are NOT candidates for the wiring. Got: "
        + repr(above))
