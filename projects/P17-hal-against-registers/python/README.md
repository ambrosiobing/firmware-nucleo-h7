# P17 in Python

**State: host, since Tuesday 27 October 2026.** The twin of
[`../c/tbmath.c`](../c/tbmath.c): the same arithmetic, the same refusals, in the
standard library alone.

| File | What it is |
|---|---|
| `tbmath.py` | the twin, and both of chapter 17's printed forms carried unused |

**This file carries the two pieces that produce the findings, as printed.**
`chapter_measure_run` is step 6's subtraction on a 32-bit unsigned value, which
wraps to 4294967291 ticks, 15.339 seconds, when an interval is below the
calibrated cost. `chapter_timebase_elapsed` is an elapsed interval through step
8's struct, which carries a width and a frequency and no direction, so for the
one counter that counts down it reports 16777116 ticks where the truth is 100.
Nothing above them calls either, and the tests run both.

**One test here is a confirmation rather than a finding**, which is worth having
among so many corrections: the chapter's three wrap periods are right. Step 8
states about 15.3 seconds, about 71.6 minutes and about 60 milliseconds, and
recomputed from the width and the frequency they are 15.339 s, 71.583 min and
59.919 ms.

## What it is proven against

6 elapsed cases, 3 wrap periods, 2 subtractions, 4 reports and 13 refusals,
driven through this implementation and the C by
`python/tests/test_tbmath_parity.py`. This half runs everywhere, including on a
laptop with no compiler, which is why the findings are demonstrated through it.

## What is not here

Everything that needs a running target or the bus, and every measured cycle.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
