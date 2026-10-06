"""P01's clock tree decode in Python, held to the same oracle as the C.

WHAT THIS IS FOR, AND IT IS NOT TO RUN ON THE PART. MicroPython on this board can
blink an LED and print a banner and it cannot decode this clock tree from RCC,
which the README beside this file says plainly. This module is the host half, and
it is the one part of P01 where Python is at no disadvantage whatever: the decode
is a pure function of seven integers, it has no deadline, and it touches no
register.

WHERE PYTHON HAS TO BE MADE TO BEHAVE, which is the interesting part of writing
this one. Python's integers are unbounded and its division floors, and both of
those are wrong here:

  - EVERY FREQUENCY IS A 32-BIT UNSIGNED DIVISION. `//` on non-negative integers
    is the same as C's `/`, so the frequencies come out right by accident rather
    than by care. What does NOT come out right by accident is the overflow guard:
    C refuses when `ref/m * n` would exceed 32 bits, and Python would simply
    return the larger number. The guard is therefore an explicit comparison here
    and not a consequence of the arithmetic, and ../clock_vectors.json has a row
    that reaches it.

  - THE PARTS PER MILLION ARE SIGNED AND HALVES GO AWAY FROM ZERO. `-7 // 2` is
    -4 in Python and -3 in C, so a floor division here would differ from the
    other three implementations on every negative row in the oracle. `ppm` below
    divides magnitudes and reapplies the sign, which is what C and Rust do by
    truncating toward zero.

  - THE SATURATION IS NOT FREE EITHER. A signed 32-bit result has no meaning to
    Python, so the clamp is explicit.

Three things that would each have passed a casual test and differed from the C.
"""
from __future__ import annotations

from typing import Dict, NamedTuple, Tuple

# The field positions, written out here rather than imported from anywhere.
#
# That duplication is the point of a second implementation. A version that read
# the C's header could not disagree with it about where a field sits, so it could
# not catch the error it exists to catch: that DIVM1 holds the value while N1 and
# P1 hold the value minus one. The authority for every number below is the same
# as the C's and is named at each define in c/board/stm32h7a3_regs.h, ST's CMSIS
# device header for this die.
CR_HSIDIV_MSK = 3 << 3
CR_HSIDIV_POS = 3
CR_HSEBYP_MSK = 1 << 18
CR_HSERDY_MSK = 1 << 17

CFGR_SWS_MSK = 7 << 3
CFGR_SWS_POS = 3
SWS_HSI = 0
SWS_HSE = 2
SWS_PLL1 = 3

PLLCKSELR_PLLSRC_MSK = 3 << 0
PLLSRC_HSE = 2
PLLCKSELR_DIVM1_MSK = 0x3F << 4
PLLCKSELR_DIVM1_POS = 4

PLLCFGR_PLL1FRACEN_MSK = 1 << 0
PLLCFGR_DIVP1EN_MSK = 1 << 16

PLL1DIVR_N1_MSK = 0x1FF << 0
PLL1DIVR_N1_POS = 0
PLL1DIVR_P1_MSK = 0x7F << 9
PLL1DIVR_P1_POS = 9

CDCFGR1_HPRE_MSK = 0xF << 0
CDCFGR1_HPRE_POS = 0
CDCFGR1_CDCPRE_MSK = 0xF << 8
CDCFGR1_CDCPRE_POS = 8
CDCFGR2_CDPPRE1_MSK = 7 << 4
CDCFGR2_CDPPRE1_POS = 4
# CDPPRE2, bits 10:8 of the same word, written out rather than derived from
# CDPPRE1 so this implementation can be wrong about it independently.
CDCFGR2_CDPPRE2_MSK = 7 << 8
CDCFGR2_CDPPRE2_POS = 8

# Three of sixteen encodings are sourced and the rest deliberately are not,
# because the remaining ratios are not evenly spaced and this volume has never
# written one. A value outside the table is a refusal and not a guess.
AHB_CPU_DIVIDER: Dict[int, int] = {0x0: 1, 0x8: 2, 0x9: 4}
APB_DIVIDER: Dict[int, int] = {0x0: 1, 0x4: 2, 0x5: 4}

