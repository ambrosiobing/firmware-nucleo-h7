# P14 in Python

**State: host, since Monday 26 October 2026.** The twin of
[`../c/recdecode.c`](../c/recdecode.c), and the reason it exists is chapter 14's
own: step 7 says to write the decoder in the same sitting as the firmware,
because a format with no second implementation is a format with no
specification. Standard library only.

| File | What it is |
|---|---|
| `recdecode.py` | the twin, and both of chapter 14's printed forms carried unused |

**This file carries step 9's relation and step 10's sum exactly as printed.**
`chapter_altitude` is the barometric relation, whose arithmetic is correct and
whose reference pressure the record does not carry; `chapter_drain_ms` is the
768 x 9 / 400000 of step 10, whose own comment names an overhead the sum omits.
Nothing above them calls either, and the tests run both, so each finding is a
demonstration rather than a sentence.

**One comparison here is numeric rather than exact, and only one.** The altitude
raises a ratio to a fractional power, which is a library call in both languages
and need not agree with gcc's in the last bit, so the parity test compares
altitudes to a relative 1e-12. Everything else is an integer or a multiply by a
power of two and is compared exactly.

## What it is proven against

11 header cases, 6 conversions, 5 altitudes, 3 resolutions, 4 drain times and 8
refusals, driven through this implementation and the C by
`python/tests/test_recdecode_parity.py`. This half runs everywhere, including on
a laptop with no compiler, which is why the findings are demonstrated through it.

## What is not here

Everything that needs the shield, and every measured number. The drain time is
computed from a bus rate, which is what step 10 asks for and explicitly not a
measurement.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
