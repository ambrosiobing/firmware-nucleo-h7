"""P01's clock tree in four languages, against one oracle, answer for answer.

THE ORACLE IS UNLIKE THE OTHER SIX, and the difference is worth naming because it
changes what this test can prove. P09 has hand-computed vectors and P05 has a
published check value: facts from outside every implementation. P08 has none, so
its parity test compares the index of the row each implementation took, which
pins four tables to one order. P01 is a third kind again. Two of the seventeen
decode rows are **this board**: the register words `p01-pll280` printed on the
console on Sunday 4 October 2026, with the frequencies the part itself reported
for them. Those two rows are an external fact in the strongest sense available
here, because the authority is the hardware.

The other fifteen are constructed, which is the only way to reach a refusal on
demand. There is no comfortable way to ask this part for a PLL with its
fractional term enabled.

APB2 JOINED THE ANSWER ON TUESDAY 6 OCTOBER 2026, so the decode line carries
five frequencies rather than four. TIM1 is on APB2 and the frequency counter's
self test needs TIM1's clock, and the register word the decode already read for
CDPPRE1 carries CDPPRE2 as well, so the input did not change at all.

Three rows were added with it, and they matter more than the field does. All
seventeen rows before them held CDPPRE2 at divide by one, where APB2 equals APB1
and both equal the AHB frequency, so an implementation that returned ahb_hz, or
returned pclk1_hz a second time, or read CDPPRE1 at the wrong offset, would have
agreed with every row in the oracle. The new rows separate those cases: one
halves APB2 while APB1 stays whole, one divides the two by different amounts so
a swap of the fields is visible, and one holds a ratio no implementation decodes
so the new refusal has to fire.

WHAT IS COMPARED, AND IT IS NOT ONLY NUMBERS. Each decode request returns five
frequencies and a refusal TOKEN, and all five are compared across the four
languages. Two implementations can agree that a configuration is unusable and
disagree about which guard caught it, and a comparison of frequencies alone would
call that agreement, because a refusal zeroes all four.

FOUR ROUTES FOR FOUR LANGUAGES, for reasons each project has had to settle:

  - C through `ctypes` against `build-host/libclocktree.so`, which is the object
    file the firmware links. No text layer between the test and the code the
    board runs.
  - C++ and Rust as filters over the protocol in
    `projects/P01-toolchain-first-light/PROTOCOL.md`, so one helper drives both.
  - Python imported directly.

THE FOUR WAYS THESE COULD DIFFER WHILE EACH LOOKING CORRECT are listed in that
protocol file and each has a row here: divide before multiply, truncate toward
zero, halves away from zero, and saturate rather than refuse. The third is the
one no language gives for free, and the one that caught a mistake in this
repository's own test on Sunday 4 October 2026.

C is the reference, as in the other six parity tests, because it is the
implementation the board links.
"""
from __future__ import annotations

import ctypes
import json
import sys
from pathlib import Path

import pytest

from conftest import (
    BUILD,
    CLOCK_CPP_FILTER,
    CLOCK_RUST_FILTER,
    ROOT,
    assert_not_vacuous,
    run_filter,
    shared_library_name,
)
from test_clocktree import BiasOut, Regs, Tree

sys.path.insert(
    0,
    str(ROOT / "projects" / "P01-toolchain-first-light" / "python"),
)

import clocktree as py_clock  # noqa: E402

LANGUAGES = ("C", "C++", "Python", "Rust")

P01 = ROOT / "projects" / "P01-toolchain-first-light"
ORACLE = json.loads((P01 / "clock_vectors.json").read_text(encoding="utf-8"))
LIBRARY = BUILD / shared_library_name("clocktree")

REG_ORDER = ("cr", "cfgr", "pllckselr", "pllcfgr", "pll1divr", "cdcfgr1", "cdcfgr2")


# --------------------------------------------------------------- the requests

def decode_request(row) -> str:
    regs = " ".join(str(row["regs"][name]) for name in REG_ORDER)
    return "D {} {} {}".format(regs, row["hsi_nominal"], row["hse_bypass"])


def bias_request(row) -> str:
    return "B {} {}".format(row["reported_hz"], row["true_hz"])


REQUESTS = ([decode_request(r) for r in ORACLE["decode"]]
            + [bias_request(r) for r in ORACLE["bias"]])
NAMES = ([r["name"] for r in ORACLE["decode"]] + [r["name"] for r in ORACLE["bias"]])
DECODE_COUNT = len(ORACLE["decode"])


# ------------------------------------------------------------- the four routes

def c_answers():
    """The shared library the firmware links, through ctypes.

    Returns None when it has not been built, which is the normal state on
    win11 aquamarine, where compiling is forbidden.
    """
    if not LIBRARY.exists():
        return None
    lib = ctypes.CDLL(str(LIBRARY))
    lib.clocktree_decode.restype = None
    lib.clocktree_decode.argtypes = [ctypes.POINTER(Regs), ctypes.c_uint32,
                                     ctypes.c_uint32, ctypes.POINTER(Tree)]
    lib.clocktree_bias.restype = ctypes.c_int
    lib.clocktree_bias.argtypes = [ctypes.c_uint32, ctypes.c_uint32,
                                   ctypes.POINTER(BiasOut)]
    lib.clocktree_refusal_text.restype = ctypes.c_char_p
    lib.clocktree_refusal_text.argtypes = [ctypes.c_int]

    out = []
    for row in ORACLE["decode"]:
        regs = Regs(**row["regs"])
        tree = Tree()
        lib.clocktree_decode(ctypes.byref(regs), row["hsi_nominal"],
                             row["hse_bypass"], ctypes.byref(tree))
        out.append("{} {} {} {} {}".format(
            tree.sys_hz, tree.core_hz, tree.ahb_hz, tree.pclk1_hz, tree.pclk2_hz,
            lib.clocktree_refusal_text(tree.refusal).decode("ascii")))
    for row in ORACLE["bias"]:
        bias = BiasOut()
        ok = lib.clocktree_bias(row["reported_hz"], row["true_hz"], ctypes.byref(bias))
        out.append("{} {} {} {}".format(
            1 if ok else 0, bias.frequency_ppm, bias.duration_ppm, bias.delay_ppm))
    return out


