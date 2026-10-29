"""P04's span arithmetic, the C and the Python against one hand-written oracle.

**What the oracle is.** `projects/P04-dma-idle-line/vectors.json`, eighteen cases
written by hand from chapter 4's steps 5 and 7, of which five expect a refusal.
It was checked before either implementation was allowed to agree with it, against
two invariants that come from neither: the span lengths sum to the advance, and no
span reaches past the end of the buffer. The advance in that check is computed
with a modulo where both implementations use a mask, so a mistake in the masking
cannot hide behind the oracle.

**Why this project starts on the host.** Chapter 4's receive path needs a
transfer engine, a line-idle interrupt and a cache invalidate, none of which a
host has. What it also needs is the arithmetic that turns two positions into at
most two spans, and that needs nothing: no peripheral, no clock, no board. So the
arithmetic is proven here and the board links the same object when it gets one.
**The project's status line says `c=host` and not `c=board` for exactly that
reason.**

**What these tests do not cover**, so that a green run is not read as more than it
is. Nothing here touches the counter read, the three handlers, the invalidate, or
the placement of the buffer in main memory rather than in tightly coupled memory.
Those are the rest of chapter 4 and they need the bench.

`tempfile` is not used here and `tmp_path` is not either, so the symlink problem
win11 aquamarine has with pytest's fixture does not arise.
"""
from __future__ import annotations

import ctypes
import json
import sys
from pathlib import Path

import pytest

from conftest import (BUILD, BUILD_HOST_CMD, SPAN_CPP_FILTER,
                      SPAN_RUST_FILTER, require_artefact, run_filter,
                      shared_library_name)

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "projects" / "P04-dma-idle-line"
sys.path.insert(0, str(PROJECT / "python"))

from rxspan import ERRORS, fresh_stats, next_spans  # noqa: E402

RXSPAN_MAX = 2


class CSpan(ctypes.Structure):
    """Must match rxspan_t in projects/P04-dma-idle-line/c/rxspan.h."""

    _fields_ = [("off", ctypes.c_uint16), ("len", ctypes.c_uint16)]


class CStats(ctypes.Structure):
    """Must match rxspan_stats_t in the same header, field for field."""

    _fields_ = [
        ("peak_advance", ctypes.c_uint16),
        ("near_lap", ctypes.c_uint32),
        ("calls", ctypes.c_uint32),
        ("deliveries", ctypes.c_uint32),
    ]


def cases():
    text = (PROJECT / "vectors.json").read_text(encoding="utf-8")
    loaded = json.loads(text)
    assert loaded["schema"] == 1, "the oracle's schema moved"
    assert loaded["cases"], "an empty oracle proves nothing"
    return loaded["cases"]


_LIB = None


def c_lib():
    """The C, which skips rather than fails where no compiler may run."""
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("rxspan")
        require_artefact(path)
        d = ctypes.CDLL(str(path))
        d.rxspan_next.restype = ctypes.c_int
        d.rxspan_next.argtypes = [
            ctypes.c_uint16, ctypes.c_uint16, ctypes.c_uint16,
            ctypes.POINTER(CSpan), ctypes.POINTER(CStats),
        ]
        _LIB = d
    return _LIB


def c_answer(case):
    lib = c_lib()
    out = (CSpan * RXSPAN_MAX)()
    stats = CStats()
    count = lib.rxspan_next(case["size"], case["old"], case["pos"], out,
                            ctypes.byref(stats))
    spans = [[out[i].off, out[i].len] for i in range(max(count, 0))]
    return count, spans, stats


def python_answer(case):
    stats = fresh_stats()
    count, spans = next_spans(case["size"], case["old"], case["pos"], stats)
    return count, [[o, l] for o, l in spans], stats


@pytest.mark.parametrize("case", cases(), ids=lambda c: c["why"][:44])
def test_the_python_twin_matches_the_oracle(case):
    count, spans, stats = python_answer(case)
    if "error" in case:
        assert count == ERRORS[case["error"]], case["why"]
        assert spans == []
        assert stats["calls"] == 0, "a refused call must not count as a call"
        return
    assert count == len(case["spans"]), case["why"]
    assert spans == case["spans"], case["why"]
    assert stats["peak_advance"] == case["advance"], case["why"]
    assert stats["near_lap"] == case["near_lap"], case["why"]
    assert stats["calls"] == 1
    assert stats["deliveries"] == len(case["spans"])


@pytest.mark.parametrize("case", cases(), ids=lambda c: c["why"][:44])
def test_the_c_matches_the_oracle(case):
    count, spans, stats = c_answer(case)
    if "error" in case:
        assert count == ERRORS[case["error"]], case["why"]
        assert stats.calls == 0, "a refused call must not count as a call"
        return
    assert count == len(case["spans"]), case["why"]
    assert spans == case["spans"], case["why"]
    assert stats.peak_advance == case["advance"], case["why"]
    assert stats.near_lap == case["near_lap"], case["why"]
    assert stats.calls == 1
    assert stats.deliveries == len(case["spans"])


def test_the_two_implementations_agree_on_every_case():
    """The pairwise comparison, which the two oracle tests do not make.

    Each of those compares one implementation with the written answer. This one
    compares them with each other, so a case the oracle happens not to cover
    cannot hide a difference between them.
    """
    for case in cases():
        c_count, c_spans, c_stats = c_answer(case)
        p_count, p_spans, p_stats = python_answer(case)
        assert c_count == p_count, case["why"]
        assert c_spans == p_spans, case["why"]
        assert c_stats.peak_advance == p_stats["peak_advance"], case["why"]
        assert c_stats.near_lap == p_stats["near_lap"], case["why"]


