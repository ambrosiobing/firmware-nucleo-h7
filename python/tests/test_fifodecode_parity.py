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

from conftest import BUILD, require_artefact, shared_library_name

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


def test_a_gap_of_exactly_the_counters_modulus_is_invisible_to_both():
    """The third finding, and it is a bound rather than a defect.

    With an n-bit counter a loss of exactly 2^n slots reads as no loss, in any
    implementation, because the counter has returned to where it was expected.
    Two buffers that differ by exactly one wrap of lost slots are identical
    here, which is why fifo_budget reports the bound beside the number.
    """
    fmt = ORACLE["cases"][1]["fmt"]
    modulus = 1 << fmt["cnt_bits"]
    base = clean_run(fmt, 4)
    # The same two surviving slots, with and without a whole wrap lost between.
    kept = [base[0], base[1]]
    rc_a, a = c_decode(fmt, kept)
    rc_b, b = c_decode(fmt, [base[0], base[1]])
    assert (rc_a, rc_b) == (0, 0)
    assert a.drops == b.drops == 0

    # Now lose exactly one modulus of slots between two words.
    lost = [base[0], base[(0 + modulus) % len(base)]]
    rc, c = c_decode(fmt, lost)
    assert rc == 0
    assert c.drops == 0, (
        "a loss of exactly {} slots was detected, which would make this bound "
        "unnecessary".format(modulus))
    rc, out = py_budget(ORACLE["budgets"][0]["spec"])
    assert rc == 0 and out["undetectable_gap"] == modulus
    print("\n  a loss of exactly {} slots is indistinguishable from none with a "
          "{}-bit counter, and fifo_budget reports that bound"
          .format(modulus, fmt["cnt_bits"]))


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