def python_answers():
    out = []
    for row in ORACLE["decode"]:
        tree = py_clock.decode(py_clock.Regs(**row["regs"]),
                               row["hsi_nominal"], row["hse_bypass"])
        out.append("{} {} {} {} {}".format(
            tree.sys_hz, tree.core_hz, tree.ahb_hz, tree.pclk1_hz, tree.pclk2_hz,
            tree.refusal))
    for row in ORACLE["bias"]:
        bias = py_clock.bias(row["reported_hz"], row["true_hz"])
        out.append("{} {} {} {}".format(
            1 if bias.ok else 0, bias.frequency_ppm, bias.duration_ppm, bias.delay_ppm))
    return out


def filter_answers(path: Path):
    """A filter's answers, or None when the binary has not been built.

    run_filter skips the test when the binary is absent, which is right for a
    test about one language and wrong here: this test has to continue with the
    languages it does have and then assert that it had enough of them.
    """
    if not path.exists():
        return None
    return run_filter(path, REQUESTS)


# --------------------------------------------------------------- the oracle

def oracle_answers():
    """The committed expectation, in the same wire form as every answer.

    Rendering the oracle into the protocol's own shape rather than comparing
    structures keeps one comparison for all four languages and makes a
    disagreement printable as two lines.
    """
    out = []
    for row in ORACLE["decode"]:
        e = row["expect"]
        out.append("{} {} {} {} {}".format(
            e["sys_hz"], e["core_hz"], e["ahb_hz"], e["pclk1_hz"], e["pclk2_hz"],
            e["refusal"]))
    for row in ORACLE["bias"]:
        e = row["expect"]
        out.append("{} {} {} {}".format(
            1 if e["ok"] else 0, e["frequency_ppm"], e["duration_ppm"], e["delay_ppm"]))
    return out


# ------------------------------------------------------------------- the test

def test_four_languages_agree_with_each_other_and_with_the_oracle(capsys):
    answers = {
        "C": c_answers(),
        "C++": filter_answers(CLOCK_CPP_FILTER),
        "Python": python_answers(),
        "Rust": filter_answers(CLOCK_RUST_FILTER),
    }
    present = {name: rows for name, rows in answers.items() if rows is not None}
    expected = oracle_answers()

    with capsys.disabled():
        print("\n  P01 parity over {} decode rows and {} bias rows, two of the decode"
              "\n  rows being this board's own register words".format(
                  DECODE_COUNT, len(ORACLE["bias"])))
        for name in LANGUAGES:
            rows = answers[name]
            if rows is None:
                print("    {:7} not built".format(name))
            else:
                print("    {:7} {}".format(
                    name, "agrees" if rows == expected else "DISAGREES"))

    assert_not_vacuous(present, "P01")

    problems = []
    for index, name in enumerate(NAMES):
        want = expected[index]
        for language, rows in sorted(present.items()):
            got = rows[index]
            if got != want:
                problems.append("  {:28} {:7} wanted {!r}\n  {:28} {:7} got    {!r}"
                                .format(name, "oracle", want, "", language, got))
    assert not problems, (
        "{} disagreement(s) between an implementation and the oracle:\n{}".format(
            len(problems), "\n".join(problems)))


def test_the_two_board_rows_are_first_and_are_really_the_board(capsys):
    """The rows whose authority is the hardware, asserted rather than assumed.

    This exists because the value of those two rows is entirely in their
    provenance, and provenance is the one property a parity run cannot check. If
    somebody edits them to make a test pass, the oracle stops being anchored to
    anything and every other row in the file still passes. So the two frequencies
    the board printed on Sunday 4 October 2026 are written out here as well, in a
    second file, where changing one of them means changing two.
    """
    rows = {r["name"]: r for r in ORACLE["decode"]}

    found = rows["this-board-as-found-after-a-reset-press"]
    assert found["expect"]["core_hz"] == 64000000
    assert found["expect"]["pclk1_hz"] == 64000000
    assert found["expect"]["pclk2_hz"] == 64000000
    assert found["regs"]["cr"] == 0x0004C025
    assert found["regs"]["cfgr"] == 0x00000000

    raised = rows["this-board-after-the-eight-step-raise"]
    assert raised["expect"]["core_hz"] == 280000000
    assert raised["expect"]["pclk1_hz"] == 140000000
    assert raised["expect"]["pclk2_hz"] == 140000000
    assert raised["regs"]["cr"] == 0x0307C025
    assert raised["regs"]["pll1divr"] == 0x01010317

    with capsys.disabled():
        print("    the two board rows still carry the words and the frequencies\n"
              "    p01-pll280 printed on Sunday 4 October 2026")


@pytest.mark.parametrize("language", ["C++", "Rust"])
def test_each_filter_answers_exactly_one_line_per_request(language):
    """A filter that splits or drops a line misaligns every answer after it.

    run_filter asserts the count itself, so this is mostly a named place for the
    failure to appear. It skips rather than failing when the binary is absent,
    because that is a statement about this laptop and not about the filter.
    """
    path = CLOCK_CPP_FILTER if language == "C++" else CLOCK_RUST_FILTER
    answers = run_filter(path, REQUESTS)
    assert len(answers) == len(REQUESTS)
