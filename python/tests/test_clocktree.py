"""P01's clock tree decode, driven from a host against the file the board links.

WHAT IS NEW HERE, AND IT IS NOT THE ARITHMETIC. Until Sunday 4 October 2026 this
decode lived as four static functions inside c/board/system.c, each of which
dereferences a memory mapped register. Nothing on a host could call them. The
only test the decode ever had was to flash the board and read the banner, and
that test can exercise exactly one configuration per flash: whichever one the
board happens to be in.

It passed that test. The measurement of Sunday 4 October 2026 agreed with the
decoded 280 MHz to 1168 parts per million and traced the remainder to the input
frequency rather than to any field. But the configurations that matter most are
the eight refusals, and the board has no convenient way to produce a PLL with its
fractional term enabled or a prescaler holding an unsourced ratio. Those are the
cases where a wrong answer is a plausible frequency rather than a dead board.

So the decode moved to c/clock/clocktree.c, taking the seven register words as an
argument, and the firmware calls it. This drives the same object file through
every row of projects/P01-toolchain-first-light/clock_vectors.json, refusals
included. It is a test of shipped code and not of a host copy of it.

THE ORACLE IS DOUBLE ENTERED. Each row of that file carries the register words
and the expected answer, both written by hand, with a why that says what the row
is for. The 280 MHz row and the measured-input row are this board; the rest are
constructed, which is the only way to reach a refusal on demand and is said
plainly in the file itself.
"""
from __future__ import annotations

import ctypes
import json
import re

import pytest

from conftest import BUILD, ROOT, shared_library_name

P01 = ROOT / "projects" / "P01-toolchain-first-light"
ORACLE = json.loads((P01 / "clock_vectors.json").read_text(encoding="utf-8"))
SOURCE = ROOT / "c" / "clock" / "clocktree.c"

LIBRARY = BUILD / shared_library_name("clocktree")


class Regs(ctypes.Structure):
    _fields_ = [("cr", ctypes.c_uint32),
                ("cfgr", ctypes.c_uint32),
                ("pllckselr", ctypes.c_uint32),
                ("pllcfgr", ctypes.c_uint32),
                ("pll1divr", ctypes.c_uint32),
                ("cdcfgr1", ctypes.c_uint32),
                ("cdcfgr2", ctypes.c_uint32)]


class Tree(ctypes.Structure):
    _fields_ = [("sys_hz", ctypes.c_uint32),
                ("core_hz", ctypes.c_uint32),
                ("ahb_hz", ctypes.c_uint32),
                ("pclk1_hz", ctypes.c_uint32),
                ("pclk2_hz", ctypes.c_uint32),
                ("refusal", ctypes.c_int)]


class BiasOut(ctypes.Structure):
    _fields_ = [("frequency_ppm", ctypes.c_int32),
                ("duration_ppm", ctypes.c_int32),
                ("delay_ppm", ctypes.c_int32)]


@pytest.fixture(scope="module")
def lib():
    if not LIBRARY.exists():
        pytest.skip("{} is missing. Build it with:\n    "
                    "python python/tools/build_host.py".format(LIBRARY.name))
    handle = ctypes.CDLL(str(LIBRARY))
    handle.clocktree_decode.restype = None
    handle.clocktree_decode.argtypes = [ctypes.POINTER(Regs), ctypes.c_uint32,
                                        ctypes.c_uint32, ctypes.POINTER(Tree)]
    handle.clocktree_bias.restype = ctypes.c_int
    handle.clocktree_bias.argtypes = [ctypes.c_uint32, ctypes.c_uint32,
                                      ctypes.POINTER(BiasOut)]
    handle.clocktree_refusal_text.restype = ctypes.c_char_p
    handle.clocktree_refusal_text.argtypes = [ctypes.c_int]
    return handle


def decode(lib, row):
    regs = Regs(**{k: v for k, v in row["regs"].items()})
    out = Tree()
    lib.clocktree_decode(ctypes.byref(regs), row["hsi_nominal"],
                         row["hse_bypass"], ctypes.byref(out))
    return {"sys_hz": out.sys_hz, "core_hz": out.core_hz, "ahb_hz": out.ahb_hz,
            "pclk1_hz": out.pclk1_hz, "pclk2_hz": out.pclk2_hz,
            "refusal": lib.clocktree_refusal_text(out.refusal).decode("ascii")}


@pytest.mark.parametrize("row", ORACLE["decode"], ids=[r["name"] for r in ORACLE["decode"]])
def test_the_decode_agrees_with_the_oracle(lib, row):
    assert decode(lib, row) == row["expect"], row["why"]


