"""P13's buffer decode, drop counters and watermark sizing, in C and Python.

**Why the oracle is trustworthy.** Every decode expectation is integer
arithmetic written by hand from the field layout, and the generator refused to
emit any case where the hand-written numbers and the implementation disagreed.
One case is named after that check doing its job: a counter that appears to go
backwards is a gap too, and a larger one than it looks, so a sequence of 0, 2, 1,
3 in a four-value field is four drops across three gaps rather than two of each.
Nothing here passes on a tolerance except the budget, which is floating point and
says so.

**The field positions are a stated layout, not a claim about the part.**
Chapter 13 writes TAG_SHIFT, CNT_SHIFT and CNT_MASK as symbols and sends the tag
format to each sensor's datasheet, noting that the depth "is not assumed from a
sibling part". Only the word shape is in the chapter, a tag byte and six data
bytes. So the layout is a parameter and every finding below holds for any counter
field narrower than a byte rather than for one guessed width.

**The findings, which are demonstrated against the sketches rather than
described.** Chapter 13's step 7 and step 8 are carried unused in the Python
module exactly as printed, and these tests run them:

* Step 7 subtracts counter values in a byte's modulus whatever the field's width
  is, and advances its expectation the same way, so after the first wrap its
  expectation holds a value the field can never produce. On a perfectly clean
  buffer with a two-bit counter it reports 252 drops per wrap and grows from
  there. Step 9's acceptance criterion is zero drops over ten minutes, which that
  code cannot report.
* Step 7 also measures the first word it ever sees against a zero nobody wrote,
  so a first drain that begins at counter three opens by reporting drops that
  never happened.
* Step 8's fill time divides the watermark by one source's batching rate while
  the buffer holds every batched source. Step 4 batches two at 104 Hz and the
  chapter prints 1230 ms, which is 128 / 104 to the millisecond; the buffer fills
  in 615 ms and the safety ratio is optimistic by the number of sources.
"""
from __future__ import annotations

import ctypes
import json
import sys
from pathlib import Path

import pytest

from conftest import (BUILD, BUILD_HOST_CMD, FIFO_CPP_FILTER,
                      FIFO_RUST_FILTER, require_artefact, run_filter,
                      shared_library_name)

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "projects" / "P13-iks4a1-fifo"
sys.path.insert(0, str(PROJECT / "python"))

from fifodecode import (ERRORS, FIFO_TAG_VALUES,  # noqa: E402
                        budget as py_budget, chapter_dropcount,
                        chapter_fill_ms, cnt_gap as py_gap,
                        decode as py_decode, new_state)

ORACLE = json.loads((PROJECT / "vectors.json").read_text(encoding="utf-8"))
assert ORACLE["schema"] == 1, "the oracle's schema moved"
MAX_SOURCES = 8


class CFmt(ctypes.Structure):
    """Must match fifo_fmt_t in c/fifodecode.h, field for field."""

    _fields_ = [("word_bytes", ctypes.c_uint8), ("tag_shift", ctypes.c_uint8),
                ("cnt_shift", ctypes.c_uint8), ("cnt_bits", ctypes.c_uint8),
                ("known_tags", ctypes.c_uint32)]


class CState(ctypes.Structure):
    """Must match fifo_state_t."""

    _fields_ = [("words", ctypes.c_uint16),
                ("per_tag", ctypes.c_uint16 * FIFO_TAG_VALUES),
                ("unknown_tag", ctypes.c_uint16), ("drops", ctypes.c_uint32),
                ("gaps", ctypes.c_uint16), ("expect_cnt", ctypes.c_uint8),
                ("started", ctypes.c_uint8)]


class CBudget(ctypes.Structure):
    """Must match fifo_budget_t."""

    _fields_ = [("watermark_words", ctypes.c_uint16),
                ("sources", ctypes.c_uint8), ("word_bytes", ctypes.c_uint8),
                ("cnt_bits", ctypes.c_uint8),
                ("batch_hz", ctypes.c_double * MAX_SOURCES),
                ("bus_hz", ctypes.c_double),
                ("decode_us_per_word", ctypes.c_double)]