UINT32_MAX = 0xFFFFFFFF
INT32_MIN = -2147483648
INT32_MAX = 2147483647

# The nine tokens, identical to clocktree_refusal_text in c/clock/clocktree.c and
# to refusal_text in ../cpp/clocktree.hpp. They are compared by the parity test,
# so two implementations cannot agree that a configuration is unusable while
# disagreeing about which guard caught it.
OK = "ok"
SWS_UNKNOWN = "sws-unknown"
HSE_NOT_BYPASS = "hse-not-bypass"
PLL_FRACTIONAL = "pll-fractional"
PLL_P_DISABLED = "pll-p-disabled"
PLL_SOURCE_UNKNOWN = "pll-source-unknown"
PLL_ZERO_DIVIDER = "pll-zero-divider"
PLL_WOULD_OVERFLOW = "pll-would-overflow"
PRESCALER_UNDECODED = "prescaler-undecoded"


class Regs(NamedTuple):
    """The seven words the decode reads, in the order clocktree_regs_t declares."""

    cr: int
    cfgr: int
    pllckselr: int
    pllcfgr: int
    pll1divr: int
    cdcfgr1: int
    cdcfgr2: int


class Tree(NamedTuple):
    sys_hz: int
    core_hz: int
    ahb_hz: int
    pclk1_hz: int
    pclk2_hz: int
    refusal: str


class Bias(NamedTuple):
    ok: bool
    frequency_ppm: int
    duration_ppm: int
    delay_ppm: int


def _hsi_hz(r: Regs, nominal: int) -> int:
    """HSIDIV is two bits and the divisor is 1 << field, so this is an exact
    shift rather than a rounded division."""
    return nominal >> ((r.cr & CR_HSIDIV_MSK) >> CR_HSIDIV_POS)


def _hse_hz(r: Regs, bypass: int) -> int:
    """A board fact and not a register fact. With the bypass bit clear the part
    is running from an oscillator this repository knows nothing about, and
    returning 8 MHz then would assert a board fact that does not apply."""
    if not (r.cr & CR_HSEBYP_MSK) or not (r.cr & CR_HSERDY_MSK):
        return 0
    return bypass


def _pll1_p_hz(r: Regs, hsi_nominal: int, hse_bypass: int) -> Tuple[int, str]:
    """PLL1's P output, or a reason.

    The reference is divided by M first and multiplied by N second, so an M that
    does not divide exactly loses the remainder before the multiply. The oracle
    carries the M of 3 case because the opposite order is just as plausible to
    write and gives 373333333 where this gives 373333240.
    """
    if r.pllcfgr & PLLCFGR_PLL1FRACEN_MSK:
        return 0, PLL_FRACTIONAL
    if not (r.pllcfgr & PLLCFGR_DIVP1EN_MSK):
        return 0, PLL_P_DISABLED

    src = r.pllckselr & PLLCKSELR_PLLSRC_MSK
    if src == 0:
        ref = _hsi_hz(r, hsi_nominal)
    elif src == PLLSRC_HSE:
        ref = _hse_hz(r, hse_bypass)
        if ref == 0:
            return 0, HSE_NOT_BYPASS
    else:
        return 0, PLL_SOURCE_UNKNOWN
    if ref == 0:
        return 0, PLL_SOURCE_UNKNOWN

    # DIVM1 holds the value; N1 and P1 hold the value minus one. ST's asymmetry,
    # and the single most likely thing for a second implementation to get wrong.
    m = (r.pllckselr & PLLCKSELR_DIVM1_MSK) >> PLLCKSELR_DIVM1_POS
    n = ((r.pll1divr & PLL1DIVR_N1_MSK) >> PLL1DIVR_N1_POS) + 1
    p = ((r.pll1divr & PLL1DIVR_P1_MSK) >> PLL1DIVR_P1_POS) + 1

    if m == 0:
        return 0, PLL_ZERO_DIVIDER
    inp = ref // m
    # The guard C gets from its types and Python does not get at all: an
    # unbounded integer would simply return the larger product.
    if inp == 0 or inp > UINT32_MAX // n:
        return 0, PLL_WOULD_OVERFLOW
    return (inp * n) // p, OK