@pytest.mark.parametrize("row", ORACLE["bias"], ids=[r["name"] for r in ORACLE["bias"]])
def test_the_signed_bias_agrees_with_the_oracle(lib, row):
    out = BiasOut()
    ok = lib.clocktree_bias(row["reported_hz"], row["true_hz"], ctypes.byref(out))
    got = {"ok": bool(ok), "frequency_ppm": out.frequency_ppm,
           "duration_ppm": out.duration_ppm, "delay_ppm": out.delay_ppm}
    assert got == row["expect"], row["why"]


def test_a_delay_and_a_frequency_carry_the_same_bias_and_a_duration_never_does(lib):
    """The result that the three quantities are two, asserted rather than claimed.

    A requested delay runs long by exactly the fraction the reported frequency is
    high, because both divide by the true frequency. A measured duration divides
    by the reported one instead, so it differs. Saying "0.117 per cent high" with
    no quantity named was wrong in three files on Sunday 4 October 2026 for
    exactly this reason.

    THE WEAKER CLAIM IS THE TRUE ONE: never the same sign, rather than always the
    opposite sign. The first version of this test asserted the stronger thing and
    was contradicted by its own oracle on the first compiler to run it, gcc 15 in
    WSL on bing@JPTOUPM678 on Sunday 4 October 2026. The row that did it is
    a-half-part-per-million-rounds-away-from-zero, where one hertz above 2 MHz
    gives a frequency of +1 and a duration of 0: the frequency divides by 2000000
    and lands on exactly half, which rounds away from zero, while the duration
    divides by 2000001 and lands just under, which rounds to nothing. The row
    below it is not its mirror for the same reason, the smaller denominator
    pushing it over instead. Both were in the vector file, both were predicted,
    and the C produced both. The generalisation was what was wrong.
    """
    strict = 0
    for row in ORACLE["bias"]:
        out = BiasOut()
        if not lib.clocktree_bias(row["reported_hz"], row["true_hz"], ctypes.byref(out)):
            continue
        assert out.delay_ppm == out.frequency_ppm, row["name"]
        assert out.duration_ppm * out.frequency_ppm <= 0, row["name"]
        if out.duration_ppm != 0 and out.frequency_ppm != 0:
            assert (out.duration_ppm < 0) != (out.frequency_ppm < 0), row["name"]
            strict += 1
    assert strict >= 3, "too few rows where both are non-zero to make the point"


def test_every_refusal_the_enum_can_produce_has_a_vector():
    """A new refusal with no vector is a hole, and this is what notices.

    clocktree_refusal_text is the list of what can be refused. Every token it
    returns, apart from ok and the unreachable default, must appear as some row's
    expected answer in the oracle. Adding a ninth refusal to the enum without a
    row to exercise it turns this red.
    """
    tokens = set()
    for row in ORACLE["decode"]:
        tokens.add(row["expect"]["refusal"])
    source = SOURCE.read_text(encoding="utf-8")
    declared = set(re.findall(r'return "([a-z0-9-]+)";', source))
    declared.discard("ok")
    declared.discard("unknown")
    assert declared, "no refusal tokens found in clocktree.c"
    missing = sorted(declared - tokens)
    assert not missing, "refusals with no vector: {}".format(", ".join(missing))


def test_the_decoder_names_no_register_it_could_dereference():
    """The one hazard the split introduced, checked rather than remembered.

    clocktree.c includes stm32h7a3_regs.h for the field masks, because that is
    where each one is sourced with the authority that settled it. The same header
    defines RCC_CR and its neighbours as REG32 expression macros, which are
    harmless to declare and fatal to evaluate off the part. A stray RCC_CR in
    this file would compile without a warning and then read address 0x58024400 on
    a host, which is a segmentation fault at best and a wrong number at worst.

    So the file is read as text and the register names are forbidden in it. The
    masks and positions, which end in _MSK and _POS, are exactly what it is
    allowed to use.
    """
    source = SOURCE.read_text(encoding="utf-8")
    # Comments first: the file discusses RCC_CR by name in its header comment,
    # and should be able to. Blanked to their own length so nothing shifts.
    stripped = re.sub(r"/\*.*?\*/", lambda m: " " * len(m.group(0)), source, flags=re.S)
    forbidden = ("RCC_CR", "RCC_CFGR", "RCC_PLLCKSELR", "RCC_PLLCFGR",
                 "RCC_PLL1DIVR", "RCC_CDCFGR1", "RCC_CDCFGR2", "PWR_CR1",
                 "PWR_CR3", "PWR_CSR1", "FLASH_ACR", "RTC_SSR")
    found = []
    for name in forbidden:
        for m in re.finditer(re.escape(name) + r"(?![A-Za-z0-9_])", stripped):
            line = stripped.count("\n", 0, m.start()) + 1
            found.append("{}:{} {}".format(SOURCE.name, line, name))
    assert not found, (
        "c/clock/clocktree.c must stay callable on a host, so it may name a mask "
        "or a position and never a register: " + "; ".join(found))