class CBudgetOut(ctypes.Structure):
    """Must match fifo_budget_out_t."""

    _fields_ = [("entries_per_s", ctypes.c_double),
                ("fill_ms", ctypes.c_double),
                ("chapter_fill_ms", ctypes.c_double),
                ("drain_us", ctypes.c_double), ("ratio", ctypes.c_double),
                ("undetectable_gap", ctypes.c_uint32),
                ("ratio_ok", ctypes.c_int)]


_LIB = None


def c_lib():
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("fifodecode")
        require_artefact(path)
        d = ctypes.CDLL(str(path))
        d.fifo_reset.restype = None
        d.fifo_reset.argtypes = [ctypes.POINTER(CState)]
        d.fifo_cnt_gap.restype = ctypes.c_uint8
        d.fifo_cnt_gap.argtypes = [ctypes.c_uint8, ctypes.c_uint8,
                                   ctypes.c_uint8]
        d.fifo_decode.restype = ctypes.c_int
        d.fifo_decode.argtypes = [ctypes.POINTER(CFmt),
                                  ctypes.POINTER(ctypes.c_uint8),
                                  ctypes.c_uint16, ctypes.POINTER(CState)]
        d.fifo_budget.restype = ctypes.c_int
        d.fifo_budget.argtypes = [ctypes.POINTER(CBudget),
                                  ctypes.POINTER(CBudgetOut)]
        _LIB = d
    return _LIB


def c_fmt(fmt):
    return CFmt(fmt["word_bytes"], fmt["tag_shift"], fmt["cnt_shift"],
                fmt["cnt_bits"], fmt["known_tags"])


def buffer_of(fmt, first_bytes):
    """One block, each word a tag byte followed by its data bytes."""
    out = []
    for b in first_bytes:
        out.append(b)
        out += [0] * (fmt["word_bytes"] - 1)
    return out


def c_decode(fmt, first_bytes, words=None):
    lib = c_lib()
    buf = buffer_of(fmt, first_bytes)
    n = len(first_bytes) if words is None else words
    arr = (ctypes.c_uint8 * max(len(buf), 1))(*buf) if buf \
        else (ctypes.c_uint8 * 1)()
    state = CState()
    lib.fifo_reset(ctypes.byref(state))
    rc = lib.fifo_decode(ctypes.byref(c_fmt(fmt)), arr, n,
                         ctypes.byref(state))
    return rc, state


def c_budget(spec):
    rates = (ctypes.c_double * MAX_SOURCES)(*spec["batch_hz"][:MAX_SOURCES])
    arg = CBudget(spec["watermark_words"], spec["sources"], spec["word_bytes"],
                  spec["cnt_bits"], rates, spec["bus_hz"],
                  spec["decode_us_per_word"])
    out = CBudgetOut()
    rc = c_lib().fifo_budget(ctypes.byref(arg), ctypes.byref(out))
    return rc, out


def as_dict(case, state_words, per_tag, unknown, drops, gaps):
    return {"words": state_words, "unknown_tag": unknown, "drops": drops,
            "gaps": gaps,
            "per_tag": {str(t): n for t, n in enumerate(per_tag) if n}}


@pytest.mark.parametrize("case", ORACLE["cases"], ids=lambda c: c["why"][:46])
def test_the_python_decodes_every_case_as_the_oracle_says(case):
    state = new_state()
    rc = py_decode(case["fmt"], buffer_of(case["fmt"], case["first_bytes"]),
                   len(case["first_bytes"]), state)
    assert rc == 0, case["why"]
    got = as_dict(case, state["words"], state["per_tag"],
                  state["unknown_tag"], state["drops"], state["gaps"])
    for key in ("words", "unknown_tag", "drops", "gaps", "per_tag"):
        assert got[key] == case[key], (key, case["why"])


