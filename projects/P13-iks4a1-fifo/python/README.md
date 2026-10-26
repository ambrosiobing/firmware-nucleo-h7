# P13 in Python

**State: host, since Monday 26 October 2026.** The twin of
[`../c/fifodecode.c`](../c/fifodecode.c): the same decode, the same gap
arithmetic, the same ratio and the same refusals, in the standard library alone,
because the suite's whole requirements file is one line.

| File | What it is |
|---|---|
| `fifodecode.py` | the twin, and both of chapter 13's sketches carried unused |

**This file carries step 7 and step 8 exactly as printed.**
`chapter_dropcount` is the drop counter, which subtracts and increments in a
byte's modulus whatever the field's width is, so on a clean buffer with a
two-bit counter it reports 252 drops per wrap and grows from there.
`chapter_fill_ms` is step 8's printed arithmetic, the watermark over one source's
batching rate, which gives the 1230 ms the chapter prints where the buffer holds
two sources and fills in 615 ms. Nothing above them calls either, and the tests
run both, so each finding is a demonstration rather than a sentence.

## What it is proven against

9 decode cases, 25 gap rows across five counter widths, 6 format refusals, 4
watermark budgets and 6 budget refusals, driven through this implementation and
the C by `python/tests/test_fifodecode_parity.py`. This half runs everywhere,
including on a laptop with no compiler, which is why every hand-written
expectation was checked here first.

## What is not here

Everything that needs the shield: the bus, the identification registers, the
watermark route, the overrun flag, the ten-minute run and the price of polling.
There is no measured number in this project.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
