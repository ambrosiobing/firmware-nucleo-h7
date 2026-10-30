"""P19's alignment arithmetic, in C and Python, against one oracle.

**This file is P19's first equivalence gate.** Until Wednesday 28 October 2026
the project held one implementation in one language, the instruction cache
enable in `c/board/icache.c`, so nothing in it could disagree with anything.
Every other project in the volume has at least two implementations, and that
comparison found a real defect in P16 on Tuesday 27 October 2026 and another in
P20 on Wednesday 28 October 2026. This is the half of P19 that can be compared.

**It is chapter 19's criteria 4 and 5.** Every buffer aligned to 32 bytes and
sized to a multiple of 32, enforced by a build-time assertion rather than by a
comment, and a deliberately misaligned region table entry stopping the build
rather than the board. Both are arithmetic. The stale read itself needs a
transfer engine and RM0455; deciding which buffers can produce one does not.

**Everything is compared exactly**, all of it unsigned integer arithmetic, so
there is no tolerance anywhere in this file.

**The widths are the point of the comparison.** The C computes on 32-bit
addresses and the Python has no width at all, so the case where an aligned end
needs the value 2 to the 32nd is a place the two could disagree rather than a
place only the C can see. Both refuse it, and for the same stated reason rather
than because one ran out of bits.

**The two readings the chapter invites are carried unused in the twin** and two
tests run them: criterion 4 read as alignment alone, which leaves the tail line
shared, and criterion 5 read as alignment to the cache line, which passes a
region the hardware refuses.
"""
from __future__ import annotations

import ctypes
import json
import sys
from pathlib import Path

import pytest

from conftest import (BUILD, BUILD_HOST_CMD, MPU_CPP_FILTER, MPU_RUST_FILTER,
                      require_artefact, run_filter, shared_library_name)

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "projects" / "P19-caches-mpu-dma"
sys.path.insert(0, str(PROJECT / "python"))

from mpualign import (ERRORS, MPU_LINE,  # noqa: E402
                      buffer_check as py_buffer_check,
                      chapter_align_only, chapter_line_aligned_region,
                      maint_range as py_maint_range,
                      region_encode as py_region_encode)

ORACLE = json.loads((PROJECT / "vectors.json").read_text(encoding="utf-8"))
assert ORACLE["schema"] == 1, "the oracle's schema moved"
assert ORACLE["line"] == MPU_LINE, "the measured cache line moved"

RANGE_FIELDS = ("base", "end", "bytes", "lines", "head_shared", "tail_shared")


class CRange(ctypes.Structure):
    """Must match mpu_range_t in c/mpualign.h, field for field."""

    _fields_ = [("base", ctypes.c_uint32),
                ("end", ctypes.c_uint32),
                ("bytes", ctypes.c_uint32),
                ("lines", ctypes.c_uint32),
                ("head_shared", ctypes.c_uint8),
                ("tail_shared", ctypes.c_uint8)]


_LIB = None


def c_lib():
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("mpualign")
        require_artefact(path)
        d = ctypes.CDLL(str(path))
        d.mpu_maint_range.restype = ctypes.c_int
        d.mpu_maint_range.argtypes = [ctypes.c_uint32, ctypes.c_uint32,
                                      ctypes.c_uint32,
                                      ctypes.POINTER(CRange)]
        d.mpu_buffer_check.restype = ctypes.c_int
        d.mpu_buffer_check.argtypes = [ctypes.c_uint32, ctypes.c_uint32,
                                       ctypes.c_uint32]
        d.mpu_region_encode.restype = ctypes.c_int
        d.mpu_region_encode.argtypes = [ctypes.c_uint32, ctypes.c_uint32,
                                        ctypes.POINTER(ctypes.c_uint8)]
        _LIB = d
    return _LIB


def c_maint_range(addr, length, line):
    out = CRange()
    rc = c_lib().mpu_maint_range(addr, length, line, ctypes.byref(out))
    return rc, out