@pytest.mark.parametrize("case", ORACLE["cases"], ids=lambda c: c["why"][:46])
def test_the_c_decodes_every_case_as_the_oracle_says(case):
    rc, state = c_decode(case["fmt"], case["first_bytes"])
    assert rc == 0, case["why"]
    got = as_dict(case, state.words, list(state.per_tag), state.unknown_tag,
                  state.drops, state.gaps)
    for key in ("words", "unknown_tag", "drops", "gaps", "per_tag"):
        assert got[key] == case[key], (key, case["why"])


@pytest.mark.parametrize("row", ORACLE["gaps"],
                         ids=lambda r: "b{}_e{}_c{}".format(r["cnt_bits"],
                                                            r["expect"],
                                                            r["cnt"]))
def test_both_implementations_take_the_gap_in_the_fields_modulus(row):
    want = row["gap"]
    assert py_gap(row["expect"], row["cnt"], row["cnt_bits"]) == want
    assert c_lib().fifo_cnt_gap(row["expect"], row["cnt"],
                                row["cnt_bits"]) == want


def test_the_gap_table_shows_the_chapters_subtraction_is_width_dependent():
    """The chapter's form is right at eight bits and wrong below it.

    This is the shape of the finding rather than one example of it: where the
    two moduli coincide the printed line is correct, and every narrower field
    makes it worse.
    """
    narrow = [r for r in ORACLE["gaps"]
              if r["cnt_bits"] < 8 and r["gap"] != r["chapter"]]
    wide = [r for r in ORACLE["gaps"] if r["cnt_bits"] == 8]
    assert narrow, "no row disagrees, so the finding is wrong"
    assert all(r["gap"] == r["chapter"] for r in wide), (
        "the chapter's subtraction disagrees even at eight bits, which would "
        "make this a different finding from the one described")
    worst = max(narrow, key=lambda r: r["chapter"] - r["gap"])
    print("\n  {} of {} gap rows disagree with step 7 as printed; the worst is "
          "a {}-bit counter expecting {} and seeing {}: {} lost slots, step 7 "
          "says {}".format(len(narrow), len(ORACLE["gaps"]), worst["cnt_bits"],
                           worst["expect"], worst["cnt"], worst["gap"],
                           worst["chapter"]))