def test_statistics_accumulate_across_a_walk_rather_than_resetting():
    """A property the per-case tests cannot see, because each starts fresh.

    A real receive path calls this thousands of times with the statistics
    carried forward, so this walks a buffer in uneven steps and checks the three
    counters against independently computed totals. The walk is deliberately not
    uniform: equal steps would never produce a wrap in the middle of a span.
    """
    size = 512
    steps = [7, 1, 300, 1, 200, 64, 0, 511, 2, 128]
    lib = c_lib()
    out = (CSpan * RXSPAN_MAX)()
    c_stats = CStats()
    p_stats = fresh_stats()

    pos = 0
    advances = []
    for step in steps:
        old, pos = pos, (pos + step) % size
        advances.append((pos - old) % size)
        lib.rxspan_next(size, old, pos, out, ctypes.byref(c_stats))
        next_spans(size, old, pos, p_stats)

    assert c_stats.calls == len(steps)
    assert c_stats.peak_advance == max(advances)
    assert c_stats.near_lap == sum(1 for a in advances if a > size // 2)
    assert c_stats.calls == p_stats["calls"]
    assert c_stats.peak_advance == p_stats["peak_advance"]
    assert c_stats.near_lap == p_stats["near_lap"]
    assert c_stats.deliveries == p_stats["deliveries"]


def test_no_span_ever_crosses_the_end_of_the_buffer():
    """The property the whole split exists to guarantee, over every position.

    The consumer is handed a plain pointer and a length, so a span that ran past
    the end of the array would be a buffer overrun rather than a wrong answer.
    This walks every pair of positions in a small buffer, which is 4 096 pairs,
    rather than sampling them.
    """
    size = 64
    lib = c_lib()
    out = (CSpan * RXSPAN_MAX)()
    stats = CStats()
    checked = 0
    for old in range(size):
        for pos in range(size):
            count = lib.rxspan_next(size, old, pos, out, ctypes.byref(stats))
            assert count >= 0
            total = 0
            for i in range(count):
                assert out[i].len > 0, (old, pos, "a zero length span")
                assert out[i].off + out[i].len <= size, (old, pos, "past the end")
                total += out[i].len
            assert total == (pos - old) % size, (old, pos, "lengths lost bytes")
            checked += 1
    assert checked == size * size
    print("\n  {} position pairs, no span past the end, no byte lost".format(checked))


# ---------------------------------------------------------------------------
# The Rust, driven as a filter over the same oracle. One process for the whole
# run, which is conftest's rule.
#
# One verb, because this project is one function, and each request runs on
# fresh statistics, which is how the oracle states its cases:
#
#     N <size> <old_pos> <pos>


def fields_of(answer):
    out = {}
    for token in answer.split():
        key, value = token.split("=", 1)
        out[key] = int(value)
    return out


def requests_for(oracle_cases):
    """The filter requests, built where a test with no compiler can reach them.

    This is a separate function because the test below skips on win11
    aquamarine, where there is no cargo, and a SKIPPED test body is never
    executed at all: the first version of this file referred to a module level
    name this file does not define, which the skip hid completely and the
    laptop with a compiler found. Anything that can be checked without the
    artefact is now checked without it.
    """
    return ["N {} {} {}".format(c["size"], c["old"], c["pos"])
            for c in oracle_cases]


def test_the_requests_are_well_formed_without_running_anything():
    """Runs everywhere, including where no filter exists, on purpose."""
    oracle_cases = cases()
    lines = requests_for(oracle_cases)
    assert len(lines) == len(oracle_cases)
    for line, case in zip(lines, oracle_cases):
        parts = line.split()
        assert parts[0] == "N" and len(parts) == 4, case["why"]
        want = [case["size"], case["old"], case["pos"]]
        assert [int(p) for p in parts[1:]] == want, case["why"]
    print("\n  {} requests built and checked with no filter and no "
          "compiler".format(len(lines)))


# The C++ and the Rust speak one protocol, so the test below runs over both and
# the language is in the test id, which is what names the one that disagreed.
FILTERS = [(SPAN_CPP_FILTER, "cpp", BUILD_HOST_CMD),
           (SPAN_RUST_FILTER, "rust", "cargo build --release --workspace")]
FILTER_ARGS = [(path, command) for path, _, command in FILTERS]
FILTER_IDS = [name for _, name, _ in FILTERS]


@pytest.mark.parametrize("path,command", FILTER_ARGS, ids=FILTER_IDS)
def test_each_filter_answers_every_case_as_the_oracle_says(path, command):
    """One process, every case in the oracle's order, refusals included."""
    require_artefact(path, command)
    oracle_cases = cases()
    lines = requests_for(oracle_cases)
    answers = run_filter(path, lines)
    assert len(answers) == len(lines)

    delivered = 0
    for case, answer in zip(oracle_cases, answers):
        if "error" in case:
            assert answer == "REFUSED {}".format(case["error"]), case["why"]
            continue
        got = fields_of(answer)
        spans = case["spans"]
        assert got["spans"] == len(spans), case["why"]
        for i, (off, length) in enumerate(spans):
            assert got["off{}".format(i)] == off, (i, case["why"])
            assert got["len{}".format(i)] == length, (i, case["why"])
        # A span index the case does not have must not be answered at all,
        # which is what the Rust's sum type makes impossible to get wrong and
        # the C's count beside an array does not.
        assert "off{}".format(len(spans)) not in got, case["why"]
        assert got["peak"] == case["advance"], case["why"]
        assert got["near_lap"] == case["near_lap"], case["why"]
        assert got["calls"] == 1, case["why"]
        assert got["deliveries"] == len(spans), case["why"]
        delivered += len(spans)

    refusals = sum(1 for c in oracle_cases if "error" in c)
    print("\n  the filter answered all {} cases, {} of them refusals, handing "
          "out {} spans in total".format(len(lines), refusals, delivered))