def decode(r: Regs, hsi_nominal: int, hse_bypass: int) -> Tree:
    sws = (r.cfgr & CFGR_SWS_MSK) >> CFGR_SWS_POS
    if sws == SWS_HSI:
        sys_hz, why = _hsi_hz(r, hsi_nominal), OK
    elif sws == SWS_HSE:
        sys_hz = _hse_hz(r, hse_bypass)
        why = OK if sys_hz else HSE_NOT_BYPASS
    elif sws == SWS_PLL1:
        sys_hz, why = _pll1_p_hz(r, hsi_nominal, hse_bypass)
    else:
        return Tree(0, 0, 0, 0, 0, SWS_UNKNOWN)

    if why != OK:
        return Tree(0, 0, 0, 0, 0, why)
    if sys_hz == 0:
        return Tree(0, 0, 0, 0, 0, SWS_UNKNOWN)

    cpu_div = AHB_CPU_DIVIDER.get((r.cdcfgr1 & CDCFGR1_CDCPRE_MSK) >> CDCFGR1_CDCPRE_POS, 0)
    ahb_div = AHB_CPU_DIVIDER.get((r.cdcfgr1 & CDCFGR1_HPRE_MSK) >> CDCFGR1_HPRE_POS, 0)
    apb1_div = APB_DIVIDER.get((r.cdcfgr2 & CDCFGR2_CDPPRE1_MSK) >> CDCFGR2_CDPPRE1_POS, 0)
    apb2_div = APB_DIVIDER.get((r.cdcfgr2 & CDCFGR2_CDPPRE2_MSK) >> CDCFGR2_CDPPRE2_POS, 0)
    if cpu_div == 0 or ahb_div == 0 or apb1_div == 0 or apb2_div == 0:
        # Reporting the undivided frequency would be wrong by that very ratio,
        # which is the one error nobody would suspect.
        return Tree(0, 0, 0, 0, 0, PRESCALER_UNDECODED)

    # sys_ck over CDCPRE is the core, the core over HPRE is the AHB buses, the
    # AHB over CDPPRE1 is APB1.
    core = sys_hz // cpu_div
    ahb = core // ahb_div
    # APB2 divides the AHB, not APB1, even though they are equal on this board.
    return Tree(sys_hz, core, ahb, ahb // apb1_div, ahb // apb2_div, OK)


def ppm(delta: int, den: int) -> int:
    """Parts per million, halves away from zero, saturating at 32-bit signed.

    WRITTEN OUT RATHER THAN LEFT TO `//`, and this is the one function in the
    file that would be wrong if it had been. Python floors, so -7 // 2 is -4
    where C and Rust give -3, and every negative row in ../clock_vectors.json
    would then disagree with the other three implementations by one. Dividing
    magnitudes and reapplying the sign is what truncation toward zero means.
    """
    half = den // 2 if den >= 0 else -((-den) // 2)
    scaled = delta * 1000000
    n = scaled + half if delta >= 0 else scaled - half
    q = abs(n) // abs(den)
    if n < 0:
        q = -q
    return max(INT32_MIN, min(INT32_MAX, q))


def bias(reported_hz: int, true_hz: int) -> Bias:
    """The signed bias, three figures that are two quantities.

    delay_ppm equals frequency_ppm by the same expression and not by copying:
    both divide by the true frequency. duration_ppm divides by the reported one,
    so it differs in the last unit or two.
    """
    if reported_hz == 0 or true_hz == 0:
        return Bias(False, 0, 0, 0)
    frequency = ppm(reported_hz - true_hz, true_hz)
    return Bias(True, frequency, ppm(true_hz - reported_hz, reported_hz), frequency)