@pytest.mark.parametrize("case", ORACLE["refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_formats(case):
    words = case.get("words", 1)
    first = [0] * words if words <= 8 else [0]
    fmt = case["fmt"]
    rc_p = py_decode(fmt, [0] * (words * max(fmt["word_bytes"], 1)), words,
                     new_state())
    assert rc_p == ERRORS[case["error"]], case["why"]
    if words > 8:
        # The words refusal must be reached without building a real buffer of
        # that size, because the point is that the length is refused before it
        # is used as an index.
        rc_c, _ = c_decode({**fmt, "word_bytes": 1}, [0], words)
    else:
        rc_c, _ = c_decode(fmt, first)
    assert rc_c == ERRORS[case["error"]], case["why"]


@pytest.mark.parametrize("case", ORACLE["budgets"], ids=lambda c: c["why"][:46])
def test_both_implementations_size_the_watermark_alike(case):
    rc_p, p = py_budget(case["spec"])
    rc_c, c = c_budget(case["spec"])
    assert (rc_p, rc_c) == (0, 0), case["why"]
    for key in ("entries_per_s", "fill_ms", "chapter_fill_ms", "drain_us",
                "ratio"):
        assert p[key] == pytest.approx(case[key], rel=1e-12), (key, case["why"])
        assert getattr(c, key) == pytest.approx(case[key], rel=1e-12), key
    assert p["ratio_ok"] == case["ratio_ok"] == c.ratio_ok, case["why"]
    assert (p["undetectable_gap"] == case["undetectable_gap"]
            == c.undetectable_gap), case["why"]


@pytest.mark.parametrize("case", ORACLE["budget_refusals"],
                         ids=lambda c: c["why"][:46])
def test_both_implementations_refuse_the_same_budgets(case):
    rc_p, out_p = py_budget(case["spec"])
    rc_c, _ = c_budget(case["spec"])
    assert rc_p == ERRORS[case["error"]] and out_p is None, case["why"]
    assert rc_c == ERRORS[case["error"]], case["why"]


# ---------------------------------------------------------------------------
# The findings, each run against the sketch rather than described.

def clean_run(fmt, slots):
    """A perfectly clean buffer: one word per slot, the counter advancing."""
    mask = (1 << fmt["cnt_bits"]) - 1
    return [((1 << fmt["tag_shift"]) | ((i & mask) << fmt["cnt_shift"]))
            for i in range(slots)]


def test_step_sevens_counter_cannot_report_zero_drops_on_a_clean_buffer():
    """The finding that makes step 9's criterion unreachable.

    Step 7 advances its expectation with a byte-wide increment, so once the
    field has wrapped its expectation holds a value the field can never produce
    and every later comparison is wrong. On a clean buffer the reported drops
    grow once per wrap. This decode reports zero, which is the truth.
    """
    fmt = ORACLE["cases"][1]["fmt"]
    mask = (1 << fmt["cnt_bits"]) - 1
    rows = []
    for slots in (4, 8, 12, 16, 40):
        words = clean_run(fmt, slots)
        theirs = chapter_dropcount(words, fmt["cnt_shift"], mask)
        rc, state = c_decode(fmt, words)
        assert rc == 0
        assert state.drops == 0, (
            "a clean buffer produced {} drops, so this decode has the same "
            "defect".format(state.drops))
        rows.append((slots, (slots - 1) // (mask + 1), theirs))

    assert rows[0][2] == 0, (
        "step 7 is already wrong before the first wrap, which is a different "
        "finding from the one described")
    assert all(b[2] > a[2] for a, b in zip(rows, rows[1:])), (
        "step 7's count does not grow with the run, so it is not wrapping")
    print("\n  a clean buffer with a {}-bit counter, by run length:"
          .format(fmt["cnt_bits"]))
    for slots, wraps, theirs in rows:
        print("    {:3d} slots, {} wraps: this decode 0 drops, step 7 as "
              "printed {} drops".format(slots, wraps, theirs))

    # And what that means for the acceptance criterion, computed rather than
    # asserted: the chapter's own rate and the counter width above.
    entries_per_s = 208.0
    seconds = 600.0
    wraps = entries_per_s * seconds / float(mask + 1)
    per_wrap = rows[1][2]
    print("    at {:.0f} entries a second over {:.0f} seconds that is {:.0f} "
          "wraps, so step 9's ten-minute run would report about {:.0f} drops "
          "from a flawless buffer".format(entries_per_s, seconds, wraps,
                                          wraps * per_wrap))


def test_step_seven_fabricates_drops_on_the_first_drain():
    """The second defect, which is separate from the modulus.

    expect_cnt is a static beginning at zero with no flag for the first word, so
    a first drain that opens at counter three is measured against a value nobody
    wrote. This decode lets the first word establish the sequence.
    """
    case = next(c for c in ORACLE["cases"] if "FIRST DRAIN" in c["why"])
    fmt = case["fmt"]
    mask = (1 << fmt["cnt_bits"]) - 1
    theirs = chapter_dropcount(case["first_bytes"], fmt["cnt_shift"], mask)
    rc, state = c_decode(fmt, case["first_bytes"])
    assert rc == 0
    assert state.drops == 0 == case["drops"]
    assert theirs > 0, (
        "step 7 reported no drops on a first drain that does not begin at "
        "zero, so this finding is wrong")
    print("\n  a first drain opening at counter {}: this decode 0 drops, step 7 "
          "as printed {} drops".format(
              (case["first_bytes"][0] >> fmt["cnt_shift"]) & mask, theirs))


def test_a_gap_of_exactly_the_counters_modulus_leaves_identical_bytes():
    """The third finding, and it is a bound rather than a defect.

    After a word carrying counter c the next is expected at c + 1, so losing k
    slots makes the next observed counter c + 1 + k and the gap is k in the
    field's modulus. A loss of exactly the modulus therefore gives zero, and the
    surviving words are BYTE FOR BYTE what they would have been with no loss at
    all. No implementation can tell those two buffers apart, which is why
    fifo_budget reports the bound beside the drop count.

    Driven through the twin rather than the library so it runs on the authoring
    laptop too, where the whole C half skips; the C's own gap arithmetic is
    covered at this boundary by the gap table above.

    The first version of this test built the lost case one slot short, giving
    two words with the same counter value, which is a gap of the modulus minus
    one and is detected. It failed on the laptop that has a compiler, before the
    branch reached main.
    """
    fmt = ORACLE["cases"][1]["fmt"]
    modulus = 1 << fmt["cnt_bits"]
    mask = modulus - 1

    def word_at(slot):
        return (1 << fmt["tag_shift"]) | ((slot & mask) << fmt["cnt_shift"])

    clean = [word_at(0), word_at(1)]
    lost_one_wrap = [word_at(0), word_at(1 + modulus)]
    assert clean == lost_one_wrap, (
        "losing exactly one wrap changed the bytes, so the counter can see it "
        "and this bound would be unnecessary")

    for words in (clean, lost_one_wrap):
        state = new_state()
        assert py_decode(fmt, buffer_of(fmt, words), len(words), state) == 0
        assert state["drops"] == 0, state["drops"]

    # The control, one slot fewer: a loss of modulus minus one IS detected, so
    # the blind spot is exactly one width wide and not a general blindness.
    state = new_state()
    short = [word_at(0), word_at(modulus)]
    assert py_decode(fmt, buffer_of(fmt, short), len(short), state) == 0
    assert state["drops"] == modulus - 1, state["drops"]

    rc, out = py_budget(ORACLE["budgets"][0]["spec"])
    assert rc == 0 and out["undetectable_gap"] == modulus
    print("\n  with a {}-bit counter a loss of exactly {} slots leaves the "
          "same bytes as no loss at all, while {} slots is reported as {}; "
          "fifo_budget carries the bound".format(fmt["cnt_bits"], modulus,
                                                 modulus - 1, modulus - 1))


def test_the_fill_time_divides_by_one_rate_where_the_buffer_holds_several():
    """The fourth finding, against step 8's printed number.

    128 words at 104 Hz is 1230 ms, which is what the chapter prints. Step 4
    batches two sources at 104 Hz each and step 6 decodes both tags, so the
    buffer fills at 208 entries a second and the fill time is 615 ms.
    """
    case = ORACLE["budgets"][0]
    spec = case["spec"]
    rc, out = py_budget(spec)
    assert rc == 0
    printed = chapter_fill_ms(spec["watermark_words"], spec["batch_hz"][0])
    assert printed == pytest.approx(out["chapter_fill_ms"], rel=1e-12), (
        "the sketch no longer reproduces the chapter's own form")
    assert round(printed) == 1231 and int(printed) == 1230, (
        "the sketch no longer prints the chapter's 1230 ms, so the arithmetic "
        "this finding rests on has changed: {:.2f}".format(printed))
    assert out["fill_ms"] == pytest.approx(printed / spec["sources"], rel=1e-12)
    print("\n  {} words, {} sources at {:.0f} Hz each: the buffer fills at "
          "{:.0f} entries a second in {:.2f} ms, where step 8 prints {:.2f} ms"
          .format(spec["watermark_words"], spec["sources"], spec["batch_hz"][0],
                  out["entries_per_s"], out["fill_ms"], printed))
    print("    the fill-to-drain ratio is {:.1f} and not {:.1f}, against the "
          "order of magnitude step 8 requires"
          .format(out["ratio"], out["ratio"] * spec["sources"]))


def test_one_source_is_the_case_where_the_chapter_is_right():
    """The control for the finding above. Without it the finding could be a
    defect in this implementation rather than in the chapter's form."""
    case = next(c for c in ORACLE["budgets"] if "one source only" in c["why"])
    rc, out = py_budget(case["spec"])
    assert rc == 0
    assert out["fill_ms"] == out["chapter_fill_ms"], (
        "the two forms differ with a single source, so the difference is not "
        "the source count")
    print("\n  control: with one batched source the two forms are identical at "
          "{:.2f} ms".format(out["fill_ms"]))


# ---------------------------------------------------------------------------
# The Rust, driven as a filter over the same oracle. One process for the whole
# run, which is conftest's rule.
#
# Three verbs, one per piece of the chapter that needs no shield:
#
#     G <expect> <cnt> <cnt_bits>
#     D <word_bytes> <tag_shift> <cnt_shift> <cnt_bits> <known_tags>
#       <words> <n> <first byte>*n
#     B <watermark> <sources> <word_bytes> <cnt_bits> <bus_hz> <decode_us>
#       <rate>*8
#
# THE WORD COUNT AND THE NUMBER OF SUPPLIED BYTES ARE SEPARATE TOKENS, and one
# oracle case is the whole reason: "more words than the landing buffer can hold"
# passes a level of 1025 with no bytes at all, because that refusal is decided
# before anything is read. A protocol deriving the level from the bytes could
# not carry that case.
#
# THE FILTER PADS THE BLOCK ITSELF, exactly as buffer_of does on this side: one
# tag byte per word followed by word_bytes - 1 zeros. The data bytes are read by
# none of this arithmetic, and sending them would make a 1024-word request seven
# thousand tokens long.
#
# ALL THIRTY-TWO TAG COUNTS COME BACK as one comma separated token, so this test
# asserts the zeros as well as the counts the oracle states.

NEWLINE = chr(10)


def gap_request(row):
    return "G {} {} {}".format(row["expect"], row["cnt"], row["cnt_bits"])


def decode_request(fmt, first_bytes, words):
    parts = ["D", str(fmt["word_bytes"]), str(fmt["tag_shift"]),
             str(fmt["cnt_shift"]), str(fmt["cnt_bits"]),
             str(fmt["known_tags"]), str(words), str(len(first_bytes))]
    parts += [str(b) for b in first_bytes]
    return " ".join(parts)


def budget_request(spec):
    parts = ["B", str(spec["watermark_words"]), str(spec["sources"]),
             str(spec["word_bytes"]), str(spec["cnt_bits"]),
             repr(float(spec["bus_hz"])),
             repr(float(spec["decode_us_per_word"]))]
    rates = list(spec["batch_hz"]) + [0.0] * MAX_SOURCES
    parts += [repr(float(r)) for r in rates[:MAX_SOURCES]]
    return " ".join(parts)


def all_requests():
    """Every request, in the order the answers are read back.

    Built where a test with no cargo can reach them, because the filter test
    SKIPS where there is no cargo and a skipped body never runs at all.
    """
    lines = [gap_request(r) for r in ORACLE["gaps"]]
    lines += [decode_request(c["fmt"], c["first_bytes"],
                             len(c["first_bytes"])) for c in ORACLE["cases"]]
    lines += [decode_request(c["fmt"], [], c.get("words", 0))
              for c in ORACLE["refusals"]]
    lines += [budget_request(c["spec"]) for c in ORACLE["budgets"]]
    lines += [budget_request(c["spec"]) for c in ORACLE["budget_refusals"]]
    return lines


def fields_of(answer):
    out = {}
    for token in answer.split():
        key, value = token.split("=", 1)
        out[key] = value
    return out


def test_every_request_says_what_its_case_says_without_running_anything():
    """Runs everywhere, including where no filter exists, on purpose.

    It reads each request back apart and compares it with the case rather than
    checking that it is well formed, which is the distinction P10's version of
    this file got wrong on Thursday 29 October 2026: a wrong value is as well
    formed as a right one.
    """
    lines = all_requests()
    at = 0
    for row in ORACLE["gaps"]:
        t = lines[at].split()
        assert t[0] == "G"
        assert [int(x) for x in t[1:]] == [row["expect"], row["cnt"],
                                           row["cnt_bits"]], row
        at += 1

    for case in ORACLE["cases"]:
        t = lines[at].split()
        fmt = case["fmt"]
        assert t[0] == "D", case["why"]
        assert int(t[1]) == fmt["word_bytes"], case["why"]
        assert int(t[2]) == fmt["tag_shift"], case["why"]
        assert int(t[3]) == fmt["cnt_shift"], case["why"]
        assert int(t[4]) == fmt["cnt_bits"], case["why"]
        assert int(t[5]) == fmt["known_tags"], case["why"]
        assert int(t[6]) == len(case["first_bytes"]), case["why"]
        assert int(t[7]) == len(case["first_bytes"]), case["why"]
        assert [int(x) for x in t[8:]] == case["first_bytes"], case["why"]
        at += 1

    carried = 0
    for case in ORACLE["refusals"]:
        t = lines[at].split()
        assert t[0] == "D", case["why"]
        assert int(t[6]) == case.get("words", 0), case["why"]
        assert int(t[7]) == 0, case["why"]
        if case.get("words", 0) > 1024:
            carried += 1
        at += 1
    assert carried == 1, (
        "the refusal for a level above the bound is the one case whose word "
        "count cannot come from the bytes, and {} of them were built"
        .format(carried))

    for case in ORACLE["budgets"] + ORACLE["budget_refusals"]:
        t = lines[at].split()
        spec = case["spec"]
        assert t[0] == "B", case["why"]
        assert int(t[1]) == spec["watermark_words"], case["why"]
        assert int(t[2]) == spec["sources"], case["why"]
        assert int(t[3]) == spec["word_bytes"], case["why"]
        assert int(t[4]) == spec["cnt_bits"], case["why"]
        assert float(t[5]) == spec["bus_hz"], case["why"]
        assert float(t[6]) == spec["decode_us_per_word"], case["why"]
        rates = [float(x) for x in t[7:15]]
        assert rates == list(spec["batch_hz"])[:MAX_SOURCES], case["why"]
        assert len(t) == 15, case["why"]
        at += 1

    assert at == len(lines)
    print(NEWLINE + "  {} requests built with no filter and no compiler, every "
          "field read back and compared with its case".format(len(lines)))


def check_the_filter_against_the_oracle(path, command, label):
    """One process, the five sections in the oracle's order.

    One function for both filters, because the C++ and the Rust speak one
    protocol by design and a second copy of this loop would be two places for
    a comparison to weaken. They print different text for one double, the
    shortest round-tripping form against %.17g, so every figure below is
    compared as a number.
    """
    require_artefact(path, command)
    lines = all_requests()
    answers = run_filter(path, lines)
    assert len(answers) == len(lines)

    at = 0
    for row in ORACLE["gaps"]:
        assert answers[at] == "gap={}".format(row["gap"]), row
        at += 1

    for case in ORACLE["cases"]:
        got = fields_of(answers[at])
        assert int(got["words"]) == case["words"], case["why"]
        assert int(got["unknown"]) == case["unknown_tag"], case["why"]
        assert int(got["drops"]) == case["drops"], case["why"]
        assert int(got["gaps"]) == case["gaps"], case["why"]
        tags = [int(x) for x in got["tags"].split(",")]
        # FIFO_TAG_VALUES and not a second 32 written here: the bound comes
        # from the implementation, which is where it can change.
        assert len(tags) == FIFO_TAG_VALUES, case["why"]
        for t, n in enumerate(tags):
            want = case["per_tag"].get(str(t), 0)
            assert n == want, (t, case["why"])
        at += 1

    refused = 0
    for case in ORACLE["refusals"]:
        assert answers[at] == "REFUSED {}".format(case["error"]), case["why"]
        refused += 1
        at += 1

    for case in ORACLE["budgets"]:
        got = fields_of(answers[at])
        # The budget figures are floating point and the oracle compares them to
        # a relative 1e-12, which is this project's own stated tolerance.
        assert float(got["entries"]) == pytest.approx(case["entries_per_s"],
                                                      rel=1e-12), case["why"]
        assert float(got["fill"]) == pytest.approx(case["fill_ms"],
                                                   rel=1e-12), case["why"]
        assert float(got["chapter"]) == pytest.approx(case["chapter_fill_ms"],
                                                      rel=1e-12), case["why"]
        assert float(got["drain"]) == pytest.approx(case["drain_us"],
                                                    rel=1e-12), case["why"]
        assert float(got["ratio"]) == pytest.approx(case["ratio"],
                                                    rel=1e-12), case["why"]
        # These two are integers and are compared exactly: the undetectable gap
        # is finding three and the flag is the verdict.
        assert int(got["gap"]) == case["undetectable_gap"], case["why"]
        assert int(got["ok"]) == case["ratio_ok"], case["why"]
        at += 1

    for case in ORACLE["budget_refusals"]:
        assert answers[at] == "REFUSED {}".format(case["error"]), case["why"]
        refused += 1
        at += 1

    assert at == len(answers)
    assert refused == len(ORACLE["refusals"]) + len(ORACLE["budget_refusals"])
    print(NEWLINE + "  the {} answered all {} requests, {} gap rows, {} "
          "decodes, {} budgets and {} refusals"
          .format(label, len(answers), len(ORACLE["gaps"]),
                  len(ORACLE["cases"]), len(ORACLE["budgets"]), refused))


def test_the_rust_filter_answers_every_case_as_the_oracle_says():
    check_the_filter_against_the_oracle(
        FIFO_RUST_FILTER, "cargo build --release --workspace", "Rust")


def test_the_cpp_filter_answers_every_case_as_the_oracle_says():
    check_the_filter_against_the_oracle(FIFO_CPP_FILTER, BUILD_HOST_CMD, "C++")


def check_the_chapters_two_sketches_through_the_filter(path, command, label):
    """Not a parity check: the two findings, through the filter.

    Finding one is that the chapter's subtraction is in the wrong modulus, and
    finding four is that its fill time divides by one rate where the buffer
    holds several. Both are asserted here against the filter's own answers, so
    the Rust is held to the corrections rather than only to agreement.
    """
    require_artefact(path, command)
    narrow = {"expect": 3, "cnt": 0, "cnt_bits": 2}
    wide = {"expect": 3, "cnt": 0, "cnt_bits": 8}
    case = next(c for c in ORACLE["budgets"] if c["spec"]["sources"] == 2)
    lines = [gap_request(narrow), gap_request(wide),
             budget_request(case["spec"])]
    answers = run_filter(path, lines)

    assert answers[0] == "gap=1", (
        "a two-bit counter expecting 3 and seeing 0 has advanced by one")
    assert answers[1] == "gap=253", (
        "the same values in the byte's modulus are the chapter's 253, which is "
        "why a wrong modulus hides behind the widest counter")
    got = fields_of(answers[2])
    fill = float(got["fill"])
    printed = float(got["chapter"])
    assert printed == pytest.approx(fill * case["spec"]["sources"], rel=1e-12)
    print(NEWLINE + "  finding one through the {}: gap 1 in two bits "
          "against the chapter's 253 in eight".format(label))
    print("  finding four through the {}: the buffer fills in {:.2f} ms "
          "where step 8 prints {:.2f} ms, for {} batched sources"
          .format(label, fill, printed, case["spec"]["sources"]))


def test_the_rust_carries_the_chapters_two_sketches_being_wrong():
    check_the_chapters_two_sketches_through_the_filter(
        FIFO_RUST_FILTER, "cargo build --release --workspace", "Rust")


def test_the_cpp_carries_the_chapters_two_sketches_being_wrong():
    check_the_chapters_two_sketches_through_the_filter(FIFO_CPP_FILTER,
                                                       BUILD_HOST_CMD, "C++")
