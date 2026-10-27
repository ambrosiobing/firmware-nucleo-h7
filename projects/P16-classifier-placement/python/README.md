# P16 in Python

**State: host, since Tuesday 27 October 2026.** The twin of
[`../c/features.c`](../c/features.c), and together they are chapter 16's
criterion 1: the equivalence gate step 4 says to build before any leg. Standard
library only.

| File | What it is |
|---|---|
| `features.py` | the twin, and the rounding the chapter permits carried unused |

**`math.isqrt` here, a bit-by-bit descent in the C.** Two different algorithms
for the same floored quantity, which is what makes the gate worth running: two
transcriptions of one algorithm would mostly prove the transcription was
faithful.

**This file carries `chapter_mean_nearest`, which nothing above it calls.** It is
a mean rounded to the nearest value, which step 3 neither requires nor forbids,
and a test runs it on a window where it differs from the pinned mean by one least
significant bit. That is how the finding is demonstrated rather than asserted:
criterion 1 compares for equality, so two implementations each obeying the
printed definition can fail the chapter's own gate.

## What it is proven against

6 feature cases, 4 shape refusals, 5 tree cases and 6 tree refusals, driven
through this implementation and the C by
`python/tests/test_features_parity.py`. This half runs everywhere, including on a
laptop with no compiler, which is why the findings are demonstrated through it.

## What is not here

The band energies, because the transform they come from is unspecified. The
dataset, the three legs and every measured number, because they need the shield
and a Raspberry Pi.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
