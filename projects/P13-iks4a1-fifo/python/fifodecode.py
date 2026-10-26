"""P13's buffer decode and watermark sizing in Python, the twin of c/fifodecode.c.

The same bounded decode, the same gap in the counter's own modulus, the same
fill-to-drain ratio and the same refusals, in the standard library alone.

It also carries, at the bottom, chapter 13's two sketches exactly as printed.
Neither is used above them, and both exist so a test can run them rather than
describe them:

* ``chapter_dropcount`` is step 7, which subtracts counter values modulo 256
  whatever the field's width is, and which compares the first word it ever sees
  against a zero nobody wrote.
* ``chapter_fill_ms`` is step 8's printed arithmetic, the watermark divided by one
  source's batching rate, where the buffer holds every batched source.
"""
from __future__ import annotations

FIFO_TAG_VALUES = 32
FIFO_MAX_WORD_BYTES = 16
FIFO_MAX_WORDS = 1024
FIFO_MAX_SOURCES = 8

ERR_ARGUMENT = -1
ERR_FORMAT = -2
ERR_WORDS = -3
ERR_RATE = -4
ERR_SOURCES = -5

ERRORS = {"argument": ERR_ARGUMENT, "format": ERR_FORMAT, "words": ERR_WORDS,
          "rate": ERR_RATE, "sources": ERR_SOURCES}


def _mask(bits):
    return 0xFF if bits >= 8 else (1 << bits) - 1


def _format_ok(fmt):
    if not 1 <= fmt["word_bytes"] <= FIFO_MAX_WORD_BYTES:
        return False
    if not 1 <= fmt["cnt_bits"] <= 8:
        return False
    if fmt["tag_shift"] > 7 or fmt["cnt_shift"] > 7:
        return False
    if (0xFF >> fmt["tag_shift"]) >= FIFO_TAG_VALUES:
        return False
    return True


def new_state():
    """The reset state, which is what the chapter's static variable cannot be."""
    return {"words": 0, "per_tag": [0] * FIFO_TAG_VALUES, "unknown_tag": 0,
            "drops": 0, "gaps": 0, "expect_cnt": 0, "started": 0}


def cnt_gap(expect, cnt, cnt_bits):
    """The gap in the counter's own modulus, which is the corrected step 7."""
    if not 1 <= cnt_bits <= 8:
        return 0
    return (cnt - expect) & _mask(cnt_bits)


def decode(fmt, buf, words, state):
    """Decode one drained block into state. Returns 0 or a refusal."""
    if fmt is None or buf is None or state is None:
        return ERR_ARGUMENT
    if not _format_ok(fmt):
        return ERR_FORMAT
    if words > FIFO_MAX_WORDS:
        return ERR_WORDS

    cnt_mask = _mask(fmt["cnt_bits"])
    for i in range(words):
        first = buf[i * fmt["word_bytes"]]
        tag = first >> fmt["tag_shift"]
        cnt = (first >> fmt["cnt_shift"]) & cnt_mask

        if (fmt["known_tags"] >> tag) & 1:
            state["per_tag"][tag] += 1
        else:
            state["unknown_tag"] += 1

        if state["started"] == 0:
            state["started"] = 1
        else:
            gap = cnt_gap(state["expect_cnt"], cnt, fmt["cnt_bits"])
            if gap:
                state["drops"] += gap
                state["gaps"] += 1
        state["expect_cnt"] = (cnt + 1) & cnt_mask
        state["words"] += 1
    return 0


def budget(spec):
    """Step 8's ratio, with the chapter's own form reported beside it."""
    if spec is None:
        return ERR_ARGUMENT, None
    if not 1 <= spec["sources"] <= FIFO_MAX_SOURCES:
        return ERR_SOURCES, None
    if not 1 <= spec["word_bytes"] <= FIFO_MAX_WORD_BYTES:
        return ERR_FORMAT, None
    if not 1 <= spec["cnt_bits"] <= 8:
        return ERR_FORMAT, None
    if not 1 <= spec["watermark_words"] <= FIFO_MAX_WORDS:
        return ERR_WORDS, None
    if not spec["bus_hz"] > 0.0 or not spec["decode_us_per_word"] >= 0.0:
        return ERR_RATE, None
    rates = spec["batch_hz"][:spec["sources"]]
    if any(not r > 0.0 for r in rates):
        return ERR_RATE, None

    entries = 0.0
    for r in rates:
        entries += r
    fill_ms = 1000.0 * spec["watermark_words"] / entries
    chapter_fill_ms = 1000.0 * spec["watermark_words"] / rates[0]

    byts = float(spec["watermark_words"]) * float(spec["word_bytes"])
    bits = (byts + 2.0) * 9.0 + 2.0
    drain_us = (1.0e6 * bits / spec["bus_hz"]
                + float(spec["watermark_words"]) * spec["decode_us_per_word"])
    ratio = (fill_ms * 1000.0) / drain_us
    return 0, {"entries_per_s": entries, "fill_ms": fill_ms,
               "chapter_fill_ms": chapter_fill_ms, "drain_us": drain_us,
               "ratio": ratio, "ratio_ok": 1 if ratio >= 10.0 else 0,
               "undetectable_gap": 256 if spec["cnt_bits"] >= 8
               else 1 << spec["cnt_bits"]}


# ---------------------------------------------------------------------------
# Chapter 13 as printed, kept to be run rather than paraphrased. Nothing above
# calls either of these.

def chapter_dropcount(words, cnt_shift, cnt_mask):
    """Step 7 exactly as printed, including its static and its modulus.

    Two things are wrong with it and both are visible from one call. The
    subtraction is cast to uint8_t, so it wraps at 256 whatever cnt_mask is: a
    two-bit counter expecting 3 and seeing 0 has advanced by one and this adds
    253. And expect_cnt starts at zero with no flag for the first word, so the
    first word of the first drain is measured against a value nobody wrote.
    """
    drops = 0
    expect_cnt = 0          # the static, at its initial value
    for first in words:
        cnt = (first >> cnt_shift) & cnt_mask
        if cnt != expect_cnt:
            drops += (cnt - expect_cnt) & 0xFF     # the uint8_t cast
        expect_cnt = (cnt + 1) & 0xFF
    return drops


def chapter_fill_ms(watermark_words, one_batch_hz):
    """Step 8's printed arithmetic: the watermark over a single source's rate.

    128 words at 104 Hz gives 1230 ms, which is the number the chapter prints.
    The buffer holds every batched source, and step 4 batches two.
    """
    return 1000.0 * watermark_words / one_batch_hz