def c_region_encode(base, size):
    out = ctypes.c_uint8(255)
    rc = c_lib().mpu_region_encode(base, size, ctypes.byref(out))
    return rc, (out.value if rc == 0 else None)


@pytest.mark.parametrize("case", ORACLE["ranges"], ids=lambda c: c["why"][:46])
def test_the_python_rounds_every_range_as_the_oracle_says(case):
    rc, got = py_maint_range(case["addr"], case["len"], case["line"])
    assert rc == 0, case["why"]
    for key in RANGE_FIELDS:
        assert got[key] == case[key], (key, case["why"])


@pytest.mark.parametrize("case", ORACLE["ranges"], ids=lambda c: c["why"][:46])
def test_the_c_rounds_every_range_as_the_oracle_says(case):
    rc, got = c_maint_range(case["addr"], case["len"], case["line"])
    assert rc == 0, case["why"]
    for key in RANGE_FIELDS:
        assert getattr(got, key) == case[key], (key, case["why"])


@pytest.mark.parametrize("case", ORACLE["range_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_ranges(case):
    rc, got = py_maint_range(case["addr"], case["len"], case["line"])
    assert rc == ERRORS[case["error"]] and got is None, case["why"]
    rc, _ = c_maint_range(case["addr"], case["len"], case["line"])
    assert rc == ERRORS[case["error"]], case["why"]


@pytest.mark.parametrize("case", ORACLE["buffers"], ids=lambda c: c["why"][:46])
def test_both_implementations_agree_on_criterion_four(case):
    want = 0 if case["error"] is None else ERRORS[case["error"]]
    assert py_buffer_check(case["addr"], case["len"], case["line"]) == want, \
        case["why"]
    assert c_lib().mpu_buffer_check(case["addr"], case["len"],
                                    case["line"]) == want, case["why"]


@pytest.mark.parametrize("case", ORACLE["regions"], ids=lambda c: c["why"][:46])
def test_both_implementations_encode_every_region_as_the_oracle_says(case):
    rc, field = py_region_encode(case["base"], case["size"])
    assert rc == 0 and field == case["size_field"], case["why"]
    rc, field = c_region_encode(case["base"], case["size"])
    assert rc == 0 and field == case["size_field"], case["why"]


@pytest.mark.parametrize("case", ORACLE["region_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_regions(case):
    rc, field = py_region_encode(case["base"], case["size"])
    assert rc == ERRORS[case["error"]] and field is None, case["why"]
    rc, field = c_region_encode(case["base"], case["size"])
    assert rc == ERRORS[case["error"]] and field is None, case["why"]


# ---------------------------------------------------------------------------
# The findings. Through the twin, so they run on the authoring laptop too.

def test_a_clean_may_over_extend_and_an_invalidate_may_not():
    """Finding one, which is the reason criterion 4 is an assertion.

    Both operations round the range outward to whole lines and the rounding is
    where they stop being the same thing. A line holding the buffer's bytes and
    a neighbour's is harmless to clean, because writing the neighbour's bytes
    back says what they already say, and destructive to invalidate, because
    that discards a write the processor has made and not yet written back.

    The chapter states the alignment rule and not this reason, so the two
    shared lines are reported separately here and a caller can be refused for
    the operation that cannot tolerate them.
    """
    case = next(c for c in ORACLE["ranges"] if "ALIGNED BUT NOT A MULTIPLE" in c["why"])
    rc, r = py_maint_range(case["addr"], case["len"], case["line"])
    assert rc == 0
    assert r["head_shared"] == 0, "the buffer starts on a line"
    assert r["tail_shared"] == 1, "and does not end on one"

    # The bytes the invalidate would discard: from the end of the buffer to the
    # end of its last line.
    buffer_end = case["addr"] + case["len"]
    lost = r["end"] - buffer_end
    assert lost == 24, "the arithmetic of this case moved"
    print("\n  a buffer of {} bytes at {:#x} occupies {} whole lines; cleaning "
          "them is harmless and invalidating them discards the {} bytes after "
          "the buffer, which belong to whatever is next"
          .format(case["len"], case["addr"], r["lines"], lost))


def test_criterion_four_has_two_halves_and_one_is_not_enough():
    """Finding two, run against the reading the chapter's wording invites.

    "Aligned to 32 bytes and sized to a multiple of 32 bytes" is two facts. A
    reader who checks the first has removed the shared head line and left the
    shared tail, and `chapter_align_only` is that reading. The oracle carries a
    buffer that passes it and fails the criterion.
    """
    case = next(c for c in ORACLE["buffers"]
                if "ALIGNED AND NOT A MULTIPLE" in c["why"])
    permitted = chapter_align_only(case["addr"], case["line"])
    pinned = py_buffer_check(case["addr"], case["len"], case["line"])
    assert permitted is True, "the alignment half passes"
    assert pinned == ERRORS["align"], "and the criterion does not"
    print("\n  a buffer of {} bytes at {:#x}: the alignment half of criterion "
          "4 passes and the criterion fails, because the second half is about "
          "the size and fixes the other end"
          .format(case["len"], case["addr"]))


def test_the_mpu_aligns_to_the_region_size_and_not_to_the_cache_line():
    """Finding three, which is why criteria 4 and 5 are listed separately.

    A region of 4096 bytes at 0x24000100 is 32 byte aligned, so it satisfies
    criterion 4 exactly, and an ARMv7-M region must start on a multiple of its
    own size, so the hardware refuses it. A reader who has internalised "align
    to 32 and you are safe" checks the wrong thing, and
    `chapter_line_aligned_region` is that check.
    """
    case = next(c for c in ORACLE["region_refusals"]
                if "32 BYTE ALIGNED BASE" in c["why"])
    base, size = case["base"], case["size"]
    assert chapter_line_aligned_region(base, MPU_LINE) is True
    assert py_buffer_check(base, size, MPU_LINE) == 0, (
        "the same base and size satisfy criterion 4")
    rc, field = py_region_encode(base, size)
    assert rc == ERRORS["align"] and field is None
    print("\n  a {} byte region at {:#x} satisfies criterion 4 and fails "
          "criterion 5: it is aligned to the {} byte line and not to its own "
          "size, and the two criteria are independent"
          .format(size, base, MPU_LINE))


def test_a_range_that_ends_at_the_top_is_refused_rather_than_encoded():
    """Finding four at its boundary, which is about the signature.

    `end` is one past the last address. A range ending exactly at the top of a
    32-bit address space needs 2 to the 32nd and a uint32_t has only zero to
    offer, and zero makes a loop from base to end run no times at all: no
    fault, no diagnostic, and the cache not maintained. So the case is refused
    rather than encoded, by both implementations, and the Python refuses it by
    the same rule rather than by running out of bits, which it never does.
    """
    case = next(c for c in ORACLE["range_refusals"]
                if "EXACTLY AT THE TOP" in c["why"])
    rc, got = py_maint_range(case["addr"], case["len"], case["line"])
    assert rc == ERRORS["wrap"] and got is None

    # The unrounded end is exactly the top, so nothing has wrapped yet and the
    # refusal is about what cannot be REPRESENTED rather than about an overflow
    # that already happened.
    assert case["addr"] + case["len"] == 0x100000000
    # And one line lower is accepted, which is the control: the refusal is
    # about the boundary and not about the address being high.
    rc, got = py_maint_range(case["addr"], case["len"] - MPU_LINE,
                             case["line"])
    assert rc == 0 and got["end"] == 0xFFFFFFE0
    print("\n  a buffer of {} bytes at {:#x} ends exactly at {:#x}, which one "
          "past the last address cannot hold, so it is refused; {} bytes "
          "fewer ends at {:#x} and is accepted"
          .format(case["len"], case["addr"], 0x100000000, MPU_LINE,
                  0xFFFFFFE0))


# ---------------------------------------------------------------------------
# The Rust, driven as a filter over the same oracle. One process for the whole
# run, which is conftest's rule.
#
# Three verbs, one per function the oracle exercises:
#
#     M <addr> <len> <line>   -> base=.. end=.. bytes=.. lines=.. head=.. tail=..
#     B <addr> <len> <line>   -> ok=1
#     R <base> <size>         -> field=N
#
# EVERY VALUE IS DECIMAL, addresses included, which is P01's decision for
# register words applied again: hexadecimal would read better to a person and
# would add a second parser to every implementation for no gain.
#
# THE THIRD WIDTH IN THIS COMPARISON IS THE POINT. The C computes the end in
# uint64_t so that the test itself cannot wrap, the Python has no width at all,
# and the Rust asks the 32 bit type whether it can hold the answer, with
# checked_add and checked_next_multiple_of. Three different ways of reaching
# one refusal, compared against the same oracle, which is worth more than two.

NEWLINE = chr(10)


def maint_request(case):
    return "M {} {} {}".format(case["addr"], case["len"], case["line"])


def buffer_request(case):
    return "B {} {} {}".format(case["addr"], case["len"], case["line"])


def region_request(case):
    return "R {} {}".format(case["base"], case["size"])


def all_requests():
    """Every request, in the oracle's own order.

    Built where a test with no cargo can reach them, because the filter test
    SKIPS where there is no cargo and a skipped body never runs at all.
    """
    lines = [maint_request(c) for c in ORACLE["ranges"]]
    lines += [maint_request(c) for c in ORACLE["range_refusals"]]
    lines += [buffer_request(c) for c in ORACLE["buffers"]]
    lines += [region_request(c) for c in ORACLE["regions"]]
    lines += [region_request(c) for c in ORACLE["region_refusals"]]
    return lines


def fields_of(answer):
    out = {}
    for token in answer.split():
        key, value = token.split("=", 1)
        out[key] = value
    return out


def test_every_request_says_what_its_case_says_without_running_anything():
    """Runs everywhere, including where no filter exists, on purpose."""
    lines = all_requests()
    at = 0
    for case in ORACLE["ranges"] + ORACLE["range_refusals"]:
        t = lines[at].split()
        assert t[0] == "M", case["why"]
        assert int(t[1]) == case["addr"], case["why"]
        assert int(t[2]) == case["len"], case["why"]
        assert int(t[3]) == case["line"], case["why"]
        assert len(t) == 4, case["why"]
        at += 1
    for case in ORACLE["buffers"]:
        t = lines[at].split()
        assert t[0] == "B", case["why"]
        assert int(t[1]) == case["addr"], case["why"]
        assert int(t[2]) == case["len"], case["why"]
        assert int(t[3]) == case["line"], case["why"]
        at += 1
    for case in ORACLE["regions"] + ORACLE["region_refusals"]:
        t = lines[at].split()
        assert t[0] == "R", case["why"]
        assert int(t[1]) == case["base"], case["why"]
        assert int(t[2]) == case["size"], case["why"]
        assert len(t) == 3, case["why"]
        at += 1
    assert at == len(lines)

    # The line length travels in every range request, and it is a parameter
    # rather than the measured 32: two of the refusal cases pass a line of
    # zero and one of 48, so a filter with the line compiled in could not
    # carry them.
    lines_asked = {int(line.split()[3]) for line in lines
                   if line.startswith(("M ", "B "))}
    assert len(lines_asked) > 1, (
        "every request asks for the same cache line, so the parameter is not "
        "being exercised: {}".format(sorted(lines_asked)))
    print(NEWLINE + "  {} requests built with no filter and no compiler, "
          "asking for {} different cache lines"
          .format(len(lines), len(lines_asked)))


def check_the_filter_against_the_oracle(path, command, label):
    """One process, the five sections in the oracle's order.

    One function for both filters, because the C++ and the Rust speak one
    protocol by design. Every value is an unsigned integer, so the two print
    the same text for the same answer.
    """
    require_artefact(path, command)
    lines = all_requests()
    answers = run_filter(path, lines)
    assert len(answers) == len(lines)

    at = 0
    refused = 0
    for case in ORACLE["ranges"]:
        got = fields_of(answers[at])
        assert int(got["base"]) == case["base"], case["why"]
        assert int(got["end"]) == case["end"], case["why"]
        assert int(got["bytes"]) == case["bytes"], case["why"]
        assert int(got["lines"]) == case["lines"], case["why"]
        assert int(got["head"]) == case["head_shared"], case["why"]
        assert int(got["tail"]) == case["tail_shared"], case["why"]
        at += 1
    for case in ORACLE["range_refusals"]:
        assert answers[at] == "REFUSED {}".format(case["error"]), case["why"]
        refused += 1
        at += 1
    for case in ORACLE["buffers"]:
        if case["error"] is None:
            assert answers[at] == "ok=1", case["why"]
        else:
            assert answers[at] == "REFUSED {}".format(case["error"]), case["why"]
            refused += 1
        at += 1
    for case in ORACLE["regions"]:
        got = fields_of(answers[at])
        assert int(got["field"]) == case["size_field"], case["why"]
        at += 1
    for case in ORACLE["region_refusals"]:
        assert answers[at] == "REFUSED {}".format(case["error"]), case["why"]
        refused += 1
        at += 1

    assert at == len(answers)
    expected = (len(ORACLE["range_refusals"]) + len(ORACLE["region_refusals"])
                + sum(1 for c in ORACLE["buffers"] if c["error"] is not None))
    assert refused == expected, (refused, expected)
    print(NEWLINE + "  the {} answered all {} requests across three verbs, "
          "{} of them refusals, every field compared exactly"
          .format(label, len(answers), refused))


def test_the_rust_filter_answers_every_case_as_the_oracle_says():
    check_the_filter_against_the_oracle(
        MPU_RUST_FILTER, "cargo build --release --workspace", "Rust")


def test_the_cpp_filter_answers_every_case_as_the_oracle_says():
    check_the_filter_against_the_oracle(MPU_CPP_FILTER, BUILD_HOST_CMD, "C++")


def check_the_boundary_through_the_filter(path, command, label):
    """Finding four and its boundary, through the filter.

    The Rust reaches this refusal with checked_add and
    checked_next_multiple_of rather than by computing in 64 bits, so the
    boundary is worth driving through the filter rather than trusting that two
    different techniques land in the same place. Three requests: the range
    whose rounded end is exactly 2 to the 32nd, the range whose unrounded sum
    already wraps, and the one line below, which must be answered.
    """
    require_artefact(path, command)
    top = 0xFFFFFFFF
    lines = ["M {} 32 32".format(top - 31),   # ends exactly at 2 ** 32
             "M {} 32 32".format(top - 15),   # the unrounded sum wraps
             "M {} 32 32".format(top - 63),   # one line lower, answerable
             "M {} 32 32".format(top - 62)]   # fits unrounded, rounds past
    answers = run_filter(path, lines)
    assert answers[0] == "REFUSED wrap", "an end of 2 ** 32 has no u32"
    assert answers[1] == "REFUSED wrap", "the sum itself wraps"
    got = fields_of(answers[2])
    assert int(got["end"]) == top - 31, "one line lower is inside the space"
    assert answers[3] == "REFUSED wrap", (
        "the unrounded sum fits and the ROUNDED end does not, which is the "
        "case a 32 bit addr + len < addr test would pass")
    print(NEWLINE + "  finding four through the {}: an end at 2 ** 32 is "
          "refused, so is a sum that wraps, so is a rounding that crosses the "
          "boundary, and one line lower answers end={}"
          .format(label, top - 31))


def test_the_rust_refuses_the_end_that_cannot_be_expressed():
    check_the_boundary_through_the_filter(
        MPU_RUST_FILTER, "cargo build --release --workspace", "Rust")


def test_the_cpp_refuses_the_end_that_cannot_be_expressed():
    check_the_boundary_through_the_filter(MPU_CPP_FILTER, BUILD_HOST_CMD,
                                          "C++")
